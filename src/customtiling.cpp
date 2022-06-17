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
    if (m_parentItem) {
        m_parentItem->removeChild(this);
    }
}

void TileData::print()
{
    static int level = 0;
    level++;
    QString spaces;
    for (int i = 0; i < level; ++i) {
        spaces += "  ";
    }
    qWarning() << spaces << m_relativeGeometry << m_layoutDirection;
    for (auto t : m_childItems) {
        t->print();
    }
    level--;
}

void TileData::setRelativeGeometry(const QRectF &geom)
{
    if (m_relativeGeometry == geom) {
        return;
    }

    m_relativeGeometry = geom;
    for (auto t : m_childItems) {
        auto childGeom = t->relativeGeometry();
        childGeom = childGeom.intersected(geom);
        if (m_layoutDirection == LayoutDirection::Horizontal) {
            childGeom.setHeight(geom.height());
        } else if (m_layoutDirection == LayoutDirection::Vertical) {
            childGeom.setWidth(geom.width());
        }
        t->setRelativeGeometry(childGeom);
    }
    Q_EMIT relativeGeometryChanged(geom);
    Q_EMIT absoluteGeometryChanged();
    Q_EMIT m_tiling->tileGeometriesChanged();
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

void TileData::resizeInLayout(qreal delta)
{
    if (!m_parentItem || m_layoutDirection == LayoutDirection::Floating) {
        return;
    }

    int index = row();

    if (index < 1) {
        return;
    }

    const QRect areaGeom = workspace()->clientArea(MaximizeArea, m_tiling->output(), VirtualDesktopManager::self()->currentDesktop());

    auto geom = m_relativeGeometry;
    auto otherGeom = m_parentItem->m_childItems[index - 1]->relativeGeometry();

    if (m_layoutDirection == LayoutDirection::Horizontal) {
        qreal relativeDelta = delta / areaGeom.width();
        geom.setLeft(geom.left() + relativeDelta);
        otherGeom.setRight(otherGeom.right() + relativeDelta);
    } else {
        qreal relativeDelta = delta / areaGeom.height();
        geom.setTop(geom.top() + relativeDelta);
        otherGeom.setBottom(otherGeom.bottom() + relativeDelta);
    }

    setRelativeGeometry(geom);
    m_parentItem->m_childItems[index - 1]->setRelativeGeometry(otherGeom);
}

void TileData::split(KWin::TileData::LayoutDirection layoutDirection)
{
    //TODO: support floating
    if (layoutDirection == LayoutDirection::Floating) {
        return;
    }
    qWarning() << "SPLITTING";
    if (m_layoutDirection == layoutDirection) {
        // Add a new cell to the current layout
        QRectF newGeo;
        if (layoutDirection == LayoutDirection::Horizontal) {
            m_relativeGeometry.setWidth(m_relativeGeometry.width() / 2);
            newGeo = m_relativeGeometry;
            newGeo.moveLeft(newGeo.x() + newGeo.width());
        } else if (layoutDirection == LayoutDirection::Vertical) {
            m_relativeGeometry.setHeight(m_relativeGeometry.height() / 2);
            newGeo = m_relativeGeometry;
            newGeo.moveTop(newGeo.y() + newGeo.height());
        }

        Q_EMIT relativeGeometryChanged(m_relativeGeometry);
        Q_EMIT absoluteGeometryChanged();
        Q_EMIT m_tiling->tileGeometriesChanged();
        m_tiling->addTile(newGeo, m_layoutDirection, m_parentItem);
    } else {
        // Do a new layout with 2 cells inside this one
        auto newGeo = m_relativeGeometry;
        if (layoutDirection == LayoutDirection::Horizontal) {
            newGeo.setWidth(m_relativeGeometry.width() / 2);
            m_tiling->addTile(newGeo, layoutDirection, this);
            newGeo.moveLeft(newGeo.x() + newGeo.width());
            m_tiling->addTile(newGeo, layoutDirection, this);
        } else if (layoutDirection == LayoutDirection::Vertical) {
            newGeo.setHeight(m_relativeGeometry.height() / 2);
            m_tiling->addTile(newGeo, layoutDirection, this);
            newGeo.moveTop(newGeo.y() + newGeo.height());
            m_tiling->addTile(newGeo, layoutDirection, this);
        }
    }
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

void TileData::removeChild(TileData *child)
{
    const bool wasEmpty = !m_childItems.isEmpty();
    m_childItems.removeAll(child);
    if (m_childItems.isEmpty() && !wasEmpty) {
        Q_EMIT isLayoutChanged(false);
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
    if (m_parentItem) {
        return m_parentItem->m_childItems.indexOf(const_cast<TileData *>(this));
    }

    return 0;
}

CustomTiling::CustomTiling(Output *parent)
    : QAbstractItemModel(parent)
    , m_output(parent)
{
    m_rootTile = new TileData(this, nullptr);
    m_rootTile->setRelativeGeometry(QRectF(0, 0, 1, 1));
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
    const auto tiles = m_rootTile->descendants();

    //TODO: can be costly: optimize?
    for (const auto *t : tiles) {
        if (!t->isLayout()) {
            geometries << t->absoluteGeometry();
        }
    }

    return geometries;
}

QVariant CustomTiling::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (role == TileDataRole) {
        return QVariant::fromValue(static_cast<TileData *>(index.internalPointer()));
    }

    return QVariant();
}

Qt::ItemFlags CustomTiling::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return QAbstractItemModel::flags(index);
}

QModelIndex CustomTiling::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    TileData *parentItem;

    if (!parent.isValid()) {
        parentItem = m_rootTile;
    } else {
        parentItem = static_cast<TileData *>(parent.internalPointer());
    }

    TileData *childItem = parentItem->child(row);
    if (childItem) {
        return createIndex(row, column, childItem);
    }
    return QModelIndex();
}

QModelIndex CustomTiling::parent(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QModelIndex();
    }

    TileData *childItem = static_cast<TileData *>(index.internalPointer());
    TileData *parentItem = childItem->parentItem();

    if (parentItem == m_rootTile) {
        return QModelIndex();
    }

    return createIndex(parentItem->row(), 0, parentItem);
}

int CustomTiling::rowCount(const QModelIndex &parent) const
{
    TileData *parentItem;
    if (parent.column() > 0) {
        return 0;
    }

    if (!parent.isValid()) {
        parentItem = m_rootTile;
    } else {
        parentItem = static_cast<TileData *>(parent.internalPointer());
    }

    return parentItem->childCount();
}

int CustomTiling::columnCount(const QModelIndex &parent) const
{
    return 1;
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
            // if (arr.count() > 1 && layoutDirection != TileData::LayoutDirection::Floating) {
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

                    auto avail = availableArea;
                    if (dir == TileData::LayoutDirection::Horizontal) {
                        const auto height = obj.value(QStringLiteral("height"));
                        if (height.isDouble() && height.toDouble() > 0) {
                            avail.setHeight(height.toDouble());
                        }
                        TileData *tile = addTile(avail, dir, parentTile);
                        parseTilingJSon(arr, dir, avail, tile);
                        ret.setTop(avail.bottom());
                        return ret;
                    } else if (dir == TileData::LayoutDirection::Vertical) {
                        const auto width = obj.value(QStringLiteral("width"));
                        if (width.isDouble() && width.toDouble() > 0) {
                            avail.setWidth(width.toDouble());
                        }
                        TileData *tile = addTile(avail, dir, parentTile);
                        parseTilingJSon(arr, dir, avail, tile);
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
                avail = parseTilingJSon(*it, layoutDirection, avail, parentTile);
            }
        }
        return avail;
    }
    return ret;
}

TileData *CustomTiling::addTile(const QRectF &relativeGeometry, TileData::LayoutDirection layoutDirection, TileData *parentTile)
{
    auto index = createIndex(parentTile->row(), 0, parentTile);
    beginInsertRows(index, parentTile->childCount(), parentTile->childCount());
    TileData *tile = new TileData(this, parentTile);
    tile->setRelativeGeometry(relativeGeometry);
    tile->setLayoutDirection(layoutDirection);
    parentTile->appendChild(tile);
    endInsertRows();
    return tile;
}

void CustomTiling::removeTile(TileData *tile)
{
    // Can't delete the root tile
    const auto parentTile = tile->parentItem();
    if (!parentTile) {
        qCWarning(KWIN_CORE) << "Can't remove the root tile";
        return;
    }

    auto index = createIndex(parentTile->row(), 0, parentTile);
    beginRemoveRows(index, tile->row(), tile->row());
    parentTile->removeChild(tile);
    endRemoveRows();
    tile->deleteLater(); // This will delete all the tile children as well
    if (parentTile->childCount() == 1) {
        auto *lastTile = parentTile->child(0);
        if (lastTile->childCount() == 0) {
            removeTile(lastTile);
        }
    }
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

    parseTilingJSon(doc.object(), TileData::LayoutDirection::Floating, QRectF(0, 0, 1, 1), m_rootTile);
    m_rootTile->print();
    Q_EMIT tileGeometriesChanged();
}

} // namespace KWin

#include "moc_customtiling.cpp"
