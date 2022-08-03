/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2018 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "composite.h"
#include "cursor.h"
#include "effectloader.h"
#include "effects.h"
#include "platform.h"
#include "wayland/display.h"
#include "wayland/output_interface.h"
#include "wayland_server.h"
#include "workspace.h"
#include "x11window.h"

#include <KConfigGroup>

#include <KWayland/Client/seat.h>
#include <KWayland/Client/server_decoration.h>
#include <KWayland/Client/surface.h>

using namespace KWin;
using namespace KWayland::Client;
static const QString s_socketName = QStringLiteral("wayland_test_kwin_crash_cursor_physical_size_empty-0");

class DontCrashCursorPhysicalSizeEmpty : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init();
    void initTestCase();
    void cleanup();
    void testMoveCursorOverDeco();
};

void DontCrashCursorPhysicalSizeEmpty::init()
{
    QVERIFY(Test::setupWaylandConnection(Test::AdditionalWaylandInterface::Decoration));

    workspace()->setActiveOutput(QPoint(640, 512));
    KWin::Cursors::self()->mouse()->setPos(QPoint(640, 512));
}

void DontCrashCursorPhysicalSizeEmpty::cleanup()
{
    Test::destroyWaylandConnection();
}

void DontCrashCursorPhysicalSizeEmpty::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    QVERIFY(applicationStartedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    if (!QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons/DMZ-White/index.theme")).isEmpty()) {
        qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    } else {
        // might be vanilla-dmz (e.g. Arch, FreeBSD)
        qputenv("XCURSOR_THEME", QByteArrayLiteral("Vanilla-DMZ"));
    }
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("0"));

    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    Test::initWaylandWorkspace();
}

void DontCrashCursorPhysicalSizeEmpty::testMoveCursorOverDeco()
{
    // This test ensures that there is no endless recursion if the cursor theme cannot be created
    // a reason for creation failure could be physical size not existing
    // see BUG: 390314
    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface());
    Test::waylandServerSideDecoration()->create(surface.get(), surface.get());
    std::unique_ptr<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.get()));

    auto window = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::blue);
    QVERIFY(window);
    QVERIFY(window->isDecorated());

    // destroy physical size
    KWaylandServer::Display *display = waylandServer()->display();
    auto output = display->outputs().first();
    output->setPhysicalSize(QSize(0, 0));
    // and fake a cursor theme change, so that the theme gets recreated
    Q_EMIT KWin::Cursors::self()->mouse()->themeChanged();

    KWin::Cursors::self()->mouse()->setPos(QPoint(window->frameGeometry().center().x(), window->clientPos().y() / 2));
}

WAYLANDTEST_MAIN(DontCrashCursorPhysicalSizeEmpty)
#include "dont_crash_cursor_physical_size_empty.moc"
