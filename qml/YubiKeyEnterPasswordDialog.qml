import QtQuick 2.0
import Sailfish.Silica 1.0

import "harbour"

Dialog {
    id: thisDialog

    property alias enterPrompt: prompt.text
    property string confirmPrompt
    property var destinationAction
    property var destination
    property var destinationProperties

    signal passwordAccepted(var target)

    canAccept: passwordField.text.length > 0
    acceptDestination: Qt.resolvedUrl("YubiKeyConfirmPasswordDialog.qml")
    acceptDestinationProperties: {
        "allowedOrientations": allowedOrientations,
        "prompt": confirmPrompt,
        "password": passwordField.text,
        "acceptDestinationAction": destinationAction,
        "acceptDestination": destination,
        "acceptDestinationProperties": destinationProperties
    }

    readonly property int _screenHeight: isLandscape ? Screen.width : Screen.height
    readonly property real _landscapeWidth: Screen.height - (('topCutout' in Screen) ? Screen.topCutout.height : 0)

    // Otherwise width is changing with a delay, causing visible layout changes
    onIsLandscapeChanged: width = isLandscape ? _landscapeWidth : Screen.width

    onStatusChanged: {
        if (status === DialogStatus.Opening) {
            passwordField.requestFocus()
        }
    }

    onAccepted: acceptDestinationInstance.password = passwordField.text

    Connections {
        target: acceptDestinationInstance
        onAccepted: passwordAccepted(target)
    }

    DialogHeader {
        id: header

        reserveExtraContent: true
        spacing: 0
    }

    DialogHeaderText {
        headerItem: header
        textItem: prompt
    }

    InfoLabel {
        id: prompt

        anchors {
            top: header.bottom
            topMargin: Theme.paddingLarge
            bottom: inputColumn.top
            bottomMargin: Theme.paddingLarge
        }

        verticalAlignment: Text.AlignVCenter

        // Hide it when it's only partially visible (and show the header label instead)
        opacity: (height >= contentHeight) ? 1 : 0
        visible: opacity > 0
    }

    Column {
        id: inputColumn

        readonly property int _ymax1: _screenHeight/2 - passwordField._backgroundRuleTopOffset - passwordField.y
        readonly property int _ymax2: thisDialog.height - passwordField.height - passwordField.y

        y: Math.min(_ymax1, _ymax2)
        width: parent.width

        HarbourPasswordInputField {
            id: passwordField

            readonly property int _backgroundRuleTopOffset: editContentItem ? (editContentItem.y + editContentItem.height) : 0

            EnterKey.enabled: canAccept
            EnterKey.onClicked: accept()
        }
    }
}
