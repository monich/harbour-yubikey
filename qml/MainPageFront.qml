import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

Item {
    id: thisItem

    property bool landscape

    anchors.fill: parent

    signal flip()

    function showSettingsButton() {
        showButtonTimer.restart()
    }

    YubiKeyImage {
        id: image

        width: height
        height: Screen.width
        opacity: label.opacity
    }

    InfoLabel {
        id: label

        anchors {
            bottom: parent.bottom
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
        }
        verticalAlignment: Text.AlignVCenter
        width: landscape ? (parent.width - image.width - Theme.horizontalPageMargin) :
            (parent.width - 2 * Theme.horizontalPageMargin)
        height: landscape ? parent.height : (parent.height - image.height*2/3)
        text: NfcSystem.enabled ?
            //: Info label
            //% "Insert YubiKey into the USB port or tap it if it supports NFC"
            qsTrId("yubikey-info-insert_or_tap") :
            //: Info label
            //% "Insert YubiKey into the USB port"
            qsTrId("yubikey-info-insert")
    }

    Timer {
        id: showButtonTimer
        interval: 2000
    }

    MouseArea {
        anchors.fill: parent
        onClicked: showSettingsButton()
    }

    HarbourIconTextButton {
        anchors {
            bottom: parent.bottom
            right: parent.right
            margins: Theme.paddingMedium
        }
        iconSource: "images/settings.svg"
        opacity: showButtonTimer.running ? 1 : 0
        visible: opacity > 0
        onClicked: {
            showButtonTimer.stop()
            thisItem.flip()
        }

        Behavior on opacity { FadeAnimation { duration: 500 } }
    }
}
