/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "output.h"
#include "outputconfiguration.h"
#include "workspace.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace KWin
{

QDebug operator<<(QDebug debug, const Output *output)
{
    QDebugStateSaver saver(debug);
    debug.nospace();
    if (output) {
        debug << output->metaObject()->className() << '(' << static_cast<const void *>(output);
        debug << ", name=" << output->name();
        debug << ", geometry=" << output->geometry();
        debug << ", scale=" << output->scale();
        if (debug.verbosity() > 2) {
            debug << ", manufacturer=" << output->manufacturer();
            debug << ", model=" << output->model();
            debug << ", serialNumber=" << output->serialNumber();
        }
        debug << ')';
    } else {
        debug << "Output(0x0)";
    }
    return debug;
}

OutputMode::OutputMode(const QSize &size, uint32_t refreshRate, Flags flags)
    : m_size(size)
    , m_refreshRate(refreshRate)
    , m_flags(flags)
{
}

QSize OutputMode::size() const
{
    return m_size;
}

uint32_t OutputMode::refreshRate() const
{
    return m_refreshRate;
}

OutputMode::Flags OutputMode::flags() const
{
    return m_flags;
}

Output::Output(QObject *parent)
    : QObject(parent)
{
    connect(Workspace::self(), &Workspace::configChanged, this, &Output::readTilingSettings);
}

Output::~Output()
{
}

QString Output::name() const
{
    return m_information.name;
}

QUuid Output::uuid() const
{
    return m_uuid;
}

Output::Transform Output::transform() const
{
    return m_transform;
}

QString Output::eisaId() const
{
    return m_information.eisaId;
}

QString Output::manufacturer() const
{
    return m_information.manufacturer;
}

QString Output::model() const
{
    return m_information.model;
}

QString Output::serialNumber() const
{
    return m_information.serialNumber;
}

bool Output::isInternal() const
{
    return m_information.internal;
}

void Output::inhibitDirectScanout()
{
    m_directScanoutCount++;
}

void Output::uninhibitDirectScanout()
{
    m_directScanoutCount--;
}

bool Output::directScanoutInhibited() const
{
    return m_directScanoutCount;
}

std::chrono::milliseconds Output::dimAnimationTime()
{
    // See kscreen.kcfg
    return std::chrono::milliseconds(KSharedConfig::openConfig()->group("Effect-Kscreen").readEntry("Duration", 250));
}

bool Output::usesSoftwareCursor() const
{
    return true;
}

QRect Output::mapFromGlobal(const QRect &rect) const
{
    return rect.translated(-geometry().topLeft());
}

Output::Capabilities Output::capabilities() const
{
    return m_information.capabilities;
}

qreal Output::scale() const
{
    return m_scale;
}

void Output::setScale(qreal scale)
{
    if (m_scale != scale) {
        m_scale = scale;
        Q_EMIT scaleChanged();
        Q_EMIT geometryChanged();
    }
}

QRect Output::geometry() const
{
    return QRect(m_position, pixelSize() / scale());
}

QSize Output::physicalSize() const
{
    return orientateSize(m_information.physicalSize);
}

int Output::refreshRate() const
{
    return m_currentMode->refreshRate();
}

void Output::moveTo(const QPoint &pos)
{
    if (m_position != pos) {
        m_position = pos;
        Q_EMIT geometryChanged();
    }
}

QSize Output::modeSize() const
{
    return m_currentMode->size();
}

QSize Output::pixelSize() const
{
    return orientateSize(m_currentMode->size());
}

QByteArray Output::edid() const
{
    return m_information.edid;
}

QList<std::shared_ptr<OutputMode>> Output::modes() const
{
    return m_modes;
}

std::shared_ptr<OutputMode> Output::currentMode() const
{
    return m_currentMode;
}

void Output::setModesInternal(const QList<std::shared_ptr<OutputMode>> &modes, const std::shared_ptr<OutputMode> &currentMode)
{
    const auto oldModes = m_modes;
    const auto oldCurrentMode = m_currentMode;

    m_modes = modes;
    m_currentMode = currentMode;

    if (m_modes != oldModes) {
        Q_EMIT modesChanged();
    }
    if (m_currentMode != oldCurrentMode) {
        Q_EMIT currentModeChanged();
        Q_EMIT geometryChanged();
    }
}

Output::SubPixel Output::subPixel() const
{
    return m_information.subPixel;
}

void Output::applyChanges(const OutputConfiguration &config)
{
    auto props = config.constChangeSet(this);
    Q_EMIT aboutToChange();

    setEnabled(props->enabled);
    setTransformInternal(props->transform);
    moveTo(props->pos);
    setScale(props->scale);
    setVrrPolicy(props->vrrPolicy);
    setRgbRangeInternal(props->rgbRange);

    Q_EMIT changed();
}

bool Output::isEnabled() const
{
    return m_isEnabled;
}

void Output::setEnabled(bool enable)
{
    if (m_isEnabled != enable) {
        m_isEnabled = enable;
        updateEnablement(enable);
        Q_EMIT enabledChanged();
    }
}

QString Output::description() const
{
    return manufacturer() + ' ' + model();
}

void Output::setCurrentModeInternal(const std::shared_ptr<OutputMode> &currentMode)
{
    if (m_currentMode != currentMode) {
        m_currentMode = currentMode;

        Q_EMIT currentModeChanged();
        Q_EMIT geometryChanged();
    }
}

static QUuid generateOutputId(const QString &eisaId, const QString &model,
                              const QString &serialNumber, const QString &name)
{
    static const QUuid urlNs = QUuid("6ba7b811-9dad-11d1-80b4-00c04fd430c8"); // NameSpace_URL
    static const QUuid kwinNs = QUuid::createUuidV5(urlNs, QStringLiteral("https://kwin.kde.org/o/"));

    const QString payload = QStringList{name, eisaId, model, serialNumber}.join(':');
    return QUuid::createUuidV5(kwinNs, payload);
}

void Output::setInformation(const Information &information)
{
    m_information = information;
    m_uuid = generateOutputId(eisaId(), model(), serialNumber(), name());
    readTilingSettings();
}

QSize Output::orientateSize(const QSize &size) const
{
    if (m_transform == Transform::Rotated90 || m_transform == Transform::Rotated270 || m_transform == Transform::Flipped90 || m_transform == Transform::Flipped270) {
        return size.transposed();
    }
    return size;
}

void Output::setTransformInternal(Transform transform)
{
    if (m_transform != transform) {
        m_transform = transform;
        Q_EMIT transformChanged();
        Q_EMIT currentModeChanged();
        Q_EMIT geometryChanged();
    }
}

void Output::setDpmsModeInternal(DpmsMode dpmsMode)
{
    if (m_dpmsMode != dpmsMode) {
        m_dpmsMode = dpmsMode;
        Q_EMIT dpmsModeChanged();
    }
}

void Output::setDpmsMode(DpmsMode mode)
{
    Q_UNUSED(mode)
}

Output::DpmsMode Output::dpmsMode() const
{
    return m_dpmsMode;
}

QMatrix4x4 Output::logicalToNativeMatrix(const QRect &rect, qreal scale, Transform transform)
{
    QMatrix4x4 matrix;
    matrix.scale(scale);

    switch (transform) {
    case Transform::Normal:
    case Transform::Flipped:
        break;
    case Transform::Rotated90:
    case Transform::Flipped90:
        matrix.translate(0, rect.width());
        matrix.rotate(-90, 0, 0, 1);
        break;
    case Transform::Rotated180:
    case Transform::Flipped180:
        matrix.translate(rect.width(), rect.height());
        matrix.rotate(-180, 0, 0, 1);
        break;
    case Transform::Rotated270:
    case Transform::Flipped270:
        matrix.translate(rect.height(), 0);
        matrix.rotate(-270, 0, 0, 1);
        break;
    }

    switch (transform) {
    case Transform::Flipped:
    case Transform::Flipped90:
    case Transform::Flipped180:
    case Transform::Flipped270:
        matrix.translate(rect.width(), 0);
        matrix.scale(-1, 1);
        break;
    default:
        break;
    }

    matrix.translate(-rect.x(), -rect.y());

    return matrix;
}

void Output::setOverscanInternal(uint32_t overscan)
{
    if (m_overscan != overscan) {
        m_overscan = overscan;
        Q_EMIT overscanChanged();
    }
}

uint32_t Output::overscan() const
{
    return m_overscan;
}

void Output::setVrrPolicy(RenderLoop::VrrPolicy policy)
{
    if (renderLoop()->vrrPolicy() != policy && (capabilities() & Capability::Vrr)) {
        renderLoop()->setVrrPolicy(policy);
        Q_EMIT vrrPolicyChanged();
    }
}

RenderLoop::VrrPolicy Output::vrrPolicy() const
{
    return renderLoop()->vrrPolicy();
}

bool Output::isPlaceholder() const
{
    return m_information.placeholder;
}

bool Output::isNonDesktop() const
{
    return m_information.nonDesktop;
}

QList<QRectF> Output::customTilingZones() const
{
    QList<QRectF> tilingZones;
    for (const auto &r : m_tiles) {
        const auto &geom = geometry();
        tilingZones << QRectF(geom.x() + r.x() * geom.width(),
                              geom.y() + r.y() * geom.height(),
                              r.width() * geom.width(),
                              r.height() * geom.height());
    }

    return tilingZones;
}

Output::RgbRange Output::rgbRange() const
{
    return m_rgbRange;
}

void Output::setRgbRangeInternal(RgbRange range)
{
    if (m_rgbRange != range) {
        m_rgbRange = range;
        Q_EMIT rgbRangeChanged();
    }
}

void Output::setColorTransformation(const std::shared_ptr<ColorTransformation> &transformation)
{
    Q_UNUSED(transformation);
}

QRectF Output::parseTilingJSon(const QJsonValue &val, const QString &layoutDirection, const QRectF &availableArea)
{
    if (availableArea.isEmpty()) {
        return availableArea;
    }

    auto ret = availableArea;

    if (val.isObject()) {
        const auto &obj = val.toObject();
        if (obj.contains(QStringLiteral("tiles"))) {
            // It's a layout
            const auto arr = obj.value(QStringLiteral("tiles"));
            const auto direction = obj.value(QStringLiteral("layoutDirection"));
            if (arr.isArray() && direction.isString()) {
                const QString dir = direction.toString();
                auto avail = availableArea;
                if (dir == QStringLiteral("horizontal")) {
                    const auto height = obj.value(QStringLiteral("height"));
                    if (height.isDouble()) {
                        avail.setHeight(height.toDouble());
                    }
                    parseTilingJSon(arr, dir, avail);
                    ret.setTop(avail.bottom());
                    return ret;
                } else if (dir == QStringLiteral("vertical")) {
                    const auto width = obj.value(QStringLiteral("width"));
                    if (width.isDouble()) {
                        avail.setWidth(width.toDouble());
                    }
                    parseTilingJSon(arr, dir, avail);
                    ret.setLeft(avail.right());
                    return ret;
                }
            }
        } else if (layoutDirection == QStringLiteral("horizontal")) {
            QRectF rect(availableArea.x(), availableArea.y(), 0, availableArea.height());
            const auto width = obj.value(QStringLiteral("width"));
            if (width.isDouble()) {
                rect.setWidth(qMin(width.toDouble(), availableArea.width()));
            }
            if (!rect.isEmpty()) {
                m_tiles << rect;
            }
            ret.setLeft(ret.left() + rect.width());
            return ret;
        } else if (layoutDirection == QStringLiteral("vertical")) {
            QRectF rect(availableArea.x(), availableArea.y(), availableArea.width(), 0);
            const auto height = obj.value(QStringLiteral("height"));
            if (height.isDouble()) {
                rect.setHeight(qMin(height.toDouble(), availableArea.height()));
            }
            if (!rect.isEmpty()) {
                m_tiles << rect;
            }
            ret.setTop(ret.top() + rect.height());
            return ret;
        }
    } else if (val.isArray()) {
        const auto arr = val.toArray();
        auto avail = availableArea;
        for (auto it = arr.cbegin(); it != arr.cend(); it++) {
            if ((*it).isObject()) {
                avail = parseTilingJSon(*it, layoutDirection, avail);
            }
        }
        return avail;
    }
    return ret;
}

void Output::readTilingSettings()
{
    m_tiles.clear();

    KConfigGroup cg = kwinApp()->config()->group(QStringLiteral("Tiling"));
    cg = KConfigGroup(&cg, uuid().toString(QUuid::WithoutBraces));

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(cg.readEntry("tiles", QByteArray()), &error);

    if (error.error != QJsonParseError::NoError) {
        qCWarning(KWIN_CORE) << "Parse error in tiles configuration for monitor" << uuid().toString(QUuid::WithoutBraces) << ":" << error.errorString();
        return;
    }

    parseTilingJSon(doc.object(), QString(), QRectF(0, 0, 1, 1));
}

} // namespace KWin
