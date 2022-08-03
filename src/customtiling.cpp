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
#include <QTimer>

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

void TileData::setGeometryFromWindow(const QRectF &geom)
{
    setGeometryFromAbsolute(geom + QMarginsF(m_leftPadding, m_topPadding, m_rightPadding, m_bottomPadding));
}

void TileData::setGeometryFromAbsolute(const QRectF &geom)
{
    const auto outGeom = m_tiling->output()->geometry();
    const QRectF relGeom((geom.x() - outGeom.x()) / outGeom.width(),
                         (geom.y() - outGeom.y()) / outGeom.height(),
                         geom.width() / outGeom.width(),
                         geom.height() / outGeom.height());

    if (layoutDirection() == LayoutDirection::Floating) {
        setRelativeGeometry(relGeom);
    } else if (layoutDirection() == LayoutDirection::Horizontal) {
        setRelativeGeometry(QRectF(relGeom.x(), m_relativeGeometry.y(), relGeom.width(), m_relativeGeometry.height()));
    } else if (layoutDirection() == LayoutDirection::Vertical) {
        setRelativeGeometry(QRectF(m_relativeGeometry.x(), relGeom.y(), m_relativeGeometry.width(), relGeom.height()));
    }
}

void TileData::setRelativeGeometry(const QRectF &geom)
{
    if (m_relativeGeometry == geom) {
        return;
    }

    QRectF finalGeom;

    if (m_parentItem) {
        finalGeom = geom.intersected(m_parentItem->relativeGeometry());
        static bool siblingRecursion = false;

        if (!siblingRecursion && m_parentItem->layoutDirection() == LayoutDirection::Horizontal) {
            if (finalGeom.left() != m_relativeGeometry.left() && row() > 0) {
                siblingRecursion = true;
                auto *leftSibling = m_parentItem->childTiles()[row() - 1];
                auto siblingGeom = leftSibling->relativeGeometry();
                siblingGeom.setRight(finalGeom.left());
                leftSibling->setRelativeGeometry(siblingGeom);
                siblingRecursion = false;
            }
            if (finalGeom.right() != m_relativeGeometry.right() && row() >= 0 && row() < m_parentItem->childCount() - 1) {
                siblingRecursion = true;
                auto *rightSibling = m_parentItem->childTiles()[row() + 1];
                auto siblingGeom = rightSibling->relativeGeometry();
                siblingGeom.setLeft(finalGeom.right());
                rightSibling->setRelativeGeometry(siblingGeom);
                siblingRecursion = false;
            }
        } else if (!siblingRecursion && m_parentItem->layoutDirection() == LayoutDirection::Vertical) {
            if (finalGeom.top() != m_relativeGeometry.top() && row() > 0) {
                siblingRecursion = true;
                auto *topSibling = m_parentItem->childTiles()[row() - 1];
                auto siblingGeom = topSibling->relativeGeometry();
                siblingGeom.setBottom(finalGeom.top());
                topSibling->setRelativeGeometry(siblingGeom);
                siblingRecursion = false;
            }
            if (finalGeom.bottom() != m_relativeGeometry.bottom() && row() >= 0 && row() < m_parentItem->childCount() - 1) {
                siblingRecursion = true;
                auto *bottomSibling = m_parentItem->childTiles()[row() + 1];
                auto siblingGeom = bottomSibling->relativeGeometry();
                siblingGeom.setTop(finalGeom.bottom());
                bottomSibling->setRelativeGeometry(siblingGeom);
                siblingRecursion = false;
            }
        }
    } else {
        finalGeom = geom;
    }

    m_relativeGeometry = finalGeom;
    for (auto t : m_childItems) {
        auto childGeom = t->relativeGeometry();
        childGeom = childGeom.intersected(geom);
        if (layoutDirection() == LayoutDirection::Horizontal) {
            childGeom.setHeight(geom.height());
        } else if (layoutDirection() == LayoutDirection::Vertical) {
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
    const auto geom = m_tiling->output()->geometry(); // workspace()->clientArea(MaximizeArea, m_tiling->output(), VirtualDesktopManager::self()->currentDesktop());
    return QRectF(qRound(geom.x() + m_relativeGeometry.x() * geom.width()),
                  qRound(geom.y() + m_relativeGeometry.y() * geom.height()),
                  qRound(m_relativeGeometry.width() * geom.width()),
                  qRound(m_relativeGeometry.height() * geom.height()));
}

QRectF TileData::workspaceGeometry() const
{
    const auto geom = absoluteGeometry();
    return geom.intersected(workspace()->clientArea(MaximizeArea, m_tiling->output(), VirtualDesktopManager::self()->currentDesktop())) - QMarginsF(m_leftPadding, m_topPadding, m_rightPadding, m_bottomPadding);
}

void TileData::setlayoutDirection(TileData::LayoutDirection dir)
{
    m_layoutDirection = dir;
}

TileData::LayoutDirection TileData::layoutDirection() const
{
    return m_layoutDirection;
}

bool TileData::isLayout() const
{
    // Items with a single child are not allowed, unless the root or its two children which are *always* layouts
    return m_childItems.count() > 0 || !m_parentItem || !m_parentItem->parentItem();
}

bool TileData::canBeRemoved() const
{
    // The root tile and its two children can *never* be removed
    return m_parentItem && m_parentItem->parentItem();
}

int TileData::leftPadding() const
{
    return m_leftPadding;
}

int TileData::topPadding() const
{
    return m_topPadding;
}

int TileData::rightPadding() const
{
    return m_rightPadding;
}

int TileData::bottomPadding() const
{
    return m_bottomPadding;
}

void TileData::resizeInLayout(qreal delta)
{
    if (!m_parentItem || layoutDirection() == LayoutDirection::Floating) {
        return;
    }
    const auto outGeom = m_tiling->output()->geometry();

    if (layoutDirection() == LayoutDirection::Horizontal) {
        qreal relativeDelta = delta / outGeom.width();
        setRelativeGeometry(QRectF(m_relativeGeometry.x() + relativeDelta, m_relativeGeometry.y(), m_relativeGeometry.width() - relativeDelta, m_relativeGeometry.height()));
    } else if (layoutDirection() == LayoutDirection::Vertical) {
        qreal relativeDelta = delta / outGeom.height();
        setRelativeGeometry(QRectF(m_relativeGeometry.x(), m_relativeGeometry.y() + relativeDelta, m_relativeGeometry.width(), m_relativeGeometry.height() - relativeDelta));
    }

    return;
    if (!m_parentItem || layoutDirection() == LayoutDirection::Floating) {
        return;
    }

    int index = row();

    if (index < 1) {
        // TODO: use resizeGravity instead?
        if (index == 0 && m_parentItem->m_childItems.count() > 1) {
            m_parentItem->m_childItems[index + 1]->resizeInLayout(-delta);
        }
        return;
    }

    const auto areaGeom = workspace()->clientArea(MaximizeArea, m_tiling->output(), VirtualDesktopManager::self()->currentDesktop());

    auto geom = m_relativeGeometry;
    auto otherGeom = m_parentItem->m_childItems[index - 1]->relativeGeometry();

    if (layoutDirection() == LayoutDirection::Horizontal) {
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

void TileData::split(KWin::TileData::LayoutDirection newDirection)
{
    if (!m_parentItem) {
        qCWarning(KWIN_CORE) << "Can't split the root tile";
    }
    //TODO: support floating
    if (newDirection == LayoutDirection::Floating) {
        return;
    }

    // If we are m_rootLayoutTile always create childrens, not siblings
    if (m_parentItem->parentItem() && (m_parentItem->m_childItems.count() < 2 || layoutDirection() == newDirection)) {
        // Add a new cell to the current layout
        m_layoutDirection = newDirection;
        QRectF newGeo;
        if (newDirection == LayoutDirection::Horizontal) {
            m_relativeGeometry.setWidth(m_relativeGeometry.width() / 2);
            newGeo = m_relativeGeometry;
            newGeo.moveLeft(newGeo.x() + newGeo.width());
        } else if (newDirection == LayoutDirection::Vertical) {
            m_relativeGeometry.setHeight(m_relativeGeometry.height() / 2);
            newGeo = m_relativeGeometry;
            newGeo.moveTop(newGeo.y() + newGeo.height());
        }

        Q_EMIT relativeGeometryChanged(m_relativeGeometry);
        Q_EMIT absoluteGeometryChanged();
        Q_EMIT m_tiling->tileGeometriesChanged();
        m_tiling->addTile(newGeo, layoutDirection(), m_parentItem);
    } else {
        // Do a new layout with 2 cells inside this one
        m_layoutDirection = newDirection;
        auto newGeo = m_relativeGeometry;
        if (newDirection == LayoutDirection::Horizontal) {
            newGeo.setWidth(m_relativeGeometry.width() / 2);
            m_tiling->addTile(newGeo, newDirection, this);
            newGeo.moveLeft(newGeo.x() + newGeo.width());
            m_tiling->addTile(newGeo, newDirection, this);
        } else if (newDirection == LayoutDirection::Vertical) {
            newGeo.setHeight(m_relativeGeometry.height() / 2);
            m_tiling->addTile(newGeo, newDirection, this);
            newGeo.moveTop(newGeo.y() + newGeo.height());
            m_tiling->addTile(newGeo, newDirection, this);
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
    const bool wasEmpty = m_childItems.isEmpty();
    m_childItems.removeAll(child);
    if (m_childItems.isEmpty() && !wasEmpty) {
        Q_EMIT isLayoutChanged(false);
    }
}

QList<TileData *> TileData::childTiles() const
{
    return m_childItems.toList();
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

TileData *TileData::descendantFromGeometry(const QRectF &geometry)
{
    if (absoluteGeometry() == geometry) {
        return this;
    }

    for (auto *tile : m_childItems) {
        if (auto *matchingTile = tile->descendantFromGeometry(geometry)) {
            return matchingTile;
        }
    }
    return nullptr;
}

TileData *TileData::ancestorWithDirection(TileData::LayoutDirection dir)
{
    if (!m_parentItem) {
        return nullptr;
    } else if (m_parentItem->layoutDirection() == dir) {
        return this;
    } else {
        return m_parentItem->ancestorWithDirection(dir);
    }
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
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(2000);
    connect(this, &CustomTiling::tileGeometriesChanged, m_saveTimer, static_cast<void (QTimer::*)()>(&QTimer::start));
    connect(m_saveTimer, &QTimer::timeout, this, &CustomTiling::saveSettings);

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

TileData *CustomTiling::bestTileForPosition(const QPointF &pos)
{
    const auto tiles = m_rootTile->descendants();
    qreal minimumDistance = std::numeric_limits<qreal>::max();
    TileData *ret = nullptr;

    for (auto *t : tiles) {
        if (!t->isLayout()) {
            const auto r = t->absoluteGeometry();
            // It's possible for tiles to overlap, so take the one which center is nearer to mouse pos
            const qreal distance = (r.center() - pos).manhattanLength();
            if (r.contains(pos) && distance < minimumDistance) {
                minimumDistance = distance;
                ret = t;
            }
        }
    }
    return ret;
}

QList<QRectF> CustomTiling::tileGeometries() const
{
    QList<QRectF> geometries;
    const auto tiles = m_rootTile->descendants();

    //TODO: can be costly: optimize?
    for (const auto *t : tiles) {
        if (!t->isLayout()) {
            geometries << t->absoluteGeometry();
        }
    }

    return geometries;
}

TileData *CustomTiling::rootTile() const
{
    return m_rootTile;
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

    if (!parentItem || parentItem == m_rootTile) {
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
    Q_UNUSED(parent)
    return 1;
}

void CustomTiling::createFloatingTile(const QRectF &relativeGeometry)
{
    if (relativeGeometry.isEmpty()) {
        return;
    }

    auto geom = relativeGeometry;
    geom.setLeft(qBound(0.0, geom.left(), 1.0));
    geom.setTop(qBound(0.0, geom.top(), 1.0));
    geom.setRight(qBound(0.0, geom.right(), 1.0));
    geom.setBottom(qBound(0.0, geom.bottom(), 1.0));

    if (geom.isEmpty()) {
        return;
    }

    if (!m_rootFloatingTile) {
        m_rootFloatingTile = addTile(QRectF(0, 0, 1, 1), TileData::LayoutDirection::Floating, m_rootTile);
    }
    addTile(geom, TileData::LayoutDirection::Floating, m_rootFloatingTile);
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

TileData *CustomTiling::parseTilingJSon(const QJsonValue &val, const QRectF &availableArea, TileData *parentTile)
{
    if (availableArea.isEmpty()) {
        return nullptr;
    }

    if (val.isObject()) {
        const auto &obj = val.toObject();
        TileData *createdTile = nullptr;

        if (parentTile == m_rootTile) {
            const auto direction = strToLayoutDirection(obj.value(QStringLiteral("layoutDirection")).toString());
            createdTile = addTile(QRect(0, 0, 1, 1), direction, parentTile);
            if (direction == TileData::LayoutDirection::Floating) {
                m_rootFloatingTile = createdTile;
            } else {
                m_rootLayoutTile = createdTile;
            }
        } else if (parentTile->layoutDirection() == TileData::LayoutDirection::Horizontal) {
            QRectF rect = availableArea;
            const auto width = obj.value(QStringLiteral("width"));
            if (width.isDouble()) {
                rect.setWidth(qMin(width.toDouble(), availableArea.width()));
            }
            if (!rect.isEmpty()) {
                createdTile = addTile(rect, parentTile->layoutDirection(), parentTile);
            }

        } else if (parentTile->layoutDirection() == TileData::LayoutDirection::Vertical) {
            QRectF rect = availableArea;
            const auto height = obj.value(QStringLiteral("height"));
            if (height.isDouble()) {
                rect.setHeight(qMin(height.toDouble(), availableArea.height()));
            }
            if (!rect.isEmpty()) {
                createdTile = addTile(rect, parentTile->layoutDirection(), parentTile);
            }

        } else if (parentTile->layoutDirection() == TileData::LayoutDirection::Floating) {
            if (!m_rootFloatingTile) {
                // This could happen only on malformed files
                m_rootFloatingTile = addTile(QRectF(0, 0, 1, 1), TileData::LayoutDirection::Floating, m_rootTile);
            }
            QRectF rect(0, 0, 1, 1);
            if (parentTile != m_rootTile) {
                rect = QRectF(obj.value(QStringLiteral("x")).toDouble(),
                              obj.value(QStringLiteral("y")).toDouble(),
                              obj.value(QStringLiteral("width")).toDouble(),
                              obj.value(QStringLiteral("height")).toDouble());
            }

            if (!rect.isEmpty()) {
                createdTile = addTile(rect, parentTile->layoutDirection(), m_rootFloatingTile);
            }
        }

        if (createdTile && obj.contains(QStringLiteral("tiles"))) {
            // It's a layout
            const auto arr = obj.value(QStringLiteral("tiles"));
            const auto direction = obj.value(QStringLiteral("layoutDirection"));
            // Ignore arrays with only a single item in it
            if (arr.isArray() && arr.toArray().count() > 0) {
                const TileData::LayoutDirection dir = strToLayoutDirection(direction.toString());
                createdTile->setlayoutDirection(dir);
                if (dir == TileData::LayoutDirection::Horizontal
                    || dir == TileData::LayoutDirection::Vertical) {
                    parseTilingJSon(arr, createdTile->relativeGeometry(), createdTile);
                } else {
                    // All floating tiles go under m_rootFloatingTile
                    parseTilingJSon(arr, QRectF(0, 0, 1, 1), m_rootFloatingTile);
                }
            }
        }
        return createdTile;
    } else if (val.isArray()) {
        const auto arr = val.toArray();
        auto avail = availableArea;
        for (auto it = arr.cbegin(); it != arr.cend(); it++) {
            if ((*it).isObject()) {
                auto *tile = parseTilingJSon(*it, avail, parentTile);
                if (tile && parentTile->layoutDirection() == TileData::LayoutDirection::Horizontal) {
                    avail.setLeft(tile->relativeGeometry().right());
                } else if (tile && parentTile->layoutDirection() == TileData::LayoutDirection::Vertical) {
                    avail.setTop(tile->relativeGeometry().bottom());
                }
            }
        }
        //make sure the children fill exactly the parent, eventually enlarging the last
        if (parentTile->layoutDirection() != TileData::LayoutDirection::Floating
            && parentTile->childCount() > 0) {
            auto *last = parentTile->child(parentTile->childCount() - 1);
            auto geom = last->relativeGeometry();
            geom.setRight(parentTile->relativeGeometry().right());
            last->setRelativeGeometry(geom);
        }
        return nullptr;
    }
    return nullptr;
}

TileData *CustomTiling::addTile(const QRectF &relativeGeometry, TileData::LayoutDirection layoutDirection, TileData *parentTile)
{
    auto index = parentTile == m_rootTile ? QModelIndex() : createIndex(parentTile->row(), 0, parentTile);
    beginInsertRows(index, parentTile->childCount(), parentTile->childCount());
    TileData *tile = new TileData(this, parentTile);
    tile->setRelativeGeometry(relativeGeometry);
    tile->setlayoutDirection(layoutDirection);
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
    KConfigGroup cg = kwinApp()->config()->group(QStringLiteral("Tiling"));
    cg = KConfigGroup(&cg, m_output->uuid().toString(QUuid::WithoutBraces));

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(cg.readEntry("tiles", QByteArray()), &error);

    if (error.error != QJsonParseError::NoError) {
        qCWarning(KWIN_CORE) << "Parse error in tiles configuration for monitor" << m_output->uuid().toString(QUuid::WithoutBraces) << ":" << error.errorString();
        if (!m_rootLayoutTile) {
            m_rootLayoutTile = addTile(QRectF(0, 0, 1, 1), TileData::LayoutDirection::Horizontal, m_rootTile);
            saveSettings();
        }
        return;
    }

    if (doc.object().contains(QStringLiteral("tiles"))) {
        const auto arr = doc.object().value(QStringLiteral("tiles"));
        if (arr.isArray() && arr.toArray().count() > 0) {
            parseTilingJSon(arr, QRectF(0, 0, 1, 1), m_rootTile);
        }
    }

    if (!m_rootLayoutTile) {
        m_rootLayoutTile = addTile(QRectF(0, 0, 1, 1), TileData::LayoutDirection::Horizontal, m_rootTile);
        saveSettings();
    }

    m_rootTile->print();
    Q_EMIT tileGeometriesChanged();
}

QJsonObject CustomTiling::tileToJSon(TileData *tile)
{
    QJsonObject obj;

    auto *parentTile = tile->parentItem();

    // Exclude the root and the two children
    if (parentTile && parentTile->parentItem()) {
        switch (parentTile->layoutDirection()) {
        case TileData::LayoutDirection::Horizontal:
            obj[QStringLiteral("width")] = tile->relativeGeometry().width();
            break;
        case TileData::LayoutDirection::Vertical:
            obj[QStringLiteral("height")] = tile->relativeGeometry().height();
            break;
        case TileData::LayoutDirection::Floating:
        default:
            obj[QStringLiteral("x")] = tile->relativeGeometry().x();
            obj[QStringLiteral("y")] = tile->relativeGeometry().y();
            obj[QStringLiteral("width")] = tile->relativeGeometry().width();
            obj[QStringLiteral("height")] = tile->relativeGeometry().height();
        }
    }

    if (tile->isLayout()) {
        // Don't write layoutDirection of Root
        if (parentTile) {
            switch (tile->layoutDirection()) {
            case TileData::LayoutDirection::Horizontal:
                obj[QStringLiteral("layoutDirection")] = QStringLiteral("horizontal");
                break;
            case TileData::LayoutDirection::Vertical:
                obj[QStringLiteral("layoutDirection")] = QStringLiteral("vertical");
                break;
            case TileData::LayoutDirection::Floating:
            default:
                obj[QStringLiteral("layoutDirection")] = QStringLiteral("floating");
            }
        }

        QJsonArray tiles;
        const int nChildren = tile->childCount();
        for (int i = 0; i < nChildren; ++i) {
            tiles.append(tileToJSon(tile->child(i)));
        }
        obj[QStringLiteral("tiles")] = tiles;
    }

    return obj;
}

void CustomTiling::saveSettings()
{
    auto obj = tileToJSon(m_rootTile);
    QJsonDocument doc(obj);
    KConfigGroup cg = kwinApp()->config()->group(QStringLiteral("Tiling"));
    cg = KConfigGroup(&cg, m_output->uuid().toString(QUuid::WithoutBraces));
    cg.writeEntry("tiles", doc.toJson(QJsonDocument::Compact));
    cg.sync(); //FIXME: less frequent?
}

} // namespace KWin

#include "moc_customtiling.cpp"
