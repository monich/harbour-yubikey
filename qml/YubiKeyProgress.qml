import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

ShaderEffectSource {
    id: thisItem

    property bool busy
    property real timeLeft
    property real progressValue: 1.0 - timeLeft / YubiKey.TotpPeriod
    property real busyProgress: invertColors ? 0.75 : 0.25
    property color backgroundColor: "transparent"
    property color progressColor: "#f8bc56"
    property bool invertColors: true

    property bool active: opacity > 0 && visible && Qt.application.active
    readonly property int imageMargins: 2 * Theme.paddingSmall

    width: Theme.iconSizeExtraLarge + 2 * imageMargins
    height: Theme.iconSizeExtraLarge + 2 * imageMargins

    sourceItem: Item {
        width: thisItem.width
        height: thisItem.height

        Rectangle {
            anchors.fill: parent
            color: Theme.primaryColor
            radius: width/2
            opacity: 0.05
        }

        ProgressCircle {
            anchors.fill: parent
            value: busy ? busyProgress : thisItem.progressValue
            progressColor: invertColors ? thisItem.backgroundColor : thisItem.progressColor
            backgroundColor: invertColors ? thisItem.progressColor : thisItem.backgroundColor
            transformOrigin: Item.Center

            RotationAnimation on rotation {
                from: 0
                to: 360
                duration: busy ? 2000 : 250
                alwaysRunToEnd: true
                running: busy && active
                loops: Animation.Infinite
            }

            Behavior on value {
                enabled: active
                NumberAnimation { duration: 500 }
            }
        }

        Image {
            anchors.centerIn: parent
            source: "images/yubikey.svg"
            sourceSize: Qt.size(parent.width - 2 * imageMargins, parent.height - 2 * imageMargins)
        }
    }
}
