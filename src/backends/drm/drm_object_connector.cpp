/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_object_connector.h"
#include "drm_gpu.h"
#include "drm_pointer.h"
#include "logging.h"
#include "drm_pipeline.h"
#include "drm_object_crtc.h"
#include "drm_output.h"

#include <main.h>
// frameworks
#include <KConfigGroup>

#include <cerrno>

namespace KWin
{

static bool checkIfEqual(const drmModeModeInfo *one, const drmModeModeInfo *two)
{
    return one->clock       == two->clock
        && one->hdisplay    == two->hdisplay
        && one->hsync_start == two->hsync_start
        && one->hsync_end   == two->hsync_end
        && one->htotal      == two->htotal
        && one->hskew       == two->hskew
        && one->vdisplay    == two->vdisplay
        && one->vsync_start == two->vsync_start
        && one->vsync_end   == two->vsync_end
        && one->vtotal      == two->vtotal
        && one->vscan       == two->vscan
        && one->vrefresh    == two->vrefresh;
}

static quint64 refreshRateForMode(_drmModeModeInfo *m)
{
    // Calculate higher precision (mHz) refresh rate
    // logic based on Weston, see compositor-drm.c
    quint64 refreshRate = (m->clock * 1000000LL / m->htotal + m->vtotal / 2) / m->vtotal;
    if (m->flags & DRM_MODE_FLAG_INTERLACE) {
        refreshRate *= 2;
    }
    if (m->flags & DRM_MODE_FLAG_DBLSCAN) {
        refreshRate /= 2;
    }
    if (m->vscan > 1) {
        refreshRate /= m->vscan;
    }
    return refreshRate;
}

DrmConnectorMode::DrmConnectorMode(DrmConnector *connector, drmModeModeInfo nativeMode)
    : m_connector(connector)
    , m_nativeMode(nativeMode)
    , m_size(nativeMode.hdisplay, nativeMode.vdisplay)
    , m_refreshRate(refreshRateForMode(&nativeMode))
{
}

DrmConnectorMode::~DrmConnectorMode()
{
    if (m_blobId) {
        drmModeDestroyPropertyBlob(m_connector->gpu()->fd(), m_blobId);
        m_blobId = 0;
    }
}

drmModeModeInfo *DrmConnectorMode::nativeMode()
{
    return &m_nativeMode;
}

QSize DrmConnectorMode::size() const
{
    return m_size;
}

uint32_t DrmConnectorMode::refreshRate() const
{
    return m_refreshRate;
}

uint32_t DrmConnectorMode::blobId()
{
    if (!m_blobId) {
        if (drmModeCreatePropertyBlob(m_connector->gpu()->fd(), &m_nativeMode, sizeof(m_nativeMode), &m_blobId) != 0) {
            qCWarning(KWIN_DRM) << "Failed to create connector mode blob:" << strerror(errno);
        }
    }
    return m_blobId;
}

bool DrmConnectorMode::operator==(const DrmConnectorMode &otherMode)
{
    return checkIfEqual(&m_nativeMode, &otherMode.m_nativeMode);
}

DrmConnector::DrmConnector(DrmGpu *gpu, uint32_t connectorId)
    : DrmObject(gpu, connectorId, {
            PropertyDefinition(QByteArrayLiteral("CRTC_ID"), Requirement::Required),
            PropertyDefinition(QByteArrayLiteral("non-desktop"), Requirement::Optional),
            PropertyDefinition(QByteArrayLiteral("DPMS"), Requirement::RequiredForLegacy),
            PropertyDefinition(QByteArrayLiteral("EDID"), Requirement::Optional),
            PropertyDefinition(QByteArrayLiteral("overscan"), Requirement::Optional),
            PropertyDefinition(QByteArrayLiteral("vrr_capable"), Requirement::Optional),
            PropertyDefinition(QByteArrayLiteral("underscan"), Requirement::Optional, {
                QByteArrayLiteral("off"),
                QByteArrayLiteral("on"),
                QByteArrayLiteral("auto")
            }),
            PropertyDefinition(QByteArrayLiteral("underscan vborder"), Requirement::Optional),
            PropertyDefinition(QByteArrayLiteral("underscan hborder"), Requirement::Optional),
            PropertyDefinition(QByteArrayLiteral("Broadcast RGB"), Requirement::Optional, {
                QByteArrayLiteral("Automatic"),
                QByteArrayLiteral("Full"),
                QByteArrayLiteral("Limited 16:235")
            }),
            PropertyDefinition(QByteArrayLiteral("max bpc"), Requirement::Optional),
            PropertyDefinition(QByteArrayLiteral("link-status"), Requirement::Optional, {
                QByteArrayLiteral("Good"),
                QByteArrayLiteral("Bad")
            }),
            PropertyDefinition(QByteArrayLiteral("panel orientation"), DrmObject::Requirement::Optional, {
                QByteArrayLiteral("Normal"),
                QByteArrayLiteral("Upside Down"),
                QByteArrayLiteral("Left Side Up"),
                QByteArrayLiteral("Right Side Up")
            })
        }, DRM_MODE_OBJECT_CONNECTOR)
    , m_pipeline(new DrmPipeline(this))
    , m_conn(drmModeGetConnector(gpu->fd(), connectorId))
{
    if (m_conn) {
        for (int i = 0; i < m_conn->count_encoders; ++i) {
            DrmScopedPointer<drmModeEncoder> enc(drmModeGetEncoder(gpu->fd(), m_conn->encoders[i]));
            if (!enc) {
                qCWarning(KWIN_DRM) << "failed to get encoder" << m_conn->encoders[i];
                continue;
            }
            m_possibleCrtcs |= enc->possible_crtcs;
        }
    } else {
        qCWarning(KWIN_DRM) << "drmModeGetConnector failed!" << strerror(errno);
    }
}

bool DrmConnector::init()
{
    return m_conn && initProps();
}

bool DrmConnector::isConnected() const
{
    if (!m_conn) {
        return false;
    }
    return m_conn->connection == DRM_MODE_CONNECTED;
}

static QHash<int, QByteArray> s_connectorNames = {
    {DRM_MODE_CONNECTOR_Unknown, QByteArrayLiteral("Unknown")},
    {DRM_MODE_CONNECTOR_VGA, QByteArrayLiteral("VGA")},
    {DRM_MODE_CONNECTOR_DVII, QByteArrayLiteral("DVI-I")},
    {DRM_MODE_CONNECTOR_DVID, QByteArrayLiteral("DVI-D")},
    {DRM_MODE_CONNECTOR_DVIA, QByteArrayLiteral("DVI-A")},
    {DRM_MODE_CONNECTOR_Composite, QByteArrayLiteral("Composite")},
    {DRM_MODE_CONNECTOR_SVIDEO, QByteArrayLiteral("SVIDEO")},
    {DRM_MODE_CONNECTOR_LVDS, QByteArrayLiteral("LVDS")},
    {DRM_MODE_CONNECTOR_Component, QByteArrayLiteral("Component")},
    {DRM_MODE_CONNECTOR_9PinDIN, QByteArrayLiteral("DIN")},
    {DRM_MODE_CONNECTOR_DisplayPort, QByteArrayLiteral("DP")},
    {DRM_MODE_CONNECTOR_HDMIA, QByteArrayLiteral("HDMI-A")},
    {DRM_MODE_CONNECTOR_HDMIB, QByteArrayLiteral("HDMI-B")},
    {DRM_MODE_CONNECTOR_TV, QByteArrayLiteral("TV")},
    {DRM_MODE_CONNECTOR_eDP, QByteArrayLiteral("eDP")},
    {DRM_MODE_CONNECTOR_VIRTUAL, QByteArrayLiteral("Virtual")},
    {DRM_MODE_CONNECTOR_DSI, QByteArrayLiteral("DSI")},
    {DRM_MODE_CONNECTOR_DPI, QByteArrayLiteral("DPI")},
#ifdef DRM_MODE_CONNECTOR_WRITEBACK
    {DRM_MODE_CONNECTOR_WRITEBACK, QByteArrayLiteral("Writeback")},
#endif
#ifdef DRM_MODE_CONNECTOR_SPI
    {DRM_MODE_CONNECTOR_SPI, QByteArrayLiteral("SPI")},
#endif
#ifdef DRM_MODE_CONNECTOR_USB
    {DRM_MODE_CONNECTOR_USB, QByteArrayLiteral("USB")},
#endif
};

QString DrmConnector::connectorName() const
{
    return s_connectorNames.value(m_conn->connector_type, QByteArrayLiteral("Unknown")) + QStringLiteral("-") + QString::number(m_conn->connector_type_id);
}

QString DrmConnector::modelName() const
{
    if (m_edid.serialNumber().isEmpty()) {
        return connectorName() + QLatin1Char('-') + m_edid.nameString();
    } else {
        return m_edid.nameString();
    }
}

bool DrmConnector::isInternal() const
{
    return m_conn->connector_type == DRM_MODE_CONNECTOR_LVDS || m_conn->connector_type == DRM_MODE_CONNECTOR_eDP
                || m_conn->connector_type == DRM_MODE_CONNECTOR_DSI;
}

QSize DrmConnector::physicalSize() const
{
    return m_physicalSize;
}

QVector<QSharedPointer<DrmConnectorMode>> DrmConnector::modes() const
{
    return m_modes;
}

QSharedPointer<DrmConnectorMode> DrmConnector::findMode(const drmModeModeInfo &modeInfo) const
{
    const auto it = std::find_if(m_modes.constBegin(), m_modes.constEnd(), [&modeInfo](const auto &mode) {
        return checkIfEqual(mode->nativeMode(), &modeInfo);
    });
    return it == m_modes.constEnd() ? nullptr : *it;
}

AbstractWaylandOutput::SubPixel DrmConnector::subpixel() const
{
    switch (m_conn->subpixel) {
    case DRM_MODE_SUBPIXEL_UNKNOWN:
        return AbstractWaylandOutput::SubPixel::Unknown;
    case DRM_MODE_SUBPIXEL_NONE:
        return AbstractWaylandOutput::SubPixel::None;
    case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
        return AbstractWaylandOutput::SubPixel::Horizontal_RGB;
    case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
        return AbstractWaylandOutput::SubPixel::Horizontal_BGR;
    case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
        return AbstractWaylandOutput::SubPixel::Vertical_RGB;
    case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
        return AbstractWaylandOutput::SubPixel::Vertical_BGR;
    default:
        Q_UNREACHABLE();
    }
}

bool DrmConnector::hasOverscan() const
{
    return getProp(PropertyIndex::Overscan) || getProp(PropertyIndex::Underscan);
}

uint32_t DrmConnector::overscan() const
{
    if (const auto &prop = getProp(PropertyIndex::Overscan)) {
        return prop->pending();
    } else if (const auto &prop = getProp(PropertyIndex::Underscan_vborder)) {
        return prop->pending();
    }
    return 0;
}

bool DrmConnector::vrrCapable() const
{
    if (const auto &prop = getProp(PropertyIndex::VrrCapable)) {
        return prop->pending();
    }
    return false;
}

bool DrmConnector::needsModeset() const
{
    if (!gpu()->atomicModeSetting()) {
        return false;
    }
    if (getProp(PropertyIndex::CrtcId)->needsCommit()) {
        return true;
    }
    if (const auto &prop = getProp(PropertyIndex::MaxBpc); prop && prop->needsCommit()) {
        return true;
    }
    const auto &rgb = getProp(PropertyIndex::Broadcast_RGB);
    return rgb && rgb->needsCommit();
}

bool DrmConnector::hasRgbRange() const
{
    const auto &rgb = getProp(PropertyIndex::Broadcast_RGB);
    return rgb && rgb->hasAllEnums();
}

AbstractWaylandOutput::RgbRange DrmConnector::rgbRange() const
{
    const auto &rgb = getProp(PropertyIndex::Broadcast_RGB);
    return rgb->enumForValue<AbstractWaylandOutput::RgbRange>(rgb->pending());
}

bool DrmConnector::updateProperties()
{
    if (!DrmObject::updateProperties()) {
        return false;
    }
    m_conn.reset(drmModeGetConnector(gpu()->fd(), id()));
    if (!m_conn) {
        return false;
    }
    if (const auto &dpms = getProp(PropertyIndex::Dpms)) {
        dpms->setLegacy();
    }

    auto underscan = m_props[static_cast<uint32_t>(PropertyIndex::Underscan)];
    auto vborder = m_props[static_cast<uint32_t>(PropertyIndex::Underscan_vborder)];
    auto hborder = m_props[static_cast<uint32_t>(PropertyIndex::Underscan_hborder)];
    if (underscan && vborder && hborder) {
        underscan->setEnum(vborder->current() > 0 ? UnderscanOptions::On : UnderscanOptions::Off);
    } else {
        deleteProp(PropertyIndex::Underscan);
        deleteProp(PropertyIndex::Underscan_vborder);
        deleteProp(PropertyIndex::Underscan_hborder);
    }

    // parse edid
    auto edidProp = getProp(PropertyIndex::Edid);
    if (edidProp) {
        DrmScopedPointer<drmModePropertyBlobRes> blob(drmModeGetPropertyBlob(gpu()->fd(), edidProp->current()));
        if (blob && blob->data) {
            m_edid = Edid(blob->data, blob->length);
            if (!m_edid.isValid()) {
                qCWarning(KWIN_DRM) << "Couldn't parse EDID for connector" << this;
            }
        }
        deleteProp(PropertyIndex::Edid);
    } else {
        qCDebug(KWIN_DRM) << "Could not find edid for connector" << this;
    }

    // check the physical size
    if (m_edid.physicalSize().isEmpty()) {
        m_physicalSize = QSize(m_conn->mmWidth, m_conn->mmHeight);
    } else {
        m_physicalSize = m_edid.physicalSize();
    }

    // the size might be completely borked. E.g. Samsung SyncMaster 2494HS reports 160x90 while in truth it's 520x292
    // as this information is used to calculate DPI info, it's going to result in everything being huge
    const QByteArray unknown = QByteArrayLiteral("unknown");
    KConfigGroup group = kwinApp()->config()->group("EdidOverwrite").group(m_edid.eisaId().isEmpty() ? unknown : m_edid.eisaId())
                                                       .group(m_edid.monitorName().isEmpty() ? unknown : m_edid.monitorName())
                                                       .group(m_edid.serialNumber().isEmpty() ? unknown : m_edid.serialNumber());
    if (group.hasKey("PhysicalSize")) {
        const QSize overwriteSize = group.readEntry("PhysicalSize", m_physicalSize);
        qCWarning(KWIN_DRM) << "Overwriting monitor physical size for" << m_edid.eisaId() << "/" << m_edid.monitorName() << "/" << m_edid.serialNumber() << " from " << m_physicalSize << "to " << overwriteSize;
        m_physicalSize = overwriteSize;
    }

    if (auto bpc = getProp(PropertyIndex::MaxBpc)) {
        // make sure the driver allows us to use high bpc
        bpc->setPending(bpc->maxValue());
    }

    // update modes
    bool equal = m_conn->count_modes == m_modes.count();
    for (int i = 0; equal && i < m_conn->count_modes; i++) {
        equal &= checkIfEqual(m_modes[i]->nativeMode(), &m_conn->modes[i]);
    }
    if (!equal) {
        // reload modes
        m_modes.clear();
        for (int i = 0; i < m_conn->count_modes; i++) {
            m_modes.append(QSharedPointer<DrmConnectorMode>::create(this, m_conn->modes[i]));
        }
        if (m_modes.isEmpty()) {
            return false;
        } else {
            if (!m_pipeline->pending.mode) {
                m_pipeline->pending.mode = m_modes.constFirst();
            }
            if (const auto output = dynamic_cast<DrmOutput*>(m_pipeline->displayDevice())) {
                output->updateModes();
            }
        }
    }

    return true;
}

bool DrmConnector::isCrtcSupported(DrmCrtc *crtc) const
{
    return (m_possibleCrtcs & (1 << crtc->pipeIndex()));
}

bool DrmConnector::isNonDesktop() const
{
    const auto &prop = getProp(PropertyIndex::NonDesktop);
    return prop && prop->current();
}

const Edid *DrmConnector::edid() const
{
    return &m_edid;
}

DrmPipeline *DrmConnector::pipeline() const
{
    return m_pipeline.data();
}

void DrmConnector::disable()
{
    setPending(PropertyIndex::CrtcId, 0);
}

DrmConnector::LinkStatus DrmConnector::linkStatus() const
{
    if (const auto &property = getProp(PropertyIndex::LinkStatus)) {
        return property->enumForValue<LinkStatus>(property->current());
    }
    return LinkStatus::Good;
}

DrmConnector::PanelOrientation DrmConnector::panelOrientation() const
{
    if (const auto &property = getProp(PropertyIndex::PanelOrientation)) {
        return property->enumForValue<PanelOrientation>(property->current());
    } else {
        return PanelOrientation::Normal;
    }
}

QDebug& operator<<(QDebug& s, const KWin::DrmConnector *obj)
{
    QDebugStateSaver saver(s);
    if (obj) {

        QString connState = QStringLiteral("Disconnected");
        if (!obj->m_conn || obj->m_conn->connection == DRM_MODE_UNKNOWNCONNECTION) {
            connState = QStringLiteral("Unknown Connection");
        } else if (obj->m_conn->connection == DRM_MODE_CONNECTED) {
            connState = QStringLiteral("Connected");
        }

        s.nospace() << "DrmConnector(id=" << obj->id() <<
                       ", gpu="<< obj->gpu() <<
                       ", name="<< obj->modelName() <<
                       ", connection=" << connState <<
                       ", countMode=" << (obj->m_conn ? obj->m_conn->count_modes : 0)
                    << ')';
    } else {
        s << "DrmConnector(0x0)";
    }
    return s;
}

}
