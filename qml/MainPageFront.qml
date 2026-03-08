import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

Item {
    id: thisItem

    visible: NfcSystem.valid
    anchors.fill: parent

    signal flip()

    readonly property bool _targetPresent: NfcAdapter.targetPresent

    function showSettingsButton() {
        showButtonTimer.restart()
    }

    BusyIndicator {
        id: busyIndicator

        anchors.centerIn: parent
        size: BusyIndicatorSize.Large
        running: _targetPresent
        visible: opacity > 0
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
        opacity: (_initialized && !busyIndicator.running) ? 1 : 0
        width: isPortrait ? (parent.width - 2 * Theme.horizontalPageMargin) :
            (parent.width - image.width - Theme.horizontalPageMargin)
        height: isPortrait ? (parent.height - image.height*2/3) : parent.height
        visible: opacity > 0
        text: _targetPresent ? "" :
            //: Info label
            //% "NFC not supported"
            !NfcSystem.present ? qsTrId("yubikey-info-nfc_not_supported") :
            //: Hint label
            //% "Touch a YubiKey NFC"
            NfcSystem.enabled ? qsTrId("yubikey-info-touch_hint") :
            //: Info label
            //% "NFC is off"
            qsTrId("yubikey-info-nfc_disabled")

        Behavior on opacity { FadeAnimation {} }
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
        opacity: (showButtonTimer.running && !_targetPresent) ? 1 : 0
        visible: opacity > 0
        onClicked: {
            showButtonTimer.stop()
            thisItem.flip()
        }

        Behavior on opacity { FadeAnimation { duration: 500 } }
    }
}
