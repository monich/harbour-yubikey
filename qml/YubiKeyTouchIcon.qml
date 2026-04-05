import QtQuick 2.0
import Sailfish.Silica 1.0
import QtGraphicalEffects 1.0

import "harbour"

ShaderEffectSource {
    id: thisItem

    visible: opacity > 0

    property bool blinking: true

    readonly property bool _darkOnLight : "colorScheme" in Theme && Theme.colorScheme === Theme.DarkOnLight
    readonly property real _size: Math.min(width, height)

    sourceItem: Item {
        width: thisItem.width
        height: thisItem.height
        visible: thisItem.blinking

        Rectangle {
            width: thisItem._size
            height: thisItem._size
            radius: thisItem._size/2
            anchors.centerIn: parent
            color: "transparent"

            RadialGradient {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "green" }
                    GradientStop { position: 0.5; color: "transparent" }
                }
            }

            SequentialAnimation on opacity {
                loops: Animation.Infinite
                running: thisItem.blinking && thisItem.visible && Qt.application.active
                NumberAnimation {
                    easing.type: Easing.InOutQuad
                    duration: 350
                    from: 0
                    to: 1
                }
                NumberAnimation {
                    easing.type: Easing.InOutQuad
                    duration: 350
                    to: 0
                }
            }
        }

        HarbourHighlightIcon {
            anchors.centerIn: parent
            sourceSize: Qt.size(thisItem._size, thisItem._size)
            source: Qt.resolvedUrl("images/yubikey-mask.svg")
            highlightColor: "#f8bc56"
        }
    }
}
