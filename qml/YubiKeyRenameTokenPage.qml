import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

Page {
    id: thisPage

    forwardNavigation: false

    property var tokenList
    property string currentName

    signal rename(var name)

    readonly property int _fullHeight: isPortrait ? Screen.height : Screen.width
    readonly property bool _duplicateName: nameField.text.length > 0 &&
        nameField.text != currentName && tokenList.containsName(nameField.text)
    readonly property bool _acceptableName: nameField.text.length > 0 &&
        nameField.text != currentName && !tokenList.containsName(nameField.text)

    onStatusChanged: {
        switch (status) {
        case DialogStatus.Opening:
            nameField.forceActiveFocus()
            nameField.text = currentName
            break
        case DialogStatus.Opened:
            forwardNavigation = false
            break
        }
    }

    // Otherwise width is changing with a delay, causing visible layout changes
    onIsLandscapeChanged: width = isLandscape ? Screen.height : Screen.width

    InfoLabel {
        id: promptLabel

        //: Input prompt to rename the token
        //% "Enter new name for %1"
        text: qsTrId("yubikey-rename_token-prompt").arg(currentName)
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * x
        anchors {
            bottom: panel.top
            bottomMargin: Theme.paddingLarge
        }

        // Hide it when it's only partially visible
        opacity: (y < Theme.paddingSmall) ? 0 : 1
        Behavior on opacity {
            enabled: !orientationTransitionRunning
            FadeAnimation { }
        }
    }

    Item {
        id: panel

        width: parent.width
        height: childrenRect.height
        y: Math.min((_fullHeight - height)/2, thisPage.height - panel.height - Theme.paddingLarge)

        TextField {
            id: nameField

            anchors {
                left: panel.left
                top: parent.top
            }

            //: Placeholder for the new token name
            //% "Enter new name"
            placeholderText: qsTrId("yubikey-rename_token-placeholder")
            //: Label for a duplicate token name
            //% "Duplicate name"
            label: _duplicateName ? qsTrId("yubikey-rename_token-duplicate_name") : ""
            EnterKey.enabled: _acceptableName
            EnterKey.onClicked: accept()

            function accept() {
                rename(text)
            }
        }

        Button {
            id: button

            anchors {
                topMargin: Theme.paddingLarge
                bottomMargin: 2 * Theme.paddingSmall
            }
            //: Button label (rename token)
            //% "Rename"
            text: qsTrId("yubikey-rename_token-button")
            enabled: _acceptableName
            onClicked: nameField.accept()
        }
    }

    states: [
        State {
            name: "portrait"
            when: !isLandscape
            changes: [
                AnchorChanges {
                    target: nameField
                    anchors.right: panel.right
                },
                PropertyChanges {
                    target: nameField
                    anchors {
                        rightMargin: 0
                        bottomMargin: Theme.paddingLarge
                    }
                },
                AnchorChanges {
                    target: button
                    anchors {
                        top: nameField.bottom
                        right: undefined
                        horizontalCenter: parent.horizontalCenter
                        bottom: undefined
                    }
                },
                PropertyChanges {
                    target: button
                    anchors.rightMargin: 0
                }
            ]
        },
        State {
            name: "landscape"
            when: isLandscape
            changes: [
                AnchorChanges {
                    target: nameField
                    anchors.right: button.left
                },
                PropertyChanges {
                    target: nameField
                    anchors {
                        rightMargin: Theme.horizontalPageMargin
                        bottomMargin: Theme.paddingSmall
                    }
                },
                AnchorChanges {
                    target: button
                    anchors {
                        top: undefined
                        right: panel.right
                        horizontalCenter: undefined
                        bottom: nameField.bottom
                    }
                },
                PropertyChanges {
                    target: button
                    anchors.rightMargin: Theme.horizontalPageMargin
                }
            ]
        }
    ]
}
