import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

Dialog {
    id: thisDialog

    canAccept: ((type !== YubiKeyCard.TypeHOTP) || counterField.acceptableInput) && digitsField.acceptableInput && _acceptableSecret && label !== ""

    property string yubiKeyId
    property alias dialogTitle: header.title
    property alias acceptText: header.acceptText
    property int type: YubiKeyCard.TypeTOTP
    property int algorithm: YubiKeyCard.HMAC_SHA1
    property alias label: labelField.text
    property alias secret: secretField.text
    property alias digits: digitsField.text
    property alias counter: counterField.text

    signal tokenAccepted(var dialog)

    readonly property bool _acceptableSecret: YubiKeyUtil.isValidBase32(secret)

    onAccepted: tokenAccepted(thisDialog)

    SilicaFlickable {
        id: flickable

        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width

            DialogHeader { id: header }

            TextField {
                id: labelField

                width: parent.width
                //: Text field label (OTP label)
                //% "Name"
                label: qsTrId("yubikey-token-name-text")

                EnterKey.enabled: text.length > 0
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: secretField.focus = true
            }

            TextField {
              id: secretField

              width: parent.width
              //: Text field label (OTP secret)
              //% "Secret"
              label: qsTrId("yubikey-token-secret-text")
              //: Text field placeholder (OTP secret)
              //% "Secret OTP key"
              placeholderText: qsTrId("yubikey-token-secret-placeholder")
              errorHighlight: !_acceptableSecret

              EnterKey.enabled: text.length > 10
              EnterKey.iconSource: "image://theme/icon-m-enter-next"
              EnterKey.onClicked: digitsField.focus = true
            }


            Grid {
                columns: isLandscape ? 2 : 1
                width: parent.width

                readonly property real columnWidth: width/columns

                TextField {
                    id: digitsField

                    width: counterField.visible ? parent.columnWidth : parent.width
                    //: Text field label (number of password digits)
                    //% "Digits (leave it 6 for Steam tokens)"
                    label: qsTrId("yubikey-token-digits-text")
                    //: Text field placeholder (number of password digits)
                    //% "Number of password digits"
                    placeholderText: qsTrId("yubikey-token-digits-placeholder")
                    text: YubiKeyUtil.DefaultDigits
                    validator: IntValidator {
                        bottom: YubiKeyUtil.MinDigits
                        top: YubiKeyUtil.MaxDigits
                    }

                    EnterKey.iconSource: "image://theme/icon-m-enter-next"
                    EnterKey.onClicked: counterField.focus = true
                }

                Item {
                    width: parent.columnWidth
                    height: (type === YubiKeyCard.TypeHOTP) ? counterField.height : 0
                    visible: height > 0
                    clip: true

                    Behavior on height {
                        enabled: isPortrait

                        SmoothedAnimation { duration: 250 }
                    }

                    TextField {
                        id: counterField

                        width: parent.width
                        //: Text field label (HOTP counter value)
                        //% "Initial counter value"
                        label: qsTrId("yubikey-token-counter-text")
                        placeholderText: label
                        text: ""
                        validator: IntValidator {}

                        EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                        EnterKey.onClicked: parent.forceActiveFocus()
                    }
                }
            }

            Grid {
                columns: isLandscape ? 2 : 1
                width: parent.width

                readonly property real columnWidth: width/columns

                ComboBox {
                    id: typeComboBox

                    width: parent.columnWidth
                    //: Combo box label (OTP type)
                    //% "Type"
                    label: qsTrId("yubikey-token-type-label")
                    menu: ContextMenu {
                        x: 0
                        width: typeComboBox.width
                        //: Menu item for counter based token
                        //% "Counter-based (HOTP)"
                        MenuItem { text: qsTrId("yubikey-token-type-hotp") }
                        //: Menu item for time based token
                        //% "Time-based (TOTP)"
                        MenuItem { text: qsTrId("yubikey-token-type-totp") }
                    }
                    // YubiKeyTokenType enum is off by 1
                    Component.onCompleted: currentIndex = type - 1
                    onCurrentIndexChanged: type = currentIndex + 1
                }

                ComboBox {
                    id: algorithmComboBox

                    width: parent.columnWidth
                    //: Combo box label
                    //% "Digest algorithm"
                    label: qsTrId("yubikey-token-digest_algorithm-label")
                    menu: ContextMenu {
                        x: 0
                        width: algorithmComboBox.width
                        //: Menu item for the default digest algorithm
                        //% "%1 (default)"
                        MenuItem { text: qsTrId("yubikey-token-digest_algorithm-default").arg("SHA1") }
                        MenuItem { text: "SHA256" }
                        MenuItem { text: "SHA512" }
                    }
                    // YubiKeyAlgorithm enum is off by 1
                    Component.onCompleted: currentIndex = algorithm - 1
                    onCurrentIndexChanged: algorithm = currentIndex + 1
                }
            }
        }

        VerticalScrollDecorator { }
    }
}
