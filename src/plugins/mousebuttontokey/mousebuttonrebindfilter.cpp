/*
    SPDX-FileCopyrightText: 2022 David Redondo <kde@david-redono.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "mousebuttonrebindfilter.h"

#include "input_event.h"
#include "keyboard_input.h"

#include <QMetaEnum>

QString InputDevice::name() const
{
    return QStringLiteral("Mouse button rebinding device");
}

QString InputDevice::sysName() const
{
    return QString();
}

KWin::LEDs InputDevice::leds() const
{
    return {};
}

void InputDevice::setLeds(KWin::LEDs leds)
{
    Q_UNUSED(leds)
}

void InputDevice::setEnabled(bool enabled)
{
    Q_UNUSED(enabled)
}

bool InputDevice::isEnabled() const
{
    return true;
}

bool InputDevice::isAlphaNumericKeyboard() const
{
    return true;
}

bool InputDevice::isKeyboard() const
{
    return true;
}

bool InputDevice::isLidSwitch() const
{
    return false;
}

bool InputDevice::isPointer() const
{
    return false;
}

bool InputDevice::isTabletModeSwitch() const
{
    return false;
}

bool InputDevice::isTabletPad() const
{
    return false;
}

bool InputDevice::isTabletTool() const
{
    return false;
}

bool InputDevice::isTouch() const
{
    return false;
}

bool InputDevice::isTouchpad() const
{
    return false;
}

MouseButtonRebindFilter::MouseButtonRebindFilter(QObject *parent)
    : m_configWatcher(KConfigWatcher::create(KSharedConfig::openConfig("kcminputrc")))
{
    KWin::input()->addInputDevice(&m_inputDevice);
    const QString groupName = QStringLiteral("MouseButtonRebinds");
    connect(m_configWatcher.get(), &KConfigWatcher::configChanged, this, [this, groupName](const KConfigGroup &group) {
        if (group.name() == groupName) {
            loadConfig(group);
        }
    });
    loadConfig(m_configWatcher->config()->group(groupName));
}

void MouseButtonRebindFilter::loadConfig(const KConfigGroup &group)
{
    m_buttonMapping.clear();
    KWin::input()->uninstallInputEventFilter(this);
    auto mouseButtonEnum = QMetaEnum::fromType<Qt::MouseButtons>();
    for (int i = 1; i <= 24; ++i) {
        const QByteArray buttonName = QByteArray("ExtraButton") + QByteArray::number(i);
        auto keys = QKeySequence::fromString(group.readEntry(buttonName.data()), QKeySequence::PortableText);
        if (!keys.isEmpty()) {
            m_buttonMapping.insert(static_cast<Qt::MouseButton>(mouseButtonEnum.keyToValue(buttonName)), keys);
        }
    }
    qDebug() << m_buttonMapping;
    if (m_buttonMapping.size() > 0) {
        KWin::input()->prependInputEventFilter(this);
    }
}

bool MouseButtonRebindFilter::pointerEvent(QMouseEvent *event, quint32 nativeButton)
{
    Q_UNUSED(nativeButton);

    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease) {
        return false;
    }

    QKeySequence keys = m_buttonMapping.value(event->button());
    qDebug() << keys;
    if (keys.isEmpty()) {
        return false;
    }
    auto keyState = event->type() == QEvent::MouseButtonPress ? KWin::InputRedirection::KeyboardKeyPressed : KWin::InputRedirection::KeyboardKeyReleased;
    auto keyCodes = KWin::input()->keyboard()->xkb()->qtKeyToXkbKeyCodes(keys[0]);
    qDebug() << keyCodes;
    for (auto key : keyCodes) {
        Q_EMIT m_inputDevice.keyChanged(key - 8, keyState, event->timestamp(), &m_inputDevice);
    }
    return !keyCodes.empty();
}
