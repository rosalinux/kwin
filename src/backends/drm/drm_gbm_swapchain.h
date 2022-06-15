/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include "drm_buffer_gbm.h"
#include "drm_surface.h"
#include "utils/damagejournal.h"

#include <QQueue>
#include <variant>

namespace KWin
{

class GbmSwapchain : public std::enable_shared_from_this<GbmSwapchain>, public DrmSurface
{
public:
    GbmSwapchain(const std::shared_ptr<GbmBuffer> &firstBuffer, const QVector<uint64_t> &modifiers, uint32_t flags);

    std::tuple<std::shared_ptr<GbmBuffer>, QRegion> acquire(const QRegion &damage);
    bool release(GbmBuffer *buffer);
    void releaseBuffers();

    uint32_t renderCounter() const;
    QVector<uint64_t> creationModifiers() const;
    uint64_t modifier() const;
    uint32_t flags() const;

    enum class Error {
        ModifiersUnsupported,
        Unknown
    };
    static std::variant<std::shared_ptr<GbmSwapchain>, Error> createSwapchain(DrmGpu *gpu, const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, uint32_t flags);

private:
    const uint64_t m_modifier;
    const uint32_t m_flags;
    const QVector<uint64_t> m_creationModifiers;

    uint32_t m_renderCounter = 0;
    DamageJournal m_damageJournal;
    QQueue<std::shared_ptr<GbmBuffer>> m_buffers;
};

}
