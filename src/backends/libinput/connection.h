/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_LIBINPUT_CONNECTION_H
#define KWIN_LIBINPUT_CONNECTION_H

#include <kwinglobals.h>

#include <KSharedConfig>

#include <QObject>
#include <QPointer>
#include <QRecursiveMutex>
#include <QSize>
#include <QStringList>
#include <QVector>
#include <deque>

class QSocketNotifier;
class QThread;

namespace KWin
{
namespace LibInput
{

class Event;
class Device;
class Context;
class PointerEvent;

class KWIN_EXPORT Connection : public QObject
{
    Q_OBJECT

public:
    ~Connection() override;

    void setInputConfig(const KSharedConfigPtr &config)
    {
        m_config = config;
    }

    void setup();
    void updateScreens();
    void deactivate();
    void processEvents();

    QStringList devicesSysNames() const;

Q_SIGNALS:
    void deviceAdded(KWin::LibInput::Device *);
    void deviceRemoved(KWin::LibInput::Device *);

    void eventsRead();

private Q_SLOTS:
    void doSetup();
    void slotKGlobalSettingsNotifyChange(int type, int arg);

private:
    Connection(Context *input, QObject *parent = nullptr);
    void handleEvent();
    void handleDiscreteAxis(PointerEvent *event);
    void handleContinuousAxis(PointerEvent *event);
    void applyDeviceConfig(Device *device);
    void applyScreenToDevice(Device *device);
    Context *m_input;
    QSocketNotifier *m_notifier;
    QRecursiveMutex m_mutex;
    std::deque<std::unique_ptr<Event>> m_eventQueue;
    QVector<Device *> m_devices;
    KSharedConfigPtr m_config;

    KWIN_SINGLETON(Connection)
};

}
}

#endif
