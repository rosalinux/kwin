/*
    SPDX-FileCopyrightText: 2022 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "clientbuffernotifier.h"
#include "clientbuffer_p.h"
#include "linuxdmabufv1clientbuffer.h"

#include <poll.h>

namespace KWaylandServer
{

ClientBufferNotifier *ClientBufferNotifier::get(ClientBuffer *buffer)
{
    auto bufferPrivate = ClientBufferPrivate::get(buffer);
    if (bufferPrivate->notifier) {
        return bufferPrivate->notifier;
    }

    auto dmabuf = qobject_cast<LinuxDmaBufV1ClientBuffer *>(buffer);
    if (!dmabuf) {
        return nullptr;
    }

    QVector<int> fds;
    for (const LinuxDmaBufV1Plane &plane : dmabuf->planes()) {
        fds << plane.fd;
    }

    return new ClientBufferNotifier(buffer, fds);
}

static bool isReadable(int fileDescriptor)
{
    pollfd pfds[1];
    pfds[0].fd = fileDescriptor;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

    if (poll(pfds, 1, 0) == -1) {
        return true;
    }

    return pfds->revents & POLLIN;
}

ClientBufferNotifier::ClientBufferNotifier(ClientBuffer *buffer, const QVector<int> &fds)
    : QObject(buffer)
{
    for (const int &fd : fds) {
        auto notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        notifier->setEnabled(false);
        connect(notifier, &QSocketNotifier::activated, this, [this, notifier]() {
            notifier->setEnabled(false);
            m_pending.removeOne(notifier);
            if (m_pending.isEmpty()) {
                Q_EMIT ready();
            }
        });
        m_notifiers << notifier;
    }
}

bool ClientBufferNotifier::start()
{
    for (QSocketNotifier *notifier : std::as_const(m_notifiers)) {
        if (!isReadable(notifier->socket())) {
            notifier->setEnabled(true);
            m_pending.append(notifier);
        }
    }
    return !m_pending.isEmpty();
}

} // namespace KWaylandServer
