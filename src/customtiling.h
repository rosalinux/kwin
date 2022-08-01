/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_CUSTOM_TILING_H
#define KWIN_CUSTOM_TILING_H

#include <kwin_export.h>

#include <QAbstractItemModel>
#include <QObject>
#include <QRectF>

#include <QJsonValue>

class QTimer;

namespace KWin
{

class Output;
class TileData;
class CustomTiling;

// This data entry looks and behaves like a tree model node, even though will live on a flat QAbstractItemModel to be represented by a single QML Repeater
class KWIN_EXPORT TileData : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QRectF relativeGeometry READ relativeGeometry NOTIFY relativeGeometryChanged)
    Q_PROPERTY(QRectF absoluteGeometry READ absoluteGeometry NOTIFY absoluteGeometryChanged)
    Q_PROPERTY(KWin::TileData::LayoutDirection layoutDirection READ layoutDirection CONSTANT)
    Q_PROPERTY(QList<TileData *> tiles READ childTiles NOTIFY childTilesChanged)
    Q_PROPERTY(bool isLayout READ isLayout NOTIFY isLayoutChanged)
    Q_PROPERTY(bool canBeRemoved READ canBeRemoved CONSTANT)

public:
    enum class LayoutDirection {
        Floating = 0,
        Horizontal = 1,
        Vertical = 2
    };
    Q_ENUM(LayoutDirection)

    explicit TileData(CustomTiling *m_tiling, TileData *parentItem = nullptr);
    ~TileData();

    void setRelativeGeometry(const QRectF &geom);
    QRectF relativeGeometry() const;
    QRectF absoluteGeometry() const;

    void setlayoutDirection(TileData::LayoutDirection dir);
    // Own direction
    TileData::LayoutDirection layoutDirection() const;

    bool isLayout() const;
    bool canBeRemoved() const;

    int leftPadding() const;
    int topPadding() const;
    int rightPadding() const;
    int bottomPadding() const;

    void appendChild(TileData *child);
    void removeChild(TileData *child);

    QList<TileData *> childTiles() const;

    Q_INVOKABLE void resizeInLayout(qreal delta);
    Q_INVOKABLE void split(KWin::TileData::LayoutDirection newDirection);
    Q_INVOKABLE void remove();

    void print();
    TileData *child(int row);
    int childCount() const;
    int row() const;
    TileData *parentItem();
    // Return a descendant that has the given geometry
    TileData *descendantFromGeometry(const QRectF &geometry);
    //Returns an ancestor which has the given layoutDirection
    TileData *ancestorWithDirection(TileData::LayoutDirection dir);

    QVector<TileData *> descendants() const;

Q_SIGNALS:
    void relativeGeometryChanged(const QRectF &relativeGeometry);
    void absoluteGeometryChanged();
    void isLayoutChanged(bool isLayout);
    void childTilesChanged();

private:
    QVector<TileData *> m_childItems;
    TileData *m_parentItem;

    CustomTiling *m_tiling;
    QRectF m_relativeGeometry;
    TileData::LayoutDirection m_layoutDirection = LayoutDirection::Floating;
    int m_leftPadding = 4;
    int m_topPadding = 4;
    int m_rightPadding = 4;
    int m_bottomPadding = 4;
    bool m_canBeRemoved = true;
};

/**
 * Custom tiling zones management per output.
 */
class KWIN_EXPORT CustomTiling : public QAbstractItemModel
{
    Q_OBJECT
    Q_PROPERTY(TileData *rootTile READ rootTile CONSTANT)

public:
    enum Roles {
        TileDataRole = Qt::UserRole + 1
    };
    explicit CustomTiling(Output *parent = nullptr);
    ~CustomTiling() override;

    Output *output() const;

    void updateTileGeometry(const QRectF &oldGeom, const QRectF &newGeom);

    QList<QRectF> tileGeometries() const;
    TileData *rootTile() const;

    // QAbstractItemModel overrides
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    Q_INVOKABLE void createFloatingTile(const QRectF &relativeGeometry);

Q_SIGNALS:
    void tileGeometriesChanged();

private:
    TileData *addTile(const QRectF &relativeGeometry, TileData::LayoutDirection layoutDirection, TileData *parentTile);
    void removeTile(TileData *tile);

    void readSettings();
    void saveSettings();
    QJsonObject tileToJSon(TileData *parentTile);
    TileData *parseTilingJSon(const QJsonValue &val, const QRectF &availableArea, TileData *parentTile);

    Q_DISABLE_COPY(CustomTiling)

    Output *m_output = nullptr;
    QTimer *m_saveTimer = nullptr;
    TileData *m_rootTile = nullptr;
    TileData *m_rootLayoutTile = nullptr;
    TileData *m_rootFloatingTile = nullptr;
    friend class TileData;
};

} // namespace KWin

#endif
