/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QString>
#include <memory>

#include "colorpipelinestage.h"
#include "kwin_export.h"

typedef void *cmsHPROFILE;

namespace KWin
{

class KWIN_EXPORT ColorSpace
{
public:
    ColorSpace(cmsHPROFILE profile);
    ~ColorSpace();

    std::shared_ptr<ColorPipelineStage> tag() const;

private:
    cmsHPROFILE m_profile;
    std::shared_ptr<ColorPipelineStage> m_tag;
};

}
