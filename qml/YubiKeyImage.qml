import QtQuick 2.0
import Sailfish.Silica 1.0
import QtGraphicalEffects 1.0

ShaderEffectSource {
    id: thisItem

    visible: opacity > 0

    property real imageHeight: Math.round(height*2/3)

    sourceItem: Item {
        width: thisItem.width
        height: thisItem.height

        Image {
            id: shadow

            anchors.centerIn: parent
            sourceSize.height: imageHeight
            source: Qt.resolvedUrl("images/yubikey-vertical-mask.svg")
            smooth: true
            visible: false
        }

        FastBlur {
            source: shadow
            anchors.fill: shadow
            radius: 32
            transparentBorder: true
        }

        Image {
            anchors.centerIn: parent
            sourceSize.height: shadow.height
            source: Qt.resolvedUrl("images/yubikey-vertical.svg")
            smooth: true
        }
    }
}
