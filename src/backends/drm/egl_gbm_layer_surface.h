/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QMap>
#include <QPointer>
#include <QRegion>
#include <optional>

#include "drm_object_plane.h"
#include "outputlayer.h"

namespace KWaylandServer
{
class SurfaceInterface;
class LinuxDmaBufV1ClientBuffer;
}

namespace KWin
{

class DrmFramebuffer;
class GbmSurface;
class DumbSwapchain;
class ShadowBuffer;
class EglGbmBackend;
class SurfaceItem;
class GLTexture;
class GbmBuffer;
class GbmSwapchain;

class GbmSwapchainWrapper
{
public:
    GbmSwapchainWrapper(const std::shared_ptr<GbmSwapchain> &swapchain, EglGbmBackend *backend);
    GbmSwapchainWrapper(const std::shared_ptr<GbmSurface> &surface, EglGbmBackend *backend);

    QSize size() const;
    uint32_t format() const;
    QVector<uint64_t> modifiers() const;
    uint32_t flags() const;

    bool makeContextCurrent() const;
    std::shared_ptr<GbmBuffer> swapBuffers(const QRegion &dirty);
    void aboutToStartPainting(DrmOutput *output, const QRegion &damagedRegion);
    QRegion repaintRegion() const;
    GLFramebuffer *fbo() const;

    std::shared_ptr<GbmBuffer> testBuffer();

private:
    EglGbmBackend *m_backend;
    const std::shared_ptr<GbmSurface> m_surface;
    const std::shared_ptr<GbmSwapchain> m_swapchain;
    std::shared_ptr<GbmBuffer> m_swapchainBuffer;
    QRegion m_repaint;
};

class EglGbmLayerSurface
{
public:
    EglGbmLayerSurface(DrmGpu *gpu, EglGbmBackend *eglBackend);
    ~EglGbmLayerSurface();

    OutputLayerBeginFrameInfo startRendering(const QSize &bufferSize, DrmPlane::Transformations renderOrientation, DrmPlane::Transformations bufferOrientation, const QMap<uint32_t, QVector<uint64_t>> &formats, uint32_t additionalFlags = 0);
    void aboutToStartPainting(DrmOutput *output, const QRegion &damagedRegion);
    std::optional<std::tuple<std::shared_ptr<DrmFramebuffer>, QRegion>> endRendering(DrmPlane::Transformations renderOrientation, const QRegion &damagedRegion);

    bool doesSurfaceFit(const QSize &size, const QMap<uint32_t, QVector<uint64_t>> &formats) const;
    std::shared_ptr<GLTexture> texture() const;
    void destroyResources();
    EglGbmBackend *eglBackend() const;
    std::shared_ptr<DrmFramebuffer> renderTestBuffer(const QSize &bufferSize, const QMap<uint32_t, QVector<uint64_t>> &formats, uint32_t additionalFlags = 0);

private:
    bool checkGbmSurface(const QSize &size, const QMap<uint32_t, QVector<uint64_t>> &formats, uint32_t flags);
    bool createGbmSurface(const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, uint32_t flags);
    bool createGbmSurface(const QSize &size, const QMap<uint32_t, QVector<uint64_t>> &formats, uint32_t flags);
    bool doesGbmSurfaceFit(GbmSwapchainWrapper *surf, const QSize &size, const QMap<uint32_t, QVector<uint64_t>> &formats) const;

    bool doesShadowBufferFit(ShadowBuffer *buffer, const QSize &size, DrmPlane::Transformations renderOrientation, DrmPlane::Transformations bufferOrientation) const;
    bool doesSwapchainFit(DumbSwapchain *swapchain) const;

    std::shared_ptr<DrmFramebuffer> importBuffer();
    std::shared_ptr<DrmFramebuffer> importDmabuf();
    std::shared_ptr<DrmFramebuffer> importWithCpu();

    enum class MultiGpuImportMode {
        Dmabuf,
        DumbBuffer,
        DumbBufferXrgb8888,
        Failed
    };
    MultiGpuImportMode m_importMode = MultiGpuImportMode::Dmabuf;

    QRegion m_currentDamage;
    std::shared_ptr<GbmBuffer> m_currentBuffer;
    std::shared_ptr<GbmSwapchainWrapper> m_gbmSurface;
    std::shared_ptr<GbmSwapchainWrapper> m_oldGbmSurface;
    std::shared_ptr<ShadowBuffer> m_shadowBuffer;
    std::shared_ptr<ShadowBuffer> m_oldShadowBuffer;
    std::shared_ptr<DumbSwapchain> m_importSwapchain;
    std::shared_ptr<DumbSwapchain> m_oldImportSwapchain;

    DrmGpu *const m_gpu;
    EglGbmBackend *const m_eglBackend;
};

}
