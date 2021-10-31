/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "resize.h"

namespace KWin
{

KWIN_EFFECT_FACTORY(ResizeEffectFactory,
                    ResizeEffect,
                    "metadata.json")

} // namespace KWin

#include "main.moc"