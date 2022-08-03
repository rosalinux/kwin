/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwin_wayland_test.h"

#include "cursor.h"
#include "output.h"
#include "outputconfiguration.h"
#include "platform.h"
#include "wayland_server.h"
#include "window.h"
#include "workspace.h"

#include <KWayland/Client/surface.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_output_changes-0");

class OutputChangesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testWindowSticksToOutputAfterOutputIsDisabled();
    void testWindowSticksToOutputAfterAnotherOutputIsDisabled();
    void testWindowSticksToOutputAfterOutputIsMoved();
};

void OutputChangesTest::initTestCase()
{
    qRegisterMetaType<Window *>();

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
    Test::initWaylandWorkspace();
}

void OutputChangesTest::init()
{
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));
    QVERIFY(Test::setupWaylandConnection());

    workspace()->setActiveOutput(QPoint(640, 512));
    Cursors::self()->mouse()->setPos(QPoint(640, 512));
}

void OutputChangesTest::cleanup()
{
    Test::destroyWaylandConnection();
}

void OutputChangesTest::testWindowSticksToOutputAfterOutputIsDisabled()
{
    auto outputs = kwinApp()->platform()->outputs();

    // Create a window.
    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.get()));
    auto window = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::blue);
    QVERIFY(window);

    // Move the window to some predefined position so the test is more robust.
    window->move(QPoint(42, 67));
    QCOMPARE(window->frameGeometry(), QRect(42, 67, 100, 50));

    // Disable the output where the window is on.
    OutputConfiguration config;
    {
        auto changeSet = config.changeSet(outputs[0]);
        changeSet->enabled = false;
    }
    kwinApp()->platform()->applyOutputChanges(config);

    // The window will be sent to the second output, which is at (1280, 0).
    QCOMPARE(window->frameGeometry(), QRect(1280 + 42, 0 + 67, 100, 50));
}

void OutputChangesTest::testWindowSticksToOutputAfterAnotherOutputIsDisabled()
{
    auto outputs = kwinApp()->platform()->outputs();

    // Create a window.
    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.get()));
    auto window = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::blue);
    QVERIFY(window);

    // Move the window to the second output.
    window->move(QPoint(1280 + 42, 67));
    QCOMPARE(window->frameGeometry(), QRect(1280 + 42, 67, 100, 50));

    // Disable the first output.
    OutputConfiguration config;
    {
        auto changeSet = config.changeSet(outputs[0]);
        changeSet->enabled = false;
    }
    {
        auto changeSet = config.changeSet(outputs[1]);
        changeSet->pos = QPoint(0, 0);
    }
    kwinApp()->platform()->applyOutputChanges(config);

    // The position of the window relative to its output should remain the same.
    QCOMPARE(window->frameGeometry(), QRect(42, 67, 100, 50));
}

void OutputChangesTest::testWindowSticksToOutputAfterOutputIsMoved()
{
    auto outputs = kwinApp()->platform()->outputs();

    // Create a window.
    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.get()));
    auto window = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::blue);
    QVERIFY(window);

    // Move the window to some predefined position so the test is more robust.
    window->move(QPoint(42, 67));
    QCOMPARE(window->frameGeometry(), QRect(42, 67, 100, 50));

    // Disable the first output.
    OutputConfiguration config;
    {
        auto changeSet = config.changeSet(outputs[0]);
        changeSet->pos = QPoint(-10, 20);
    }
    kwinApp()->platform()->applyOutputChanges(config);

    // The position of the window relative to its output should remain the same.
    QCOMPARE(window->frameGeometry(), QRect(-10 + 42, 20 + 67, 100, 50));
}

}

WAYLANDTEST_MAIN(KWin::OutputChangesTest)
#include "outputchanges_test.moc"
