/*
    SPDX-FileCopyrightText: 2022 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "lockscreenallowed_interface.h"
#include "display.h"
#include "seat_interface.h"
#include "surface_interface.h"

#include "qwayland-server-kde-lockscreenallowed-v1.h"

namespace KWaylandServer
{
static constexpr int s_version = 1;

class LockscreenAllowedV1InterfacePrivate : public QtWaylandServer::kde_lockscreenallowed_v1
{
public:
    LockscreenAllowedV1InterfacePrivate(Display *display, LockscreenAllowedV1Interface *q)
        : QtWaylandServer::kde_lockscreenallowed_v1(*display, s_version)
        , q(q)
    {
    }

protected:
    void kde_lockscreenallowed_v1_allow(Resource *resource, struct ::wl_resource *surface) override
    {
        auto surfaceIface = SurfaceInterface::get(surface);
        if (resource->client() == surfaceIface->client()->client()) {
            Q_EMIT q->allowRequested(surfaceIface);
        }
    }
    void kde_lockscreenallowed_v1_destroy(Resource *resource) override
    {
        wl_resource_destroy(resource->handle);
    }

private:
    LockscreenAllowedV1Interface *const q;
};

LockscreenAllowedV1Interface::~LockscreenAllowedV1Interface() = default;

LockscreenAllowedV1Interface::LockscreenAllowedV1Interface(Display *display, QObject *parent)
    : QObject(parent)
    , d(new LockscreenAllowedV1InterfacePrivate(display, this))
{
}

}
