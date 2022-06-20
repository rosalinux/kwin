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
    Q_PROPERTY(bool isLayout READ isLayout NOTIFY isLayoutChanged)

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

    void setLayoutDirection(TileData::LayoutDirection dir);
    TileData::LayoutDirection layoutDirection() const;

    bool isLayout() const;

    void appendChild(TileData *child);
    void removeChild(TileData *child);

    Q_INVOKABLE void resizeInLayout(qreal delta);
    Q_INVOKABLE void split(KWin::TileData::LayoutDirection layoutDirection);
    Q_INVOKABLE void remove();

    void print();
    TileData *child(int row);
    int childCount() const;
    int row() const;
    TileData *parentItem();

    QVector<TileData *> descendants() const;

Q_SIGNALS:
    void relativeGeometryChanged(const QRectF &relativeGeometry);
    void absoluteGeometryChanged();
    void isLayoutChanged(bool isLayout);

private:
    QVector<TileData *> m_childItems;
    TileData *m_parentItem;

    CustomTiling *m_tiling;
    QRectF m_relativeGeometry;
    TileData::LayoutDirection m_layoutDirection;
};

/**
 * Custom tiling zones management per output.
 */
class KWIN_EXPORT CustomTiling : public QAbstractItemModel
{
    Q_OBJECT
    //TODO: a model
    Q_PROPERTY(QList<QRectF> tileGeometries READ tileGeometries CONSTANT)

public:
    enum Roles {
        TileDataRole = Qt::UserRole + 1
    };
    explicit CustomTiling(Output *parent = nullptr);
    ~CustomTiling() override;

    Output *output() const;

    QList<QRectF> tileGeometries() const;

    // QAbstractItemModel overrides
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

Q_SIGNALS:
    void tileGeometriesChanged();

private:
    TileData *addTile(const QRectF &relativeGeometry, TileData::LayoutDirection layoutDirection, TileData *parentTile);
    void removeTile(TileData *tile);

    void readSettings();
    void saveSettings();
    QJsonObject tileToJSon(TileData *parentTile);
    QRectF parseTilingJSon(const QJsonValue &val, TileData::LayoutDirection layoutDirection, const QRectF &availableArea, TileData *parentTile);

    Q_DISABLE_COPY(CustomTiling)

    Output *m_output = nullptr;
    QTimer *m_saveTimer = nullptr;
    TileData *m_rootTile;
    friend class TileData;
};

} // namespace KWin

#endif
