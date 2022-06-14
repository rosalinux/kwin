/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "customtiling.h"
#include "output.h"
#include "workspace.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace KWin
{

CustomTiling::CustomTiling(Output *parent)
    : QObject(parent)
    , m_output(parent)
{
    connect(Workspace::self(), &Workspace::configChanged, this, &CustomTiling::readSettings);
    connect(m_output, &Output::informationChanged, this, &CustomTiling::readSettings);
}

CustomTiling::~CustomTiling()
{
}

QList<QRectF> CustomTiling::tileGeometries() const
{
    QList<QRectF> geometries;
    for (const auto &r : m_tiles) {
        const auto &geom = m_output->geometry();
        geometries << QRectF(geom.x() + r.x() * geom.width(),
                             geom.y() + r.y() * geom.height(),
                             r.width() * geom.width(),
                             r.height() * geom.height());
    }

    return geometries;
}

CustomTiling::LayoutDirection strToLayoutDirection(const QString &dir)
{
    if (dir == QStringLiteral("horizontal")) {
        return CustomTiling::LayoutDirection::Horizontal;
    } else if (dir == QStringLiteral("vertical")) {
        return CustomTiling::LayoutDirection::Vertical;
    } else {
        return CustomTiling::LayoutDirection::Floating;
    }
}

QRectF CustomTiling::parseTilingJSon(const QJsonValue &val, CustomTiling::LayoutDirection layoutDirection, const QRectF &availableArea)
{
    if (availableArea.isEmpty()) {
        return availableArea;
    }

    auto ret = availableArea;

    if (val.isObject()) {
        const auto &obj = val.toObject();
        if (obj.contains(QStringLiteral("tiles")) || obj.contains(QStringLiteral("floatingTiles"))) {
            if (obj.contains(QStringLiteral("floatingTiles"))) {
                // Are there floating tiles?
                const auto arr = obj.value(QStringLiteral("floatingTiles"));
                if (arr.isArray()) {
                    parseTilingJSon(arr, LayoutDirection::Floating, QRectF(0, 0, 1, 1));
                }
            }
            if (obj.contains(QStringLiteral("tiles"))) {
                // It's a layout
                const auto arr = obj.value(QStringLiteral("tiles"));
                const auto direction = obj.value(QStringLiteral("layoutDirection"));
                if (arr.isArray() && direction.isString()) {
                    const LayoutDirection dir = strToLayoutDirection(direction.toString());
                    auto avail = availableArea;
                    if (dir == LayoutDirection::Horizontal) {
                        const auto height = obj.value(QStringLiteral("height"));
                        if (height.isDouble()) {
                            avail.setHeight(height.toDouble());
                        }
                        parseTilingJSon(arr, dir, avail);
                        ret.setTop(avail.bottom());
                        return ret;
                    } else if (dir == LayoutDirection::Vertical) {
                        const auto width = obj.value(QStringLiteral("width"));
                        if (width.isDouble()) {
                            avail.setWidth(width.toDouble());
                        }
                        parseTilingJSon(arr, dir, avail);
                        ret.setLeft(avail.right());
                        return ret;
                    }
                }
            }
        } else if (layoutDirection == LayoutDirection::Horizontal) {
            QRectF rect(availableArea.x(), availableArea.y(), 0, availableArea.height());
            const auto width = obj.value(QStringLiteral("width"));
            if (width.isDouble()) {
                rect.setWidth(qMin(width.toDouble(), availableArea.width()));
            }
            if (!rect.isEmpty()) {
                m_tiles << rect;
            }
            ret.setLeft(ret.left() + rect.width());
            return ret;
        } else if (layoutDirection == LayoutDirection::Vertical) {
            QRectF rect(availableArea.x(), availableArea.y(), availableArea.width(), 0);
            const auto height = obj.value(QStringLiteral("height"));
            if (height.isDouble()) {
                rect.setHeight(qMin(height.toDouble(), availableArea.height()));
            }
            if (!rect.isEmpty()) {
                m_tiles << rect;
            }
            ret.setTop(ret.top() + rect.height());
            return ret;
        } else if (layoutDirection == LayoutDirection::Floating) {
            QRectF rect(obj.value(QStringLiteral("x")).toDouble(),
                        obj.value(QStringLiteral("y")).toDouble(),
                        obj.value(QStringLiteral("width")).toDouble(),
                        obj.value(QStringLiteral("height")).toDouble());
            if (!rect.isEmpty()) {
                m_tiles << rect;
            }
            return availableArea;
        }
    } else if (val.isArray()) {
        const auto arr = val.toArray();
        auto avail = availableArea;
        for (auto it = arr.cbegin(); it != arr.cend(); it++) {
            if ((*it).isObject()) {
                avail = parseTilingJSon(*it, layoutDirection, avail);
            }
        }
        return avail;
    }
    return ret;
}

void CustomTiling::readSettings()
{
    m_tiles.clear();

    KConfigGroup cg = kwinApp()->config()->group(QStringLiteral("Tiling"));
    cg = KConfigGroup(&cg, m_output->uuid().toString(QUuid::WithoutBraces));

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(cg.readEntry("tiles", QByteArray()), &error);

    if (error.error != QJsonParseError::NoError) {
        qCWarning(KWIN_CORE) << "Parse error in tiles configuration for monitor" << m_output->uuid().toString(QUuid::WithoutBraces) << ":" << error.errorString();
        return;
    }

    parseTilingJSon(doc.object(), LayoutDirection::Floating, QRectF(0, 0, 1, 1));
    Q_EMIT tileGeometriesChanged();
}

} // namespace KWin

#include "moc_customtiling.cpp"
