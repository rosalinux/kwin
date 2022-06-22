/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/common.h"

#include <QHash>
#include <QRect>
#include <QString>
#include <QVector>

namespace KWin
{

class Window;

class PlacementTracker : public QObject
{
    Q_OBJECT
public:
    void add(Window *window);
    void remove(Window *window);

    void restore(const QString &key);
    void setKey(const QString &key);

    void inhibit();
    void uninhibit();

private:
    void saveGeometry(Window *window);
    void saveInteractionCounter(Window *window);
    void saveMaximize(KWin::Window *window, MaximizeMode mode);
    void saveQuickTile();
    void saveFullscreen();

    struct WindowData
    {
        QRectF geometry;
        MaximizeMode maximize;
        QuickTileMode quickTile;
        bool fullscreen;
        uint32_t interactiveMoveResizeCount;
    };
    QHash<QString, QHash<Window *, WindowData>> m_data;
    QString m_currentKey;
    int m_inhibitCount = 0;
};

}
