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
    id: delegate
    required property QtObject tileData
    property alias deleteVisible: deleteButton.visible

    x: Math.round(tileData.absoluteGeometry.x)
    y: Math.round(tileData.absoluteGeometry.y)
    z: tileData.layoutDirection === KWinComponents.TileData.Floating ? 1 : 0
    width: Math.round(tileData.absoluteGeometry.width)
    height: Math.round(tileData.absoluteGeometry.height)
    ResizeHandle {
        anchors {
            horizontalCenter: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        tileData: delegate.tileData
        edge: Qt.LeftEdge
    }
    ResizeHandle {
        anchors {
            verticalCenter: parent.top
            left: parent.left
            right: parent.right
        }
        tileData: delegate.tileData
        edge: Qt.TopEdge
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
                onReleased: tileData.split(KWinComponents.TileData.Horizontal)
            }
            PlasmaComponents.Button {
                Layout.fillWidth: true
                icon.name: "view-split-top-bottom"
                text: i18n("Split Vertically")
                onReleased: tileData.split(KWinComponents.TileData.Vertical)
            }
            PlasmaComponents.Button {
                id: deleteButton
                Layout.fillWidth: true
                icon.name: "edit-delete"
                text: i18n("Delete")
                onReleased: tileData.remove()
            }
        }
    }
}
