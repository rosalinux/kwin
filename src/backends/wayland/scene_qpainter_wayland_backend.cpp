/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2013, 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "scene_qpainter_wayland_backend.h"
#include "wayland_backend.h"
#include "wayland_output.h"

#include "composite.h"
#include "kwineffects.h"
#include "logging.h"

#include <KWayland/Client/buffer.h>
#include <KWayland/Client/shm_pool.h>
#include <KWayland/Client/surface.h>

#include <cmath>

namespace KWin
{
namespace Wayland
{

WaylandQPainterBufferSlot::WaylandQPainterBufferSlot(QSharedPointer<KWayland::Client::Buffer> buffer)
    : buffer(buffer)
    , image(buffer->address(), buffer->size().width(), buffer->size().height(), QImage::Format_RGB32)
{
    buffer->setUsed(true);
}

WaylandQPainterBufferSlot::~WaylandQPainterBufferSlot()
{
    buffer->setUsed(false);
}

WaylandQPainterOutput::WaylandQPainterOutput(WaylandOutput *output)
    : m_waylandOutput(output)
{
}

WaylandQPainterOutput::~WaylandQPainterOutput()
{
    qDeleteAll(m_slots);
    m_slots.clear();
}

bool WaylandQPainterOutput::init(KWayland::Client::ShmPool *pool)
{
    m_pool = pool;

    connect(pool, &KWayland::Client::ShmPool::poolResized, this, &WaylandQPainterOutput::remapBuffer);
    connect(m_waylandOutput, &WaylandOutput::sizeChanged, this, &WaylandQPainterOutput::updateSize);

    return true;
}

void WaylandQPainterOutput::remapBuffer()
{
    qCDebug(KWIN_WAYLAND_BACKEND) << "Remapped back buffer of surface" << m_waylandOutput->surface();

    const QSize nativeSize(m_waylandOutput->geometry().size() * m_waylandOutput->scale());
    for (WaylandQPainterBufferSlot *slot : qAsConst(m_slots)) {
        slot->image = QImage(slot->buffer->address(), nativeSize.width(), nativeSize.height(), QImage::Format_ARGB32);
    }
}

void WaylandQPainterOutput::updateSize(const QSize &size)
{
    Q_UNUSED(size)
    m_back = nullptr;
    qDeleteAll(m_slots);
    m_slots.clear();
}

void WaylandQPainterOutput::present()
{
    for (WaylandQPainterBufferSlot *slot : qAsConst(m_slots)) {
        if (slot == m_back) {
            slot->age = 1;
        } else if (slot->age > 0) {
            slot->age++;
        }
    }

    auto s = m_waylandOutput->surface();
    s->attachBuffer(m_back->buffer);
    s->damage(m_damageJournal.lastDamage());
    s->setScale(std::ceil(m_waylandOutput->scale()));
    s->commit();
}

WaylandQPainterBufferSlot *WaylandQPainterOutput::back() const
{
    return m_back;
}

WaylandQPainterBufferSlot *WaylandQPainterOutput::acquire()
{
    for (WaylandQPainterBufferSlot *slot : qAsConst(m_slots)) {
        if (slot->buffer->isReleased()) {
            m_back = slot;
            slot->buffer->setReleased(false);
            return m_back;
        }
    }

    const QSize nativeSize(m_waylandOutput->geometry().size() * m_waylandOutput->scale());
    auto buffer = m_pool->getBuffer(nativeSize, nativeSize.width() * 4).toStrongRef();
    if (!buffer) {
        qCDebug(KWIN_WAYLAND_BACKEND) << "Did not get a new Buffer from Shm Pool";
        return nullptr;
    }

    m_back = new WaylandQPainterBufferSlot(buffer);
    m_slots.append(m_back);

    //    qCDebug(KWIN_WAYLAND_BACKEND) << "Created a new back buffer for output surface" << m_waylandOutput->surface();
    return m_back;
}

QRegion WaylandQPainterOutput::accumulateDamage(int bufferAge) const
{
    return m_damageJournal.accumulate(bufferAge, infiniteRegion());
}

OutputLayerBeginFrameInfo WaylandQPainterOutput::beginFrame()
{
    WaylandQPainterBufferSlot *slot = acquire();
    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(&slot->image),
        .repaint = accumulateDamage(slot->age),
    };
}

bool WaylandQPainterOutput::endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    Q_UNUSED(renderedRegion)

    m_damageJournal.add(damagedRegion);
    return true;
}

WaylandQPainterBackend::WaylandQPainterBackend(Wayland::WaylandBackend *b)
    : QPainterBackend()
    , m_backend(b)
{

    const auto waylandOutputs = m_backend->waylandOutputs();
    for (auto *output : waylandOutputs) {
        createOutput(output);
    }
    connect(m_backend, &WaylandBackend::outputAdded, this, &WaylandQPainterBackend::createOutput);
    connect(m_backend, &WaylandBackend::outputRemoved, this, [this](Output *waylandOutput) {
        auto it = std::ranges::find_if(m_outputs, [waylandOutput](const auto &output) {
            return output->m_waylandOutput == waylandOutput;
        });
        if (it == m_outputs.end()) {
            return;
        }
        m_outputs.erase(it);
    });
}

WaylandQPainterBackend::~WaylandQPainterBackend()
{
}

void WaylandQPainterBackend::createOutput(Output *waylandOutput)
{
    const auto output = std::make_shared<WaylandQPainterOutput>(static_cast<WaylandOutput *>(waylandOutput));
    output->init(m_backend->shmPool());
    m_outputs.insert(waylandOutput, output);
}

void WaylandQPainterBackend::present(Output *output)
{
    m_outputs[output]->present();
}

OutputLayer *WaylandQPainterBackend::primaryLayer(Output *output)
{
    return m_outputs[output].get();
}
}
}
