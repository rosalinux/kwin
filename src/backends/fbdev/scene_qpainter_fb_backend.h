/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SCENE_QPAINTER_FB_BACKEND_H
#define KWIN_SCENE_QPAINTER_FB_BACKEND_H

#include "outputlayer.h"
#include "qpainterbackend.h"

#include <QImage>
#include <QObject>

namespace KWin
{
class FramebufferBackend;
class FramebufferQPainterBackend;

class FramebufferQPainterOutput : public OutputLayer
{
public:
    FramebufferQPainterOutput(FramebufferQPainterBackend *backend);

    std::optional<OutputLayerBeginFrameInfo> beginFrame() override;
    bool endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion) override;
    FramebufferQPainterBackend *m_backend;
};

class FramebufferQPainterBackend : public QPainterBackend
{
    Q_OBJECT
public:
    FramebufferQPainterBackend(FramebufferBackend *backend);
    ~FramebufferQPainterBackend() override;

    OutputLayer *primaryLayer(Output *output) override
    {
        Q_UNUSED(output);
        return &m_output_layer;
    }
    void present(Output *output) override;

private:
    void reactivate();
    void deactivate();
    OutputLayerBeginFrameInfo beginFrame();
    /**
     * @brief mapped memory buffer on fb device
     */
    QImage m_renderBuffer;
    /**
     * @brief buffer to draw into
     */
    QImage m_backBuffer;
    FramebufferBackend *m_backend;
    FramebufferQPainterOutput m_output_layer;

    friend FramebufferQPainterOutput;
};

}

#endif
