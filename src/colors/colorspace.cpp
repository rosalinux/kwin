/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "colorspace.h"

#include <lcms2.h>

namespace KWin
{

ColorSpace::ColorSpace(cmsHPROFILE profile)
    : m_profile(profile)
{
    cmsToneCurve **vcgt = static_cast<cmsToneCurve **>(cmsReadTag(m_profile, cmsSigVcgtTag));
    if (vcgt && vcgt[0]) {
        cmsToneCurve *toneCurves[] = {
            cmsDupToneCurve(vcgt[0]),
            cmsDupToneCurve(vcgt[1]),
            cmsDupToneCurve(vcgt[2]),
        };
        m_tag = std::make_shared<ColorPipelineStage>(cmsStageAllocToneCurves(nullptr, 3, toneCurves));
    }
}

ColorSpace::~ColorSpace()
{
    cmsCloseProfile(m_profile);
}

std::shared_ptr<ColorPipelineStage> ColorSpace::tag() const
{
    return m_tag;
}

}
