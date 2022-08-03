/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwin_wayland_test.h"

#include "composite.h"
#include "effectloader.h"
#include "effects.h"
#include "platform.h"
#include "renderbackend.h"
#include "wayland_server.h"
#include "window.h"
#include "workspace.h"

#include <KWayland/Client/plasmashell.h>
#include <KWayland/Client/plasmawindowmanagement.h>
#include <KWayland/Client/surface.h>

using namespace KWin;

static const QString s_socketName = QStringLiteral("wayland_test_effects_minimize_animation-0");

class MinimizeAnimationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMinimizeUnminimize_data();
    void testMinimizeUnminimize();
};

void MinimizeAnimationTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    qRegisterMetaType<KWin::Window *>();
    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    QVERIFY(applicationStartedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName));

    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    const auto builtinNames = EffectLoader().listOfKnownEffects();
    for (const QString &name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }
    config->sync();
    kwinApp()->setConfig(config);

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));

    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    Test::initWaylandWorkspace();

    QCOMPARE(Compositor::self()->backend()->compositingType(), KWin::OpenGLCompositing);
}

void MinimizeAnimationTest::init()
{
    QVERIFY(Test::setupWaylandConnection(
        Test::AdditionalWaylandInterface::PlasmaShell | Test::AdditionalWaylandInterface::WindowManagement));
}

void MinimizeAnimationTest::cleanup()
{
    auto effectsImpl = qobject_cast<EffectsHandlerImpl *>(effects);
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::destroyWaylandConnection();
}

void MinimizeAnimationTest::testMinimizeUnminimize_data()
{
    QTest::addColumn<QString>("effectName");

    QTest::newRow("Magic Lamp") << QStringLiteral("magiclamp");
    QTest::newRow("Squash") << QStringLiteral("kwin4_effect_squash");
}

void MinimizeAnimationTest::testMinimizeUnminimize()
{
    // This test verifies that a minimize effect tries to animate a window
    // when it's minimized or unminimized.

    using namespace KWayland::Client;

    QSignalSpy plasmaWindowCreatedSpy(Test::waylandWindowManagement(), &PlasmaWindowManagement::windowCreated);
    QVERIFY(plasmaWindowCreatedSpy.isValid());

    // Create a panel at the top of the screen.
    const QRect panelRect = QRect(0, 0, 1280, 36);
    std::unique_ptr<KWayland::Client::Surface> panelSurface(Test::createSurface());
    QVERIFY(panelSurface != nullptr);
    std::unique_ptr<Test::XdgToplevel> panelShellSurface(Test::createXdgToplevelSurface(panelSurface.get()));
    QVERIFY(panelShellSurface != nullptr);
    std::unique_ptr<PlasmaShellSurface> plasmaPanelShellSurface(Test::waylandPlasmaShell()->createSurface(panelSurface.get()));
    QVERIFY(plasmaPanelShellSurface != nullptr);
    plasmaPanelShellSurface->setRole(PlasmaShellSurface::Role::Panel);
    plasmaPanelShellSurface->setPosition(panelRect.topLeft());
    plasmaPanelShellSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::AlwaysVisible);
    Window *panel = Test::renderAndWaitForShown(panelSurface.get(), panelRect.size(), Qt::blue);
    QVERIFY(panel);
    QVERIFY(panel->isDock());
    QCOMPARE(panel->frameGeometry(), panelRect);
    QVERIFY(plasmaWindowCreatedSpy.wait());
    QCOMPARE(plasmaWindowCreatedSpy.count(), 1);

    // Create the test window.
    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface());
    QVERIFY(surface != nullptr);
    std::unique_ptr<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.get()));
    QVERIFY(shellSurface != nullptr);
    Window *window = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::red);
    QVERIFY(window);
    QVERIFY(plasmaWindowCreatedSpy.wait());
    QCOMPARE(plasmaWindowCreatedSpy.count(), 2);

    // We have to set the minimized geometry because the squash effect needs it,
    // otherwise it won't start animation.
    auto plasmaWindow = plasmaWindowCreatedSpy.last().first().value<PlasmaWindow *>();
    QVERIFY(plasmaWindow);
    const QRect iconRect = QRect(0, 0, 42, 36);
    plasmaWindow->setMinimizedGeometry(panelSurface.get(), iconRect);
    Test::flushWaylandConnection();
    QTRY_COMPARE(window->iconGeometry(), iconRect.translated(panel->frameGeometry().topLeft().toPoint()));

    // Load effect that will be tested.
    QFETCH(QString, effectName);
    auto effectsImpl = qobject_cast<EffectsHandlerImpl *>(effects);
    QVERIFY(effectsImpl);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect *effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Start the minimize animation.
    window->minimize();
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Start the unminimize animation.
    window->unminimize();
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Destroy the panel.
    panelSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(panel));

    // Destroy the test window.
    surface.reset();
    QVERIFY(Test::waitForWindowDestroyed(window));
}

WAYLANDTEST_MAIN(MinimizeAnimationTest)
#include "minimize_animation_test.moc"
