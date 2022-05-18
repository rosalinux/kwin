/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include "clientbuffer.h"

namespace KWaylandServer
{

class ClientBufferNotifier;

class ClientBufferPrivate
{
public:
    virtual ~ClientBufferPrivate()
    {
    }

    static ClientBufferPrivate *get(ClientBuffer *buffer)
    {
        return buffer->d_ptr.data();
    }

    ClientBufferNotifier *notifier = nullptr;
    int refCount = 0;
    wl_resource *resource = nullptr;
    bool isDestroyed = false;
};

} // namespace KWaylandServer
