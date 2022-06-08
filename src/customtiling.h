/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_CUSTOM_TILING_H
#define KWIN_CUSTOM_TILING_H

#include <kwin_export.h>

#include <QObject>
#include <QRectF>

#include <QJsonValue>

namespace KWin
{

class Output;

/**
 * Custom tiling zones management per output.
 */
class KWIN_EXPORT CustomTiling : public QObject
{
    Q_OBJECT

public:
    enum class LayoutDirection {
        Floating = 0,
        Horizontal = 1,
        Vertical = 1
    };
    Q_ENUM(LayoutDirection)

    explicit CustomTiling(Output *parent = nullptr);
    ~CustomTiling() override;

    QList<QRectF> tileGeometries() const;

Q_SIGNALS:
    void tileGeometriesChanged();

private:
    void readSettings();
    QRectF parseTilingJSon(const QJsonValue &val, const QString &layoutDirection, const QRectF &availableArea);

    Q_DISABLE_COPY(CustomTiling)

    Output *m_output = nullptr;
    QList<QRectF> m_tiles;
};

} // namespace KWin

#endif
