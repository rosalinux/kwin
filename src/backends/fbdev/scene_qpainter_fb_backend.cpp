/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "scene_qpainter_fb_backend.h"

#include "composite.h"
#include "cursor.h"
#include "fb_backend.h"
#include "main.h"
#include "platform.h"
#include "renderloop.h"
#include "scene.h"
#include "screens.h"
#include "session.h"
#include "vsyncmonitor.h"
// Qt
#include <QPainter>

namespace KWin
{

FramebufferQPainterOutput::FramebufferQPainterOutput(FramebufferQPainterBackend *backend)
    : m_backend(backend)
{
}

std::optional<OutputLayerBeginFrameInfo> FramebufferQPainterOutput::beginFrame()
{
    return m_backend->beginFrame();
}

bool FramebufferQPainterOutput::endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    Q_UNUSED(renderedRegion)
    Q_UNUSED(damagedRegion)
    return true;
}

FramebufferQPainterBackend::FramebufferQPainterBackend(FramebufferBackend *backend)
    : QPainterBackend()
    , m_renderBuffer(backend->screenSize(), QImage::Format_RGB32)
    , m_backend(backend)
    , m_output_layer(this)
{
    m_renderBuffer.fill(Qt::black);
    m_backend->map();

    m_backBuffer = QImage((uchar *)m_backend->mappedMemory(),
                          m_backend->bytesPerLine() / (m_backend->bitsPerPixel() / 8),
                          m_backend->bufferSize() / m_backend->bytesPerLine(),
                          m_backend->bytesPerLine(), m_backend->imageFormat());
    m_backBuffer.fill(Qt::black);

    connect(kwinApp()->platform()->session(), &Session::activeChanged, this, [this](bool active) {
        if (active) {
            reactivate();
        } else {
            deactivate();
        }
    });
}

void FramebufferQPainterBackend::reactivate()
{
    const QVector<Output *> outputs = m_backend->outputs();
    for (Output *output : outputs) {
        output->renderLoop()->uninhibit();
    }
    Compositor::self()->scene()->addRepaintFull();
}

void FramebufferQPainterBackend::deactivate()
{
    const QVector<Output *> outputs = m_backend->outputs();
    for (Output *output : outputs) {
        output->renderLoop()->inhibit();
    }
}

FramebufferQPainterBackend::~FramebufferQPainterBackend() = default;

void FramebufferQPainterBackend::present(Output *output)
{
    if (!kwinApp()->platform()->session()->isActive()) {
        return;
    }

    static_cast<FramebufferOutput *>(output)->vsyncMonitor()->arm();

    QPainter p(&m_backBuffer);
    p.drawImage(QPoint(0, 0), m_backend->isBGR() ? m_renderBuffer.rgbSwapped() : m_renderBuffer);
}

OutputLayerBeginFrameInfo FramebufferQPainterBackend::beginFrame()
{

    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(&m_renderBuffer),
        .repaint = QRect(QPoint(0, 0), m_backend->screenSize()),
    };
}
}
