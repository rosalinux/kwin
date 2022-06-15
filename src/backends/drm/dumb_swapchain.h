/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2021 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "drm_surface.h"
#include "utils/damagejournal.h"

#include <QImage>
#include <QSize>
#include <QVector>
#include <memory>

namespace KWin
{

class DrmDumbBuffer;
class DrmGpu;

class DumbSwapchain : public DrmSurface
{
public:
    DumbSwapchain(DrmGpu *gpu, const QSize &size, uint32_t drmFormat);

    std::shared_ptr<DrmDumbBuffer> acquireBuffer(QRegion *needsRepaint = nullptr);
    std::shared_ptr<DrmDumbBuffer> currentBuffer() const;
    void releaseBuffer(const std::shared_ptr<DrmDumbBuffer> &buffer, const QRegion &damage = {});

    qsizetype slotCount() const;
    bool isEmpty() const;

private:
    struct Slot
    {
        std::shared_ptr<DrmDumbBuffer> buffer;
        int age = 0;
    };

    int index = 0;
    QVector<Slot> m_slots;
    DamageJournal m_damageJournal;
};

}
