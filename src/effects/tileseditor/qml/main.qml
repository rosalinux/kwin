/*
    SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.12
import QtQuick.Layouts 1.4
import QtGraphicalEffects 1.12
import org.kde.kwin 3.0 as KWinComponents
import org.kde.kwin.private.effects 1.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.plasma.extras 2.0 as PlasmaExtras
import org.kde.KWin.Effect.WindowView 1.0

Item {
    id: root

    Keys.onEscapePressed: effect.deactivate();

    required property QtObject effect
    required property QtObject targetScreen


    Repeater {
        model: KWinComponents.ClientFilterModel {
            activity: KWinComponents.Workspace.currentActivity
            desktop: KWinComponents.Workspace.currentVirtualDesktop
            screenName: targetScreen.name
            clientModel: KWinComponents.ClientModel {}
        }

        KWinComponents.WindowThumbnailItem {
            wId: model.client.internalId
            x: model.client.x - targetScreen.geometry.x
            y: model.client.y - targetScreen.geometry.y
            width: model.client.width
            height: model.client.height
            z: model.client.stackingOrder
            visible: !model.client.minimized
        }
    }

    Repeater {
        model: KWinComponents.Workspace.customTilingForScreen(targetScreen.name).tileGeometries
        Item {
            x: modelData.x
            y: modelData.y
            z: 999
            width: modelData.width
            height: modelData.height
            Rectangle {
                anchors {
                    fill: parent
                    margins: PlasmaCore.Units.smallSpacing
                }
                radius: 3
                opacity: 0.3
                color: PlasmaCore.Theme.backgroundColor
                border.color: PlasmaCore.Theme.textColor
            }
        }
    }
    RowLayout {
        z: 999
        PlasmaComponents.Button {
            text: "close"
            onClicked: effect.deactivate(0);
        }
        PlasmaComponents.Button {
            text: "print"
            onClicked: {
                print(root.targetScreen)
                print(KWinComponents.Workspace.customTilingForScreen(targetScreen.name))
            }
        }
    }
}
