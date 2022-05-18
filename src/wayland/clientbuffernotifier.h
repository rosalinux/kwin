/*
    SPDX-FileCopyrightText: 2022 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include <QObject>
#include <QSocketNotifier>

namespace KWaylandServer
{

class ClientBuffer;

class ClientBufferNotifier : public QObject
{
    Q_OBJECT

public:
    static ClientBufferNotifier *get(ClientBuffer *buffer);

public Q_SLOTS:
    bool start();

Q_SIGNALS:
    void ready();

private:
    explicit ClientBufferNotifier(ClientBuffer *buffer, const QVector<int> &fds);

    QVector<QSocketNotifier *> m_pending;
    QVector<QSocketNotifier *> m_notifiers;
};

} // namespace KWaylandServer
