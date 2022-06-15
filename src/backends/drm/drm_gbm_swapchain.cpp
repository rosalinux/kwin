/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_gbm_swapchain.h"
#include "drm_gpu.h"
#include "kwineffects.h"
#include "logging.h"

#include <drm_fourcc.h>

namespace KWin
{

GbmSwapchain::GbmSwapchain(const std::shared_ptr<GbmBuffer> &firstBuffer, const QVector<uint64_t> &modifiers, uint32_t flags)
    : DrmSurface(firstBuffer->gpu(), firstBuffer->size(), firstBuffer->format())
    , m_modifier(firstBuffer->modifier())
    , m_flags(flags)
    , m_creationModifiers(modifiers)
{
    m_buffers.push_back(firstBuffer);
}

std::tuple<std::shared_ptr<GbmBuffer>, QRegion> GbmSwapchain::acquire(const QRegion &damage)
{
    if (m_buffers.isEmpty()) {
        m_damageJournal.add(damage);
        gbm_bo *bo;
        if (m_modifier != DRM_FORMAT_MOD_INVALID) {
            bo = gbm_bo_create_with_modifiers2(m_gpu->gbmDevice(), m_size.width(), m_size.height(), m_format, &m_modifier, 1, m_flags);
        } else {
            bo = gbm_bo_create(m_gpu->gbmDevice(), m_size.width(), m_size.height(), m_format, m_flags);
        }
        m_renderCounter++;
        return {std::make_shared<GbmBuffer>(m_gpu, bo, shared_from_this()), infiniteRegion()};
    } else {
        auto ret = m_buffers.takeFirst();
        QRegion repaint = m_damageJournal.accumulate(m_renderCounter - ret->renderCounter(), infiniteRegion());
        m_damageJournal.add(damage);
        m_renderCounter++;
        ret->setRenderCounter(m_renderCounter);
        return {ret, repaint};
    }
}

bool GbmSwapchain::release(GbmBuffer *buffer)
{
    if (m_buffers.size() < 4) {
        m_buffers.push_back(std::make_shared<GbmBuffer>(buffer));
        return true;
    } else {
        return false;
    }
}

void GbmSwapchain::releaseBuffers()
{
    m_buffers.clear();
}

QVector<uint64_t> GbmSwapchain::creationModifiers() const
{
    return m_creationModifiers;
}

uint64_t GbmSwapchain::modifier() const
{
    return m_modifier;
}

uint32_t GbmSwapchain::flags() const
{
    return m_flags;
}

uint32_t GbmSwapchain::renderCounter() const
{
    return m_renderCounter;
}

std::variant<std::shared_ptr<GbmSwapchain>, GbmSwapchain::Error> GbmSwapchain::createSwapchain(DrmGpu *gpu, const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, uint32_t flags)
{
    const bool useModifiers = !modifiers.isEmpty() && (modifiers.size() > 1 || modifiers.first() != DRM_FORMAT_MOD_INVALID);

    gbm_bo *bo;
    if (useModifiers) {
        bo = gbm_bo_create_with_modifiers2(gpu->gbmDevice(), size.width(), size.height(), format, modifiers.constData(), modifiers.size(), flags);
    }
    if (!useModifiers || (!bo && errno == ENOSYS)) {
        bo = gbm_bo_create(gpu->gbmDevice(), size.width(), size.height(), format, flags);
    }
    if (bo) {
        return std::make_shared<GbmSwapchain>(std::make_shared<GbmBuffer>(gpu, bo), modifiers, flags);
    } else if (errno == ENOSYS) {
        return Error::ModifiersUnsupported;
    } else {
        return Error::Unknown;
    }
}

}
