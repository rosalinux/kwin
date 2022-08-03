/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "cursor.h"
#include "output.h"
#include "platform.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"

#include <KWayland/Client/output.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/xdgoutput.h>

using namespace KWin;
using namespace KWayland::Client;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_screen_changes-0");

class ScreenChangesTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testScreenAddRemove();
};

void ScreenChangesTest::initTestCase()
{
    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    QVERIFY(applicationStartedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName));

    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    setenv("QT_QPA_PLATFORM", "wayland", true);
    Test::initWaylandWorkspace();
}

void ScreenChangesTest::init()
{
    QVERIFY(Test::setupWaylandConnection());

    workspace()->setActiveOutput(QPoint(640, 512));
    KWin::Cursors::self()->mouse()->setPos(QPoint(640, 512));
}

void ScreenChangesTest::cleanup()
{
    Test::destroyWaylandConnection();
}

void ScreenChangesTest::testScreenAddRemove()
{
    // this test verifies that when a new screen is added it gets synced to Wayland

    // first create a registry to get signals about Outputs announced/removed
    Registry registry;
    QSignalSpy allAnnounced(&registry, &Registry::interfacesAnnounced);
    QVERIFY(allAnnounced.isValid());
    QSignalSpy outputAnnouncedSpy(&registry, &Registry::outputAnnounced);
    QVERIFY(outputAnnouncedSpy.isValid());
    QSignalSpy outputRemovedSpy(&registry, &Registry::outputRemoved);
    QVERIFY(outputRemovedSpy.isValid());
    registry.create(Test::waylandConnection());
    QVERIFY(registry.isValid());
    registry.setup();
    QVERIFY(allAnnounced.wait());
    const auto xdgOMData = registry.interface(Registry::Interface::XdgOutputUnstableV1);
    auto xdgOutputManager = registry.createXdgOutputManager(xdgOMData.name, xdgOMData.version);

    // should be one output
    QCOMPARE(workspace()->outputs().count(), 1);
    QCOMPARE(outputAnnouncedSpy.count(), 1);
    const quint32 firstOutputId = outputAnnouncedSpy.first().first().value<quint32>();
    QVERIFY(firstOutputId != 0u);
    outputAnnouncedSpy.clear();

    // let's announce a new output
    QSignalSpy screensChangedSpy(workspace()->screens(), &Screens::changed);
    QVERIFY(screensChangedSpy.isValid());
    const QVector<QRect> geometries{QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)};
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs",
                              Qt::DirectConnection,
                              Q_ARG(int, 2),
                              Q_ARG(QVector<QRect>, geometries));
    QVERIFY(screensChangedSpy.wait());
    QCOMPARE(screensChangedSpy.count(), 2);
    auto outputs = workspace()->outputs();
    QCOMPARE(outputs.count(), 2);
    QCOMPARE(outputs[0]->geometry(), geometries[0]);
    QCOMPARE(outputs[1]->geometry(), geometries[1]);

    // this should result in it getting announced, two new outputs are added...
    QVERIFY(outputAnnouncedSpy.wait());
    if (outputAnnouncedSpy.count() < 2) {
        QVERIFY(outputAnnouncedSpy.wait());
    }
    QCOMPARE(outputAnnouncedSpy.count(), 2);
    // ... and afterward the previous output gets removed
    if (outputRemovedSpy.isEmpty()) {
        QVERIFY(outputRemovedSpy.wait());
    }
    QCOMPARE(outputRemovedSpy.count(), 1);
    QCOMPARE(outputRemovedSpy.first().first().value<quint32>(), firstOutputId);

    // let's wait a little bit to ensure we don't get more events
    QTest::qWait(100);
    QCOMPARE(outputAnnouncedSpy.count(), 2);
    QCOMPARE(outputRemovedSpy.count(), 1);

    // let's create the output objects to ensure they are correct
    std::unique_ptr<KWayland::Client::Output> o1(registry.createOutput(outputAnnouncedSpy.first().first().value<quint32>(), outputAnnouncedSpy.first().last().value<quint32>()));
    QVERIFY(o1->isValid());
    QSignalSpy o1ChangedSpy(o1.get(), &KWayland::Client::Output::changed);
    QVERIFY(o1ChangedSpy.isValid());
    QVERIFY(o1ChangedSpy.wait());
    QCOMPARE(o1->geometry(), geometries.at(0));
    std::unique_ptr<KWayland::Client::Output> o2(registry.createOutput(outputAnnouncedSpy.last().first().value<quint32>(), outputAnnouncedSpy.last().last().value<quint32>()));
    QVERIFY(o2->isValid());
    QSignalSpy o2ChangedSpy(o2.get(), &KWayland::Client::Output::changed);
    QVERIFY(o2ChangedSpy.isValid());
    QVERIFY(o2ChangedSpy.wait());
    QCOMPARE(o2->geometry(), geometries.at(1));

    // and check XDGOutput is synced
    std::unique_ptr<XdgOutput> xdgO1(xdgOutputManager->getXdgOutput(o1.get()));
    QSignalSpy xdgO1ChangedSpy(xdgO1.get(), &XdgOutput::changed);
    QVERIFY(xdgO1ChangedSpy.isValid());
    QVERIFY(xdgO1ChangedSpy.wait());
    QCOMPARE(xdgO1->logicalPosition(), geometries.at(0).topLeft());
    QCOMPARE(xdgO1->logicalSize(), geometries.at(0).size());
    std::unique_ptr<XdgOutput> xdgO2(xdgOutputManager->getXdgOutput(o2.get()));
    QSignalSpy xdgO2ChangedSpy(xdgO2.get(), &XdgOutput::changed);
    QVERIFY(xdgO2ChangedSpy.isValid());
    QVERIFY(xdgO2ChangedSpy.wait());
    QCOMPARE(xdgO2->logicalPosition(), geometries.at(1).topLeft());
    QCOMPARE(xdgO2->logicalSize(), geometries.at(1).size());

    QVERIFY(xdgO1->name().startsWith("Virtual-"));
    QVERIFY(xdgO1->name() != xdgO2->name());
    QVERIFY(!xdgO1->description().isEmpty());

    // now let's try to remove one output again
    outputAnnouncedSpy.clear();
    outputRemovedSpy.clear();
    screensChangedSpy.clear();

    QSignalSpy o1RemovedSpy(o1.get(), &KWayland::Client::Output::removed);
    QVERIFY(o1RemovedSpy.isValid());
    QSignalSpy o2RemovedSpy(o2.get(), &KWayland::Client::Output::removed);
    QVERIFY(o2RemovedSpy.isValid());

    const QVector<QRect> geometries2{QRect(0, 0, 1280, 1024)};
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs",
                              Qt::DirectConnection,
                              Q_ARG(int, 1),
                              Q_ARG(QVector<QRect>, geometries2));
    QVERIFY(screensChangedSpy.wait());
    QCOMPARE(screensChangedSpy.count(), 2);
    outputs = workspace()->outputs();
    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs[0]->geometry(), geometries2.at(0));

    QVERIFY(outputAnnouncedSpy.wait());
    QCOMPARE(outputAnnouncedSpy.count(), 1);
    if (o1RemovedSpy.isEmpty()) {
        QVERIFY(o1RemovedSpy.wait());
    }
    if (o2RemovedSpy.isEmpty()) {
        QVERIFY(o2RemovedSpy.wait());
    }
    // now wait a bit to ensure we don't get more events
    QTest::qWait(100);
    QCOMPARE(outputAnnouncedSpy.count(), 1);
    QCOMPARE(o1RemovedSpy.count(), 1);
    QCOMPARE(o2RemovedSpy.count(), 1);
    QCOMPARE(outputRemovedSpy.count(), 2);
}

WAYLANDTEST_MAIN(ScreenChangesTest)
#include "screen_changes_test.moc"
