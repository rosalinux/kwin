/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "atoms.h"
#include "cursor.h"
#include "deleted.h"
#include "output.h"
#include "platform.h"
#include "rules.h"
#include "wayland_server.h"
#include "workspace.h"
#include "x11window.h"

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_window_rules-0");

class WindowRuleTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testApplyInitialMaximizeVert_data();
    void testApplyInitialMaximizeVert();
    void testWindowClassChange();
};

void WindowRuleTest::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    qRegisterMetaType<KWin::Deleted *>();
    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    QVERIFY(applicationStartedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    const auto outputs = workspace()->outputs();
    QCOMPARE(outputs.count(), 2);
    QCOMPARE(outputs[0]->geometry(), QRect(0, 0, 1280, 1024));
    QCOMPARE(outputs[1]->geometry(), QRect(1280, 0, 1280, 1024));
    setenv("QT_QPA_PLATFORM", "wayland", true);
    Test::initWaylandWorkspace();
}

void WindowRuleTest::init()
{
    workspace()->setActiveOutput(QPoint(640, 512));
    Cursors::self()->mouse()->setPos(QPoint(640, 512));
    QVERIFY(waylandServer()->windows().isEmpty());
}

void WindowRuleTest::cleanup()
{
    // discards old rules
    workspace()->rulebook()->load();
}

struct XcbConnectionDeleter
{
    void operator()(xcb_connection_t *pointer)
    {
        xcb_disconnect(pointer);
    }
};

void WindowRuleTest::testApplyInitialMaximizeVert_data()
{
    QTest::addColumn<QByteArray>("role");

    QTest::newRow("lowercase") << QByteArrayLiteral("mainwindow");
    QTest::newRow("CamelCase") << QByteArrayLiteral("MainWindow");
}

void WindowRuleTest::testApplyInitialMaximizeVert()
{
    // this test creates the situation of BUG 367554: creates a window and initial apply maximize vertical
    // the window is matched by class and role
    // load the rule
    QFile ruleFile(QFINDTESTDATA("./data/rules/maximize-vert-apply-initial"));
    QVERIFY(ruleFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QMetaObject::invokeMethod(workspace()->rulebook(), "temporaryRulesMessage", Q_ARG(QString, QString::fromUtf8(ruleFile.readAll())));

    // create the test window
    std::unique_ptr<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t windowId = xcb_generate_id(c.get());
    const QRect windowGeometry = QRect(0, 0, 10, 20);
    const uint32_t values[] = {
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW};
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, values);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_icccm_set_wm_class(c.get(), windowId, 9, "kpat\0kpat");

    QFETCH(QByteArray, role);
    xcb_change_property(c.get(), XCB_PROP_MODE_REPLACE, windowId, atoms->wm_window_role, XCB_ATOM_STRING, 8, role.length(), role.constData());

    NETWinInfo info(c.get(), windowId, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.last().first().value<X11Window *>();
    QVERIFY(window);
    QVERIFY(window->isDecorated());
    QVERIFY(!window->hasStrut());
    QVERIFY(!window->isHiddenInternal());
    QVERIFY(!window->readyForPainting());
    QMetaObject::invokeMethod(window, "setReadyForPainting");
    QVERIFY(window->readyForPainting());
    QVERIFY(Test::waitForWaylandSurface(window));
    QCOMPARE(window->maximizeMode(), MaximizeVertical);

    // destroy window again
    QSignalSpy windowClosedSpy(window, &X11Window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), windowId);
    xcb_destroy_window(c.get(), windowId);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

void WindowRuleTest::testWindowClassChange()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);

    auto group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", 2);
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", 1);
    group.sync();

    workspace()->rulebook()->setConfig(config);
    workspace()->slotReconfigure();

    // create the test window
    std::unique_ptr<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t windowId = xcb_generate_id(c.get());
    const QRect windowGeometry = QRect(0, 0, 10, 20);
    const uint32_t values[] = {
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW};
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, values);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_icccm_set_wm_class(c.get(), windowId, 23, "org.kde.bar\0org.kde.bar");

    NETWinInfo info(c.get(), windowId, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.last().first().value<X11Window *>();
    QVERIFY(window);
    QVERIFY(window->isDecorated());
    QVERIFY(!window->hasStrut());
    QVERIFY(!window->isHiddenInternal());
    QVERIFY(!window->readyForPainting());
    QMetaObject::invokeMethod(window, "setReadyForPainting");
    QVERIFY(window->readyForPainting());
    QVERIFY(Test::waitForWaylandSurface(window));
    QCOMPARE(window->keepAbove(), false);

    // now change class
    QSignalSpy windowClassChangedSpy{window, &X11Window::windowClassChanged};
    QVERIFY(windowClassChangedSpy.isValid());
    xcb_icccm_set_wm_class(c.get(), windowId, 23, "org.kde.foo\0org.kde.foo");
    xcb_flush(c.get());
    QVERIFY(windowClassChangedSpy.wait());
    QCOMPARE(window->keepAbove(), true);

    // destroy window
    QSignalSpy windowClosedSpy(window, &X11Window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), windowId);
    xcb_destroy_window(c.get(), windowId);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::WindowRuleTest)
#include "window_rules_test.moc"
