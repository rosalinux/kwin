/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QSize>

namespace KWin
{

class DrmGpu;

class DrmSurface
{
public:
    DrmSurface(DrmGpu *gpu, const QSize &size, uint32_t format);

    DrmGpu *gpu() const;
    QSize size() const;
    uint32_t format() const;

protected:
    DrmGpu *const m_gpu;
    const QSize m_size;
    const uint32_t m_format;
};

}
