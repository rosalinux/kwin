/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "customtiling.h"
#include "output.h"
#include "virtualdesktops.h"
#include "workspace.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace KWin
{

TileData::TileData(CustomTiling *tiling, TileData *parent)
    : QObject(parent)
    , m_parentItem(parent)
    , m_tiling(tiling)
{
    if (!parent && tiling) {
        setParent(tiling);
    }
}

TileData::~TileData()
{
    qDeleteAll(m_childItems);
}
void TileData::print()
{
    qWarning() << m_relativeGeometry << m_layoutDirection;
    for (auto t : m_childItems) {
        t->print();
    }
}
void TileData::setRelativeGeometry(const QRectF &geom)
{
    if (m_relativeGeometry == geom) {
        return;
    }

    m_relativeGeometry = geom;
    Q_EMIT relativeGeometryChanged(geom);
    Q_EMIT absoluteGeometryChanged();
}

QRectF TileData::relativeGeometry() const
{
    return m_relativeGeometry;
}

QRectF TileData::absoluteGeometry() const
{
    const QRect geom = workspace()->clientArea(MaximizeArea, m_tiling->output(), VirtualDesktopManager::self()->currentDesktop());
    return QRectF(geom.x() + m_relativeGeometry.x() * geom.width(),
                  geom.y() + m_relativeGeometry.y() * geom.height(),
                  m_relativeGeometry.width() * geom.width(),
                  m_relativeGeometry.height() * geom.height());
}

void TileData::setLayoutDirection(TileData::LayoutDirection dir)
{
    m_layoutDirection = dir;
}

TileData::LayoutDirection TileData::layoutDirection() const
{
    return m_layoutDirection;
}

bool TileData::isLayout() const
{
    return !m_childItems.isEmpty();
}

void TileData::split(KWin::TileData::LayoutDirection layoutDirection)
{
    // TODO: manage layoutDirection change
    m_relativeGeometry.setWidth(m_relativeGeometry.width() / 2);
    auto newGeo = m_relativeGeometry;
    newGeo.moveLeft(newGeo.x() + newGeo.width());
    Q_EMIT relativeGeometryChanged(m_relativeGeometry);
    Q_EMIT absoluteGeometryChanged();
    m_tiling->addTile(newGeo, m_layoutDirection, m_parentItem);
}

void TileData::remove()
{
    const int idx = row();
    if (idx > 0) {
        auto *sibling = m_parentItem->m_childItems[idx - 1];
        sibling->setRelativeGeometry(m_relativeGeometry.united(sibling->relativeGeometry()));
    } else if (m_parentItem->m_childItems.count() > 1) {
        auto *sibling = m_parentItem->m_childItems[idx + 1];
        sibling->setRelativeGeometry(m_relativeGeometry.united(sibling->relativeGeometry()));
    } //TODO special case for when we are the last child
    m_tiling->removeTile(this);
}

void TileData::appendChild(TileData *item)
{
    const bool wasEmpty = m_childItems.isEmpty();
    m_childItems.append(item);
    if (wasEmpty) {
        Q_EMIT isLayoutChanged(true);
    }
}

TileData *TileData::child(int row)
{
    if (row < 0 || row >= m_childItems.size())
        return nullptr;
    return m_childItems.at(row);
}

int TileData::childCount() const
{
    return m_childItems.count();
}

QVector<TileData *> TileData::descendants() const
{
    QVector<TileData *> tiles;
    for (auto *t : m_childItems) {
        tiles << t << t->descendants();
    }
    return tiles;
}

TileData *TileData::parentItem()
{
    return m_parentItem;
}

int TileData::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<TileData *>(this));

    return 0;
}

CustomTiling::CustomTiling(Output *parent)
    : QAbstractListModel(parent)
    , m_output(parent)
{
    TileData *tile = new TileData(this, nullptr);
    tile->setRelativeGeometry(QRectF(0, 0, 1, 1));
    m_tileData << tile;
    connect(Workspace::self(), &Workspace::configChanged, this, &CustomTiling::readSettings);
    connect(m_output, &Output::informationChanged, this, &CustomTiling::readSettings);
}

CustomTiling::~CustomTiling()
{
}

Output *CustomTiling::output() const
{
    return m_output;
}

QHash<int, QByteArray> CustomTiling::roleNames() const
{
    return {
        {TileDataRole, QByteArrayLiteral("tileData")}};
}

QList<QRectF> CustomTiling::tileGeometries() const
{
    QList<QRectF> geometries;
    const QRect geom = workspace()->clientArea(MaximizeArea, m_output, VirtualDesktopManager::self()->currentDesktop());
    for (const auto &r : m_tiles) {
        geometries << QRectF(geom.x() + r.x() * geom.width(),
                             geom.y() + r.y() * geom.height(),
                             r.width() * geom.width(),
                             r.height() * geom.height());
    }

    return geometries;
}

QVariant CustomTiling::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    const int row = index.row();
    if (row < 0 || row >= m_tileData.count()) {
    }
    // TODO: roles
    return QVariant::fromValue(m_tileData[row]);

    return QVariant();
}

Qt::ItemFlags CustomTiling::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return QAbstractListModel::flags(index);
}

int CustomTiling::rowCount(const QModelIndex &parent) const
{
    return m_tileData.count();
}

TileData::LayoutDirection strToLayoutDirection(const QString &dir)
{
    if (dir == QStringLiteral("horizontal")) {
        return TileData::LayoutDirection::Horizontal;
    } else if (dir == QStringLiteral("vertical")) {
        return TileData::LayoutDirection::Vertical;
    } else {
        return TileData::LayoutDirection::Floating;
    }
}

QRectF CustomTiling::parseTilingJSon(const QJsonValue &val, TileData::LayoutDirection layoutDirection, const QRectF &availableArea, TileData *parentTile)
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
                    parseTilingJSon(arr, TileData::LayoutDirection::Floating, QRectF(0, 0, 1, 1), parentTile);
                }
            }
            if (obj.contains(QStringLiteral("tiles"))) {
                // It's a layout
                const auto arr = obj.value(QStringLiteral("tiles"));
                const auto direction = obj.value(QStringLiteral("layoutDirection"));
                if (arr.isArray() && direction.isString()) {
                    const TileData::LayoutDirection dir = strToLayoutDirection(direction.toString());
                    parentTile->setLayoutDirection(dir);
                    auto avail = availableArea;
                    if (dir == TileData::LayoutDirection::Horizontal) {
                        const auto height = obj.value(QStringLiteral("height"));
                        if (height.isDouble()) {
                            avail.setHeight(height.toDouble());
                        }
                        parseTilingJSon(arr, dir, avail, parentTile);
                        ret.setTop(avail.bottom());
                        return ret;
                    } else if (dir == TileData::LayoutDirection::Vertical) {
                        const auto width = obj.value(QStringLiteral("width"));
                        if (width.isDouble()) {
                            avail.setWidth(width.toDouble());
                        }
                        parseTilingJSon(arr, dir, avail, parentTile);
                        ret.setLeft(avail.right());
                        return ret;
                    }
                }
            }
        } else if (layoutDirection == TileData::LayoutDirection::Horizontal) {
            QRectF rect(availableArea.x(), availableArea.y(), 0, availableArea.height());
            const auto width = obj.value(QStringLiteral("width"));
            if (width.isDouble()) {
                rect.setWidth(qMin(width.toDouble(), availableArea.width()));
            }
            if (!rect.isEmpty()) {
                m_tiles << rect;
                addTile(rect, layoutDirection, parentTile);
            }
            ret.setLeft(ret.left() + rect.width());
            return ret;
        } else if (layoutDirection == TileData::LayoutDirection::Vertical) {
            QRectF rect(availableArea.x(), availableArea.y(), availableArea.width(), 0);
            const auto height = obj.value(QStringLiteral("height"));
            if (height.isDouble()) {
                rect.setHeight(qMin(height.toDouble(), availableArea.height()));
            }
            if (!rect.isEmpty()) {
                m_tiles << rect;
                addTile(rect, layoutDirection, parentTile);
            }
            ret.setTop(ret.top() + rect.height());
            return ret;
        } else if (layoutDirection == TileData::LayoutDirection::Floating) {
            QRectF rect(obj.value(QStringLiteral("x")).toDouble(),
                        obj.value(QStringLiteral("y")).toDouble(),
                        obj.value(QStringLiteral("width")).toDouble(),
                        obj.value(QStringLiteral("height")).toDouble());
            if (!rect.isEmpty()) {
                m_tiles << rect;
                addTile(rect, layoutDirection, parentTile);
            }
            return availableArea;
        }
    } else if (val.isArray()) {
        const auto arr = val.toArray();
        auto avail = availableArea;
        for (auto it = arr.cbegin(); it != arr.cend(); it++) {
            if ((*it).isObject()) {
                TileData *tile = addTile(avail, layoutDirection, parentTile);
                avail = parseTilingJSon(*it, layoutDirection, avail, tile);
            }
        }
        return avail;
    }
    return ret;
}

TileData *CustomTiling::addTile(const QRectF &relativeGeometry, TileData::LayoutDirection layoutDirection, TileData *parentTile)
{
    beginInsertRows(QModelIndex(), m_tileData.count(), m_tileData.count());
    TileData *tile = new TileData(this, parentTile);
    tile->setRelativeGeometry(relativeGeometry);
    tile->setLayoutDirection(layoutDirection);
    parentTile->appendChild(tile);
    m_tileData << tile;
    endInsertRows();
    return tile;
}

void CustomTiling::removeTile(TileData *tile)
{
    Q_ASSERT(m_tileData.contains(tile));

    const auto allTiles = tile->descendants();
    const int from = m_tileData.indexOf(tile);
    beginRemoveRows(QModelIndex(), from, from + allTiles.count());
    m_tileData.remove(from, allTiles.count() + 1);
    endRemoveRows();
    tile->deleteLater(); // This will delete all the children as well
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

    parseTilingJSon(doc.object(), TileData::LayoutDirection::Floating, QRectF(0, 0, 1, 1), m_tileData.first());
    m_tileData.first()->print();
    Q_EMIT tileGeometriesChanged();
}

} // namespace KWin

#include "moc_customtiling.cpp"
