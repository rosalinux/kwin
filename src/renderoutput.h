/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QRect>
#include <QScopedPointer>

#include "outputlayer.h"

namespace KWin
{

class Output;

class RenderOutput
{
public:
    virtual ~RenderOutput() = default;

    virtual QRect geometry() const = 0;
    virtual Output *platformOutput() const = 0;

    QSize pixelSize() const;
};

class SimpleRenderOutput : public RenderOutput
{
public:
    SimpleRenderOutput(Output *output);

    QRect geometry() const override;
    Output *platformOutput() const override;

private:
    Output *const m_output;
};

}
