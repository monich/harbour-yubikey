import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

Dialog {
    id: thisDialog

    property var otpModel
    property string currentName

    signal rename(var name)

    canAccept: inputField.text.length > 0 && inputField.text != currentName && !_duplicateName

    readonly property real _landscapeWidth: Screen.height - (('topCutout' in Screen) ? Screen.topCutout.height : 0)
    readonly property int _fullHeight: isPortrait ? Screen.height : Screen.width
    readonly property bool _duplicateName: inputField.text != currentName && otpModel.containsName(inputField.text)

    onStatusChanged: {
        if (status == DialogStatus.Opening) {
            inputField.forceActiveFocus()
            inputField.text = currentName
        }
    }

    // Otherwise width is changing with a delay, causing visible layout changes
    onIsLandscapeChanged: width = isLandscape ? _landscapeWidth : Screen.width

    onAccepted: thisDialog.rename(inputField.text)

    DialogHeader {
        id: header

        //: Button label (rename token)
        //% "Rename"
        acceptText: qsTrId("yubikey-rename_token-accept-button")
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
            bottom: inputField.top
            bottomMargin: Theme.paddingLarge
        }

        //: Input prompt to rename the token
        //% "Enter a new name"
        text: qsTrId("yubikey-rename_token-prompt")
        verticalAlignment: Text.AlignVCenter

        // Hide it when it's only partially visible (and show the header label instead)
        opacity: (height >= contentHeight) ? 1 : 0
        visible: opacity > 0
    }

    TextField {
        id: inputField

        // Sailfish OS 4.0 renamed TextBase._contentItem into contentItem
        readonly property var _editContentItem: ('contentItem' in inputField) ? contentItem : ('_contentItem' in inputField) ? _contentItem : null
        readonly property int _backgroundRuleTopOffset: _editContentItem ? (_editContentItem.y + _editContentItem.height) : 0
        readonly property int _ymax1: _screenHeight/2 - _backgroundRuleTopOffset
        readonly property int _ymax2: thisDialog.height - height

        y: Math.min(_ymax1, _ymax2)
        width: parent.width
        //: Placeholder for the new token name
        //% "New name"
        placeholderText: qsTrId("yubikey-rename_token-placeholder")
        //: Label for a duplicate token name
        //% "Duplicate name"
        label: _duplicateName ? qsTrId("yubikey-rename_token-duplicate_name") : placeholderText
        EnterKey.enabled: canAccept
        EnterKey.onClicked: accept()
    }
}
