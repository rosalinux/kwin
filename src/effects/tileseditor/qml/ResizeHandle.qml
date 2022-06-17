/*
    SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.15
import QtQuick.Layouts 1.4
import QtGraphicalEffects 1.12
import org.kde.kwin 3.0 as KWinComponents
import org.kde.kwin.private.effects 1.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.plasma.extras 2.0 as PlasmaExtras
import org.kde.KWin.Effect.WindowView 1.0
import org.kde.kitemmodels 1.0 as KitemModels

Rectangle {
    id: handle

    required property QtObject tileData
    property int edge
    readonly property int orientation: edge == Qt.LeftEdge || edge == Qt.RightEdge
    readonly property bool valid: tileData.layoutDirection == KWinComponents.TileData.Floating || (orientation == Qt.Horizontal && tileData.layoutDirection != KWinComponents.TileData.Horizontal)
        || (orientation == Qt.Vertical && tileData.layoutDirection != KWinComponents.TileData.Vertical)

    z: 2

    implicitWidth: PlasmaCore.Units.smallSpacing * 2
    implicitHeight: PlasmaCore.Units.smallSpacing * 2

    radius: 3
    color: PlasmaCore.Theme.highlightColor
    opacity: hoverHandler.hovered || dragHandler.active ? 0.4 : 0

    HoverHandler {
        id: hoverHandler
        cursorShape: orientation == Qt.Horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }

    DragHandler {
        id: dragHandler
        target: null
        property point oldPoint: Qt.point(0, 0)
        property point dragPoint: centroid.scenePosition
        onActiveChanged: {
            if (active) {
                oldPoint = dragPoint;
            }
        }
        onDragPointChanged: {
            if (!active) {
                return;
            }
            if (handle.orientation == Qt.Horizontal) {
                tileData.resizeInLayout(dragPoint.x - oldPoint.x);
            } else {
                tileData.resizeInLayout(dragPoint.y - oldPoint.y);
            }
            oldPoint = dragPoint;
        }
    }
}
