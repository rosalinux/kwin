/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "cursor.h"
#include "input.h"
#include "platform.h"
#include "scripting/scripting.h"
#include "useractions.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "window.h"
#include "workspace.h"

#include <KWayland/Client/surface.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>

using namespace KWin;
using namespace KWayland::Client;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_kwinbindings-0");

class KWinBindingsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSwitchWindow();
    void testSwitchWindowScript();
    void testWindowToDesktop_data();
    void testWindowToDesktop();
};

void KWinBindingsTest::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    QVERIFY(applicationStartedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName));

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    Test::initWaylandWorkspace();
}

void KWinBindingsTest::init()
{
    QVERIFY(Test::setupWaylandConnection());
    workspace()->setActiveOutput(QPoint(640, 512));
    KWin::Cursors::self()->mouse()->setPos(QPoint(640, 512));
}

void KWinBindingsTest::cleanup()
{
    Test::destroyWaylandConnection();
}

void KWinBindingsTest::testSwitchWindow()
{
    // first create windows
    std::unique_ptr<KWayland::Client::Surface> surface1(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface1(Test::createXdgToplevelSurface(surface1.get()));
    auto c1 = Test::renderAndWaitForShown(surface1.get(), QSize(100, 50), Qt::blue);
    std::unique_ptr<KWayland::Client::Surface> surface2(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface2(Test::createXdgToplevelSurface(surface2.get()));
    auto c2 = Test::renderAndWaitForShown(surface2.get(), QSize(100, 50), Qt::blue);
    std::unique_ptr<KWayland::Client::Surface> surface3(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface3(Test::createXdgToplevelSurface(surface3.get()));
    auto c3 = Test::renderAndWaitForShown(surface3.get(), QSize(100, 50), Qt::blue);
    std::unique_ptr<KWayland::Client::Surface> surface4(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface4(Test::createXdgToplevelSurface(surface4.get()));
    auto c4 = Test::renderAndWaitForShown(surface4.get(), QSize(100, 50), Qt::blue);

    QVERIFY(c4->isActive());
    QVERIFY(c4 != c3);
    QVERIFY(c3 != c2);
    QVERIFY(c2 != c1);

    // let's position all windows
    c1->move(QPoint(0, 0));
    c2->move(QPoint(200, 0));
    c3->move(QPoint(200, 200));
    c4->move(QPoint(0, 200));

    // now let's trigger the shortcuts

    // invoke global shortcut through dbus
    auto invokeShortcut = [](const QString &shortcut) {
        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.kglobalaccel"),
            QStringLiteral("/component/kwin"),
            QStringLiteral("org.kde.kglobalaccel.Component"),
            QStringLiteral("invokeShortcut"));
        msg.setArguments(QList<QVariant>{shortcut});
        QDBusConnection::sessionBus().asyncCall(msg);
    };
    invokeShortcut(QStringLiteral("Switch Window Up"));
    QTRY_COMPARE(workspace()->activeWindow(), c1);
    invokeShortcut(QStringLiteral("Switch Window Right"));
    QTRY_COMPARE(workspace()->activeWindow(), c2);
    invokeShortcut(QStringLiteral("Switch Window Down"));
    QTRY_COMPARE(workspace()->activeWindow(), c3);
    invokeShortcut(QStringLiteral("Switch Window Left"));
    QTRY_COMPARE(workspace()->activeWindow(), c4);
    // test opposite direction
    invokeShortcut(QStringLiteral("Switch Window Left"));
    QTRY_COMPARE(workspace()->activeWindow(), c3);
    invokeShortcut(QStringLiteral("Switch Window Down"));
    QTRY_COMPARE(workspace()->activeWindow(), c2);
    invokeShortcut(QStringLiteral("Switch Window Right"));
    QTRY_COMPARE(workspace()->activeWindow(), c1);
    invokeShortcut(QStringLiteral("Switch Window Up"));
    QTRY_COMPARE(workspace()->activeWindow(), c4);
}

void KWinBindingsTest::testSwitchWindowScript()
{
    QVERIFY(Scripting::self());

    // first create windows
    std::unique_ptr<KWayland::Client::Surface> surface1(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface1(Test::createXdgToplevelSurface(surface1.get()));
    auto c1 = Test::renderAndWaitForShown(surface1.get(), QSize(100, 50), Qt::blue);
    std::unique_ptr<KWayland::Client::Surface> surface2(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface2(Test::createXdgToplevelSurface(surface2.get()));
    auto c2 = Test::renderAndWaitForShown(surface2.get(), QSize(100, 50), Qt::blue);
    std::unique_ptr<KWayland::Client::Surface> surface3(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface3(Test::createXdgToplevelSurface(surface3.get()));
    auto c3 = Test::renderAndWaitForShown(surface3.get(), QSize(100, 50), Qt::blue);
    std::unique_ptr<KWayland::Client::Surface> surface4(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface4(Test::createXdgToplevelSurface(surface4.get()));
    auto c4 = Test::renderAndWaitForShown(surface4.get(), QSize(100, 50), Qt::blue);

    QVERIFY(c4->isActive());
    QVERIFY(c4 != c3);
    QVERIFY(c3 != c2);
    QVERIFY(c2 != c1);

    // let's position all windows
    c1->move(QPoint(0, 0));
    c2->move(QPoint(200, 0));
    c3->move(QPoint(200, 200));
    c4->move(QPoint(0, 200));

    auto runScript = [](const QString &slot) {
        QTemporaryFile tmpFile;
        QVERIFY(tmpFile.open());
        QTextStream out(&tmpFile);
        out << "workspace." << slot << "()";
        out.flush();

        const int id = Scripting::self()->loadScript(tmpFile.fileName());
        QVERIFY(id != -1);
        QVERIFY(Scripting::self()->isScriptLoaded(tmpFile.fileName()));
        auto s = Scripting::self()->findScript(tmpFile.fileName());
        QVERIFY(s);
        QSignalSpy runningChangedSpy(s, &AbstractScript::runningChanged);
        QVERIFY(runningChangedSpy.isValid());
        s->run();
        QTRY_COMPARE(runningChangedSpy.count(), 1);
    };

    runScript(QStringLiteral("slotSwitchWindowUp"));
    QTRY_COMPARE(workspace()->activeWindow(), c1);
    runScript(QStringLiteral("slotSwitchWindowRight"));
    QTRY_COMPARE(workspace()->activeWindow(), c2);
    runScript(QStringLiteral("slotSwitchWindowDown"));
    QTRY_COMPARE(workspace()->activeWindow(), c3);
    runScript(QStringLiteral("slotSwitchWindowLeft"));
    QTRY_COMPARE(workspace()->activeWindow(), c4);
}

void KWinBindingsTest::testWindowToDesktop_data()
{
    QTest::addColumn<int>("desktop");

    QTest::newRow("2") << 2;
    QTest::newRow("3") << 3;
    QTest::newRow("4") << 4;
    QTest::newRow("5") << 5;
    QTest::newRow("6") << 6;
    QTest::newRow("7") << 7;
    QTest::newRow("8") << 8;
    QTest::newRow("9") << 9;
    QTest::newRow("10") << 10;
    QTest::newRow("11") << 11;
    QTest::newRow("12") << 12;
    QTest::newRow("13") << 13;
    QTest::newRow("14") << 14;
    QTest::newRow("15") << 15;
    QTest::newRow("16") << 16;
    QTest::newRow("17") << 17;
    QTest::newRow("18") << 18;
    QTest::newRow("19") << 19;
    QTest::newRow("20") << 20;
}

void KWinBindingsTest::testWindowToDesktop()
{
    // first go to desktop one
    VirtualDesktopManager::self()->setCurrent(VirtualDesktopManager::self()->desktops().first());

    // now create a window
    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.get()));
    auto window = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::blue);
    QSignalSpy desktopChangedSpy(window, &Window::desktopChanged);
    QVERIFY(desktopChangedSpy.isValid());
    QCOMPARE(workspace()->activeWindow(), window);

    QFETCH(int, desktop);
    VirtualDesktopManager::self()->setCount(desktop);

    // now trigger the shortcut
    auto invokeShortcut = [](int desktop) {
        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.kglobalaccel"),
            QStringLiteral("/component/kwin"),
            QStringLiteral("org.kde.kglobalaccel.Component"),
            QStringLiteral("invokeShortcut"));
        msg.setArguments(QList<QVariant>{QStringLiteral("Window to Desktop %1").arg(desktop)});
        QDBusConnection::sessionBus().asyncCall(msg);
    };
    invokeShortcut(desktop);
    QVERIFY(desktopChangedSpy.wait());
    QCOMPARE(window->desktop(), desktop);
    // back to desktop 1
    invokeShortcut(1);
    QVERIFY(desktopChangedSpy.wait());
    QCOMPARE(window->desktop(), 1);
    // invoke with one desktop too many
    invokeShortcut(desktop + 1);
    // that should fail
    QVERIFY(!desktopChangedSpy.wait(100));
}

WAYLANDTEST_MAIN(KWinBindingsTest)
#include "kwinbindings_test.moc"
