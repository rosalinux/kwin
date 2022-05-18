/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2020 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include "surface_interface.h"
#include "utils.h"
// Qt
#include <QHash>
#include <QQueue>
#include <QVector>
// Wayland
#include "qwayland-server-wayland.h"

namespace KWaylandServer
{
class IdleInhibitorV1Interface;
class SurfaceRole;
class ViewportInterface;

class SurfaceExtensionInterface
{
public:
    virtual ~SurfaceExtensionInterface() = default;

    virtual void surfaceStateStashed(quint32 serial) = 0;
    virtual void surfaceStateApplied(quint32 serial) = 0;
};

template<typename State>
class SurfaceExtension : public SurfaceExtensionInterface
{
public:
    explicit SurfaceExtension(SurfaceInterface *surface);
    ~SurfaceExtension() override;

    void surfaceStateStashed(quint32 serial) override final;
    void surfaceStateApplied(quint32 serial) override final;

    virtual void applyState(State *next) = 0;

    QPointer<SurfaceInterface> m_surface;
    State m_current;
    State m_pending;
    QQueue<State> m_stashed;
};

struct SurfaceState
{
    SurfaceState();

    void mergeInto(SurfaceState *target);

    quint32 serial = 0;
    quint32 locks = 0;

    QRegion damage = QRegion();
    QRegion bufferDamage = QRegion();
    QRegion opaque = QRegion();
    QRegion input = infiniteRegion();
    bool inputIsSet = false;
    bool opaqueIsSet = false;
    bool bufferIsSet = false;
    bool shadowIsSet = false;
    bool blurIsSet = false;
    bool contrastIsSet = false;
    bool slideIsSet = false;
    bool subsurfaceOrderChanged = false;
    bool subsurfacePositionChanged = false;
    bool bufferScaleIsSet = false;
    bool bufferTransformIsSet = false;
    qint32 bufferScale = 1;
    KWin::Output::Transform bufferTransform = KWin::Output::Transform::Normal;
    wl_list frameCallbacks;
    QPoint offset = QPoint();
    QPointer<ClientBuffer> buffer;
    QPointer<ShadowInterface> shadow;
    QPointer<BlurInterface> blur;
    QPointer<ContrastInterface> contrast;
    QPointer<SlideInterface> slide;

    struct
    {
        // Subsurfaces are stored in two lists. The below list contains subsurfaces that
        // are below their parent surface; the above list contains subsurfaces that are
        // placed above the parent surface.
        QList<SubSurfaceInterface *> below;
        QList<SubSurfaceInterface *> above;

        // Subsurface position is here becase it is a part of the parent surface's state.
        QHash<SubSurfaceInterface *, QPoint> position;
    } subsurface;

    struct
    {
        QRectF sourceGeometry = QRectF();
        QSize destinationSize = QSize();
        bool sourceGeometryIsSet = false;
        bool destinationSizeIsSet = false;
    } viewport;
};

class SurfaceInterfacePrivate : public QtWaylandServer::wl_surface
{
public:
    static SurfaceInterfacePrivate *get(SurfaceInterface *surface)
    {
        return surface->d.data();
    }

    explicit SurfaceInterfacePrivate(SurfaceInterface *q);
    ~SurfaceInterfacePrivate() override;

    void addChild(SubSurfaceInterface *subsurface);
    void removeChild(SubSurfaceInterface *subsurface);
    bool raiseChild(SubSurfaceInterface *subsurface, SurfaceInterface *anchor);
    bool lowerChild(SubSurfaceInterface *subsurface, SurfaceInterface *anchor);
    void setShadow(const QPointer<ShadowInterface> &shadow);
    void setBlur(const QPointer<BlurInterface> &blur);
    void setContrast(const QPointer<ContrastInterface> &contrast);
    void setSlide(const QPointer<SlideInterface> &slide);
    void installPointerConstraint(LockedPointerV1Interface *lock);
    void installPointerConstraint(ConfinedPointerV1Interface *confinement);
    void installIdleInhibitor(IdleInhibitorV1Interface *inhibitor);

    QMatrix4x4 buildSurfaceToBufferMatrix();
    void applyState(SurfaceState *next);

    quint32 lockState(SurfaceState *state);
    void unlockState(quint32 serial);

    void addExtension(SurfaceExtensionInterface *extension);
    void removeExtension(SurfaceExtensionInterface *extension);

    bool computeEffectiveMapped() const;
    void updateEffectiveMapped();

    CompositorInterface *compositor;
    SurfaceInterface *q;
    SurfaceRole *role = nullptr;
    SurfaceState *current;
    SurfaceState *pending;
    QList<SurfaceState *> stashed;
    SubSurfaceInterface *subSurface = nullptr;
    QMatrix4x4 surfaceToBufferMatrix;
    QMatrix4x4 bufferToSurfaceMatrix;
    QSize bufferSize;
    QSize implicitSurfaceSize;
    QSize surfaceSize;
    QRegion inputRegion;
    QRegion opaqueRegion;
    ClientBuffer *bufferRef = nullptr;
    bool mapped = false;

    QVector<OutputInterface *> outputs;
    QVector<SurfaceExtensionInterface *> extensions;

    LockedPointerV1Interface *lockedPointer = nullptr;
    ConfinedPointerV1Interface *confinedPointer = nullptr;
    QHash<OutputInterface *, QMetaObject::Connection> outputDestroyedConnections;
    QHash<OutputInterface *, QMetaObject::Connection> outputBoundConnections;

    QVector<IdleInhibitorV1Interface *> idleInhibitors;
    ViewportInterface *viewportExtension = nullptr;
    QScopedPointer<LinuxDmaBufV1Feedback> dmabufFeedbackV1;
    ClientConnection *client = nullptr;

protected:
    void surface_destroy_resource(Resource *resource) override;
    void surface_destroy(Resource *resource) override;
    void surface_attach(Resource *resource, struct ::wl_resource *buffer, int32_t x, int32_t y) override;
    void surface_damage(Resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) override;
    void surface_frame(Resource *resource, uint32_t callback) override;
    void surface_set_opaque_region(Resource *resource, struct ::wl_resource *region) override;
    void surface_set_input_region(Resource *resource, struct ::wl_resource *region) override;
    void surface_commit(Resource *resource) override;
    void surface_set_buffer_transform(Resource *resource, int32_t transform) override;
    void surface_set_buffer_scale(Resource *resource, int32_t scale) override;
    void surface_damage_buffer(Resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) override;
    void surface_offset(Resource *resource, int32_t x, int32_t y) override;

private:
    QMetaObject::Connection constrainsOneShotConnection;
    QMetaObject::Connection constrainsUnboundConnection;
};

template<typename State>
SurfaceExtension<State>::SurfaceExtension(SurfaceInterface *surface)
    : m_surface(surface)
{
    auto surfacePrivate = SurfaceInterfacePrivate::get(surface);
    surfacePrivate->addExtension(this);

    m_current.serial = surfacePrivate->current->serial;
    m_pending.serial = surfacePrivate->pending->serial;
}

template<typename State>
SurfaceExtension<State>::~SurfaceExtension()
{
    if (m_surface) {
        SurfaceInterfacePrivate::get(m_surface)->removeExtension(this);
    }
}

template<typename State>
void SurfaceExtension<State>::surfaceStateStashed(quint32 serial)
{
    State stash;
    m_pending.mergeInto(&stash);
    m_stashed.append(stash);
    m_pending.serial = serial + 1;
}

template<typename State>
void SurfaceExtension<State>::surfaceStateApplied(quint32 serial)
{
    if (m_pending.serial == serial) {
        applyState(&m_pending);
        m_pending.serial = serial + 1;
    } else if (!m_stashed.isEmpty() && m_stashed[0].serial == serial) {
        State stash = m_stashed.takeFirst();
        applyState(&stash);
    }
}

} // namespace KWaylandServer
