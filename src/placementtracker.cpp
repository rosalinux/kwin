/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "placementtracker.h"
#include "platform.h"
#include "window.h"

namespace KWin
{

void PlacementTracker::add(Window *window)
{
    if (window->isUnmanaged()) {
        return;
    }
    connect(window, &Window::frameGeometryChanged, this, &PlacementTracker::saveGeometry);
    connect(window, qOverload<Window *, MaximizeMode>(&Window::clientMaximizedStateChanged), this, &PlacementTracker::saveMaximize);
    connect(window, &Window::quickTileModeChanged, this, &PlacementTracker::saveQuickTile);
    connect(window, &Window::fullScreenChanged, this, &PlacementTracker::saveFullscreen);
    connect(window, &Window::clientFinishUserMovedResized, this, &PlacementTracker::saveInteractionCounter);
    m_data[m_currentKey][window] = WindowData{
        .geometry = window->moveResizeGeometry(),
        .maximize = window->maximizeMode(),
        .quickTile = window->quickTileMode(),
        .fullscreen = window->isFullScreen(),
        .interactiveMoveResizeCount = window->interactiveMoveResizeCount(),
    };
}

void PlacementTracker::remove(Window *window)
{
    if (window->isUnmanaged()) {
        return;
    }
    disconnect(window, &Window::frameGeometryChanged, this, &PlacementTracker::saveGeometry);
    disconnect(window, qOverload<Window *, MaximizeMode>(&Window::clientMaximizedStateChanged), this, &PlacementTracker::saveMaximize);
    disconnect(window, &Window::quickTileModeChanged, this, &PlacementTracker::saveQuickTile);
    disconnect(window, &Window::fullScreenChanged, this, &PlacementTracker::saveFullscreen);
    disconnect(window, &Window::clientFinishUserMovedResized, this, &PlacementTracker::saveInteractionCounter);
    for (auto &dataMap : m_data) {
        dataMap.remove(window);
    }
}

void PlacementTracker::restore(const QString &key)
{
    inhibit();
    auto &dataMap = m_data[key];
    for (auto it = dataMap.begin(); it != dataMap.end(); it++) {
        Window *window = it.key();
        WindowData data = it.value();
        // don't touch windows where the user intentionally changed their state
        const bool userAction = window->interactiveMoveResizeCount() != data.interactiveMoveResizeCount
            || (window->maximizeMode() && window->maximizeMode() != data.maximize)
            || (window->quickTileMode() && window->quickTileMode() != data.quickTile)
            || (window->isFullScreen() && window->isFullScreen() != data.fullscreen);
        if (!userAction) {
            window->moveResize(data.geometry);
        }
    }
    uninhibit();
}

void PlacementTracker::setKey(const QString &key)
{
    m_currentKey = key;
}

void PlacementTracker::saveGeometry(Window *window)
{
    if (m_inhibitCount == 0) {
        m_data[m_currentKey][window].geometry = window->moveResizeGeometry();
    }
}

void PlacementTracker::saveInteractionCounter(Window *window)
{
    if (m_inhibitCount == 0) {
        m_data[m_currentKey][window].interactiveMoveResizeCount = window->interactiveMoveResizeCount();
    }
}

void PlacementTracker::saveMaximize(Window *window, MaximizeMode mode)
{
    if (m_inhibitCount == 0) {
        m_data[m_currentKey][window].maximize = mode;
    }
}

void PlacementTracker::saveQuickTile()
{
    Window *window = qobject_cast<Window *>(QObject::sender());
    Q_ASSERT(window);
    if (m_inhibitCount == 0) {
        m_data[m_currentKey][window].quickTile = window->quickTileMode();
    }
}

void PlacementTracker::saveFullscreen()
{
    Window *window = qobject_cast<Window *>(QObject::sender());
    Q_ASSERT(window);
    if (m_inhibitCount == 0) {
        m_data[m_currentKey][window].fullscreen = window->isFullScreen();
    }
}

void PlacementTracker::inhibit()
{
    m_inhibitCount++;
}

void PlacementTracker::uninhibit()
{
    Q_ASSERT(m_inhibitCount > 0);
    m_inhibitCount--;
}
}
