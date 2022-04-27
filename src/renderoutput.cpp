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

SimpleRenderOutput::SimpleRenderOutput(Output *output)
    : m_output(output)
{
}

QRect SimpleRenderOutput::geometry() const
{
    return m_output->geometry();
}

Output *SimpleRenderOutput::platformOutput() const
{
    return m_output;
}

}
