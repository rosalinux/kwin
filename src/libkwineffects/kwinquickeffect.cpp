/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwinquickeffect.h"

#include "sharedqmlengine.h"

#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>

namespace KWin
{

class QuickSceneEffectPrivate
{
public:
    static QuickSceneEffectPrivate *get(QuickSceneEffect *effect)
    {
        return effect->d.data();
    }
    bool isItemOnScreen(QQuickItem *item, EffectScreen *screen);

    SharedQmlEngine::Ptr qmlEngine;
    QScopedPointer<QQmlComponent> qmlComponent;
    QUrl source;
    QHash<EffectScreen *, QuickSceneView *> views;
    QPointer<QuickSceneView> mouseImplicitGrab;
    bool running = false;
    QScopedPointer<QWindow> dummyWindow;
    EffectScreen *paintedScreen = nullptr;
};

bool QuickSceneEffectPrivate::isItemOnScreen(QQuickItem *item, EffectScreen *screen)
{
    if (!item || !screen || !views.contains(screen)) {
        return false;
    }

    auto *view = views[screen];
    auto *rootItem = view->rootItem();
    auto candidate = item->parentItem();
    // Is there a more efficient way?
    while (candidate) {
        if (candidate == rootItem) {
            return true;
        }
        candidate = candidate->parentItem();
    }
    return false;
}

QuickSceneView::QuickSceneView(QuickSceneEffect *effect, EffectScreen *screen)
    : OffscreenQuickView(effect, QuickSceneEffectPrivate::get(effect)->dummyWindow.data())
    , m_effect(effect)
    , m_screen(screen)
{
    setGeometry(screen->geometry());
    connect(screen, &EffectScreen::geometryChanged, this, [this, screen]() {
        setGeometry(screen->geometry());
    });
}

QuickSceneView::~QuickSceneView()
{
}

QQuickItem *QuickSceneView::rootItem() const
{
    return m_rootItem.data();
}

void QuickSceneView::setRootItem(QQuickItem *item)
{
    Q_ASSERT_X(item, "setRootItem", "root item cannot be null");
    m_rootItem.reset(item);
    m_rootItem->setParentItem(contentItem());

    auto updateSize = [this]() {
        m_rootItem->setSize(contentItem()->size());
    };
    updateSize();
    connect(contentItem(), &QQuickItem::widthChanged, m_rootItem.data(), updateSize);
    connect(contentItem(), &QQuickItem::heightChanged, m_rootItem.data(), updateSize);
}

QuickSceneEffect *QuickSceneView::effect() const
{
    return m_effect;
}

EffectScreen *QuickSceneView::screen() const
{
    return m_screen;
}

bool QuickSceneView::isDirty() const
{
    return m_dirty;
}

void QuickSceneView::markDirty()
{
    m_dirty = true;
}

void QuickSceneView::resetDirty()
{
    m_dirty = false;
}

void QuickSceneView::scheduleRepaint()
{
    markDirty();
    effects->addRepaint(geometry());
}

QuickSceneEffect::QuickSceneEffect(QObject *parent)
    : Effect(parent)
    , d(new QuickSceneEffectPrivate)
{
}

QuickSceneEffect::~QuickSceneEffect()
{
}

bool QuickSceneEffect::supported()
{
    return effects->compositingType() == OpenGLCompositing;
}

void QuickSceneEffect::checkItemDraggedOutOfScreen(QQuickItem *item)
{
    const QRectF globalGeom = QRectF(item->mapToGlobal(QPointF(0, 0)), QSizeF(item->width(), item->height()));
    QList<EffectScreen *> screens;

    for (auto it = d->views.constBegin(); it != d->views.constEnd(); ++it) {
        if (!d->isItemOnScreen(item, it.key()) && it.key()->geometry().intersects(globalGeom.toRect())) {
            screens << it.key();
        }
    }

    Q_EMIT itemDraggedOutOfScreen(item, screens);
}

void QuickSceneEffect::checkItemDroppedOutOfScreen(const QPointF &globalPos, QQuickItem *item)
{
    EffectScreen *screen = nullptr;
    for (auto it = d->views.constBegin(); it != d->views.constEnd(); ++it) {
        if (!d->isItemOnScreen(item, it.key()) && it.key()->geometry().contains(globalPos.toPoint())) {
            screen = it.key();
            break;
        }
    }

    if (screen) {
        Q_EMIT itemDroppedOutOfScreen(globalPos, item, screen);
    }
}

bool QuickSceneEffect::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::CursorChange) {
        if (const QWindow *window = qobject_cast<QWindow *>(watched)) {
            effects->defineCursor(window->cursor().shape());
        }
    }
    return false;
}

bool QuickSceneEffect::isRunning() const
{
    return d->running;
}

void QuickSceneEffect::setRunning(bool running)
{
    if (d->running != running) {
        if (running) {
            startInternal();
        } else {
            stopInternal();
        }
    }
}

QUrl QuickSceneEffect::source() const
{
    return d->source;
}

void QuickSceneEffect::setSource(const QUrl &url)
{
    if (isRunning()) {
        qWarning() << "Cannot change QuickSceneEffect.source while running";
        return;
    }
    if (d->source != url) {
        d->source = url;
        d->qmlComponent.reset();
    }
}

QHash<EffectScreen *, QuickSceneView *> QuickSceneEffect::views() const
{
    return d->views;
}

QuickSceneView *QuickSceneEffect::viewAt(const QPoint &pos) const
{
    for (QuickSceneView *screenView : qAsConst(d->views)) {
        if (screenView->geometry().contains(pos)) {
            return screenView;
        }
    }
    return nullptr;
}

void QuickSceneEffect::activateView(QuickSceneView *view)
{
    for (auto *otherView : d->views) {
        if (otherView == view && !view->window()->activeFocusItem()) {
            QFocusEvent focusEvent(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
            qApp->sendEvent(view->window(), &focusEvent);
        } else if (otherView->window()->activeFocusItem()) {
            QFocusEvent focusEvent(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
            qApp->sendEvent(otherView->window(), &focusEvent);
        }
    }
}

void QuickSceneEffect::paintScreen(int mask, const QRegion &region, ScreenPaintData &data)
{
    Q_UNUSED(mask)
    Q_UNUSED(region)

    d->paintedScreen = data.screen();

    if (effects->waylandDisplay()) {
        QuickSceneView *screenView = d->views.value(data.screen());
        if (screenView) {
            effects->renderOffscreenQuickView(screenView);
        }
    } else {
        for (QuickSceneView *screenView : qAsConst(d->views)) {
            effects->renderOffscreenQuickView(screenView);
        }
    }
}

void QuickSceneEffect::postPaintScreen()
{
    // Screen views are repainted after kwin performs its compositing cycle. Another alternative
    // is to update the views after receiving a vblank.
    if (effects->waylandDisplay()) {
        QuickSceneView *screenView = d->views.value(d->paintedScreen);
        if (screenView && screenView->isDirty()) {
            QMetaObject::invokeMethod(screenView, &QuickSceneView::update, Qt::QueuedConnection);
            screenView->resetDirty();
        }
    } else {
        for (QuickSceneView *screenView : qAsConst(d->views)) {
            if (screenView->isDirty()) {
                QMetaObject::invokeMethod(screenView, &QuickSceneView::update, Qt::QueuedConnection);
                screenView->resetDirty();
            }
        }
    }
    effects->postPaintScreen();
}

bool QuickSceneEffect::isActive() const
{
    return !d->views.isEmpty() && !effects->isScreenLocked();
}

QVariantMap QuickSceneEffect::initialProperties(EffectScreen *screen)
{
    Q_UNUSED(screen)
    return QVariantMap();
}

void QuickSceneEffect::handleScreenAdded(EffectScreen *screen)
{
    addScreen(screen);
}

void QuickSceneEffect::handleScreenRemoved(EffectScreen *screen)
{
    delete d->views.take(screen);
}

void QuickSceneEffect::addScreen(EffectScreen *screen)
{
    QuickSceneView *view = new QuickSceneView(this, screen);
    auto properties = initialProperties(screen);
    properties["width"] = view->geometry().width();
    properties["height"] = view->geometry().height();
    view->setRootItem(qobject_cast<QQuickItem *>(d->qmlComponent->createWithInitialProperties(properties)));
    // we need the focus always set to the view of activescreen at first, and changed only upon user interaction
    if (view->contentItem()) {
        view->contentItem()->setFocus(false);
    }
    view->setAutomaticRepaint(false);

    connect(view, &QuickSceneView::repaintNeeded, this, [view]() {
        effects->addRepaint(view->geometry());
    });
    connect(view, &QuickSceneView::renderRequested, view, &QuickSceneView::scheduleRepaint);
    connect(view, &QuickSceneView::sceneChanged, view, &QuickSceneView::scheduleRepaint);

    view->scheduleRepaint();
    d->views.insert(screen, view);
}

void QuickSceneEffect::startInternal()
{
    if (effects->activeFullScreenEffect()) {
        return;
    }

    if (Q_UNLIKELY(d->source.isEmpty())) {
        qWarning() << "QuickSceneEffect.source is empty. Did you forget to call setSource()?";
        return;
    }

    if (!d->qmlEngine) {
        d->qmlEngine = SharedQmlEngine::engine();
    }

    if (!d->qmlComponent) {
        d->qmlComponent.reset(new QQmlComponent(d->qmlEngine.get()));
        d->qmlComponent->loadUrl(d->source);
        if (d->qmlComponent->isError()) {
            qWarning().nospace() << "Failed to load " << d->source << ": " << d->qmlComponent->errors();
            d->qmlComponent.reset();
            return;
        }
    }

    effects->setActiveFullScreenEffect(this);
    d->running = true;

    // Install an event filter to monitor cursor shape changes.
    qApp->installEventFilter(this);

    // This is an ugly hack to make hidpi rendering work as expected on wayland until we switch
    // to Qt 6.3 or newer. See https://codereview.qt-project.org/c/qt/qtdeclarative/+/361506
    if (effects->waylandDisplay()) {
        d->dummyWindow.reset(new QWindow());
        d->dummyWindow->setOpacity(0);
        d->dummyWindow->resize(1, 1);
        d->dummyWindow->setFlag(Qt::FramelessWindowHint);
        d->dummyWindow->setVisible(true);
        d->dummyWindow->requestActivate();
    }

    const QList<EffectScreen *> screens = effects->screens();
    for (EffectScreen *screen : screens) {
        addScreen(screen);
    }

    connect(effects, &EffectsHandler::screenAdded, this, &QuickSceneEffect::handleScreenAdded);
    connect(effects, &EffectsHandler::screenRemoved, this, &QuickSceneEffect::handleScreenRemoved);

    effects->grabKeyboard(this);
    effects->startMouseInterception(this, Qt::ArrowCursor);
}

void QuickSceneEffect::stopInternal()
{
    disconnect(effects, &EffectsHandler::screenAdded, this, &QuickSceneEffect::handleScreenAdded);
    disconnect(effects, &EffectsHandler::screenRemoved, this, &QuickSceneEffect::handleScreenRemoved);

    qDeleteAll(d->views);
    d->views.clear();
    d->dummyWindow.reset();
    d->running = false;
    qApp->removeEventFilter(this);
    effects->ungrabKeyboard();
    effects->stopMouseInterception(this);
    effects->setActiveFullScreenEffect(nullptr);
    effects->addRepaintFull();
}

void QuickSceneEffect::windowInputMouseEvent(QEvent *event)
{
    Qt::MouseButtons buttons;
    QPoint globalPosition;
    if (QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event)) {
        buttons = mouseEvent->buttons();
        globalPosition = mouseEvent->globalPos();
    } else if (QWheelEvent *wheelEvent = dynamic_cast<QWheelEvent *>(event)) {
        buttons = wheelEvent->buttons();
        globalPosition = wheelEvent->globalPosition().toPoint();
    } else {
        return;
    }

    if (buttons) {
        if (!d->mouseImplicitGrab) {
            d->mouseImplicitGrab = viewAt(globalPosition);
        }
    }

    QuickSceneView *target = d->mouseImplicitGrab;
    if (!target) {
        target = viewAt(globalPosition);
    }

    if (!buttons) {
        d->mouseImplicitGrab = nullptr;
    }

    if (target) {
        if (buttons) {
            activateView(target);
        }
        target->forwardMouseEvent(event);
    }
}

void QuickSceneEffect::grabbedKeyboardEvent(QKeyEvent *keyEvent)
{
    auto it = std::ranges::find_if(std::as_const(d->views), [](QuickSceneView *v) {
        return v->window()->activeFocusItem();
    });

    if (it == d->views.constEnd()) {
        QuickSceneView *screenView = d->views.value(effects->activeScreen());
        if (screenView) {
            activateView(screenView);
            screenView->forwardKeyEvent(keyEvent);
        }
    } else {
        (*it)->forwardKeyEvent(keyEvent);
        return;
    }
}

bool QuickSceneEffect::touchDown(qint32 id, const QPointF &pos, quint32 time)
{
    for (QuickSceneView *screenView : qAsConst(d->views)) {
        if (screenView->geometry().contains(pos.toPoint())) {
            activateView(screenView);
            return screenView->forwardTouchDown(id, pos, time);
        }
    }
    return false;
}

bool QuickSceneEffect::touchMotion(qint32 id, const QPointF &pos, quint32 time)
{
    for (QuickSceneView *screenView : qAsConst(d->views)) {
        if (screenView->geometry().contains(pos.toPoint())) {
            return screenView->forwardTouchMotion(id, pos, time);
        }
    }
    return false;
}

bool QuickSceneEffect::touchUp(qint32 id, quint32 time)
{
    for (QuickSceneView *screenView : qAsConst(d->views)) {
        if (screenView->forwardTouchUp(id, time)) {
            return true;
        }
    }
    return false;
}

} // namespace KWin
