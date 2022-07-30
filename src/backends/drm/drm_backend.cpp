/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_backend.h"

#include <config-kwin.h>

#include "backends/libinput/libinputbackend.h"
#include "drm_egl_backend.h"
#include "drm_fourcc.h"
#include "drm_gpu.h"
#include "drm_logging.h"
#include "drm_object_connector.h"
#include "drm_object_crtc.h"
#include "drm_object_plane.h"
#include "drm_output.h"
#include "drm_pipeline.h"
#include "drm_qpainter_backend.h"
#include "drm_render_backend.h"
#include "drm_virtual_output.h"
#include "gbm_dmabuf.h"
#include "main.h"
#include "outputconfiguration.h"
#include "renderloop.h"
#include "session.h"
#include "utils/udev.h"
// KF5
#include <KCoreAddons>
#include <KLocalizedString>
// Qt
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
// system
#include <algorithm>
#include <cerrno>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
// drm
#include <gbm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>

namespace KWin
{

static QStringList splitPathList(const QString &input, const QChar delimiter)
{
    QStringList ret;
    QString tmp;
    for (int i = 0; i < input.size(); i++) {
        if (input[i] == delimiter) {
            if (i > 0 && input[i - 1] == '\\') {
                tmp[tmp.size() - 1] = delimiter;
            } else if (!tmp.isEmpty()) {
                ret.append(tmp);
                tmp = QString();
            }
        } else {
            tmp.append(input[i]);
        }
    }
    if (!tmp.isEmpty()) {
        ret.append(tmp);
    }
    return ret;
}

DrmBackend::DrmBackend(Session *session, QObject *parent)
    : Platform(parent)
    , m_udev(std::make_unique<Udev>())
    , m_udevMonitor(m_udev->monitor())
    , m_session(session)
    , m_explicitGpus(splitPathList(qEnvironmentVariable("KWIN_DRM_DEVICES"), ':'))
    , m_dpmsFilter()
{
    setSupportsPointerWarping(true);
    setSupportsGammaControl(true);
    supportsOutputChanges();
}

DrmBackend::~DrmBackend() = default;

Session *DrmBackend::session() const
{
    return m_session;
}

bool DrmBackend::isActive() const
{
    return m_active;
}

Outputs DrmBackend::outputs() const
{
    return m_outputs;
}

void DrmBackend::createDpmsFilter()
{
    if (m_dpmsFilter) {
        // already another output is off
        return;
    }
    m_dpmsFilter = std::make_unique<DpmsInputEventFilter>();
    input()->prependInputEventFilter(m_dpmsFilter.get());
}

void DrmBackend::turnOutputsOn()
{
    m_dpmsFilter.reset();
    for (auto it = m_enabledOutputs.constBegin(), end = m_enabledOutputs.constEnd(); it != end; it++) {
        (*it)->setDpmsMode(Output::DpmsMode::On);
    }
}

void DrmBackend::checkOutputsAreOn()
{
    if (!m_dpmsFilter) {
        // already disabled, all outputs are on
        return;
    }
    for (auto it = m_enabledOutputs.constBegin(), end = m_enabledOutputs.constEnd(); it != end; it++) {
        if ((*it)->dpmsMode() != Output::DpmsMode::On) {
            // dpms still disabled, need to keep the filter
            return;
        }
    }
    // all outputs are on, disable the filter
    m_dpmsFilter.reset();
}

void DrmBackend::activate(bool active)
{
    if (active) {
        qCDebug(KWIN_DRM) << "Activating session.";
        reactivate();
    } else {
        qCDebug(KWIN_DRM) << "Deactivating session.";
        deactivate();
    }
}

void DrmBackend::reactivate()
{
    if (m_active) {
        return;
    }
    m_active = true;

    for (const auto &output : qAsConst(m_outputs)) {
        output->renderLoop()->uninhibit();
        output->renderLoop()->scheduleRepaint();
    }

    // While the session had been inactive, an output could have been added or
    // removed, we need to re-scan outputs.
    updateOutputs();
    Q_EMIT activeChanged();
}

void DrmBackend::deactivate()
{
    if (!m_active) {
        return;
    }

    for (const auto &output : qAsConst(m_outputs)) {
        output->renderLoop()->inhibit();
    }

    m_active = false;
    Q_EMIT activeChanged();
}

bool DrmBackend::initialize()
{
    // TODO: Pause/Resume individual GPU devices instead.
    connect(m_session, &Session::devicePaused, this, [this](dev_t deviceId) {
        if (primaryGpu()->deviceId() == deviceId) {
            deactivate();
        }
    });
    connect(m_session, &Session::deviceResumed, this, [this](dev_t deviceId) {
        if (primaryGpu()->deviceId() == deviceId) {
            reactivate();
        }
    });
    connect(m_session, &Session::awoke, this, &DrmBackend::turnOutputsOn);

    if (!m_explicitGpus.isEmpty()) {
        for (const QString &fileName : m_explicitGpus) {
            addGpu(fileName);
        }
    } else {
        const auto devices = m_udev->listGPUs();
        for (const UdevDevice::Ptr &device : devices) {
            if (device->seat() == m_session->seat()) {
                addGpu(device->devNode());
            }
        }
    }

    if (m_gpus.empty()) {
        qCWarning(KWIN_DRM) << "No suitable DRM devices have been found";
        return false;
    }

    // setup udevMonitor
    if (m_udevMonitor) {
        m_udevMonitor->filterSubsystemDevType("drm");
        const int fd = m_udevMonitor->fd();
        if (fd != -1) {
            QSocketNotifier *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
            connect(notifier, &QSocketNotifier::activated, this, &DrmBackend::handleUdevEvent);
            m_udevMonitor->enable();
        }
    }
    setReady(true);
    return true;
}

void DrmBackend::handleUdevEvent()
{
    while (auto device = m_udevMonitor->getDevice()) {
        if (!m_active) {
            continue;
        }

        // Ignore the device seat if the KWIN_DRM_DEVICES envvar is set.
        if (!m_explicitGpus.isEmpty()) {
            if (!m_explicitGpus.contains(device->devNode())) {
                continue;
            }
        } else {
            if (device->seat() != m_session->seat()) {
                continue;
            }
        }

        if (device->action() == QStringLiteral("add")) {
            qCDebug(KWIN_DRM) << "New gpu found:" << device->devNode();
            if (addGpu(device->devNode())) {
                updateOutputs();
            }
        } else if (device->action() == QStringLiteral("remove")) {
            DrmGpu *gpu = findGpu(device->devNum());
            if (gpu) {
                if (primaryGpu() == gpu) {
                    qCCritical(KWIN_DRM) << "Primary gpu has been removed! Quitting...";
                    kwinApp()->quit();
                    return;
                } else {
                    removeGpu(gpu);
                    updateOutputs();
                }
            }
        } else if (device->action() == QStringLiteral("change")) {
            DrmGpu *gpu = findGpu(device->devNum());
            if (!gpu) {
                gpu = addGpu(device->devNode());
            }
            if (gpu) {
                qCDebug(KWIN_DRM) << "Received change event for monitored drm device" << gpu->devNode();
                updateOutputs();
            }
        }
    }
}

DrmGpu *DrmBackend::addGpu(const QString &fileName)
{
    int fd = m_session->openRestricted(fileName);
    if (fd < 0) {
        qCWarning(KWIN_DRM) << "failed to open drm device at" << fileName;
        return nullptr;
    }

    // try to make a simple drm get resource call, if it fails it is not useful for us
    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) {
        qCDebug(KWIN_DRM) << "Skipping KMS incapable drm device node at" << fileName;
        m_session->closeRestricted(fd);
        return nullptr;
    }
    drmModeFreeResources(resources);

    struct stat buf;
    if (fstat(fd, &buf) == -1) {
        qCDebug(KWIN_DRM, "Failed to fstat %s: %s", qPrintable(fileName), strerror(errno));
        m_session->closeRestricted(fd);
        return nullptr;
    }

    m_gpus.push_back(std::make_unique<DrmGpu>(this, fileName, fd, buf.st_rdev));
    auto gpu = m_gpus.back().get();
    m_active = true;
    connect(gpu, &DrmGpu::outputAdded, this, &DrmBackend::addOutput);
    connect(gpu, &DrmGpu::outputRemoved, this, &DrmBackend::removeOutput);
    return gpu;
}

void DrmBackend::removeGpu(DrmGpu *gpu)
{
    auto it = std::find_if(m_gpus.begin(), m_gpus.end(), [gpu](const auto &g) {
        return g.get() == gpu;
    });
    if (it != m_gpus.end()) {
        qCDebug(KWIN_DRM) << "Removing gpu" << gpu->devNode();
        m_gpus.erase(it);
    }
}

void DrmBackend::addOutput(DrmAbstractOutput *o)
{
    m_outputs.append(o);
    Q_EMIT outputAdded(o);
    o->setEnabled(true);
}

void DrmBackend::removeOutput(DrmAbstractOutput *o)
{
    o->setEnabled(false);
    m_outputs.removeOne(o);
    Q_EMIT outputRemoved(o);
}

void DrmBackend::updateOutputs()
{
    const auto oldOutputs = m_outputs;
    for (auto it = m_gpus.begin(); it < m_gpus.end();) {
        auto gpu = it->get();
        gpu->updateOutputs();
        if (gpu->outputs().isEmpty() && gpu != primaryGpu()) {
            qCDebug(KWIN_DRM) << "removing unused GPU" << gpu->devNode();
            it = m_gpus.erase(it);
        } else {
            it++;
        }
    }

    std::sort(m_outputs.begin(), m_outputs.end(), [](DrmAbstractOutput *a, DrmAbstractOutput *b) {
        auto da = qobject_cast<DrmOutput *>(a);
        auto db = qobject_cast<DrmOutput *>(b);
        if (da && !db) {
            return true;
        } else if (da && db) {
            return da->pipeline()->connector()->id() < db->pipeline()->connector()->id();
        } else {
            return false;
        }
    });
    if (oldOutputs != m_outputs) {
        readOutputsConfiguration(m_outputs);
    }
    Q_EMIT screensQueried();
}

namespace KWinKScreenIntegration
{
/// See KScreen::Output::hashMd5
QString outputHash(DrmAbstractOutput *output)
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!output->edid().isEmpty()) {
        hash.addData(output->edid());
    } else {
        hash.addData(output->name().toLatin1());
    }
    return QString::fromLatin1(hash.result().toHex());
}

/// See KScreen::Config::connectedOutputsHash in libkscreen
QString connectedOutputsHash(const QVector<DrmAbstractOutput *> &outputs)
{
    QStringList hashedOutputs;
    hashedOutputs.reserve(outputs.count());
    for (auto output : qAsConst(outputs)) {
        if (!output->isPlaceholder() && !output->isNonDesktop()) {
            hashedOutputs << outputHash(output);
        }
    }
    std::sort(hashedOutputs.begin(), hashedOutputs.end());
    const auto hash = QCryptographicHash::hash(hashedOutputs.join(QString()).toLatin1(), QCryptographicHash::Md5);
    return QString::fromLatin1(hash.toHex());
}

QMap<DrmAbstractOutput *, QJsonObject> outputsConfig(const QVector<DrmAbstractOutput *> &outputs)
{
    const QString kscreenJsonPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kscreen/") % connectedOutputsHash(outputs));
    if (kscreenJsonPath.isEmpty()) {
        return {};
    }

    QFile f(kscreenJsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(KWIN_DRM) << "Could not open file" << kscreenJsonPath;
        return {};
    }

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(KWIN_DRM) << "Failed to parse" << kscreenJsonPath << error.errorString();
        return {};
    }

    QMap<DrmAbstractOutput *, QJsonObject> ret;
    const auto outputsJson = doc.array();
    for (const auto &outputJson : outputsJson) {
        const auto outputObject = outputJson.toObject();
        for (auto it = outputs.constBegin(), itEnd = outputs.constEnd(); it != itEnd;) {
            if (!ret.contains(*it) && outputObject["id"] == outputHash(*it)) {
                ret[*it] = outputObject;
                continue;
            }
            ++it;
        }
    }
    return ret;
}

/// See KScreen::Output::Rotation
enum Rotation {
    None = 1,
    Left = 2,
    Inverted = 4,
    Right = 8,
};

DrmOutput::Transform toDrmTransform(int rotation)
{
    switch (Rotation(rotation)) {
    case None:
        return DrmOutput::Transform::Normal;
    case Left:
        return DrmOutput::Transform::Rotated90;
    case Inverted:
        return DrmOutput::Transform::Rotated180;
    case Right:
        return DrmOutput::Transform::Rotated270;
    default:
        Q_UNREACHABLE();
    }
}

std::shared_ptr<OutputMode> parseMode(Output *output, const QJsonObject &modeInfo)
{
    const QJsonObject size = modeInfo["size"].toObject();
    const QSize modeSize = QSize(size["width"].toInt(), size["height"].toInt());
    const int refreshRate = round(modeInfo["refresh"].toDouble() * 1000);

    const auto modes = output->modes();
    auto it = std::find_if(modes.begin(), modes.end(), [&modeSize, &refreshRate](const auto &mode) {
        return mode->size() == modeSize && mode->refreshRate() == refreshRate;
    });
    return (it != modes.end()) ? *it : nullptr;
}
}

bool DrmBackend::readOutputsConfiguration(const QVector<DrmAbstractOutput *> &outputs)
{
    Q_ASSERT(!outputs.isEmpty());
    const auto outputsInfo = KWinKScreenIntegration::outputsConfig(outputs);

    OutputConfiguration cfg;
    // default position goes from left to right
    QPoint pos(0, 0);
    for (const auto &output : qAsConst(outputs)) {
        if (output->isPlaceholder() || output->isNonDesktop()) {
            continue;
        }
        auto props = cfg.changeSet(output);
        const QJsonObject outputInfo = outputsInfo[output];
        qCDebug(KWIN_DRM) << "Reading output configuration for " << output;
        if (!outputInfo.isEmpty()) {
            props->enabled = outputInfo["enabled"].toBool(true);
            const QJsonObject pos = outputInfo["pos"].toObject();
            props->pos = QPoint(pos["x"].toInt(), pos["y"].toInt());
            if (const QJsonValue scale = outputInfo["scale"]; !scale.isUndefined()) {
                props->scale = scale.toDouble(1.);
            }
            props->transform = KWinKScreenIntegration::toDrmTransform(outputInfo["rotation"].toInt());

            props->overscan = static_cast<uint32_t>(outputInfo["overscan"].toInt(props->overscan));
            props->vrrPolicy = static_cast<RenderLoop::VrrPolicy>(outputInfo["vrrpolicy"].toInt(static_cast<uint32_t>(props->vrrPolicy)));
            props->rgbRange = static_cast<Output::RgbRange>(outputInfo["rgbrange"].toInt(static_cast<uint32_t>(props->rgbRange)));

            if (const QJsonObject modeInfo = outputInfo["mode"].toObject(); !modeInfo.isEmpty()) {
                if (auto mode = KWinKScreenIntegration::parseMode(output, modeInfo)) {
                    props->mode = mode;
                }
            }
        } else {
            props->enabled = true;
            props->pos = pos;
            props->transform = DrmOutput::Transform::Normal;
        }
        pos.setX(pos.x() + output->geometry().width());
    }
    bool allDisabled = std::all_of(outputs.begin(), outputs.end(), [&cfg](const auto &output) {
        return !cfg.changeSet(output)->enabled;
    });
    if (allDisabled) {
        qCWarning(KWIN_DRM) << "KScreen config would disable all outputs!";
        return false;
    }
    if (!applyOutputChanges(cfg)) {
        qCWarning(KWIN_DRM) << "Applying KScreen config failed!";
        return false;
    }
    return true;
}

void DrmBackend::enableOutput(DrmAbstractOutput *output, bool enable)
{
    if (m_enabledOutputs.contains(output) == enable) {
        return;
    }
    if (enable) {
        m_enabledOutputs << output;
        checkOutputsAreOn();
        if (m_placeHolderOutput && !output->isNonDesktop()) {
            qCDebug(KWIN_DRM) << "removing placeholder output";
            primaryGpu()->removeVirtualOutput(m_placeHolderOutput);
            m_placeHolderOutput = nullptr;
            m_placeholderFilter.reset();
        }
    } else {
        int normalOutputsCount = std::count_if(m_enabledOutputs.begin(), m_enabledOutputs.end(), [](const auto output) {
            return !output->isNonDesktop();
        });
        if (normalOutputsCount == 1 && !output->isNonDesktop() && !kwinApp()->isTerminating()) {
            qCDebug(KWIN_DRM) << "adding placeholder output";
            m_placeHolderOutput = primaryGpu()->createVirtualOutput({}, m_enabledOutputs.constFirst()->pixelSize(), 1, DrmVirtualOutput::Type::Placeholder);
            // placeholder doesn't actually need to render anything
            m_placeHolderOutput->renderLoop()->inhibit();
            m_placeholderFilter = std::make_unique<PlaceholderInputEventFilter>();
            input()->prependInputEventFilter(m_placeholderFilter.get());
        }
        m_enabledOutputs.removeOne(output);
    }
}

std::unique_ptr<InputBackend> DrmBackend::createInputBackend()
{
    return std::make_unique<LibinputBackend>(m_session);
}

std::unique_ptr<QPainterBackend> DrmBackend::createQPainterBackend()
{
    return std::make_unique<DrmQPainterBackend>(this);
}

std::unique_ptr<OpenGLBackend> DrmBackend::createOpenGLBackend()
{
    return std::make_unique<EglGbmBackend>(this);
}

void DrmBackend::sceneInitialized()
{
    if (m_outputs.isEmpty()) {
        updateOutputs();
    } else {
        for (const auto &gpu : qAsConst(m_gpus)) {
            gpu->recreateSurfaces();
        }
    }
}

QVector<CompositingType> DrmBackend::supportedCompositors() const
{
    if (selectedCompositor() != NoCompositing) {
        return {selectedCompositor()};
    }
    return QVector<CompositingType>{OpenGLCompositing, QPainterCompositing};
}

QString DrmBackend::supportInformation() const
{
    QString supportInfo;
    QDebug s(&supportInfo);
    s.nospace();
    s << "Name: "
      << "DRM" << Qt::endl;
    s << "Active: " << m_active << Qt::endl;
    for (size_t g = 0; g < m_gpus.size(); g++) {
        s << "Atomic Mode Setting on GPU " << g << ": " << m_gpus.at(g)->atomicModeSetting() << Qt::endl;
    }
    return supportInfo;
}

Output *DrmBackend::createVirtualOutput(const QString &name, const QSize &size, double scale)
{
    auto output = primaryGpu()->createVirtualOutput(name, size * scale, scale, DrmVirtualOutput::Type::Virtual);
    readOutputsConfiguration(m_outputs);
    Q_EMIT screensQueried();
    return output;
}

void DrmBackend::removeVirtualOutput(Output *output)
{
    auto virtualOutput = qobject_cast<DrmVirtualOutput *>(output);
    if (!virtualOutput) {
        return;
    }
    primaryGpu()->removeVirtualOutput(virtualOutput);
}

gbm_bo *DrmBackend::createBo(const QSize &size, quint32 format, const QVector<uint64_t> &modifiers)
{
    const auto eglBackend = dynamic_cast<EglGbmBackend *>(m_renderBackend);
    if (!eglBackend || !primaryGpu()->gbmDevice()) {
        return nullptr;
    }

    return createGbmBo(primaryGpu()->gbmDevice(), size, format, modifiers);
}

std::optional<DmaBufParams> DrmBackend::testCreateDmaBuf(const QSize &size, quint32 format, const QVector<uint64_t> &modifiers)
{
    gbm_bo *bo = createBo(size, format, modifiers);
    if (!bo) {
        return {};
    }

    auto ret = dmaBufParamsForBo(bo);
    gbm_bo_destroy(bo);
    return ret;
}

std::shared_ptr<DmaBufTexture> DrmBackend::createDmaBufTexture(const QSize &size, quint32 format, uint64_t modifier)
{
    QVector<uint64_t> mods = {modifier};
    gbm_bo *bo = createBo(size, format, mods);
    if (!bo) {
        return {};
    }

    // The bo will be kept around until the last fd is closed.
    const DmaBufAttributes attributes = dmaBufAttributesForBo(bo);
    gbm_bo_destroy(bo);
    const auto eglBackend = static_cast<EglGbmBackend *>(m_renderBackend);
    eglBackend->makeCurrent();
    if (auto texture = eglBackend->importDmaBufAsTexture(attributes)) {
        return std::make_shared<DmaBufTexture>(texture, attributes);
    } else {
        for (int i = 0; i < attributes.planeCount; ++i) {
            ::close(attributes.fd[i]);
        }
        return nullptr;
    }
}

DrmGpu *DrmBackend::primaryGpu() const
{
    return m_gpus.empty() ? nullptr : m_gpus.front().get();
}

DrmGpu *DrmBackend::findGpu(dev_t deviceId) const
{
    auto it = std::find_if(m_gpus.begin(), m_gpus.end(), [deviceId](const auto &gpu) {
        return gpu->deviceId() == deviceId;
    });
    return it == m_gpus.end() ? nullptr : it->get();
}

bool DrmBackend::applyOutputChanges(const OutputConfiguration &config)
{
    QVector<DrmOutput *> toBeEnabled;
    QVector<DrmOutput *> toBeDisabled;
    for (const auto &gpu : qAsConst(m_gpus)) {
        const auto &outputs = gpu->outputs();
        for (const auto &o : outputs) {
            DrmOutput *output = qobject_cast<DrmOutput *>(o);
            if (!output || output->isNonDesktop()) {
                // virtual and non-desktop outputs don't need testing
                continue;
            }
            output->queueChanges(config);
            if (config.constChangeSet(output)->enabled) {
                toBeEnabled << output;
            } else {
                toBeDisabled << output;
            }
        }
        if (gpu->testPendingConfiguration() != DrmPipeline::Error::None) {
            for (const auto &output : qAsConst(toBeEnabled)) {
                output->revertQueuedChanges();
            }
            for (const auto &output : qAsConst(toBeDisabled)) {
                output->revertQueuedChanges();
            }
            return false;
        }
    }
    // first, apply changes to drm outputs.
    // This may remove the placeholder output and thus change m_outputs!
    for (const auto &output : qAsConst(toBeEnabled)) {
        output->applyQueuedChanges(config);
    }
    for (const auto &output : qAsConst(toBeDisabled)) {
        output->applyQueuedChanges(config);
    }
    // only then apply changes to the virtual outputs
    for (const auto &output : qAsConst(m_outputs)) {
        if (!qobject_cast<DrmOutput *>(output)) {
            output->applyChanges(config);
        }
    }
    return true;
}

void DrmBackend::setRenderBackend(DrmRenderBackend *backend)
{
    m_renderBackend = backend;
}

DrmRenderBackend *DrmBackend::renderBackend() const
{
    return m_renderBackend;
}

void DrmBackend::releaseBuffers()
{
    for (const auto &gpu : qAsConst(m_gpus)) {
        gpu->releaseBuffers();
    }
}
}
