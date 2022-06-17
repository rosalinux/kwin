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

Item {
    required property QtObject tileData
    x: tileData.absoluteGeometry.x
    y: tileData.absoluteGeometry.y
    z: tileData.layoutDirection === KWinComponents.TileData.Floating ? 1 : 0
    width: tileData.absoluteGeometry.width
    height: tileData.absoluteGeometry.height
    Rectangle {
        anchors {
            horizontalCenter: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        z: 2
        width: PlasmaCore.Units.gridUnit
        radius: 3
        opacity: hoverHandler.hovered
        HoverHandler {
            id: hoverHandler
            cursorShape: Qt.SizeHorCursor
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
    Item {
        anchors.fill: parent
        visible: !tileData.isLayout
        Rectangle {
            anchors {
                fill: parent
                margins: PlasmaCore.Units.smallSpacing
            }
            radius: 3
            opacity: tileData.layoutDirection === KWinComponents.TileData.Floating ? 0.6 : 0.3
            color: PlasmaCore.Theme.backgroundColor
            border.color: PlasmaCore.Theme.textColor
        }
        ColumnLayout {
            anchors.centerIn: parent
            PlasmaComponents.Button {
                Layout.fillWidth: true
                icon.name: "view-split-left-right"
                text: i18n("Split Horizontally")
                onClicked: tileData.split(KWinComponents.TileData.Horizontal)
            }
            PlasmaComponents.Button {
                Layout.fillWidth: true
                icon.name: "view-split-top-bottom"
                text: i18n("Split Vertically")
                onClicked: tileData.split(KWinComponents.TileData.Vertical)
            }
            PlasmaComponents.Button {
                Layout.fillWidth: true
                icon.name: "edit-delete"
                text: i18n("Delete")
                onClicked: tileData.remove()
            }
        }
    }
}
