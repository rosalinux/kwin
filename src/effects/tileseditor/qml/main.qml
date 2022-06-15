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

FocusScope {
    id: root
    focus: true

    Keys.onEscapePressed: {
        root.active = false;
        effect.deactivate(effect.animationDuration);
    }

    required property QtObject effect
    required property QtObject targetScreen

    property bool active: false

    Component.onCompleted: {
        root.active = true;
    }

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
                opacity: root.active ? 0.3 : 0
                color: PlasmaCore.Theme.backgroundColor
                border.color: PlasmaCore.Theme.textColor
                Behavior on opacity {
                    OpacityAnimator {
                        duration: effect.animationDuration
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }
}
