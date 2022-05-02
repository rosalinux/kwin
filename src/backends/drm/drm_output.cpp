/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_output.h"
#include "drm_backend.h"
#include "drm_buffer.h"
#include "drm_gpu.h"
#include "drm_object_connector.h"
#include "drm_object_crtc.h"
#include "drm_pipeline.h"

#include "composite.h"
#include "cursor.h"
#include "drm_dumb_buffer.h"
#include "drm_layer.h"
#include "dumb_swapchain.h"
#include "egl_gbm_backend.h"
#include "kwinglutils.h"
#include "logging.h"
#include "main.h"
#include "outputconfiguration.h"
#include "renderloop.h"
#include "renderloop_p.h"
#include "scene.h"
#include "screens.h"
#include "session.h"
// Qt
#include <QCryptographicHash>
#include <QMatrix4x4>
#include <QPainter>
// c++
#include <cerrno>
// drm
#include <drm_fourcc.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>

namespace KWin
{

DrmOutput::DrmOutput(const QVector<DrmConnector *> &connectors)
    : DrmAbstractOutput(connectors.first()->gpu())
    , m_connectors(connectors)
{
    const auto first = connectors.first();
    Capabilities capabilities = Capability::Dpms;
    if (first->hasOverscan() && connectors.size() == 1) {
        capabilities |= Capability::Overscan;
        setOverscanInternal(first->overscan());
    }
    if (std::all_of(connectors.begin(), connectors.end(), [](const auto &c) {
            return c->vrrCapable();
        })) {
        capabilities |= Capability::Vrr;
        setVrrPolicy(RenderLoop::VrrPolicy::Automatic);
    }
    if (std::all_of(connectors.begin(), connectors.end(), [](const auto &c) {
            return c->hasRgbRange();
        })) {
        capabilities |= Capability::RgbRange;
        setRgbRangeInternal(first->rgbRange());
    }

    const Edid *edid = first->edid();

    setInformation(Information{
        .name = first->connectorName(),
        .manufacturer = edid->manufacturerString(),
        .model = first->modelName(),
        .serialNumber = edid->serialNumber(),
        .eisaId = edid->eisaId(),
        .physicalSize = first->physicalSize(),
        .edid = edid->raw(),
        .subPixel = first->subpixel(),
        .capabilities = capabilities,
        .internal = first->isInternal(),
    });

    const QList<QSharedPointer<OutputMode>> modes = getModes();
    QSharedPointer<OutputMode> currentMode = first->pipeline()->mode();
    if (!currentMode || connectors.size() > 1) {
        currentMode = modes.constFirst();
    }
    setModesInternal(modes, currentMode);
    m_renderLoop->setRefreshRate(currentMode->refreshRate());

    m_turnOffTimer.setSingleShot(true);
    m_turnOffTimer.setInterval(dimAnimationTime());
    connect(&m_turnOffTimer, &QTimer::timeout, this, [this] {
        setDrmDpmsMode(DpmsMode::Off);
    });

    for (const auto &conn : connectors) {
        m_renderOutputs << QSharedPointer<DrmRenderOutput>::create(this, conn->pipeline());
        m_pipelines << conn->pipeline();
        conn->pipeline()->setOutput(this);
    }
}

DrmOutput::~DrmOutput()
{
    for (const auto &pipeline : qAsConst(m_pipelines)) {
        pipeline->setOutput(nullptr);
    }
}

QList<QSharedPointer<OutputMode>> DrmOutput::getModes() const
{
    if (m_connectors.size() == 1) {
        const auto drmModes = m_connectors.first()->modes();

        QList<QSharedPointer<OutputMode>> ret;
        ret.reserve(drmModes.count());
        for (const QSharedPointer<DrmConnectorMode> &drmMode : drmModes) {
            ret.append(drmMode);
        }
        return ret;
    } else {
        return {QSharedPointer<OutputMode>::create(m_connectors.first()->totalTiledOutputSize(), m_connectors.first()->modes().constFirst()->refreshRate(), OutputMode::Flag::Preferred)};
    }
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
    for (const auto &pipeline : qAsConst(m_pipelines)) {
        pipeline->setActive(active);
    }
    if (DrmPipeline::commitPipelines(m_pipelines, active ? DrmPipeline::CommitMode::Test : DrmPipeline::CommitMode::CommitModeset)) {
        applyPipelines();
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
        revertPipelines();
        if (isEnabled() && isActive && !active) {
            m_gpu->platform()->checkOutputsAreOn();
        }
        return false;
    }
}

DrmPlane::Transformations outputToPlaneTransform(DrmOutput::Transform transform)
{
    using OutTrans = DrmOutput::Transform;
    using PlaneTrans = DrmPlane::Transformation;

    // TODO: Do we want to support reflections (flips)?

    switch (transform) {
    case OutTrans::Normal:
    case OutTrans::Flipped:
        return PlaneTrans::Rotate0;
    case OutTrans::Rotated90:
    case OutTrans::Flipped90:
        return PlaneTrans::Rotate90;
    case OutTrans::Rotated180:
    case OutTrans::Flipped180:
        return PlaneTrans::Rotate180;
    case OutTrans::Rotated270:
    case OutTrans::Flipped270:
        return PlaneTrans::Rotate270;
    default:
        Q_UNREACHABLE();
    }
}

void DrmOutput::updateModes()
{
    const QList<QSharedPointer<OutputMode>> modes = getModes();

    if (m_pipelines.constFirst()->crtc()) {
        bool needsCommit = false;
        for (const auto &pipeline : qAsConst(m_pipelines)) {
            const auto currentMode = pipeline->connector()->findMode(pipeline->crtc()->queryCurrentMode());
            if (currentMode != pipeline->mode()) {
                // DrmConnector::findCurrentMode might fail
                pipeline->setMode(currentMode ? currentMode : pipeline->connector()->modes().constFirst());
                needsCommit = true;
            }
        }
        if (needsCommit) {
            if (m_gpu->testPendingConfiguration()) {
                applyPipelines();
                m_renderLoop->setRefreshRate(m_pipelines.constFirst()->mode()->refreshRate());
            } else {
                qCWarning(KWIN_DRM) << "Setting changed mode failed!";
                revertPipelines();
            }
        }
    }

    QSharedPointer<OutputMode> currentMode = m_pipelines.constFirst()->mode();
    if (!currentMode) {
        currentMode = modes.constFirst();
    }

    setModesInternal(modes, currentMode);
}

bool DrmOutput::present()
{
    RenderLoopPrivate *renderLoopPrivate = RenderLoopPrivate::get(m_renderLoop);
    bool needsTest = false;
    for (const auto &pipeline : qAsConst(m_pipelines)) {
        if (pipeline->syncMode() != renderLoopPrivate->presentMode) {
            pipeline->setSyncMode(renderLoopPrivate->presentMode);
            needsTest = true;
        }
    }
    if (needsTest) {
        if (DrmPipeline::commitPipelines(m_pipelines, DrmPipeline::CommitMode::Test)) {
            applyPipelines();
        } else {
            revertPipelines();
        }
    }
    bool modeset = gpu()->needsModeset();
    if (modeset ? DrmPipeline::maybeModeset(m_pipelines) : DrmPipeline::presentPipelines(m_pipelines)) {
        QRegion damage;
        for (const auto &pipeline : qAsConst(m_pipelines)) {
            damage |= pipeline->primaryLayer()->currentDamage();
        }
        Q_EMIT outputChange(damage);
        return true;
    } else if (!modeset) {
        qCWarning(KWIN_DRM) << "Presentation failed!" << strerror(errno);
        frameFailed();
    }
    return false;
}

QVector<DrmConnector *> DrmOutput::connectors() const
{
    return m_connectors;
}

QVector<DrmPipeline *> DrmOutput::pipelines() const
{
    return m_pipelines;
}

bool DrmOutput::queueChanges(const OutputConfiguration &config)
{
    static bool valid;
    static int envOnlySoftwareRotations = qEnvironmentVariableIntValue("KWIN_DRM_SW_ROTATIONS_ONLY", &valid) == 1 || !valid;

    const auto props = config.constChangeSet(this);
    for (const auto &pipeline : qAsConst(m_pipelines)) {
        pipeline->setActive(props->enabled);
        const auto modelist = pipeline->connector()->modes();
        if (m_pipelines.size() == 1) {
            const auto it = std::find_if(modelist.begin(), modelist.end(), [&props](const auto &mode) {
                return mode->size() == props->modeSize && mode->refreshRate() == props->refreshRate;
            });
            if (it == modelist.end()) {
                qCWarning(KWIN_DRM).nospace() << "Could not find mode " << props->modeSize << "@" << props->refreshRate << " for output " << this;
                return false;
            }
            pipeline->setMode(*it);
        } else {
            pipeline->setMode(pipeline->connector()->modes().constFirst());
        }
        pipeline->setOverscan(props->overscan);
        pipeline->setRgbRange(props->rgbRange);
        pipeline->setRenderOrientation(outputToPlaneTransform(props->transform));
        if (!envOnlySoftwareRotations && m_gpu->atomicModeSetting()) {
            pipeline->setBufferOrientation(pipeline->renderOrientation());
        }
        pipeline->setEnable(props->enabled);
    }
    return true;
}

void DrmOutput::applyQueuedChanges(const OutputConfiguration &config)
{
    if (!std::all_of(m_connectors.begin(), m_connectors.end(), [](const auto &c) {
            return c->isConnected();
        })) {
        return;
    }
    Q_EMIT aboutToChange();
    applyPipelines();

    auto props = config.constChangeSet(this);
    setEnabled(props->enabled && m_pipelines.constFirst()->crtc());
    bool modeset = std::any_of(m_pipelines.constBegin(), m_pipelines.constEnd(), [](const auto &pipeline) {
        return pipeline->needsModeset();
    });
    if (!isEnabled() && modeset) {
        m_gpu->maybeModeset();
    }
    moveTo(props->pos);
    setScale(props->scale);
    setTransformInternal(props->transform);

    if (m_connectors.size() == 1) {
        const auto mode = m_pipelines.constFirst()->mode();
        setCurrentModeInternal(mode);
        m_renderLoop->setRefreshRate(mode->refreshRate());
    }
    setOverscanInternal(props->overscan);
    setRgbRangeInternal(props->rgbRange);
    setVrrPolicy(props->vrrPolicy);

    m_renderLoop->scheduleRepaint();
    Q_EMIT changed();

    for (const auto &renderOutput : qAsConst(m_renderOutputs)) {
        static_cast<DrmRenderOutput *>(renderOutput.get())->updateCursor();
    }
}

void DrmOutput::revertQueuedChanges()
{
    revertPipelines();
}

void DrmOutput::setColorTransformation(const QSharedPointer<ColorTransformation> &transformation)
{
    for (const auto &pipeline : qAsConst(m_pipelines)) {
        pipeline->setColorTransformation(transformation);
    }
    if (DrmPipeline::commitPipelines(m_pipelines, DrmPipeline::CommitMode::Test)) {
        applyPipelines();
        m_renderLoop->scheduleRepaint();
    } else {
        revertPipelines();
    }
}

QVector<QSharedPointer<RenderOutput>> DrmOutput::renderOutputs() const
{
    return m_renderOutputs;
}

void DrmOutput::applyPipelines()
{
    for (const auto &pipeline : qAsConst(m_pipelines)) {
        pipeline->applyPendingChanges();
    }
}

void DrmOutput::revertPipelines()
{
    for (const auto &pipeline : qAsConst(m_pipelines)) {
        pipeline->revertPendingChanges();
    }
}

void DrmOutput::pageFlipped(std::chrono::nanoseconds timestamp) const
{
    bool allFlipped = std::none_of(m_pipelines.constBegin(), m_pipelines.constEnd(), [](const auto &pipeline) {
        return pipeline->pageflipPending();
    });
    if (allFlipped) {
        DrmAbstractOutput::pageFlipped(timestamp);
    }
}

void DrmOutput::updateCursor()
{
    for (const auto &renderOutput : qAsConst(m_renderOutputs)) {
        static_cast<DrmRenderOutput *>(renderOutput.get())->updateCursor();
    }
}

DrmRenderOutput::DrmRenderOutput(DrmOutput *output, DrmPipeline *pipeline)
    : m_output(output)
    , m_pipeline(pipeline)
{
    updateGeometry();
    connect(output, &DrmOutput::geometryChanged, this, &DrmRenderOutput::updateGeometry);
    connect(Cursors::self(), &Cursors::currentCursorChanged, this, &DrmRenderOutput::updateCursor);
    connect(Cursors::self(), &Cursors::hiddenChanged, this, &DrmRenderOutput::updateCursor);
    connect(Cursors::self(), &Cursors::positionChanged, this, &DrmRenderOutput::moveCursor);
}

void DrmRenderOutput::updateGeometry()
{
    const auto geom = m_output->geometry();
    int x = geom.x() + geom.width() * m_pipeline->connector()->tilePosition().x();
    int y = geom.y() + geom.height() * m_pipeline->connector()->tilePosition().y();
    int width = geom.width() * m_pipeline->connector()->tileSize().width();
    int height = geom.height() * m_pipeline->connector()->tileSize().height();
    m_geometry = QRect(x, y, width, height);
    Q_EMIT geometryChanged();
}

DrmOutputLayer *DrmRenderOutput::outputLayer() const
{
    return m_pipeline->primaryLayer();
}

QRect DrmRenderOutput::geometry() const
{
    return m_geometry;
}

Output *DrmRenderOutput::platformOutput() const
{
    return m_output;
}

bool DrmRenderOutput::usesSoftwareCursor() const
{
    return !m_setCursorSuccessful || !m_moveCursorSuccessful;
}

void DrmRenderOutput::updateCursor()
{
    const auto gpu = m_output->gpu();
    static bool valid;
    static const bool forceSoftwareCursor = qEnvironmentVariableIntValue("KWIN_FORCE_SW_CURSOR", &valid) == 1 && valid;
    // hardware cursors are broken with the NVidia proprietary driver
    if (forceSoftwareCursor || (!valid && gpu->isNVidia())) {
        m_setCursorSuccessful = false;
        return;
    }
    const auto layer = m_pipeline->cursorLayer();
    if (!m_pipeline->crtc() || !layer) {
        return;
    }
    const Cursor *cursor = Cursors::self()->currentCursor();
    if (!cursor || cursor->image().isNull() || Cursors::self()->isCursorHidden()) {
        if (layer->isVisible()) {
            layer->setVisible(false);
            m_pipeline->setCursor();
        }
        return;
    }
    const QMatrix4x4 monitorMatrix = Output::logicalToNativeMatrix(geometry(), m_output->scale(), m_output->transform());
    const QRect cursorRect = monitorMatrix.mapRect(cursor->geometry());
    if (cursorRect.width() > gpu->cursorSize().width() || cursorRect.height() > gpu->cursorSize().height()) {
        if (layer->isVisible()) {
            layer->setVisible(false);
            m_pipeline->setCursor();
        }
        m_setCursorSuccessful = false;
        return;
    }
    if (dynamic_cast<EglGbmBackend *>(gpu->platform()->renderBackend())) {
        renderCursorOpengl(cursor->geometry().size() * m_output->scale());
    } else {
        renderCursorQPainter();
    }
    const QSize surfaceSize = gpu->cursorSize() / m_output->scale();
    const QRect layerRect = monitorMatrix.mapRect(QRect(cursor->geometry().topLeft(), surfaceSize));
    layer->setPosition(layerRect.topLeft());
    layer->setVisible(cursor->geometry().intersects(geometry()));
    if (layer->isVisible()) {
        m_setCursorSuccessful = m_pipeline->setCursor(Output::logicalToNativeMatrix(QRect(QPoint(), layerRect.size()), m_output->scale(), m_output->transform()).map(cursor->hotspot()));
        layer->setVisible(m_setCursorSuccessful);
    }
}

void DrmRenderOutput::moveCursor()
{
    if (!m_setCursorSuccessful || !m_pipeline->crtc()) {
        return;
    }
    const auto layer = m_pipeline->cursorLayer();
    Cursor *cursor = Cursors::self()->currentCursor();
    if (!cursor || cursor->image().isNull() || Cursors::self()->isCursorHidden() || !cursor->geometry().intersects(geometry())) {
        if (layer->isVisible()) {
            layer->setVisible(false);
            m_pipeline->setCursor();
        }
        return;
    }
    const QMatrix4x4 monitorMatrix = Output::logicalToNativeMatrix(geometry(), m_output->scale(), m_output->transform());
    const QSize surfaceSize = m_output->gpu()->cursorSize() / m_output->scale();
    const QRect cursorRect = monitorMatrix.mapRect(QRect(cursor->geometry().topLeft(), surfaceSize));
    layer->setVisible(true);
    layer->setPosition(cursorRect.topLeft());
    m_moveCursorSuccessful = m_pipeline->moveCursor();
    layer->setVisible(m_moveCursorSuccessful);
    if (!m_moveCursorSuccessful) {
        m_pipeline->setCursor();
    }
}

void DrmRenderOutput::renderCursorOpengl(const QSize &cursorSize)
{
    const auto layer = m_pipeline->cursorLayer();
    auto allocateTexture = [this]() {
        const QImage img = Cursors::self()->currentCursor()->image();
        if (img.isNull()) {
            m_cursorTextureDirty = false;
            return;
        }
        m_cursorTexture.reset(new GLTexture(img));
        m_cursorTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        m_cursorTextureDirty = false;
    };

    const auto [renderTarget, repaint] = layer->beginFrame();

    if (!m_cursorTexture) {
        allocateTexture();

        // handle shape update on case cursor image changed
        connect(Cursors::self(), &Cursors::currentCursorChanged, this, [this]() {
            m_cursorTextureDirty = true;
        });
    } else if (m_cursorTextureDirty) {
        const QImage image = Cursors::self()->currentCursor()->image();
        if (image.size() == m_cursorTexture->size()) {
            m_cursorTexture->update(image);
            m_cursorTextureDirty = false;
        } else {
            allocateTexture();
        }
    }

    QMatrix4x4 mvp;
    mvp.ortho(QRect(QPoint(), renderTarget.size()));

    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_cursorTexture->bind();
    ShaderBinder binder(ShaderTrait::MapTexture);
    binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, mvp);
    m_cursorTexture->render(QRect(0, 0, cursorSize.width(), cursorSize.height()));
    m_cursorTexture->unbind();
    glDisable(GL_BLEND);

    layer->endFrame(infiniteRegion(), infiniteRegion());
}

void DrmRenderOutput::renderCursorQPainter()
{
    const auto layer = m_pipeline->cursorLayer();
    const Cursor *cursor = Cursors::self()->currentCursor();
    const QImage cursorImage = cursor->image();

    const auto [renderTarget, repaint] = layer->beginFrame();

    QImage *c = std::get<QImage *>(renderTarget.nativeHandle());
    c->setDevicePixelRatio(m_output->scale());
    c->fill(Qt::transparent);

    QPainter p;
    p.begin(c);
    p.setWorldTransform(Output::logicalToNativeMatrix(cursor->rect(), 1, m_output->transform()).toTransform());
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QPoint(0, 0), cursorImage);
    p.end();

    layer->endFrame(infiniteRegion(), infiniteRegion());
}
}
