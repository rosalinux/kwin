/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <kwinquickeffect.h>

namespace KWin
{

class TilesEditorEffect : public QuickSceneEffect
{
    Q_OBJECT

public:
    TilesEditorEffect();
    ~TilesEditorEffect() override;

public Q_SLOTS:
    void toggle();
    void activate();
    void deactivate(int timeout);

protected:
    QVariantMap initialProperties(EffectScreen *screen) override;

private:
    void realDeactivate();

    QTimer *m_shutdownTimer = nullptr;
    QAction *m_toggleAction = nullptr;
    QList<QKeySequence> m_toggleShortcut;
};

} // namespace KWin
