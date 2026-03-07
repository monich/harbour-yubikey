import QtQuick 2.0
import Sailfish.Silica 1.0
import org.nemomobile.notifications 1.0
import harbour.yubikey 1.0

import "harbour"

Page {
    id: thisPage

    property var yubiKey
    property string yubiKeyId
    property string yubiKeySerial
    property string yubiKeyFirmware

    readonly property bool yubiKeyPresent: yubiKey.present
    readonly property int totpTimeLeft: yubiKey.totpTimeLeft
    readonly property string favoriteName: otpListModel.favoriteName
    readonly property int favoriteTokenType: otpListModel.favoriteTokenType
    readonly property string favoritePassword: otpListModel.favoritePassword
    readonly property bool favoriteMarkedForRefresh: otpListModel.favoriteMarkedForRefresh

    showNavigationIndicator: false

    property bool _resetPopupShown
    readonly property int _fullHeight: isPortrait ? Screen.height : Screen.width
    readonly property bool _authorized: yubiKey.authAccess === YubiKey.AccessOpen ||
        yubiKey.authAccess === YubiKey.AccessGranted

    // Otherwise width is changing with a delay, causing visible layout changes
    onIsLandscapeChanged: width = isLandscape ? Screen.height : Screen.width

    onStatusChanged: {
        if (status === PageStatus.Active) {
            backNavigation = Qt.binding(function() { return !yubiKeyPresent})
            showNavigationIndicator = true
            if (yubiKey.haveBeenReset && !_resetPopupShown) {
                _resetPopupShown = true
                //: Pop-up notification
                //% "YubiKey has been reset"
                _showSuccessPopup(qsTrId("yubikey-notification-reset"))
            }
        }
    }

    function isOk() {
        return yubiKey.yubiKeyId === yubiKeyId &&
            yubiKey.authAccess !== YubiKey.AccessNotActivated &&
            yubiKey.authAccess !== YubiKey.AccessDenied
    }

    function _returnToYubiKeyPage() {
        returnToYubiKeyPageRequest.schedule()
    }

    function _showSuccessPopup(text) {
        popupPanel.iconSource = "images/yubikey-ok.svg"
        popupPanel.text = text
        popupPanel.show(false)
    }

    function _showQuestionPopup(text) {
        popupPanel.iconSource = "images/yubikey-question.svg"
        popupPanel.text = text
        popupPanel.show(false)
    }

    function _showErrorPopup(text) {
        popupPanel.iconSource = "images/yubikey-error.svg"
        popupPanel.text = text
        popupPanel.show(false)
    }

    function _resetYubiKey() {
        pageStack.push("WarningDialog.qml", {
            //: Warning text
            //% "Resetting the YubiKey irreversibly removes all secrets stored in it. This action cannot be undone. Are you sure that you really want to do it?"
            "text": qsTrId("yubikey-warning-reset_text"),
            "allowedOrientations": thisPage.allowedOrientations,
            "acceptDestinationAction": PageStackAction.Replace,
            "acceptDestination": waitOpPageComponent,
            "acceptDestinationProperties": {
                //: Status label
                //% "Touch the same YubiKey to reset it"
                "text": qsTrId("yubikey-status-waiting_to_reset")
            }
        }).accepted.connect(function() {
            // There's no need to do anything when the operation is finished.
            // If reset succeeds, the YubiKey ID will change and YubiKeyPage
            // will get replaced with the new instance. Let's hope that it works)
            var op = yubiKey.reset()
            if (!yubiKeyPresent) {
                pageStack.replaceAbove(thisPage, waitOpPageComponent, {
                    //: Status label
                    //% "Touch the same YubiKey to reset it"
                    "text": qsTrId("yubikey-status-waiting_to_reset"),
                    "op": op})
            }
        })
    }

    function _clearPasswordAfterTouch()
    {
        var op = yubiKey.clearPassword()
        pageStack.push(waitOpPageComponent, {
            //: Status label
            //% "Touch the same YubiKey to clear the password"
            "text":  qsTrId("yubikey-wait-clear_password"),
            "op": op
        }).opFinished.connect(_returnToYubiKeyPage)
        op.opFinished.connect(function (result) {
            if (result === YubiKey.Success) {
                //: Pop-up notification
                //% "YubiKey password has been removed"
                _showSuccessPopup(qsTrId("yubikey-popup-clear_password_success"))
            }
        })
    }

    function _clearPassword()
    {
        if (yubiKeyPresent) {
            var remorsePopup = remorsePopupComponent.createObject(thisPage)
            remorsePopup.canceled.connect(remorsePopup.destroy)
            remorsePopup.triggered.connect(function() {
                if (yubiKeyPresent) {
                    yubiKey.clearPassword().opFinished.connect(function (result) {
                        if (result === YubiKey.Success) {
                            //: Pop-up notification
                            //% "YubiKey password has been removed"
                            _showSuccessPopup(qsTrId("yubikey-popup-clear_password_success"))
                        }
                    })
                } else {
                    _clearPasswordAfterTouch()
                }
                remorsePopup.destroy()
            })
            //: Remorse popup text
            //% "Clearing YubiKey password"
            remorsePopup.execute(qsTrId("yubikey-remorse-clearing_password"));
        } else {
            _clearPasswordAfterTouch()
        }
    }

    function _setPassword(enterPrompt, confirmPrompt, touchPrompt, successMsg)
    {
        pageStack.push("YubiKeyEnterPasswordDialog.qml", {
            "allowedOrientations": allowedOrientations,
            "enterPrompt": enterPrompt,
            "confirmPrompt": confirmPrompt,
            "destinationAction": yubiKeyPresent  ? PageStackAction.Pop : PageStackAction.Push,
            "destination": yubiKeyPresent ? thisPage : waitOpPageComponent,
            "destinationProperties": yubiKeyPresent ? {} : { "text": touchPrompt }
        }).passwordAccepted.connect(function(dialog) {
            var op = yubiKey.setPassword(dialog.password)

            // if the destination page has the 'op' property, assume that
            // it's an instance of YubiKeyWaitOpPage
            var dest = dialog.acceptDestinationInstance
            if (dest && 'op' in dest) {
                dest.op = op
                dest.opFinished.connect(_returnToYubiKeyPage)
            }

            op.opFinished.connect(function (result) {
                if (result === YubiKey.Success) {
                    _showSuccessPopup(successMsg)
                }
            })
        })
    }

    function _createPassword()
    {
            //: Input prompt (there was no password, creating one)
            //% "Enter password for this YubiKey"
        _setPassword(qsTrId("yubikey-info-enter_password-set_prompt"),
            //: Input prompt
            //% "Please type in your YubiKey password one more time"
            qsTrId("yubikey-confirm_password-prompt-set"),
            //: Status label
            //% "Touch the same YubiKey to set the password"
            qsTrId("yubikey-wait-set_password"),
            //: Pop-up notification
            //% "YubiKey has been password protected"
            qsTrId("yubikey-popup-set_password_success"))
    }

    function _changePassword()
    {
            //: Input prompt (existing password is being replaced with a new one)
            //% "Enter new password for this YubiKey"
        _setPassword(qsTrId("yubikey-info-enter_password-change_prompt"),
            //: Input prompt
            //% "Please type in your new YubiKey password one more time"
            qsTrId("yubikey-confirm_password-prompt-change"),
            //: Status label
            //% "Touch the same YubiKey to change the password"
            qsTrId("yubikey-wait-change_password"),
            //: Pop-up notification
            //% "YubiKey password has been changed"
            qsTrId("yubikey-popup-change_password_success"))
    }

    function _errorText(code) {
        switch (code) {
        case YubiKey.Success:
            return "OK" // Not really being used, no need to localize
        case YubiKey.ErrorNoSpace:
            //: Error message (No space left on YubiKey)
            //% "No space left"
            return qsTrId("yubikey-error-no_space")
        }
        return code.toString(16)
    }

    function _putToken(props) {
        pageStack.push(editTokenDialogComponent, props).tokenAccepted.connect(function(dialog) {
            var label = dialog.label
            var op = yubiKey.putToken(dialog.type, dialog.algorithm, label,
                dialog.secret, dialog.digits, dialog.counter || 0)

            // if the destination page has the 'op' property, assume that
            // it's an instance of YubiKeyWaitOpPage
            var dest = dialog.acceptDestinationInstance
            if (dest && 'op' in dest) {
                dest.op = op
                dest.opFinished.connect(_returnToYubiKeyPage)
            }

            op.opFinished.connect(function (result) {
                if (result === YubiKey.Success) {
                    //: Pop-up notification (%1 is the token label)
                    //% "Saved %1"
                    _showSuccessPopup(qsTrId("yubikey-popup-put_token_success").arg(label))
                } else {
                    //: Pop-up notification (%1 is the token label, %2 is the error message/code)
                    //% "Failed to save %1 (%2)"
                    _showErrorPopup(qsTrId("yubikey-popup-put_token_error").arg(label).arg(_errorText(result)))
                }
            })
        })
    }

    function _putTokens(model) {
        var dialog = pageStack.push("YubiKeyImportDialog.qml", {
            "allowedOrientations": allowedOrientations,
            "model": model,
            "acceptDestinationAction": PageStackAction.Push,
            "acceptDestination": putTokensPageComponent
        })
        dialog.accepted.connect(function() {
            var dest = dialog.acceptDestinationInstance
            dest.opsFinished.connect(function(result) {
                if (result === YubiKey.Success) {
                    //: Pop-up notification
                    //% "Tokens saved"
                    _showSuccessPopup(qsTrId("yubikey-popup-put_tokens_success"))
                } else {
                    //: Pop-up notification (%1 is the error message/code)
                    //% "Failed to save one or more tokens (%1)"
                    _showErrorPopup(qsTrId("yubikey-popup-put_tokens_error").arg(_errorText(result)))
                }
                _returnToYubiKeyPage()
            })
            dest.opIds = yubiKey.putTokens(model.selectedTokens)
        })
    }

    Connections {
        target: thisPage.yubiKey
        onInvalidYubiKeyConnected: {
            //: Wait page popup (the touched YubiKey is not the one we are waiting for)
            //% "Wrong YubiKey"
            _showQuestionPopup(qsTrId("yubikey-popup-wrong_touch"))
        }
    }

    PageTransitionRequest {
        id: returnToYubiKeyPageRequest

        onExecute: pageStack.pop(thisPage)
    }

    Buzz {
        id: buzz
    }

    Notification {
        id: clipboardNotification

        //: Pop-up notification
        //% "Password copied to clipboard"
        previewBody: qsTrId("yubikey-notification-copied_to_clipboard")
        expireTimeout: 2000
        Component.onCompleted: {
            if ("icon" in clipboardNotification) {
                clipboardNotification.icon = "icon-s-clipboard"
            }
        }
    }

    Component {
        id: remorsePopupComponent

        RemorsePopup { }
    }

    Component {
        id: editTokenDialogComponent

        YubiKeyEditTokenDialog {
            allowedOrientations: thisPage.allowedOrientations
            acceptDestinationAction: yubiKeyPresent  ? PageStackAction.Pop : PageStackAction.Push
            acceptDestination: yubiKeyPresent ? thisPage : waitOpPageComponent
            //: Status label
            //% "Touch the same YubiKey to save the token"
            acceptDestinationProperties: { "text":  qsTrId("yubikey-wait-put_token") }
        }
    }

    Component {
        id: waitOpPageComponent

        YubiKeyWaitOpPage {
            id: waitOpPage

            allowedOrientations: thisPage.allowedOrientations
            yubiKeyPresent: thisPage.yubiKeyPresent

            Connections {
                target: thisPage.yubiKey
                onInvalidYubiKeyConnected: waitOpPage.invalidYubiKeyConnected()
            }
        }
    }

    Component {
        id: putTokensPageComponent

        YubiKeyWaitOpsPage {
            id: putTokensPage

            allowedOrientations: thisPage.allowedOrientations
            yubiKey: thisPage.yubiKey
            text: yubiKeyPresent ?
                //: Status label
                //% "Saving the tokens..."
                qsTrId("yubikey-wait-saving_tokens") :
                //: Status label
                //% "Touch the same YubiKey to save the selected tokens"
                qsTrId("yubikey-wait-put_selected_tokens")

        }
    }

    SilicaFlickable {
        id: pageFlickable

        width: parent.width
        height: _fullHeight
        interactive: !popupPanel.expanded

        PullDownMenu {
            id: pulleyMenu

            visible: pageFlickable.interactive

            Component.onCompleted: updateVisibility()
            onActiveChanged: updateVisibility()

            function updateVisibility() {
                if (!active) {
                    resetMenuItem.visible = resetMenuItem.enabled
                    clearPasswordMenuItem.visible = clearPasswordMenuItem.enabled
                    setPasswordMenuItem.visible = setPasswordMenuItem.enabled
                    addTokenMenuItem.visible = addTokenMenuItem.enabled
                }
            }

            MenuItem {
                id: resetMenuItem

                //: Pulley menu item
                //% "Reset the key"
                text: qsTrId("yubikey-menu-reset")
                onEnabledChanged: pulleyMenu.updateVisibility()
                onClicked: _resetYubiKey()
            }

            MenuItem {
                id: clearPasswordMenuItem

                //: Pulley menu item
                //% "Clear password"
                text: qsTrId("yubikey-menu-clear_password")
                enabled: yubiKey.authAccess === YubiKey.AccessGranted
                onEnabledChanged: pulleyMenu.updateVisibility()
                onClicked: _clearPassword()
            }

            MenuItem {
                id: setPasswordMenuItem

                text: yubiKey.authAccess === YubiKey.AccessGranted ?
                    //: Pulley menu item
                    //% "Change password"
                    qsTrId("yubikey-menu-change_password") :
                    //: Pulley menu item
                    //% "Set password"
                    qsTrId("yubikey-menu-set_password")
                enabled: _authorized
                onEnabledChanged: pulleyMenu.updateVisibility()
                onClicked: {
                    if (yubiKey.authAccess === YubiKey.AccessGranted) {
                        _changePassword()
                    } else {
                        _createPassword()
                    }
                }
            }

            MenuItem {
                id: addTokenMenuItem

                //: Pulley menu item
                //% "Add token"
                text: qsTrId("yubikey-menu-add_token")
                enabled: _authorized
                onEnabledChanged: pulleyMenu.updateVisibility()
                onClicked: {
                    var page = pageStack.push("ScanPage.qml", {
                        "allowedOrientations": allowedOrientations
                    })
                    page.skip.connect(function() { _putToken({}) })
                    page.tokenDetected.connect(function(token) {
                        _putToken({
                            "type": token.type,
                            "algorithm": token.algorithm,
                            "label": token.label,
                            "secret": token.secret,
                            "digits": token.digits,
                            "counter": token.counter
                        })
                    })
                    page.tokensDetected.connect(_putTokens)
                }
            }
        }

        Item {
            id: yubiKeyIconContainer

            readonly property real minHeight: yubiKeyIcon.height + 4 * Theme.paddingLarge
            readonly property real maxHeight: yubiKeyIcon.height + 2 * Theme.itemSizeLarge

            YubiKeyProgress {
                id: yubiKeyIcon

                anchors.centerIn: parent
                timeLeft: yubiKey.haveTotpCodes ? yubiKey.totpTimeLeft : 0
            }
        }

        SilicaFlickable {
            id: contentFlickable

            contentHeight: content.y + content.height + content.spacing
            clip: true
            anchors {
                bottom: parent.bottom
                right: parent.right
            }

            Column {
                id: content

                width: parent.width
                spacing: Theme.paddingLarge

                SilicaListView {
                    id: otpList

                    model: YubiKeyOtpListModel {
                        id: otpListModel

                        yubiKey: thisPage.yubiKey
                    }

                    width: parent.width
                    height: Math.max(contentHeight, contentFlickable.height -
                        content.y - (headerItem.visible ?  headerItem.height : 0) -
                        cardInfoColumn.height - 2 * content.spacing)

                    header: Component {
                        ListSeparator {
                            visible: otpList.count > 0
                        }
                    }

                    delegate: YubiKeyOtpListItem {
                        id: delegate

                        readonly property string itemName: model.name
                        readonly property string itemPassword: model.password
                        readonly property bool itemFavorite: model.favorite
                        readonly property bool itemCanBeSteamToken: model.type === YubiKey.TypeTOTP
                        readonly property bool itemMarkedForRefresh: model.entryOp === YubiKeyOtpListModel.EntryOpRefresh
                        readonly property bool itemMarkedForDeletion: model.entryOp === YubiKeyOtpListModel.EntryOpDelete
                        readonly property bool itemCanCopyPassword: !itemMarkedForRefresh && !itemMarkedForDeletion && itemPassword !== ""
                        readonly property bool itemCanRename: !itemMarkedForRefresh && !itemMarkedForDeletion &&
                            yubiKey.yubiKeyVersion >= YubiKey.Version_5_3_0

                        landscape: thisPage.isLandscape
                        name: itemName
                        newName: model.newName
                        type: model.type
                        entryOp: model.entryOp
                        password: itemPassword
                        steam: model.steam
                        favorite: itemFavorite
                        expired: model.type === YubiKey.TypeTOTP && !totpTimeLeft
                        menu: Component {
                            ContextMenu {
                                id: contextMenu

                                readonly property bool menuExpanded: delegate.menuOpen

                                onHeightChanged: {
                                    // Make sure we are inside the screen area
                                    var flickable = contentFlickable
                                    var bottom = parent.mapToItem(flickable, x, y).y + height
                                    if (bottom > flickable.height) {
                                        flickable.contentY += bottom - flickable.height
                                    }
                                }

                                MenuItem {
                                    id: renameMenuItem

                                    //: Generic menu item
                                    //% "Rename"
                                    text: qsTrId("yubikey-menu-rename")
                                    onEnabledChanged: contextMenu.updateVisibility()
                                    enabled: delegate.itemCanRename
                                    onClicked: delegate.renameToken(itemName)
                                }
                                MenuItem {
                                    id: deleteMenuItem

                                    //: Generic menu item
                                    //% "Delete"
                                    text: qsTrId("yubikey-menu-delete")
                                    onEnabledChanged: contextMenu.updateVisibility()
                                    enabled: !delegate.itemMarkedForDeletion
                                    onClicked: delegate.deleteToken()
                                }
                                MenuItem {
                                    id: cancelMenuItem

                                    //: Context menu item
                                    //% "Cancel refresh"
                                    text: delegate.itemMarkedForRefresh ? qsTrId("yubikey-menu-cancel_refresh") :
                                        //: Context menu item
                                        //% "Cancel deletion"
                                        delegate.itemMarkedForDeletion ? qsTrId("yubikey-menu-cancel_delete") : ""

                                    onEnabledChanged: contextMenu.updateVisibility()
                                    enabled: delegate.itemMarkedForRefresh || delegate.itemMarkedForDeletion
                                    onClicked: otpListModel.cancelPendingOp(itemName)
                                }
                                MenuItem {
                                    id: favoriteMenuItem

                                    text: delegate.itemFavorite ?
                                        //: Context menu item
                                        //% "Remove from cover"
                                        qsTrId("yubikey-menu-remove_from_cover") :
                                        //: Context menu item
                                        //% "Show on cover"
                                        qsTrId("yubikey-menu-show_on_cover")
                                    onEnabledChanged: contextMenu.updateVisibility()
                                    enabled: !delegate.itemMarkedForDeletion
                                    onClicked: delegate.toggleFavorite()
                                }
                                MenuItem {
                                    id: steamMenuItem

                                    text: model.steam ?
                                        //: Context menu item
                                        //% "Standard token"
                                        qsTrId("yubikey-menu-use_as_standard_token") :
                                        //: Context menu item
                                        //% "Steam token"
                                        qsTrId("yubikey-menu-use_as_steam_token")
                                    onEnabledChanged: contextMenu.updateVisibility()
                                    enabled: delegate.itemCanBeSteamToken && !delegate.itemMarkedForDeletion
                                    onClicked: model.steam = !model.steam
                                }

                                Component.onCompleted: updateVisibility()
                                onMenuExpandedChanged: updateVisibility()

                                function updateVisibility() {
                                    if (!menuExpanded) {
                                        renameMenuItem.visible = renameMenuItem.enabled
                                        deleteMenuItem.visible = deleteMenuItem.enabled
                                        cancelMenuItem.visible = cancelMenuItem.enabled
                                        favoriteMenuItem.visible = favoriteMenuItem.enabled
                                        steamMenuItem.visible = steamMenuItem.enabled
                                    }
                                }
                            }
                        }

                        onRequestRefresh: otpListModel.refreshToken(itemName)

                        onCancel: otpListModel.cancelPendingOp(itemName)

                        onClicked: {
                            if (copyPassword()) {
                                clipboardNotification.publish()
                                buzz.play()
                            }
                        }

                        function renameToken(from) {
                            pageStack.push("YubiKeyRenameTokenDialog.qml", {
                                "otpModel": otpListModel,
                                "allowedOrientations": allowedOrientations,
                                "acceptDestinationAction": PageStackAction.Replace,
                                "currentName":  from
                            }).rename.connect(function(to) {
                                otpListModel.renameToken(from, to)
                            })
                        }

                        function deleteToken() {
                            if (yubiKeyPresent) {
                                //: Remorse popup text
                                //% "Deleting"
                                remorseAction(qsTrId("yubikey-menu-delete_remorse"), function() {
                                    otpListModel.deleteToken(itemName)
                                })
                            } else {
                                otpListModel.deleteToken(itemName)
                            }
                        }

                        function copyPassword() {
                            if (itemCanCopyPassword) {
                                Clipboard.text = itemPassword
                                return true
                            } else {
                                return false
                            }
                        }

                        function toggleFavorite() {
                            model.favorite = !model.favorite
                        }

                        ListView.onAdd: AddAnimation { target: delegate }
                        ListView.onRemove: RemoveAnimation { target: delegate }
                    }

                    InfoLabel {
                        //: Card info label
                        //% "No credentials are stored on this YubiKey"
                        text: qsTrId("yubikey-info-no_creds")
                        visible: !otpList.count && yubiKey.otpListFetched
                        anchors {
                            verticalCenter: parent.verticalCenter
                            verticalCenterOffset:  isLandscape ? 0 : (-Theme.itemSizeHuge/2)
                        }
                    }
                }

                Column {
                    id: cardInfoColumn

                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x

                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        visible: yubiKey.yubiKeySerial !== 0
                        //: Card info label
                        //% "Serial: %1"
                        text: qsTrId("yubikey-info-serial").arg(thisPage.yubiKeySerial)
                    }

                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        visible: yubiKey.yubiKeyVersionString !== ""
                        //: Card info label
                        //% "Firmware: %1"
                        text: qsTrId("yubikey-info-firmware").arg(thisPage.yubiKeyFirmware)
                    }

                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        text: yubiKeyId
                    }
                }
            }

            VerticalScrollDecorator { }
        }

        YubiKeyPopup {
            id: popupPanel

            autoHide: !yubiKeyPresent
        }
    }

    states: [
        State {
            name: "portrait"
            when: isPortrait
            changes: [
                PropertyChanges {
                    target: yubiKeyIconContainer
                    width: parent.width
                    height: Math.max(Math.min(yubiKeyIconContainer.maxHeight, _fullHeight - contentFlickable.contentHeight), yubiKeyIconContainer.minHeight)
                },
                PropertyChanges {
                    target: content
                    y: 0
                },
                AnchorChanges {
                    target: contentFlickable
                    anchors {
                        top: yubiKeyIconContainer.bottom
                        left: parent.left
                    }
                }
            ]
        },
        State {
            name: "landscape"
            when: !isPortrait
            changes: [
                PropertyChanges {
                    target: yubiKeyIconContainer
                    width: yubiKeyIcon.width + 2 * Theme.itemSizeLarge
                    height: _fullHeight
                },
                PropertyChanges {
                    target: content
                    y: Theme.paddingLarge
                },
                AnchorChanges {
                    target: contentFlickable
                    anchors {
                        top: parent.top
                        left: yubiKeyIconContainer.right
                    }
                }
            ]
        }
    ]
}
