/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "cursor.h"
#include "deleted.h"
#include "output.h"
#include "platform.h"
#include "wayland/seat_interface.h"
#include "wayland_server.h"
#include "workspace.h"
#include "x11window.h"

#include <QSocketNotifier>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_xwayland_input-0");

class XWaylandInputTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void testPointerEnterLeaveSsd();
    void testPointerEventLeaveCsd();
};

void XWaylandInputTest::initTestCase()
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

void XWaylandInputTest::init()
{
    workspace()->setActiveOutput(QPoint(640, 512));
    Cursors::self()->mouse()->setPos(QPoint(640, 512));
    xcb_warp_pointer(connection(), XCB_WINDOW_NONE, kwinApp()->x11RootWindow(), 0, 0, 0, 0, 640, 512);
    xcb_flush(connection());
    QVERIFY(waylandServer()->windows().isEmpty());
}

struct XcbConnectionDeleter
{
    void operator()(xcb_connection_t *pointer)
    {
        xcb_disconnect(pointer);
    }
};

class X11EventReaderHelper : public QObject
{
    Q_OBJECT
public:
    X11EventReaderHelper(xcb_connection_t *c);

Q_SIGNALS:
    void entered(const QPoint &localPoint);
    void left(const QPoint &localPoint);

private:
    void processXcbEvents();
    xcb_connection_t *m_connection;
    QSocketNotifier *m_notifier;
};

X11EventReaderHelper::X11EventReaderHelper(xcb_connection_t *c)
    : QObject()
    , m_connection(c)
    , m_notifier(new QSocketNotifier(xcb_get_file_descriptor(m_connection), QSocketNotifier::Read, this))
{
    connect(m_notifier, &QSocketNotifier::activated, this, &X11EventReaderHelper::processXcbEvents);
    connect(QCoreApplication::eventDispatcher(), &QAbstractEventDispatcher::aboutToBlock, this, &X11EventReaderHelper::processXcbEvents);
    connect(QCoreApplication::eventDispatcher(), &QAbstractEventDispatcher::awake, this, &X11EventReaderHelper::processXcbEvents);
}

void X11EventReaderHelper::processXcbEvents()
{
    while (auto event = xcb_poll_for_event(m_connection)) {
        const uint8_t eventType = event->response_type & ~0x80;
        switch (eventType) {
        case XCB_ENTER_NOTIFY: {
            auto enterEvent = reinterpret_cast<xcb_enter_notify_event_t *>(event);
            Q_EMIT entered(QPoint(enterEvent->event_x, enterEvent->event_y));
            break;
        }
        case XCB_LEAVE_NOTIFY: {
            auto leaveEvent = reinterpret_cast<xcb_leave_notify_event_t *>(event);
            Q_EMIT left(QPoint(leaveEvent->event_x, leaveEvent->event_y));
            break;
        }
        }
        free(event);
    }
    xcb_flush(m_connection);
}

void XWaylandInputTest::testPointerEnterLeaveSsd()
{
    // this test simulates a pointer enter and pointer leave on a server-side decorated X11 window

    // create the test window
    std::unique_ptr<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.get()));
    if (xcb_get_setup(c.get())->release_number < 11800000) {
        QSKIP("XWayland 1.18 required");
    }
    X11EventReaderHelper eventReader(c.get());
    QSignalSpy enteredSpy(&eventReader, &X11EventReaderHelper::entered);
    QVERIFY(enteredSpy.isValid());
    QSignalSpy leftSpy(&eventReader, &X11EventReaderHelper::left);
    QVERIFY(leftSpy.isValid());
    // atom for the screenedge show hide functionality
    Xcb::Atom atom(QByteArrayLiteral("_KDE_NET_WM_SCREEN_EDGE_SHOW"), false, c.get());

    xcb_window_t windowId = xcb_generate_id(c.get());
    const QRect windowGeometry = QRect(0, 0, 100, 200);
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

    // move pointer into the window, should trigger an enter
    QVERIFY(!window->frameGeometry().contains(Cursors::self()->mouse()->pos()));
    QVERIFY(enteredSpy.isEmpty());
    Cursors::self()->mouse()->setPos(window->frameGeometry().center());
    QCOMPARE(waylandServer()->seat()->focusedPointerSurface(), window->surface());
    QVERIFY(enteredSpy.wait());
    QCOMPARE(enteredSpy.last().first(), window->frameGeometry().center() - window->clientPos());

    // move out of window
    Cursors::self()->mouse()->setPos(window->frameGeometry().bottomRight() + QPoint(10, 10));
    QVERIFY(leftSpy.wait());
    QCOMPARE(leftSpy.last().first(), window->frameGeometry().center() - window->clientPos());

    // destroy window again
    QSignalSpy windowClosedSpy(window, &X11Window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), windowId);
    xcb_destroy_window(c.get(), windowId);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

void XWaylandInputTest::testPointerEventLeaveCsd()
{
    // this test simulates a pointer enter and pointer leave on a client-side decorated X11 window

    std::unique_ptr<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.get()));

    if (xcb_get_setup(c.get())->release_number < 11800000) {
        QSKIP("XWayland 1.18 required");
    }
    if (!Xcb::Extensions::self()->isShapeAvailable()) {
        QSKIP("SHAPE extension is required");
    }

    X11EventReaderHelper eventReader(c.get());
    QSignalSpy enteredSpy(&eventReader, &X11EventReaderHelper::entered);
    QVERIFY(enteredSpy.isValid());
    QSignalSpy leftSpy(&eventReader, &X11EventReaderHelper::left);
    QVERIFY(leftSpy.isValid());

    // Extents of the client-side drop-shadow.
    NETStrut clientFrameExtent;
    clientFrameExtent.left = 10;
    clientFrameExtent.right = 10;
    clientFrameExtent.top = 5;
    clientFrameExtent.bottom = 20;

    // Need to set the bounding shape in order to create a window without decoration.
    xcb_rectangle_t boundingRect;
    boundingRect.x = 0;
    boundingRect.y = 0;
    boundingRect.width = 100 + clientFrameExtent.left + clientFrameExtent.right;
    boundingRect.height = 200 + clientFrameExtent.top + clientFrameExtent.bottom;

    xcb_window_t windowId = xcb_generate_id(c.get());
    const uint32_t values[] = {
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW};
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      boundingRect.x, boundingRect.y, boundingRect.width, boundingRect.height,
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, values);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, boundingRect.x, boundingRect.y);
    xcb_icccm_size_hints_set_size(&hints, 1, boundingRect.width, boundingRect.height);
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_shape_rectangles(c.get(), XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
                         XCB_CLIP_ORDERING_UNSORTED, windowId, 0, 0, 1, &boundingRect);
    NETWinInfo info(c.get(), windowId, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    info.setGtkFrameExtents(clientFrameExtent);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.last().first().value<X11Window *>();
    QVERIFY(window);
    QVERIFY(!window->isDecorated());
    QVERIFY(window->isClientSideDecorated());
    QCOMPARE(window->bufferGeometry(), QRect(0, 0, 120, 225));
    QCOMPARE(window->frameGeometry(), QRect(10, 5, 100, 200));

    QMetaObject::invokeMethod(window, "setReadyForPainting");
    QVERIFY(window->readyForPainting());
    QVERIFY(Test::waitForWaylandSurface(window));

    // Move pointer into the window, should trigger an enter.
    QVERIFY(!window->frameGeometry().contains(Cursors::self()->mouse()->pos()));
    QVERIFY(enteredSpy.isEmpty());
    Cursors::self()->mouse()->setPos(window->frameGeometry().center());
    QCOMPARE(waylandServer()->seat()->focusedPointerSurface(), window->surface());
    QVERIFY(enteredSpy.wait());
    QCOMPARE(enteredSpy.last().first(), QPointF(60, 105));

    // Move out of the window, should trigger a leave.
    QVERIFY(leftSpy.isEmpty());
    Cursors::self()->mouse()->setPos(window->frameGeometry().bottomRight() + QPoint(100, 100));
    QVERIFY(leftSpy.wait());
    QCOMPARE(leftSpy.last().first(), QPointF(60, 105));

    // Destroy the window.
    QSignalSpy windowClosedSpy(window, &X11Window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), windowId);
    xcb_destroy_window(c.get(), windowId);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::XWaylandInputTest)
#include "xwayland_input_test.moc"
