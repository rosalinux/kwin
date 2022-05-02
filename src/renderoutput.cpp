/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "renderoutput.h"
#include "output.h"

namespace KWin
{

QSize RenderOutput::pixelSize() const
{
    return geometry().size() * platformOutput()->scale();
}

QRect RenderOutput::rect() const
{
    return QRect(QPoint(), geometry().size());
}

bool RenderOutput::usesSoftwareCursor() const
{
    return true;
}

QRect RenderOutput::mapFromGlobal(const QRect &rect) const
{
    return rect.translated(-geometry().topLeft());
}

QRect RenderOutput::relativePixelGeometry() const
{
    return geometry();
}

SimpleRenderOutput::SimpleRenderOutput(Output *output, bool useSoftwareCursor)
    : m_output(output)
    , m_useSoftwareCursor(useSoftwareCursor)
{
    connect(output, &Output::geometryChanged, this, &RenderOutput::geometryChanged);
}

QRect SimpleRenderOutput::geometry() const
{
    return m_output->geometry();
}

Output *SimpleRenderOutput::platformOutput() const
{
    return m_output;
}

bool SimpleRenderOutput::usesSoftwareCursor() const
{
    return m_useSoftwareCursor;
}

QRect SimpleRenderOutput::relativePixelGeometry() const
{
    QPoint position(geometry().topLeft().x() * m_output->scale() / m_output->pixelSize().width(), geometry().topLeft().y() * m_output->scale() / m_output->pixelSize().height());
    return QRect(position, m_output->pixelSize());
}
}
