/*
    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "contenttype_v1_interface.h"

#include "display.h"
#include "surface_interface_p.h"

namespace KWaylandServer
{

static constexpr uint32_t s_version = 1;

static KWin::ContentType waylandToKwinContentType(uint32_t type)
{
    using Type = QtWaylandServer::wp_content_type_v1::type;
    switch (type) {
    case Type::type_photo:
        return KWin::ContentType::Photo;
    case Type::type_video:
        return KWin::ContentType::Video;
    case Type::type_game:
        return KWin::ContentType::Game;
    default:
        return KWin::ContentType::None;
    }
}

ContentTypeManagerV1::ContentTypeManagerV1(Display *display)
    : QtWaylandServer::wp_content_type_manager_v1(*display, s_version)
{
}

void ContentTypeManagerV1::wp_content_type_manager_v1_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void ContentTypeManagerV1::wp_content_type_manager_v1_get_surface_content_type(Resource *resource, uint32_t id, struct ::wl_resource *wlSurface)
{
    SurfaceInterface *surface = SurfaceInterface::get(wlSurface);
    if (!surface) {
        return;
    } else if (SurfaceInterfacePrivate::get(surface)->contentTypeInterface) {
        wl_resource_post_error(resource->handle, error_already_constructed, "Surface already has a wp_content_type_v1");
        return;
    }
    SurfaceInterfacePrivate::get(surface)->contentTypeInterface = new ContentTypeV1Interface(surface, resource->client(), id);
}

ContentTypeV1Interface::ContentTypeV1Interface(SurfaceInterface *surface, wl_client *client, uint32_t id)
    : QtWaylandServer::wp_content_type_v1(client, id, s_version)
    , m_surface(surface)
{
}

void ContentTypeV1Interface::wp_content_type_v1_set_content_type(Resource *, uint32_t content_type)
{
    if (!m_surface) {
        return;
    }
    SurfaceInterfacePrivate::get(m_surface)->pending.contentType = waylandToKwinContentType(content_type);
}

void ContentTypeV1Interface::wp_content_type_v1_destroy(Resource *resource)
{
    SurfaceInterfacePrivate::get(m_surface)->pending.contentType = KWin::ContentType::None;
    wl_resource_destroy(resource->handle);
}

void ContentTypeV1Interface::wp_content_type_v1_destroy_resource(Resource *)
{
    delete this;
}

}
