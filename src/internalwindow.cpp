/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "internalwindow.h"
#include "decorations/decorationbridge.h"
#include "deleted.h"
#include "platform.h"
#include "surfaceitem.h"
#include "windowitem.h"
#include "workspace.h"

#include <KDecoration2/Decoration>

#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QWindow>

Q_DECLARE_METATYPE(NET::WindowType)

static const QByteArray s_skipClosePropertyName = QByteArrayLiteral("KWIN_SKIP_CLOSE_ANIMATION");
static const QByteArray s_shadowEnabledPropertyName = QByteArrayLiteral("kwin_shadow_enabled");

namespace KWin
{

InternalWindow::InternalWindow(QWindow *handle)
    : m_handle(handle)
    , m_internalWindowFlags(handle->flags())
{
    connect(m_handle, &QWindow::xChanged, this, &InternalWindow::updateInternalWindowGeometry);
    connect(m_handle, &QWindow::yChanged, this, &InternalWindow::updateInternalWindowGeometry);
    connect(m_handle, &QWindow::widthChanged, this, &InternalWindow::updateInternalWindowGeometry);
    connect(m_handle, &QWindow::heightChanged, this, &InternalWindow::updateInternalWindowGeometry);
    connect(m_handle, &QWindow::windowTitleChanged, this, &InternalWindow::setCaption);
    connect(m_handle, &QWindow::opacityChanged, this, &InternalWindow::setOpacity);
    connect(m_handle, &QWindow::destroyed, this, &InternalWindow::destroyWindow);

    const QVariant windowType = m_handle->property("kwin_windowType");
    if (!windowType.isNull()) {
        m_windowType = windowType.value<NET::WindowType>();
    }

    setCaption(m_handle->title());
    setIcon(QIcon::fromTheme(QStringLiteral("kwin")));
    setOnAllDesktops(true);
    setOpacity(m_handle->opacity());
    setSkipCloseAnimation(m_handle->property(s_skipClosePropertyName).toBool());

    // Create scene window, effect window, and update server-side shadow.
    setupCompositing();
    updateColorScheme();

    blockGeometryUpdates(true);
    commitGeometry(m_handle->geometry());
    updateDecoration(true);
    moveResize(clientRectToFrameRect(m_handle->geometry()));
    blockGeometryUpdates(false);

    m_handle->installEventFilter(this);
}

InternalWindow::~InternalWindow()
{
}

WindowItem *InternalWindow::createItem()
{
    return new WindowItemInternal(this);
}

bool InternalWindow::isClient() const
{
    return true;
}

bool InternalWindow::hitTest(const QPointF &point) const
{
    if (!Window::hitTest(point)) {
        return false;
    }

    const QRegion mask = m_handle->mask();
    if (!mask.isEmpty() && !mask.contains(mapToLocal(point).toPoint())) {
        return false;
    } else if (m_handle->property("outputOnly").toBool()) {
        return false;
    }

    return true;
}

void InternalWindow::pointerEnterEvent(const QPointF &globalPos)
{
    Window::pointerEnterEvent(globalPos);

    QEnterEvent enterEvent(pos(), pos(), globalPos);
    QCoreApplication::sendEvent(m_handle, &enterEvent);
}

void InternalWindow::pointerLeaveEvent()
{
    Window::pointerLeaveEvent();

    QEvent event(QEvent::Leave);
    QCoreApplication::sendEvent(m_handle, &event);
}

bool InternalWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_handle && event->type() == QEvent::DynamicPropertyChange) {
        QDynamicPropertyChangeEvent *pe = static_cast<QDynamicPropertyChangeEvent *>(event);
        if (pe->propertyName() == s_skipClosePropertyName) {
            setSkipCloseAnimation(m_handle->property(s_skipClosePropertyName).toBool());
        }
        if (pe->propertyName() == s_shadowEnabledPropertyName) {
            // Some dialog e.g. Plasma::Dialog may update shadow in the middle of rendering.
            // The opengl context changed by updateShadow may break the QML Window rendering
            // and cause crash.
            QMetaObject::invokeMethod(
                this, [this]() {
                    updateShadow();
                },
                Qt::QueuedConnection);
        }
        if (pe->propertyName() == "kwin_windowType") {
            m_windowType = m_handle->property("kwin_windowType").value<NET::WindowType>();
            workspace()->updateClientArea();
        }
    }
    return false;
}

qreal InternalWindow::bufferScale() const
{
    if (m_handle) {
        return m_handle->devicePixelRatio();
    }
    return 1;
}

QString InternalWindow::captionNormal() const
{
    return m_captionNormal;
}

QString InternalWindow::captionSuffix() const
{
    return m_captionSuffix;
}

QSizeF InternalWindow::minSize() const
{
    return m_handle->minimumSize();
}

QSizeF InternalWindow::maxSize() const
{
    return m_handle->maximumSize();
}

NET::WindowType InternalWindow::windowType(bool direct, int supported_types) const
{
    Q_UNUSED(direct)
    Q_UNUSED(supported_types)
    return m_windowType;
}

void InternalWindow::killWindow()
{
    // We don't kill our internal windows.
}

bool InternalWindow::isPopupWindow() const
{
    if (Window::isPopupWindow()) {
        return true;
    }
    return m_internalWindowFlags.testFlag(Qt::Popup);
}

QByteArray InternalWindow::windowRole() const
{
    return QByteArray();
}

void InternalWindow::closeWindow()
{
    if (m_handle) {
        m_handle->hide();
    }
}

bool InternalWindow::isCloseable() const
{
    return true;
}

bool InternalWindow::isMovable() const
{
    return true;
}

bool InternalWindow::isMovableAcrossScreens() const
{
    return true;
}

bool InternalWindow::isResizable() const
{
    return true;
}

bool InternalWindow::isPlaceable() const
{
    return !m_internalWindowFlags.testFlag(Qt::BypassWindowManagerHint) && !m_internalWindowFlags.testFlag(Qt::Popup);
}

bool InternalWindow::noBorder() const
{
    return m_userNoBorder || m_internalWindowFlags.testFlag(Qt::FramelessWindowHint) || m_internalWindowFlags.testFlag(Qt::Popup);
}

bool InternalWindow::userCanSetNoBorder() const
{
    return !m_internalWindowFlags.testFlag(Qt::FramelessWindowHint) || m_internalWindowFlags.testFlag(Qt::Popup);
}

bool InternalWindow::wantsInput() const
{
    return false;
}

bool InternalWindow::isInternal() const
{
    return true;
}

bool InternalWindow::isLockScreen() const
{
    if (m_handle) {
        return m_handle->property("org_kde_ksld_emergency").toBool();
    }
    return false;
}

bool InternalWindow::isOutline() const
{
    if (m_handle) {
        return m_handle->property("__kwin_outline").toBool();
    }
    return false;
}

bool InternalWindow::isShown() const
{
    return readyForPainting();
}

bool InternalWindow::isHiddenInternal() const
{
    return false;
}

void InternalWindow::hideClient()
{
}

void InternalWindow::showClient()
{
}

void InternalWindow::resizeWithChecks(const QSizeF &size)
{
    if (!m_handle) {
        return;
    }
    const QRectF area = workspace()->clientArea(WorkArea, this);
    resize(size.boundedTo(area.size()));
}

void InternalWindow::moveResizeInternal(const QRectF &rect, MoveResizeMode mode)
{
    if (areGeometryUpdatesBlocked()) {
        setPendingMoveResizeMode(mode);
        return;
    }

    const QSizeF requestedClientSize = frameSizeToClientSize(rect.size());
    if (clientSize() == requestedClientSize) {
        commitGeometry(rect);
    } else {
        requestGeometry(rect);
    }
}

Window *InternalWindow::findModal(bool allow_itself)
{
    Q_UNUSED(allow_itself)
    return nullptr;
}

bool InternalWindow::takeFocus()
{
    return false;
}

void InternalWindow::setNoBorder(bool set)
{
    if (!userCanSetNoBorder()) {
        return;
    }
    if (m_userNoBorder == set) {
        return;
    }
    m_userNoBorder = set;
    updateDecoration(true);
}

void InternalWindow::createDecoration(const QRectF &oldGeometry)
{
    setDecoration(std::shared_ptr<KDecoration2::Decoration>(Workspace::self()->decorationBridge()->createDecoration(this)));
    moveResize(oldGeometry);

    Q_EMIT geometryShapeChanged(this, oldGeometry);
}

void InternalWindow::destroyDecoration()
{
    const QSizeF clientSize = frameSizeToClientSize(moveResizeGeometry().size());
    setDecoration(nullptr);
    resize(clientSize);
}

void InternalWindow::updateDecoration(bool check_workspace_pos, bool force)
{
    if (!force && isDecorated() == !noBorder()) {
        return;
    }

    GeometryUpdatesBlocker blocker(this);

    const QRectF oldFrameGeometry = frameGeometry();
    if (force) {
        destroyDecoration();
    }

    if (!noBorder()) {
        createDecoration(oldFrameGeometry);
    } else {
        destroyDecoration();
    }

    updateShadow();

    if (check_workspace_pos) {
        checkWorkspacePosition(oldFrameGeometry);
    }
}

void InternalWindow::invalidateDecoration()
{
    updateDecoration(true, true);
}

void InternalWindow::destroyWindow()
{
    markAsZombie();
    if (isInteractiveMoveResize()) {
        leaveInteractiveMoveResize();
        Q_EMIT clientFinishUserMovedResized(this);
    }

    Deleted *deleted = Deleted::create(this);
    Q_EMIT windowClosed(this, deleted);

    destroyDecoration();

    workspace()->removeInternalWindow(this);

    deleted->unrefWindow();
    m_handle = nullptr;

    delete this;
}

bool InternalWindow::hasPopupGrab() const
{
    return !m_handle->flags().testFlag(Qt::WindowTransparentForInput) && m_handle->flags().testFlag(Qt::Popup) && !m_handle->flags().testFlag(Qt::ToolTip);
}

void InternalWindow::popupDone()
{
    m_handle->hide();
}

void InternalWindow::present(const std::shared_ptr<QOpenGLFramebufferObject> fbo)
{
    Q_ASSERT(m_internalImage.isNull());

    const QSizeF bufferSize = fbo->size() / bufferScale();

    commitGeometry(QRectF(pos(), clientSizeToFrameSize(bufferSize)));
    markAsMapped();

    m_internalFBO = fbo;

    setDepth(32);
    surfaceItem()->addDamage(surfaceItem()->rect().toAlignedRect());
}

void InternalWindow::present(const QImage &image, const QRegion &damage)
{
    Q_ASSERT(m_internalFBO == nullptr);

    const QSize bufferSize = image.size() / bufferScale();

    commitGeometry(QRectF(pos(), clientSizeToFrameSize(bufferSize)));
    markAsMapped();

    m_internalImage = image;

    setDepth(32);
    surfaceItem()->addDamage(damage);
}

QWindow *InternalWindow::handle() const
{
    return m_handle;
}

bool InternalWindow::acceptsFocus() const
{
    return false;
}

bool InternalWindow::belongsToSameApplication(const Window *other, SameApplicationChecks checks) const
{
    Q_UNUSED(checks)
    const InternalWindow *otherInternal = qobject_cast<const InternalWindow *>(other);
    if (!otherInternal) {
        return false;
    }
    if (otherInternal == this) {
        return true;
    }
    return otherInternal->handle()->isAncestorOf(handle()) || handle()->isAncestorOf(otherInternal->handle());
}

void InternalWindow::doInteractiveResizeSync()
{
    requestGeometry(moveResizeGeometry());
}

void InternalWindow::updateCaption()
{
    const QString oldSuffix = m_captionSuffix;
    const auto shortcut = shortcutCaptionSuffix();
    m_captionSuffix = shortcut;
    if ((!isSpecialWindow() || isToolbar()) && findWindowWithSameCaption()) {
        int i = 2;
        do {
            m_captionSuffix = shortcut + QLatin1String(" <") + QString::number(i) + QLatin1Char('>');
            i++;
        } while (findWindowWithSameCaption());
    }
    if (m_captionSuffix != oldSuffix) {
        Q_EMIT captionChanged();
    }
}

void InternalWindow::requestGeometry(const QRectF &rect)
{
    if (m_handle) {
        m_handle->setGeometry(frameRectToClientRect(rect).toRect());
    }
}

void InternalWindow::commitGeometry(const QRectF &rect)
{
    // The client geometry and the buffer geometry are the same.
    const QRectF oldClientGeometry = m_clientGeometry;
    const QRectF oldFrameGeometry = m_frameGeometry;
    const Output *oldOutput = m_output;

    Q_EMIT frameGeometryAboutToChange(this);

    m_clientGeometry = frameRectToClientRect(rect);
    m_frameGeometry = rect;
    m_bufferGeometry = m_clientGeometry;

    if (oldClientGeometry == m_clientGeometry && oldFrameGeometry == m_frameGeometry) {
        return;
    }

    m_output = workspace()->outputAt(rect.center());
    syncGeometryToInternalWindow();

    if (oldClientGeometry != m_clientGeometry) {
        Q_EMIT bufferGeometryChanged(this, oldClientGeometry);
        Q_EMIT clientGeometryChanged(this, oldClientGeometry);
    }
    if (oldFrameGeometry != m_frameGeometry) {
        Q_EMIT frameGeometryChanged(this, oldFrameGeometry);
    }
    if (oldOutput != m_output) {
        Q_EMIT screenChanged();
    }
    Q_EMIT geometryShapeChanged(this, oldFrameGeometry);
}

void InternalWindow::setCaption(const QString &caption)
{
    if (m_captionNormal == caption) {
        return;
    }

    m_captionNormal = caption;

    const QString oldCaptionSuffix = m_captionSuffix;
    updateCaption();

    if (m_captionSuffix == oldCaptionSuffix) {
        Q_EMIT captionChanged();
    }
}

void InternalWindow::markAsMapped()
{
    if (!ready_for_painting) {
        setReadyForPainting();
        workspace()->addInternalWindow(this);
    }
}

void InternalWindow::syncGeometryToInternalWindow()
{
    if (m_handle->geometry() == frameRectToClientRect(frameGeometry())) {
        return;
    }

    QTimer::singleShot(0, this, [this] {
        requestGeometry(frameGeometry());
    });
}

void InternalWindow::updateInternalWindowGeometry()
{
    if (!isInteractiveMoveResize()) {
        const QRectF rect = clientRectToFrameRect(m_handle->geometry());
        setMoveResizeGeometry(rect);
        commitGeometry(rect);
    }
}

}
