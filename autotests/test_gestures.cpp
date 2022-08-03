/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "gestures.h"

#include <QSignalSpy>
#include <QTest>
#include <QtWidgets/qaction.h>
#include <iostream>

using namespace KWin;

class GestureTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testSwipeMinFinger_data();
    void testSwipeMinFinger();
    void testPinchMinFinger_data();
    void testPinchMinFinger();

    void testSwipeMaxFinger_data();
    void testSwipeMaxFinger();
    void testPinchMaxFinger_data();
    void testPinchMaxFinger();

    void testSwipeDirection_data();
    void testSwipeDirection();
    void testPinchDirection_data();
    void testPinchDirection();

    // swipe only
    void testMinimumX_data();
    void testMinimumX();
    void testMinimumY_data();
    void testMinimumY();
    void testMaximumX_data();
    void testMaximumX();
    void testMaximumY_data();
    void testMaximumY();
    void testStartGeometry();

    // swipe and pinch
    void testSetMinimumDelta();
    void testMinimumDeltaReached_data();
    void testMinimumDeltaReached();
    void testMinimumScaleDelta();
    void testUnregisterSwipeCancels();
    void testUnregisterPinchCancels();
    void testDeleteSwipeCancels();
    void testSwipeCancel_data();
    void testSwipeCancel();
    void testSwipeUpdateTrigger_data();
    void testSwipeUpdateTrigger();

    // both
    void testSwipeMinFingerStart_data();
    void testSwipeMinFingerStart();
    void testSwipeMaxFingerStart_data();
    void testSwipeMaxFingerStart();
    void testNotEmitCallbacksBeforeDirectionDecided();

    // swipe only
    void testSwipeGeometryStart_data();
    void testSwipeGeometryStart();
};

void GestureTest::testSwipeMinFinger_data()
{
    QTest::addColumn<uint>("count");
    QTest::addColumn<uint>("expectedCount");

    QTest::newRow("0") << 0u << 0u;
    QTest::newRow("1") << 1u << 1u;
    QTest::newRow("10") << 10u << 10u;
}

void GestureTest::testSwipeMinFinger()
{
    SwipeGesture swipeGesture;
    QCOMPARE(swipeGesture.minimumFingerCountIsRelevant(), false);
    QCOMPARE(swipeGesture.minimumFingerCount(), 0u);
    QFETCH(uint, count);
    swipeGesture.setMinimumFingerCount(count);
    QCOMPARE(swipeGesture.minimumFingerCountIsRelevant(), true);
    QTEST(swipeGesture.minimumFingerCount(), "expectedCount");
    swipeGesture.setMinimumFingerCount(0);
    QCOMPARE(swipeGesture.minimumFingerCountIsRelevant(), true);
    QCOMPARE(swipeGesture.minimumFingerCount(), 0u);
}

void GestureTest::testPinchMinFinger_data()
{
    QTest::addColumn<uint>("count");
    QTest::addColumn<uint>("expectedCount");

    QTest::newRow("0") << 0u << 0u;
    QTest::newRow("1") << 1u << 1u;
    QTest::newRow("10") << 10u << 10u;
}

void GestureTest::testPinchMinFinger()
{
    PinchGesture pinchGesture;
    QCOMPARE(pinchGesture.minimumFingerCountIsRelevant(), false);
    QCOMPARE(pinchGesture.minimumFingerCount(), 0u);
    QFETCH(uint, count);
    pinchGesture.setMinimumFingerCount(count);
    QCOMPARE(pinchGesture.minimumFingerCountIsRelevant(), true);
    QTEST(pinchGesture.minimumFingerCount(), "expectedCount");
    pinchGesture.setMinimumFingerCount(0);
    QCOMPARE(pinchGesture.minimumFingerCountIsRelevant(), true);
    QCOMPARE(pinchGesture.minimumFingerCount(), 0u);
}

void GestureTest::testSwipeMaxFinger_data()
{
    QTest::addColumn<uint>("count");
    QTest::addColumn<uint>("expectedCount");

    QTest::newRow("0") << 0u << 0u;
    QTest::newRow("1") << 1u << 1u;
    QTest::newRow("10") << 10u << 10u;
}

void GestureTest::testSwipeMaxFinger()
{
    SwipeGesture gesture;
    QCOMPARE(gesture.maximumFingerCountIsRelevant(), false);
    QCOMPARE(gesture.maximumFingerCount(), 0u);
    QFETCH(uint, count);
    gesture.setMaximumFingerCount(count);
    QCOMPARE(gesture.maximumFingerCountIsRelevant(), true);
    QTEST(gesture.maximumFingerCount(), "expectedCount");
    gesture.setMaximumFingerCount(0);
    QCOMPARE(gesture.maximumFingerCountIsRelevant(), true);
    QCOMPARE(gesture.maximumFingerCount(), 0u);
}

void GestureTest::testPinchMaxFinger_data()
{
    QTest::addColumn<uint>("count");
    QTest::addColumn<uint>("expectedCount");

    QTest::newRow("0") << 0u << 0u;
    QTest::newRow("1") << 1u << 1u;
    QTest::newRow("10") << 10u << 10u;
}

void GestureTest::testPinchMaxFinger()
{
    PinchGesture gesture;
    QCOMPARE(gesture.maximumFingerCountIsRelevant(), false);
    QCOMPARE(gesture.maximumFingerCount(), 0u);
    QFETCH(uint, count);
    gesture.setMaximumFingerCount(count);
    QCOMPARE(gesture.maximumFingerCountIsRelevant(), true);
    QTEST(gesture.maximumFingerCount(), "expectedCount");
    gesture.setMaximumFingerCount(0);
    QCOMPARE(gesture.maximumFingerCountIsRelevant(), true);
    QCOMPARE(gesture.maximumFingerCount(), 0u);
}

void GestureTest::testSwipeDirection_data()
{
    QTest::addColumn<KWin::SwipeGesture::Direction>("swipe_direction");

    QTest::newRow("Up") << KWin::SwipeGesture::Direction::Up;
    QTest::newRow("Left") << KWin::SwipeGesture::Direction::Left;
    QTest::newRow("Right") << KWin::SwipeGesture::Direction::Right;
    QTest::newRow("Down") << KWin::SwipeGesture::Direction::Down;
}

void GestureTest::testSwipeDirection()
{
    SwipeGesture gesture;
    QCOMPARE(gesture.direction(), SwipeGesture::Direction::Down);
    QFETCH(KWin::SwipeGesture::Direction, swipe_direction);
    gesture.setDirection(swipe_direction);
    QCOMPARE(gesture.direction(), swipe_direction);
    // back to down
    gesture.setDirection(SwipeGesture::Direction::Down);
    QCOMPARE(gesture.direction(), SwipeGesture::Direction::Down);
}

void GestureTest::testPinchDirection_data()
{
    QTest::addColumn<KWin::PinchGesture::Direction>("pinch_direction");

    QTest::newRow("Contracting") << KWin::PinchGesture::Direction::Contracting;
    QTest::newRow("Expanding") << KWin::PinchGesture::Direction::Expanding;
}

void GestureTest::testPinchDirection()
{
    PinchGesture gesture;
    QCOMPARE(gesture.direction(), PinchGesture::Direction::Expanding);
    QFETCH(KWin::PinchGesture::Direction, pinch_direction);
    gesture.setDirection(pinch_direction);
    QCOMPARE(gesture.direction(), pinch_direction);
    // back to down
    gesture.setDirection(PinchGesture::Direction::Expanding);
    QCOMPARE(gesture.direction(), PinchGesture::Direction::Expanding);
}

void GestureTest::testMinimumX_data()
{
    QTest::addColumn<int>("min");

    QTest::newRow("0") << 0;
    QTest::newRow("-1") << -1;
    QTest::newRow("1") << 1;
}

void GestureTest::testMinimumX()
{
    SwipeGesture gesture;
    QCOMPARE(gesture.minimumX(), 0);
    QCOMPARE(gesture.minimumXIsRelevant(), false);
    QFETCH(int, min);
    gesture.setMinimumX(min);
    QCOMPARE(gesture.minimumX(), min);
    QCOMPARE(gesture.minimumXIsRelevant(), true);
}

void GestureTest::testMinimumY_data()
{
    QTest::addColumn<int>("min");

    QTest::newRow("0") << 0;
    QTest::newRow("-1") << -1;
    QTest::newRow("1") << 1;
}

void GestureTest::testMinimumY()
{
    SwipeGesture gesture;
    QCOMPARE(gesture.minimumY(), 0);
    QCOMPARE(gesture.minimumYIsRelevant(), false);
    QFETCH(int, min);
    gesture.setMinimumY(min);
    QCOMPARE(gesture.minimumY(), min);
    QCOMPARE(gesture.minimumYIsRelevant(), true);
}

void GestureTest::testMaximumX_data()
{
    QTest::addColumn<int>("max");

    QTest::newRow("0") << 0;
    QTest::newRow("-1") << -1;
    QTest::newRow("1") << 1;
}

void GestureTest::testMaximumX()
{
    SwipeGesture gesture;
    QCOMPARE(gesture.maximumX(), 0);
    QCOMPARE(gesture.maximumXIsRelevant(), false);
    QFETCH(int, max);
    gesture.setMaximumX(max);
    QCOMPARE(gesture.maximumX(), max);
    QCOMPARE(gesture.maximumXIsRelevant(), true);
}

void GestureTest::testMaximumY_data()
{
    QTest::addColumn<int>("max");

    QTest::newRow("0") << 0;
    QTest::newRow("-1") << -1;
    QTest::newRow("1") << 1;
}

void GestureTest::testMaximumY()
{
    SwipeGesture gesture;
    QCOMPARE(gesture.maximumY(), 0);
    QCOMPARE(gesture.maximumYIsRelevant(), false);
    QFETCH(int, max);
    gesture.setMaximumY(max);
    QCOMPARE(gesture.maximumY(), max);
    QCOMPARE(gesture.maximumYIsRelevant(), true);
}

void GestureTest::testStartGeometry()
{
    SwipeGesture gesture;
    gesture.setStartGeometry(QRect(1, 2, 20, 30));
    QCOMPARE(gesture.minimumXIsRelevant(), true);
    QCOMPARE(gesture.minimumYIsRelevant(), true);
    QCOMPARE(gesture.maximumXIsRelevant(), true);
    QCOMPARE(gesture.maximumYIsRelevant(), true);
    QCOMPARE(gesture.minimumX(), 1);
    QCOMPARE(gesture.minimumY(), 2);
    QCOMPARE(gesture.maximumX(), 21);
    QCOMPARE(gesture.maximumY(), 32);
}

void GestureTest::testSetMinimumDelta()
{
    SwipeGesture swipeGesture;
    QCOMPARE(swipeGesture.isMinimumDeltaRelevant(), false);
    QCOMPARE(swipeGesture.minimumDelta(), QSizeF());
    QCOMPARE(swipeGesture.minimumDeltaReached(QSizeF()), true);
    swipeGesture.setMinimumDelta(QSizeF(2, 3));
    QCOMPARE(swipeGesture.isMinimumDeltaRelevant(), true);
    QCOMPARE(swipeGesture.minimumDelta(), QSizeF(2, 3));
    QCOMPARE(swipeGesture.minimumDeltaReached(QSizeF()), false);
    QCOMPARE(swipeGesture.minimumDeltaReached(QSizeF(2, 3)), true);

    PinchGesture pinchGesture;
    QCOMPARE(pinchGesture.isMinimumScaleDeltaRelevant(), false);
    QCOMPARE(pinchGesture.minimumScaleDelta(), DEFAULT_UNIT_SCALE_DELTA);
    QCOMPARE(pinchGesture.minimumScaleDeltaReached(1.25), true);
    pinchGesture.setMinimumScaleDelta(.5);
    QCOMPARE(pinchGesture.isMinimumScaleDeltaRelevant(), true);
    QCOMPARE(pinchGesture.minimumScaleDelta(), .5);
    QCOMPARE(pinchGesture.minimumScaleDeltaReached(1.24), false);
    QCOMPARE(pinchGesture.minimumScaleDeltaReached(1.5), true);
}

void GestureTest::testMinimumDeltaReached_data()
{
    QTest::addColumn<KWin::SwipeGesture::Direction>("direction");
    QTest::addColumn<QSizeF>("minimumDelta");
    QTest::addColumn<QSizeF>("delta");
    QTest::addColumn<bool>("reached");
    QTest::addColumn<qreal>("progress");

    QTest::newRow("Up (more)") << KWin::SwipeGesture::Direction::Up << QSizeF(0, -30) << QSizeF(0, -40) << true << 1.0;
    QTest::newRow("Up (exact)") << KWin::SwipeGesture::Direction::Up << QSizeF(0, -30) << QSizeF(0, -30) << true << 1.0;
    QTest::newRow("Up (less)") << KWin::SwipeGesture::Direction::Up << QSizeF(0, -30) << QSizeF(0, -29) << false << 29.0 / 30.0;
    QTest::newRow("Left (more)") << KWin::SwipeGesture::Direction::Left << QSizeF(-30, -30) << QSizeF(-40, 20) << true << 1.0;
    QTest::newRow("Left (exact)") << KWin::SwipeGesture::Direction::Left << QSizeF(-30, -40) << QSizeF(-30, 0) << true << 1.0;
    QTest::newRow("Left (less)") << KWin::SwipeGesture::Direction::Left << QSizeF(-30, -30) << QSizeF(-29, 0) << false << 29.0 / 30.0;
    QTest::newRow("Right (more)") << KWin::SwipeGesture::Direction::Right << QSizeF(30, -30) << QSizeF(40, 20) << true << 1.0;
    QTest::newRow("Right (exact)") << KWin::SwipeGesture::Direction::Right << QSizeF(30, -40) << QSizeF(30, 0) << true << 1.0;
    QTest::newRow("Right (less)") << KWin::SwipeGesture::Direction::Right << QSizeF(30, -30) << QSizeF(29, 0) << false << 29.0 / 30.0;
    QTest::newRow("Down (more)") << KWin::SwipeGesture::Direction::Down << QSizeF(0, 30) << QSizeF(0, 40) << true << 1.0;
    QTest::newRow("Down (exact)") << KWin::SwipeGesture::Direction::Down << QSizeF(0, 30) << QSizeF(0, 30) << true << 1.0;
    QTest::newRow("Down (less)") << KWin::SwipeGesture::Direction::Down << QSizeF(0, 30) << QSizeF(0, 29) << false << 29.0 / 30.0;
}

void GestureTest::testMinimumDeltaReached()
{
    GestureRecognizer recognizer;

    // swipe gesture
    SwipeGesture gesture;
    QFETCH(SwipeGesture::Direction, direction);
    gesture.setDirection(direction);
    QFETCH(QSizeF, minimumDelta);
    gesture.setMinimumDelta(minimumDelta);
    QFETCH(QSizeF, delta);
    QFETCH(bool, reached);
    QCOMPARE(gesture.minimumDeltaReached(delta), reached);

    recognizer.registerSwipeGesture(&gesture);

    QSignalSpy startedSpy(&gesture, &SwipeGesture::started);
    QVERIFY(startedSpy.isValid());
    QSignalSpy triggeredSpy(&gesture, &SwipeGesture::triggered);
    QVERIFY(triggeredSpy.isValid());
    QSignalSpy cancelledSpy(&gesture, &SwipeGesture::cancelled);
    QVERIFY(cancelledSpy.isValid());
    QSignalSpy progressSpy(&gesture, &SwipeGesture::progress);
    QVERIFY(progressSpy.isValid());

    recognizer.startSwipeGesture(1);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(triggeredSpy.count(), 0);
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(progressSpy.count(), 0);

    recognizer.updateSwipeGesture(delta);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(triggeredSpy.count(), 0);
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(progressSpy.count(), 1);
    QTEST(progressSpy.first().first().value<qreal>(), "progress");

    recognizer.endSwipeGesture();
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(triggeredSpy.isEmpty(), !reached);
    QCOMPARE(cancelledSpy.isEmpty(), reached);
}

void GestureTest::testMinimumScaleDelta()
{
    // pinch gesture
    PinchGesture gesture;
    gesture.setDirection(PinchGesture::Direction::Contracting);
    gesture.setMinimumScaleDelta(.5);
    gesture.setMinimumFingerCount(3);
    gesture.setMaximumFingerCount(4);

    QCOMPARE(gesture.minimumScaleDeltaReached(1.25), false);
    QCOMPARE(gesture.minimumScaleDeltaReached(1.5), true);

    GestureRecognizer recognizer;
    recognizer.registerPinchGesture(&gesture);

    QSignalSpy startedSpy(&gesture, &PinchGesture::started);
    QVERIFY(startedSpy.isValid());
    QSignalSpy triggeredSpy(&gesture, &PinchGesture::triggered);
    QVERIFY(triggeredSpy.isValid());
    QSignalSpy cancelledSpy(&gesture, &PinchGesture::cancelled);
    QVERIFY(cancelledSpy.isValid());
    QSignalSpy progressSpy(&gesture, &PinchGesture::progress);
    QVERIFY(progressSpy.isValid());

    recognizer.startPinchGesture(4);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(triggeredSpy.count(), 0);
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(progressSpy.count(), 0);
}

void GestureTest::testUnregisterSwipeCancels()
{
    GestureRecognizer recognizer;
    std::unique_ptr<SwipeGesture> gesture(new SwipeGesture);
    QSignalSpy startedSpy(gesture.get(), &SwipeGesture::started);
    QVERIFY(startedSpy.isValid());
    QSignalSpy cancelledSpy(gesture.get(), &SwipeGesture::cancelled);
    QVERIFY(cancelledSpy.isValid());

    recognizer.registerSwipeGesture(gesture.get());
    recognizer.startSwipeGesture(1);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);
    recognizer.unregisterSwipeGesture(gesture.get());
    QCOMPARE(cancelledSpy.count(), 1);

    // delete the gesture should not trigger cancel
    gesture.reset();
    QCOMPARE(cancelledSpy.count(), 1);
}

void GestureTest::testUnregisterPinchCancels()
{
    GestureRecognizer recognizer;
    std::unique_ptr<PinchGesture> gesture(new PinchGesture);
    QSignalSpy startedSpy(gesture.get(), &PinchGesture::started);
    QVERIFY(startedSpy.isValid());
    QSignalSpy cancelledSpy(gesture.get(), &PinchGesture::cancelled);
    QVERIFY(cancelledSpy.isValid());

    recognizer.registerPinchGesture(gesture.get());
    recognizer.startPinchGesture(1);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);
    recognizer.unregisterPinchGesture(gesture.get());
    QCOMPARE(cancelledSpy.count(), 1);

    // delete the gesture should not trigger cancel
    gesture.reset();
    QCOMPARE(cancelledSpy.count(), 1);
}

void GestureTest::testDeleteSwipeCancels()
{
    GestureRecognizer recognizer;
    std::unique_ptr<SwipeGesture> gesture(new SwipeGesture);
    QSignalSpy startedSpy(gesture.get(), &SwipeGesture::started);
    QVERIFY(startedSpy.isValid());
    QSignalSpy cancelledSpy(gesture.get(), &SwipeGesture::cancelled);
    QVERIFY(cancelledSpy.isValid());

    recognizer.registerSwipeGesture(gesture.get());
    recognizer.startSwipeGesture(1);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);
    gesture.reset();
    QCOMPARE(cancelledSpy.count(), 1);
}

void GestureTest::testSwipeCancel_data()
{
    QTest::addColumn<KWin::SwipeGesture::Direction>("direction");

    QTest::newRow("Up") << KWin::SwipeGesture::Direction::Up;
    QTest::newRow("Left") << KWin::SwipeGesture::Direction::Left;
    QTest::newRow("Right") << KWin::SwipeGesture::Direction::Right;
    QTest::newRow("Down") << KWin::SwipeGesture::Direction::Down;
}

void GestureTest::testSwipeCancel()
{
    GestureRecognizer recognizer;
    std::unique_ptr<SwipeGesture> gesture(new SwipeGesture);
    QFETCH(SwipeGesture::Direction, direction);
    gesture->setDirection(direction);
    QSignalSpy startedSpy(gesture.get(), &SwipeGesture::started);
    QVERIFY(startedSpy.isValid());
    QSignalSpy cancelledSpy(gesture.get(), &SwipeGesture::cancelled);
    QVERIFY(cancelledSpy.isValid());
    QSignalSpy triggeredSpy(gesture.get(), &SwipeGesture::triggered);
    QVERIFY(triggeredSpy.isValid());

    recognizer.registerSwipeGesture(gesture.get());
    recognizer.startSwipeGesture(1);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);
    recognizer.cancelSwipeGesture();
    QCOMPARE(cancelledSpy.count(), 1);
    QCOMPARE(triggeredSpy.count(), 0);
}

void GestureTest::testSwipeUpdateTrigger_data()
{
    QTest::addColumn<KWin::SwipeGesture::Direction>("direction");
    QTest::addColumn<QSizeF>("delta");

    QTest::newRow("Up") << KWin::SwipeGesture::Direction::Up << QSizeF(2, -3);
    QTest::newRow("Left") << KWin::SwipeGesture::Direction::Left << QSizeF(-3, 1);
    QTest::newRow("Right") << KWin::SwipeGesture::Direction::Right << QSizeF(20, -19);
    QTest::newRow("Down") << KWin::SwipeGesture::Direction::Down << QSizeF(0, 50);
}

void GestureTest::testSwipeUpdateTrigger()
{
    GestureRecognizer recognizer;
    SwipeGesture gesture;
    QFETCH(SwipeGesture::Direction, direction);
    gesture.setDirection(direction);

    QSignalSpy triggeredSpy(&gesture, &SwipeGesture::triggered);
    QVERIFY(triggeredSpy.isValid());
    QSignalSpy cancelledSpy(&gesture, &SwipeGesture::cancelled);
    QVERIFY(cancelledSpy.isValid());

    recognizer.registerSwipeGesture(&gesture);

    recognizer.startSwipeGesture(1);
    QFETCH(QSizeF, delta);
    recognizer.updateSwipeGesture(delta);
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(triggeredSpy.count(), 0);

    recognizer.endSwipeGesture();
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(triggeredSpy.count(), 1);
}

void GestureTest::testSwipeMinFingerStart_data()
{
    QTest::addColumn<uint>("min");
    QTest::addColumn<uint>("count");
    QTest::addColumn<bool>("started");

    QTest::newRow("same") << 1u << 1u << true;
    QTest::newRow("less") << 2u << 1u << false;
    QTest::newRow("more") << 1u << 2u << true;
}

void GestureTest::testSwipeMinFingerStart()
{
    GestureRecognizer recognizer;
    SwipeGesture gesture;
    QFETCH(uint, min);
    gesture.setMinimumFingerCount(min);

    QSignalSpy startedSpy(&gesture, &SwipeGesture::started);
    QVERIFY(startedSpy.isValid());

    recognizer.registerSwipeGesture(&gesture);
    QFETCH(uint, count);
    recognizer.startSwipeGesture(count);
    QTEST(!startedSpy.isEmpty(), "started");
}

void GestureTest::testSwipeMaxFingerStart_data()
{
    QTest::addColumn<uint>("max");
    QTest::addColumn<uint>("count");
    QTest::addColumn<bool>("started");

    QTest::newRow("same") << 1u << 1u << true;
    QTest::newRow("less") << 2u << 1u << true;
    QTest::newRow("more") << 1u << 2u << false;
}

void GestureTest::testSwipeMaxFingerStart()
{
    GestureRecognizer recognizer;
    SwipeGesture gesture;
    QFETCH(uint, max);
    gesture.setMaximumFingerCount(max);

    QSignalSpy startedSpy(&gesture, &SwipeGesture::started);
    QVERIFY(startedSpy.isValid());

    recognizer.registerSwipeGesture(&gesture);
    QFETCH(uint, count);
    recognizer.startSwipeGesture(count);
    QTEST(!startedSpy.isEmpty(), "started");
}

void GestureTest::testNotEmitCallbacksBeforeDirectionDecided()
{
    GestureRecognizer recognizer;
    SwipeGesture up;
    SwipeGesture down;
    SwipeGesture right;
    PinchGesture expand;
    PinchGesture contract;
    up.setDirection(SwipeGesture::Direction::Up);
    down.setDirection(SwipeGesture::Direction::Down);
    right.setDirection(SwipeGesture::Direction::Right);
    expand.setDirection(PinchGesture::Direction::Expanding);
    contract.setDirection(PinchGesture::Direction::Contracting);
    recognizer.registerSwipeGesture(&up);
    recognizer.registerSwipeGesture(&down);
    recognizer.registerSwipeGesture(&right);
    recognizer.registerPinchGesture(&expand);
    recognizer.registerPinchGesture(&contract);

    QSignalSpy upSpy(&up, &SwipeGesture::progress);
    QSignalSpy downSpy(&down, &SwipeGesture::progress);
    QSignalSpy rightSpy(&right, &SwipeGesture::progress);
    QSignalSpy expandSpy(&expand, &PinchGesture::progress);
    QSignalSpy contractSpy(&contract, &PinchGesture::progress);

    // don't release callback until we know the direction of swipe gesture
    recognizer.startSwipeGesture(4);
    QCOMPARE(upSpy.count(), 0);
    QCOMPARE(downSpy.count(), 0);
    QCOMPARE(rightSpy.count(), 0);

    // up (negative y)
    recognizer.updateSwipeGesture(QSizeF(0, -1.5));
    QCOMPARE(upSpy.count(), 1);
    QCOMPARE(downSpy.count(), 0);
    QCOMPARE(rightSpy.count(), 0);

    // down (positive y)
    // recognizer.updateSwipeGesture(QSizeF(0, 0));
    recognizer.updateSwipeGesture(QSizeF(0, 3));
    QCOMPARE(upSpy.count(), 1);
    QCOMPARE(downSpy.count(), 1);
    QCOMPARE(rightSpy.count(), 0);

    // right
    recognizer.cancelSwipeGesture();
    recognizer.startSwipeGesture(4);
    recognizer.updateSwipeGesture(QSizeF(1, 0));
    QCOMPARE(upSpy.count(), 1);
    QCOMPARE(downSpy.count(), 1);
    QCOMPARE(rightSpy.count(), 1);

    recognizer.cancelSwipeGesture();

    // same test for pinch gestures
    recognizer.startPinchGesture(4);
    QCOMPARE(expandSpy.count(), 0);
    QCOMPARE(contractSpy.count(), 0);

    // contracting
    recognizer.updatePinchGesture(.5, 0, QSizeF(0, 0));
    QCOMPARE(expandSpy.count(), 0);
    QCOMPARE(contractSpy.count(), 1);

    // expanding
    recognizer.updatePinchGesture(1.5, 0, QSizeF(0, 0));
    QCOMPARE(expandSpy.count(), 1);
    QCOMPARE(contractSpy.count(), 1);
}

void GestureTest::testSwipeGeometryStart_data()
{
    QTest::addColumn<QRect>("geometry");
    QTest::addColumn<QPointF>("startPos");
    QTest::addColumn<bool>("started");

    QTest::newRow("top left") << QRect(0, 0, 10, 20) << QPointF(0, 0) << true;
    QTest::newRow("top right") << QRect(0, 0, 10, 20) << QPointF(10, 0) << true;
    QTest::newRow("bottom left") << QRect(0, 0, 10, 20) << QPointF(0, 20) << true;
    QTest::newRow("bottom right") << QRect(0, 0, 10, 20) << QPointF(10, 20) << true;
    QTest::newRow("x too small") << QRect(10, 20, 30, 40) << QPointF(9, 25) << false;
    QTest::newRow("y too small") << QRect(10, 20, 30, 40) << QPointF(25, 19) << false;
    QTest::newRow("x too large") << QRect(10, 20, 30, 40) << QPointF(41, 25) << false;
    QTest::newRow("y too large") << QRect(10, 20, 30, 40) << QPointF(25, 61) << false;
    QTest::newRow("inside") << QRect(10, 20, 30, 40) << QPointF(25, 25) << true;
}

void GestureTest::testSwipeGeometryStart()
{
    GestureRecognizer recognizer;
    SwipeGesture gesture;
    QFETCH(QRect, geometry);
    gesture.setStartGeometry(geometry);

    QSignalSpy startedSpy(&gesture, &SwipeGesture::started);
    QVERIFY(startedSpy.isValid());

    recognizer.registerSwipeGesture(&gesture);
    QFETCH(QPointF, startPos);
    recognizer.startSwipeGesture(startPos);
    QTEST(!startedSpy.isEmpty(), "started");
}

QTEST_MAIN(GestureTest)
#include "test_gestures.moc"
