/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "wayland_output.h"
#include "renderloop.h"
#include "wayland_backend.h"
#include "wayland_server.h"

#include <KWayland/Client/pointerconstraints.h>
#include <KWayland/Client/surface.h>

#include <KLocalizedString>

namespace KWin
{
namespace Wayland
{

using namespace KWayland::Client;
static const int s_refreshRate = 60000; // TODO: can we get refresh rate data from Wayland host?

WaylandOutput::WaylandOutput(const QString &name, Surface *surface, WaylandBackend *backend)
    : Output(backend)
    , m_renderLoop(std::make_unique<RenderLoop>())
    , m_surface(surface)
    , m_backend(backend)
{
    setInformation(Information{
        .name = name,
        .model = name,
        .capabilities = Capability::Dpms,
    });

    connect(surface, &Surface::frameRendered, this, &WaylandOutput::frameRendered);
    m_turnOffTimer.setSingleShot(true);
    m_turnOffTimer.setInterval(dimAnimationTime());
    connect(&m_turnOffTimer, &QTimer::timeout, this, [this] {
        setDpmsModeInternal(DpmsMode::Off);
    });
}

WaylandOutput::~WaylandOutput()
{
    m_surface->destroy();
    delete m_surface;
}

RenderLoop *WaylandOutput::renderLoop() const
{
    return m_renderLoop.get();
}

void WaylandOutput::init(const QPoint &logicalPosition, const QSize &pixelSize)
{
    m_renderLoop->setRefreshRate(s_refreshRate);

    auto mode = std::make_shared<OutputMode>(pixelSize, s_refreshRate);
    setModesInternal({mode}, mode);

    moveTo(logicalPosition);
    setScale(backend()->initialOutputScale());
}

void WaylandOutput::setGeometry(const QPoint &logicalPosition, const QSize &pixelSize)
{
    auto mode = std::make_shared<OutputMode>(pixelSize, s_refreshRate);
    setModesInternal({mode}, mode);

    moveTo(logicalPosition);
    Q_EMIT m_backend->screensQueried();
}

void WaylandOutput::setDpmsMode(DpmsMode mode)
{
    if (mode == DpmsMode::Off) {
        if (!m_turnOffTimer.isActive()) {
            Q_EMIT aboutToTurnOff(std::chrono::milliseconds(m_turnOffTimer.interval()));
            m_turnOffTimer.start();
        }
        m_backend->createDpmsFilter();
    } else {
        m_turnOffTimer.stop();
        m_backend->clearDpmsFilter();

        if (mode != dpmsMode()) {
            setDpmsModeInternal(mode);
            Q_EMIT wakeUp();
        }
    }
}

XdgShellOutput::XdgShellOutput(const QString &name, Surface *surface, XdgShell *xdgShell, WaylandBackend *backend, int number)
    : WaylandOutput(name, surface, backend)
    , m_number(number)
{
    m_xdgShellSurface = xdgShell->createSurface(surface, this);
    updateWindowTitle();

    connect(m_xdgShellSurface, &XdgShellSurface::configureRequested, this, &XdgShellOutput::handleConfigure);
    connect(m_xdgShellSurface, &XdgShellSurface::closeRequested, qApp, &QCoreApplication::quit);
    connect(this, &WaylandOutput::enabledChanged, this, &XdgShellOutput::updateWindowTitle);
    connect(this, &WaylandOutput::dpmsModeChanged, this, &XdgShellOutput::updateWindowTitle);

    connect(backend, &WaylandBackend::pointerLockSupportedChanged, this, &XdgShellOutput::updateWindowTitle);
    connect(backend, &WaylandBackend::pointerLockChanged, this, [this](bool locked) {
        if (locked) {
            if (!m_hasPointerLock) {
                // some other output has locked the pointer
                // this surface can stop trying to lock the pointer
                lockPointer(nullptr, false);
                // set it true for the other surface
                m_hasPointerLock = true;
            }
        } else {
            // just try unlocking
            lockPointer(nullptr, false);
        }
        updateWindowTitle();
    });

    surface->commit(KWayland::Client::Surface::CommitFlag::None);
}

XdgShellOutput::~XdgShellOutput()
{
    m_xdgShellSurface->destroy();
    delete m_xdgShellSurface;
}

void XdgShellOutput::handleConfigure(const QSize &size, XdgShellSurface::States states, quint32 serial)
{
    Q_UNUSED(states);
    m_xdgShellSurface->ackConfigure(serial);
    if (size.width() > 0 && size.height() > 0) {
        setGeometry(geometry().topLeft(), size);
        if (m_hasBeenConfigured) {
            Q_EMIT sizeChanged(size);
        }
    }

    if (!m_hasBeenConfigured) {
        m_hasBeenConfigured = true;
        backend()->addConfiguredOutput(this);
    }
}

void XdgShellOutput::updateWindowTitle()
{
    QString grab;
    if (m_hasPointerLock) {
        grab = i18n("Press right control to ungrab pointer");
    } else if (backend()->pointerConstraints()) {
        grab = i18n("Press right control key to grab pointer");
    }

    QString title = i18nc("Title of nested KWin Wayland with Wayland socket identifier as argument",
                          "KDE Wayland Compositor #%1 (%2)", m_number, waylandServer()->socketName());

    if (!isEnabled()) {
        title += i18n("- Output disabled");
    } else if (dpmsMode() != DpmsMode::On) {
        title += i18n("- Output dimmed");
    } else if (!grab.isEmpty()) {
        title += QStringLiteral(" — ") + grab;
    }
    m_xdgShellSurface->setTitle(title);
}

void XdgShellOutput::lockPointer(Pointer *pointer, bool lock)
{
    if (!lock) {
        const bool surfaceWasLocked = m_pointerLock && m_hasPointerLock;
        delete m_pointerLock;
        m_pointerLock = nullptr;
        m_hasPointerLock = false;
        if (surfaceWasLocked) {
            Q_EMIT backend()->pointerLockChanged(false);
        }
        return;
    }

    Q_ASSERT(!m_pointerLock);
    m_pointerLock = backend()->pointerConstraints()->lockPointer(surface(), pointer, nullptr,
                                                                 PointerConstraints::LifeTime::OneShot,
                                                                 this);
    if (!m_pointerLock->isValid()) {
        delete m_pointerLock;
        m_pointerLock = nullptr;
        return;
    }
    connect(m_pointerLock, &LockedPointer::locked, this, [this]() {
        m_hasPointerLock = true;
        Q_EMIT backend()->pointerLockChanged(true);
    });
    connect(m_pointerLock, &LockedPointer::unlocked, this, [this]() {
        delete m_pointerLock;
        m_pointerLock = nullptr;
        m_hasPointerLock = false;
        Q_EMIT backend()->pointerLockChanged(false);
    });
}

}
}
