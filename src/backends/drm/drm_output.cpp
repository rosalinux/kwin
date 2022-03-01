/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_output.h"
#include "drm_backend.h"
#include "drm_object_crtc.h"
#include "drm_object_connector.h"
#include "drm_gpu.h"
#include "drm_pipeline.h"
#include "drm_buffer.h"

#include "composite.h"
#include "cursor.h"
#include "logging.h"
#include "main.h"
#include "renderloop.h"
#include "renderloop_p.h"
#include "scene.h"
#include "screens.h"
#include "session.h"
#include "waylandoutputconfig.h"
#include "dumb_swapchain.h"
#include "cursor.h"
#include "drm_layer.h"
// Qt
#include <QMatrix4x4>
#include <QCryptographicHash>
#include <QPainter>
// c++
#include <cerrno>
// drm
#include <xf86drm.h>
#include <libdrm/drm_mode.h>
#include <drm_fourcc.h>

namespace KWin
{

DrmOutput::DrmOutput(DrmPipeline *pipeline)
    : DrmAbstractOutput(pipeline->connector()->gpu())
    , m_pipeline(pipeline)
    , m_connector(pipeline->connector())
{
    m_pipeline->setDisplayDevice(this);
    const auto conn = m_pipeline->connector();
    m_renderLoop->setRefreshRate(m_pipeline->pending.mode->refreshRate());
    setSubPixelInternal(conn->subpixel());
    setInternal(conn->isInternal());
    setCapabilityInternal(DrmOutput::Capability::Dpms);
    if (conn->hasOverscan()) {
        setCapabilityInternal(Capability::Overscan);
        setOverscanInternal(conn->overscan());
    }
    if (conn->vrrCapable()) {
        setCapabilityInternal(Capability::Vrr);
        setVrrPolicy(RenderLoop::VrrPolicy::Automatic);
    }
    if (conn->hasRgbRange()) {
        setCapabilityInternal(Capability::RgbRange);
        setRgbRangeInternal(conn->rgbRange());
    }
    initOutputDevice();

    m_turnOffTimer.setSingleShot(true);
    m_turnOffTimer.setInterval(dimAnimationTime());
    connect(&m_turnOffTimer, &QTimer::timeout, this, [this] {
        setDrmDpmsMode(DpmsMode::Off);
    });

    connect(Cursors::self(), &Cursors::currentCursorChanged, this, &DrmOutput::updateCursor);
    connect(Cursors::self(), &Cursors::hiddenChanged, this, &DrmOutput::updateCursor);
    connect(Cursors::self(), &Cursors::positionChanged, this, &DrmOutput::moveCursor);
}

DrmOutput::~DrmOutput()
{
    m_pipeline->setDisplayDevice(nullptr);
}

static bool isCursorSpriteCompatible(const QImage *buffer, const QImage *sprite)
{
    // Note that we need compare the rects in the device independent pixels because the
    // buffer and the cursor sprite image may have different scale factors.
    const QRect bufferRect(QPoint(0, 0), buffer->size() / buffer->devicePixelRatio());
    const QRect spriteRect(QPoint(0, 0), sprite->size() / sprite->devicePixelRatio());

    return bufferRect.contains(spriteRect);
}

void DrmOutput::updateCursor()
{
    static bool valid;
    static const bool forceSoftwareCursor = qEnvironmentVariableIntValue("KWIN_FORCE_SW_CURSOR", &valid) == 1 && valid;
    if (forceSoftwareCursor) {
        m_setCursorSuccessful = false;
        return;
    }
    if (!m_pipeline->pending.crtc) {
        return;
    }
    const Cursor *cursor = Cursors::self()->currentCursor();
    if (!cursor) {
        m_pipeline->setCursor(nullptr);
        return;
    }
    const QImage cursorImage = cursor->image();
    if (cursorImage.isNull() || Cursors::self()->isCursorHidden()) {
        m_pipeline->setCursor(nullptr);
        return;
    }
    if (m_cursor && m_cursor->isEmpty()) {
        m_pipeline->setCursor(nullptr);
        return;
    }
    const auto plane = m_pipeline->pending.crtc->cursorPlane();
    if (!m_cursor || (plane && !plane->formats().value(m_cursor->drmFormat()).contains(DRM_FORMAT_MOD_LINEAR))) {
        if (plane) {
            const auto formatModifiers = plane->formats();
            const auto formats = formatModifiers.keys();
            for (uint32_t format : formats) {
                if (!formatModifiers[format].contains(DRM_FORMAT_MOD_LINEAR)) {
                    continue;
                }
                m_cursor = QSharedPointer<DumbSwapchain>::create(m_gpu, m_gpu->cursorSize(), format, QImage::Format::Format_ARGB32_Premultiplied);
                if (!m_cursor->isEmpty()) {
                    break;
                }
            }
        } else {
            m_cursor = QSharedPointer<DumbSwapchain>::create(m_gpu, m_gpu->cursorSize(), DRM_FORMAT_XRGB8888, QImage::Format::Format_ARGB32_Premultiplied);
        }
        if (!m_cursor || m_cursor->isEmpty()) {
            m_pipeline->setCursor(nullptr);
            m_setCursorSuccessful = false;
            return;
        }
    }
    m_cursor->releaseBuffer(m_cursor->currentBuffer());
    m_cursor->acquireBuffer();
    QImage *c = m_cursor->currentBuffer()->image();
    c->setDevicePixelRatio(scale());
    if (!isCursorSpriteCompatible(c, &cursorImage)) {
        // If the cursor image is too big, fall back to rendering the software cursor.
        m_pipeline->setCursor(nullptr);
        m_setCursorSuccessful = false;
        return;
    }
    c->fill(Qt::transparent);
    QPainter p;
    p.begin(c);
    p.setWorldTransform(logicalToNativeMatrix(cursor->rect(), 1, transform()).toTransform());
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QPoint(0, 0), cursorImage);
    p.end();
    m_setCursorSuccessful = m_pipeline->setCursor(m_cursor->currentBuffer(), logicalToNativeMatrix(cursor->rect(), scale(), transform()).map(cursor->hotspot()));
    moveCursor();
}

void DrmOutput::moveCursor()
{
    if (!m_setCursorSuccessful || !m_pipeline->pending.crtc) {
        return;
    }
    Cursor *cursor = Cursors::self()->currentCursor();
    const QMatrix4x4 monitorMatrix = logicalToNativeMatrix(geometry(), scale(), transform());
    const QMatrix4x4 hotspotMatrix = logicalToNativeMatrix(cursor->rect(), scale(), transform());
    m_moveCursorSuccessful = m_pipeline->moveCursor(monitorMatrix.map(cursor->pos()) - hotspotMatrix.map(cursor->hotspot()));
    if (!m_moveCursorSuccessful) {
        m_pipeline->setCursor(nullptr);
    }
}

QVector<AbstractWaylandOutput::Mode> DrmOutput::getModes() const
{
    bool modeFound = false;
    QVector<Mode> modes;
    const auto modelist = m_pipeline->connector()->modes();

    modes.reserve(modelist.count());
    for (int i = 0; i < modelist.count(); ++i) {
        Mode mode;
        // compare the actual mode objects, not the pointers!
        if (*modelist[i] == *m_pipeline->pending.mode) {
            mode.flags |= ModeFlag::Current;
            modeFound = true;
        }
        if (modelist[i]->nativeMode()->type & DRM_MODE_TYPE_PREFERRED) {
            mode.flags |= ModeFlag::Preferred;
        }

        mode.id = i;
        mode.size = modelist[i]->size();
        mode.refreshRate = modelist[i]->refreshRate();
        modes << mode;
    }
    if (!modeFound) {
        // select first mode by default
        modes[0].flags |= ModeFlag::Current;
    }
    return modes;
}

void DrmOutput::initOutputDevice()
{
    const auto conn = m_pipeline->connector();
    setName(conn->connectorName());
    initialize(conn->modelName(), conn->edid()->manufacturerString(),
               conn->edid()->eisaId(), conn->edid()->serialNumber(),
               conn->physicalSize(), getModes(), conn->edid()->raw());
}

void DrmOutput::updateEnablement(bool enable)
{
    m_gpu->platform()->enableOutput(this, enable);
}

void DrmOutput::setDpmsMode(DpmsMode mode)
{
    if (mode == DpmsMode::Off) {
        if (!m_turnOffTimer.isActive()) {
            Q_EMIT aboutToTurnOff(std::chrono::milliseconds(m_turnOffTimer.interval()));
            m_turnOffTimer.start();
        }
        if (isEnabled()) {
            m_gpu->platform()->createDpmsFilter();
        }
    } else {
        m_turnOffTimer.stop();
        if (mode != dpmsMode() && setDrmDpmsMode(mode)) {
            Q_EMIT wakeUp();
        }
    }
}

bool DrmOutput::setDrmDpmsMode(DpmsMode mode)
{
    if (!isEnabled()) {
        return false;
    }
    bool active = mode == DpmsMode::On;
    bool isActive = dpmsMode() == DpmsMode::On;
    if (active == isActive) {
        setDpmsModeInternal(mode);
        return true;
    }
    m_pipeline->pending.active = active;
    if (DrmPipeline::commitPipelines({m_pipeline}, active ? DrmPipeline::CommitMode::Test : DrmPipeline::CommitMode::CommitModeset)) {
        m_pipeline->applyPendingChanges();
        setDpmsModeInternal(mode);
        if (active) {
            m_renderLoop->uninhibit();
            m_gpu->platform()->checkOutputsAreOn();
            if (Compositor::compositing()) {
                Compositor::self()->scene()->addRepaintFull();
            }
        } else {
            m_renderLoop->inhibit();
            m_gpu->platform()->createDpmsFilter();
        }
        return true;
    } else {
        qCWarning(KWIN_DRM) << "Setting dpms mode failed!";
        m_pipeline->revertPendingChanges();
        if (isEnabled() && isActive && !active) {
            m_gpu->platform()->checkOutputsAreOn();
        }
        return false;
    }
}

void DrmOutput::updateModes()
{
    setModes(getModes());
    if (m_pipeline->pending.crtc) {
        const auto currentMode = m_pipeline->connector()->findMode(m_pipeline->pending.crtc->queryCurrentMode());
        if (currentMode != m_pipeline->pending.mode) {
            // DrmConnector::findCurrentMode might fail
            m_pipeline->pending.mode = currentMode ? currentMode : m_pipeline->connector()->modes().constFirst();
            if (m_gpu->testPendingConfiguration(DrmGpu::TestMode::TestWithCrtcReallocation)) {
                m_pipeline->applyPendingChanges();
                setCurrentModeInternal(m_pipeline->pending.mode->size(), m_pipeline->pending.mode->refreshRate());
                m_renderLoop->setRefreshRate(m_pipeline->pending.mode->refreshRate());
            } else {
                qCWarning(KWIN_DRM) << "Setting changed mode failed!";
                m_pipeline->revertPendingChanges();
            }
        }
    }
}

bool DrmOutput::present()
{
    RenderLoopPrivate *renderLoopPrivate = RenderLoopPrivate::get(m_renderLoop);
    if (m_pipeline->pending.syncMode != renderLoopPrivate->presentMode) {
        m_pipeline->pending.syncMode = renderLoopPrivate->presentMode;
        if (DrmPipeline::commitPipelines({m_pipeline}, DrmPipeline::CommitMode::Test)) {
            m_pipeline->applyPendingChanges();
        } else {
            m_pipeline->revertPendingChanges();
            setVrrPolicy(RenderLoop::VrrPolicy::Never);
        }
    }
    if (m_pipeline->present()) {
        Q_EMIT outputChange(m_pipeline->pending.layer->currentDamage());
        return true;
    } else {
        qCWarning(KWIN_DRM) << "Presentation failed!" << strerror(errno);
        frameFailed();
        return false;
    }
}

bool DrmOutput::testScanout()
{
    return m_pipeline->testScanout();
}

int DrmOutput::gammaRampSize() const
{
    return m_pipeline->pending.crtc ? m_pipeline->pending.crtc->gammaRampSize() : 256;
}

bool DrmOutput::setGammaRamp(const GammaRamp &gamma)
{
    m_pipeline->pending.gamma = QSharedPointer<DrmGammaRamp>::create(m_gpu, gamma);
    if (DrmPipeline::commitPipelines({m_pipeline}, DrmPipeline::CommitMode::Test)) {
        m_pipeline->applyPendingChanges();
        m_renderLoop->scheduleRepaint();
        return true;
    } else {
        m_pipeline->revertPendingChanges();
        return false;
    }
}

DrmConnector *DrmOutput::connector() const
{
    return m_connector;
}

DrmPipeline *DrmOutput::pipeline() const
{
    return m_pipeline;
}

QSize DrmOutput::bufferSize() const
{
    return m_pipeline->bufferSize();
}

QSize DrmOutput::sourceSize() const
{
    return m_pipeline->sourceSize();
}

bool DrmOutput::isFormatSupported(uint32_t drmFormat) const
{
    return m_pipeline->isFormatSupported(drmFormat);
}

QVector<uint64_t> DrmOutput::supportedModifiers(uint32_t drmFormat) const
{
    return m_pipeline->supportedModifiers(drmFormat);
}

static uint32_t angle(DrmOutput::Transform transform)
{
    switch (transform) {
    case DrmOutput::Transform::Normal:
        return 0;
    case DrmOutput::Transform::Rotated90:
        return 90;
    case DrmOutput::Transform::Rotated180:
        return 180;
    case DrmOutput::Transform::Rotated270:
        return 270;
    default:
        Q_UNREACHABLE();
    }
}

static uint32_t angle(DrmConnector::PanelOrientation orientation) {
    switch (orientation) {
    case DrmConnector::PanelOrientation::Normal:
        return 0;
    case DrmConnector::PanelOrientation::RightUp:
        return 90;
    case DrmConnector::PanelOrientation::UpsideDown:
        return 180;
    case DrmConnector::PanelOrientation::LeftUp:
        return 270;
    default:
        Q_UNREACHABLE();
    }
}

static DrmPlane::Transformations transformation(uint32_t angle) {
    switch (angle % 360) {
    case 0:
        return DrmPlane::Transformation::Rotate0;
    case 90:
        return DrmPlane::Transformation::Rotate90;
    case 180:
        return DrmPlane::Transformation::Rotate180;
    case 270:
        return DrmPlane::Transformation::Rotate270;
    default:
        Q_UNREACHABLE();
    }
}

bool DrmOutput::queueChanges(const WaylandOutputConfig &config)
{
    static bool valid;
    static int envOnlySoftwareRotations = qEnvironmentVariableIntValue("KWIN_DRM_SW_ROTATIONS_ONLY", &valid) == 1 || !valid;

    const auto props = config.constChangeSet(this);
    m_pipeline->pending.active = props->enabled;
    const auto modelist = m_connector->modes();
    const auto it = std::find_if(modelist.begin(), modelist.end(), [&props](const auto &mode) {
        return mode->size() == props->modeSize && mode->refreshRate() == props->refreshRate;
    });
    if (it == modelist.end()) {
        qCWarning(KWIN_DRM).nospace() << "Could not find mode " << props->modeSize << "@" << props->refreshRate << " for output " << this;
        return false;
    }
    m_pipeline->pending.mode = *it;
    m_pipeline->pending.overscan = props->overscan;
    m_pipeline->pending.rgbRange = props->rgbRange;
    m_pipeline->pending.sourceTransformation = transformation(angle(props->transform) + angle(m_connector->panelOrientation()));
    if (!envOnlySoftwareRotations && m_gpu->atomicModeSetting()) {
        m_pipeline->pending.bufferTransformation = m_pipeline->pending.sourceTransformation;
    }
    m_pipeline->pending.enabled = props->enabled;
    return true;
}

void DrmOutput::applyQueuedChanges(const WaylandOutputConfig &config)
{
    if (!m_connector->isConnected()) {
        return;
    }
    Q_EMIT aboutToChange();
    m_pipeline->applyPendingChanges();

    auto props = config.constChangeSet(this);
    setEnabled(props->enabled && m_pipeline->pending.crtc);
    moveTo(props->pos);
    setScale(props->scale);
    setTransformInternal(props->transform);

    const auto &mode = m_pipeline->pending.mode;
    setCurrentModeInternal(mode->size(), mode->refreshRate());
    m_renderLoop->setRefreshRate(mode->refreshRate());
    setOverscanInternal(m_pipeline->pending.overscan);
    setRgbRangeInternal(m_pipeline->pending.rgbRange);
    setVrrPolicy(props->vrrPolicy);

    m_renderLoop->scheduleRepaint();
    Q_EMIT changed();
}

void DrmOutput::revertQueuedChanges()
{
    m_pipeline->revertPendingChanges();
}

int DrmOutput::maxBpc() const
{
    auto prop = m_connector->getProp(DrmConnector::PropertyIndex::MaxBpc);
    return prop ? prop->maxValue() : 8;
}

bool DrmOutput::usesSoftwareCursor() const
{
    return !m_setCursorSuccessful || !m_moveCursorSuccessful;
}

DrmPlane::Transformations DrmOutput::softwareTransforms() const
{
    if (m_pipeline->pending.bufferTransformation == m_pipeline->pending.sourceTransformation) {
        return DrmPlane::Transformation::Rotate0;
    } else {
        // TODO handle sourceTransformation != Rotate0
        return m_pipeline->pending.sourceTransformation;
    }
}

DrmLayer *DrmOutput::outputLayer() const
{
    return m_pipeline->pending.layer.data();
}

}
