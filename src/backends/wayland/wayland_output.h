/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

#include <KWayland/Client/xdgshell.h>

#include <QObject>
#include <QTimer>

namespace KWayland
{
namespace Client
{
class Surface;

class Shell;
class ShellSurface;

class Pointer;
class LockedPointer;
}
}

namespace KWin
{
namespace Wayland
{
class WaylandBackend;

class WaylandOutput : public Output
{
    Q_OBJECT
public:
    WaylandOutput(const QString &name, KWayland::Client::Surface *surface, WaylandBackend *backend);
    ~WaylandOutput() override;

    RenderLoop *renderLoop() const override;

    void init(const QPoint &logicalPosition, const QSize &pixelSize);

    virtual void lockPointer(KWayland::Client::Pointer *pointer, bool lock)
    {
        Q_UNUSED(pointer)
        Q_UNUSED(lock)
    }

    virtual bool pointerIsLocked()
    {
        return false;
    }

    /**
     * @brief defines the geometry of the output
     * @param logicalPosition top left position of the output in compositor space
     * @param pixelSize output size as seen from the outside
     */
    void setGeometry(const QPoint &logicalPosition, const QSize &pixelSize);

    KWayland::Client::Surface *surface() const
    {
        return m_surface;
    }

    void setDpmsMode(DpmsMode mode) override;

Q_SIGNALS:
    void sizeChanged(const QSize &size);
    void frameRendered();

protected:
    WaylandBackend *backend()
    {
        return m_backend;
    }

private:
    std::unique_ptr<RenderLoop> m_renderLoop;
    KWayland::Client::Surface *m_surface;
    WaylandBackend *m_backend;
    QTimer m_turnOffTimer;
};

class XdgShellOutput : public WaylandOutput
{
public:
    XdgShellOutput(const QString &name,
                   KWayland::Client::Surface *surface,
                   KWayland::Client::XdgShell *xdgShell,
                   WaylandBackend *backend, int number);
    ~XdgShellOutput() override;

    void lockPointer(KWayland::Client::Pointer *pointer, bool lock) override;

private:
    void handleConfigure(const QSize &size, KWayland::Client::XdgShellSurface::States states, quint32 serial);
    void updateWindowTitle();

    KWayland::Client::XdgShellSurface *m_xdgShellSurface = nullptr;
    int m_number;
    KWayland::Client::LockedPointer *m_pointerLock = nullptr;
    bool m_hasPointerLock = false;
    bool m_hasBeenConfigured = false;
};

} // namespace Wayland
} // namespace KWin
