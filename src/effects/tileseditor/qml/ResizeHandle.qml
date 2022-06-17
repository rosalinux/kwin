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
    property Qt.Edge edge
    readonly property Qt.Orientation orientation: edge == Qt.LeftEdge || edge == Qt.RightEdge
    readonly property bool valid: tileData.layoutDirection == KWinComponents.TileData.Floating || (orientation == Qt.Horizontal && tileData.layoutDirection != KWinComponents.TileData.Horizontal)
        || (orientation == Qt.Vertical && tileData.layoutDirection != KWinComponents.TileData.Vertical)

    z: 2

    implicitWidth: PlasmaCore.Units.gridUnit
    implicitHeight: PlasmaCore.Units.gridUnit

    radius: 3
    opacity: hoverHandler.hovered ? 0.4 : 0

    HoverHandler {
        id: hoverHandler
        cursorShape: orientation == Qt.Horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }

    DragHandler {
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
            print(oldPoint.x - dragPoint.x);
            tileData.resizeInLayout(dragPoint.x - oldPoint.x);
            oldPoint = dragPoint;
        }
    }
}
