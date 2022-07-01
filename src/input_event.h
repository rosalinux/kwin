/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_INPUT_EVENT_H
#define KWIN_INPUT_EVENT_H

#include "input.h"

#include <QInputEvent>

namespace KWin
{

class InputDevice;

class MouseEvent : public QMouseEvent
{
public:
    explicit MouseEvent(QEvent::Type type, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons,
                        Qt::KeyboardModifiers modifiers, quint32 timestamp,
                        const QSizeF &delta, const QSizeF &deltaNonAccelerated, quint64 timestampMicroseconds,
                        InputDevice *device);

    QSizeF delta() const
    {
        return m_delta;
    }

    QSizeF deltaUnaccelerated() const
    {
        return m_deltaUnccelerated;
    }

    quint64 timestampMicroseconds() const
    {
        return m_timestampMicroseconds;
    }

    InputDevice *device() const
    {
        return m_device;
    }

    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const
    {
        return m_modifiersRelevantForShortcuts;
    }

    void setModifiersRelevantForGlobalShortcuts(const Qt::KeyboardModifiers &mods)
    {
        m_modifiersRelevantForShortcuts = mods;
    }

    quint32 nativeButton() const
    {
        return m_nativeButton;
    }

    void setNativeButton(quint32 button)
    {
        m_nativeButton = button;
    }

private:
    QSizeF m_delta;
    QSizeF m_deltaUnccelerated;
    quint64 m_timestampMicroseconds;
    InputDevice *m_device;
    Qt::KeyboardModifiers m_modifiersRelevantForShortcuts = Qt::KeyboardModifiers();
    quint32 m_nativeButton = 0;
};

// TODO: Don't derive from QWheelEvent, this event is quite domain specific.
class WheelEvent : public QWheelEvent
{
public:
    explicit WheelEvent(const QPointF &pos, qreal delta, qint32 deltaV120, Qt::Orientation orientation,
                        Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers, InputRedirection::PointerAxisSource source,
                        quint32 timestamp, InputDevice *device);

    Qt::Orientation orientation() const
    {
        return m_orientation;
    }

    qreal delta() const
    {
        return m_delta;
    }

    qint32 deltaV120() const
    {
        return m_deltaV120;
    }

    InputRedirection::PointerAxisSource axisSource() const
    {
        return m_source;
    }

    InputDevice *device() const
    {
        return m_device;
    }

    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const
    {
        return m_modifiersRelevantForShortcuts;
    }

    void setModifiersRelevantForGlobalShortcuts(const Qt::KeyboardModifiers &mods)
    {
        m_modifiersRelevantForShortcuts = mods;
    }

private:
    InputDevice *m_device;
    Qt::Orientation m_orientation;
    qreal m_delta;
    qint32 m_deltaV120;
    InputRedirection::PointerAxisSource m_source;
    Qt::KeyboardModifiers m_modifiersRelevantForShortcuts = Qt::KeyboardModifiers();
};

class KeyEvent : public QKeyEvent
{
public:
    explicit KeyEvent(QEvent::Type type, Qt::Key key, Qt::KeyboardModifiers modifiers, quint32 code, quint32 keysym,
                      const QString &text, bool autorepeat, quint32 timestamp, InputDevice *device);

    InputDevice *device() const
    {
        return m_device;
    }

    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const
    {
        return m_modifiersRelevantForShortcuts;
    }

    void setModifiersRelevantForGlobalShortcuts(const Qt::KeyboardModifiers &mods)
    {
        m_modifiersRelevantForShortcuts = mods;
    }

private:
    InputDevice *m_device;
    Qt::KeyboardModifiers m_modifiersRelevantForShortcuts = Qt::KeyboardModifiers();
};

class SwitchEvent : public QInputEvent
{
public:
    enum class State {
        Off,
        On
    };
    explicit SwitchEvent(State state, quint32 timestamp, quint64 timestampMicroseconds, InputDevice *device);

    State state() const
    {
        return m_state;
    }

    quint64 timestampMicroseconds() const
    {
        return m_timestampMicroseconds;
    }

    InputDevice *device() const
    {
        return m_device;
    }

private:
    State m_state;
    quint64 m_timestampMicroseconds;
    InputDevice *m_device;
};

class TabletToolId
{
public:
    const InputRedirection::TabletToolType m_toolType;
    const QVector<InputRedirection::Capability> m_capabilities;
    const quint64 m_serialId;
    const quint64 m_uniqueId;
    void *const m_deviceGroupData;
};

class TabletPadId
{
public:
    void *data;
};

class TabletEvent : public QTabletEvent
{
public:
    TabletEvent(Type t, const QPointF &pos, const QPointF &globalPos,
                int device, int pointerType, qreal pressure, int xTilt, int yTilt,
                qreal tangentialPressure, qreal rotation, int z,
                Qt::KeyboardModifiers keyState, qint64 uniqueID,
                Qt::MouseButton button, Qt::MouseButtons buttons, const TabletToolId &tabletId);

    const TabletToolId &tabletId() const
    {
        return m_id;
    }

private:
    const TabletToolId m_id;
};

}

#endif
