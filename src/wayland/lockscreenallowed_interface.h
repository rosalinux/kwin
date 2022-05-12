/*
    SPDX-FileCopyrightText: 2022 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include "kwin_export.h"

#include <QObject>
#include <QVector>
#include <functional>
#include <optional>

struct wl_resource;

namespace KWaylandServer
{
class Display;
class SurfaceInterface;

class LockscreenAllowedV1InterfacePrivate;

class KWIN_EXPORT LockscreenAllowedV1Interface : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(LockscreenAllowedV1Interface)
public:
    explicit LockscreenAllowedV1Interface(Display *display, QObject *parent = nullptr);
    ~LockscreenAllowedV1Interface() override;

Q_SIGNALS:
    /// Notifies about the @p surface being activated
    void allowRequested(SurfaceInterface *surface);

private:
    friend class LockscreenAllowedV1InterfacePrivate;
    LockscreenAllowedV1Interface(LockscreenAllowedV1Interface *parent);
    QScopedPointer<LockscreenAllowedV1InterfacePrivate> d;
};

}
