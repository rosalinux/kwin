/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_surface.h"

namespace KWin
{

DrmSurface::DrmSurface(DrmGpu *gpu, const QSize &size, uint32_t format)
    : m_gpu(gpu)
    , m_size(size)
    , m_format(format)
{
}

DrmGpu *DrmSurface::gpu() const
{
    return m_gpu;
}

QSize DrmSurface::size() const
{
    return m_size;
}

uint32_t DrmSurface::format() const
{
    return m_format;
}

}
