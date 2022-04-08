/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwineffects.h"
#include "kwingltexture.h"
#include "kwinglutils.h"

namespace KWin
{

struct DeformOffscreenData
{
    QScopedPointer<GLTexture> texture;
    QScopedPointer<GLRenderTarget> renderTarget;
    QSize textureSize;
    bool isDirty = true;
};

class DeformEffectPrivate
{
public:
    QHash<EffectWindow *, DeformOffscreenData *> windows;
    QMetaObject::Connection windowDamagedConnection;
    QMetaObject::Connection windowDeletedConnection;

    void paint(EffectWindow *window, GLTexture *texture, const QRegion &region,
               const WindowPaintData &data, const WindowQuadList &quads);

    GLTexture *maybeRender(EffectWindow *window, DeformOffscreenData *offscreenData);
    bool live = true;
};

} // namespace KWin
