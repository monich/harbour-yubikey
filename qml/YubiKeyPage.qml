import QtQuick 2.0
import Sailfish.Silica 1.0
import org.nemomobile.notifications 1.0
import harbour.yubikey 1.0

import "harbour"

Page {
    id: thisPage

    showNavigationIndicator: false

    property alias yubiKeyId: yubiKey.yubiKeyId
    property alias yubiKeyPresent: yubiKey.present
    property alias totpTimeLeft: yubiKey.totpTimeLeft
    property alias favoriteName: otpListModel.favoriteName
    property alias favoriteTokenType: otpListModel.favoriteTokenType
    property alias favoritePassword: otpListModel.favoritePassword
    property alias favoriteMarkedForRefresh: otpListModel.favoriteMarkedForRefresh
    readonly property bool yubiKeyAccessDenied: yubiKey.authAccess === YubiKeyCard.AccessDenied

    property bool _invalidPassword
    readonly property int _fullHeight: isPortrait ? Screen.height : Screen.width
    readonly property bool _authorized: yubiKey.authAccess === YubiKeyCard.AccessOpen ||
        yubiKey.authAccess === YubiKeyCard.AccessGranted

    signal replacePage(var cardId)
    signal yubiKeyReset()

    property bool _replaced

    onStatusChanged: {
        if (status === PageStatus.Active) {
            backNavigation = Qt.binding(function() { return !NfcAdapter.targetPresent })
            showNavigationIndicator = true
        }
    }

    // Otherwise width is changing with a delay, causing visible layout changes
    onIsLandscapeChanged: width = isLandscape ? Screen.height : Screen.width

    function replace() {
        if (!_replaced) {
            _replaced = true
            replacePage(yubiKeyId)
        }
    }

    function renameToken(tokenName) {
        var renamePage = pageStack.push("YubiKeyRenameTokenPage.qml", {
            "tokenList": otpListModel,
            "allowedOrientations": allowedOrientations,
            "currentName":  tokenName
        })
        renamePage.rename.connect(function(name) {
            renamePage.forwardNavigation = true
            if (yubiKey.renameToken(tokenName, name)) {
                replace()
            } else {
                pageStack.push(waitPageComponent, {
                    //: Status label
                    //% "Touch the same YubiKey to rename the token"
                    "text":  qsTrId("yubikey-wait-rename_token"),
                    "complete": function(page,keyState,waitId,success) {
                        if (!waitId || !success) {
                            page.waitForId(yubiKey.renameToken(tokenName, name))
                        } else {
                            page.tryAgain()
                        }
                    }
                }).waitDone.connect(replace)
            }
        })
    }

    YubiKeyCard {
        id: yubiKey

        readonly property bool canPerformPendingOperations: present &&
            yubiKeyState === YubiKeyCard.YubiKeyStateReady &&
            thisPage.status === PageStatus.Active

        onCanPerformPendingOperationsChanged: {
            if (canPerformPendingOperations) {
                deleteTokens(otpListModel.markedForDeletion)
                refreshTokens(otpListModel.markedForRefresh)
            }
        }
        onPasswordChanged: {
            // Check if a transition is already ongoing
            if (thisPage.status === PageStatus.Active ||
                thisPage.status === PageStatus.Inactive) {
                thisPage.replace()
            }
        }
        onPasswordRemoved: {
            // Check if a transition is already ongoing
            if (thisPage.status === PageStatus.Active ||
                thisPage.status === PageStatus.Inactive) {
                thisPage.replace()
            }
        }
        onAccessKeyNotAccepted: {
            // Don't do anything if the page is being deactivated
            if (thisPage.status !== PageStatus.Deactivating) {
                _invalidPassword = true
            }
        }
        onYubiKeyReset: thisPage.yubiKeyReset()
        onTotpCodesExpired: otpListModel.totpCodesExpired()
        onTokenRenamed: otpListModel.tokenRenamed(from, to)
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
        id: warningDialogComponent

        WarningDialog {
            allowedOrientations: thisPage.allowedOrientations
            //: Warning dialog title
            //% "Warning"
            title: qsTrId("yubikey-warning-title")
        }
    }

    Component {
        id: waitPageComponent

        YubiKeyWaitPage {
            allowedOrientations: thisPage.allowedOrientations
            yubiKeyId: thisPage.yubiKeyId
        }
    }

    Component {
        id: addTokenDialogComponent

        YubiKeyTokenDialog {
            allowedOrientations: thisPage.allowedOrientations
            yubiKeyId: thisPage.yubiKeyId
            //: Dialog title
            //% "Add token"
            dialogTitle: qsTrId("yubikey-add_token-title")
            //: Dialog button
            //% "Save"
            acceptText: qsTrId("yubikey-add_token-save")
            acceptDestinationAction: PageStackAction.Push
            acceptDestination: waitPageComponent
            acceptDestinationProperties: {
                "destinationPage": thisPage,
                //: Status label
                //% "Touch YubiKey to save the token"
                "text":  qsTrId("yubikey-wait-put_token"),
                "complete": function(page,keyState,waitId,success) {
                    if (keyState === YubiKeyCard.YubiKeyStateReady) {
                        if (success) {
                            pageStack.pop(thisPage)
                        } else {
                            var dialog = pageStack.previousPage(page)
                            page.waitForId(yubiKey.putToken(dialog.type,
                                dialog.algorithm, dialog.label, dialog.secret,
                                dialog.digits, dialog.counter ? dialog.counter : 0))
                        }
                    } else {
                        page.tryAgain()
                    }
                }
            }
        }
    }

    SilicaFlickable {
        id: pageFlickable

        width: parent.width
        height: _fullHeight
        interactive: pulleyMenu.visible

        PullDownMenu {
            id: pulleyMenu

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
                enabled: !NfcAdapter.targetPresent
                onEnabledChanged: pulleyMenu.updateVisibility()
                onClicked: {
                    pageStack.push(warningDialogComponent, {
                        //: Warning text
                        //% "Resetting the YubiKey irreversibly removes all secrets stored in it. This action cannot be undone. Are you sure that you really want to do it?"
                        "text": qsTrId("yubikey-warning-reset_text"),
                        "acceptDestinationAction": PageStackAction.Replace,
                        "acceptDestination": waitPageComponent,
                        "acceptDestinationProperties": {
                            //: Status label
                            //% "Touch the same YubiKey to reset it"
                            "text": qsTrId("yubikey-status-waiting_to_reset"),
                            "complete": function(page,keyState,waitId,success) {
                                // When reset succeeds, YubiKeyRecognizer from
                                // MainPage is expected to replace YubiKeyPage
                                // when it detects YubiKey ID change
                                if (!waitId || !success) {
                                    page.waitForId(yubiKey.reset())
                                } else {
                                    page.tryAgain()
                                }
                            }
                        }
                    })
                }
            }

            MenuItem {
                id: clearPasswordMenuItem

                //: Pulley menu item
                //% "Clear password"
                text: qsTrId("yubikey-menu-clear_password")
                enabled: !NfcAdapter.targetPresent && yubiKey.authAccess === YubiKeyCard.AccessGranted
                onEnabledChanged: pulleyMenu.updateVisibility()
                onClicked: {
                    pageStack.push(waitPageComponent, {
                        //: Status label
                        //% "Touch the same YubiKey to clear the password"
                        "text":  qsTrId("yubikey-wait-clear_password"),
                        "complete": function(page,keyState,waitId,success) {
                            if (keyState === YubiKeyCard.YubiKeyStateUnauthorized) {
                                _invalidPassword = true
                            } else if (!success) {
                                page.waitForId(yubiKey.setPassword(""))
                            }
                        }
                    })
                }
            }

            MenuItem {
                id: setPasswordMenuItem

                text: yubiKey.authAccess === YubiKeyCard.AccessGranted ?
                    //: Pulley menu item
                    //% "Change password"
                    qsTrId("yubikey-menu-change_password") :
                    //: Pulley menu item
                    //% "Set password"
                    qsTrId("yubikey-menu-set_password")
                enabled: _authorized
                onEnabledChanged: pulleyMenu.updateVisibility()
                onClicked: pageStack.push("YubiKeyEnterPasswordPage.qml", {
                    "yubiKeyId": yubiKeyId,
                    "allowedOrientations": allowedOrientations,
                    "destinationPage": thisPage,
                    "changingPassword":  yubiKey.authAccess === YubiKeyCard.AccessGranted,
                })
            }

            MenuItem {
                id: addTokenMenuItem

                //: Pulley menu item
                //% "Add token"
                text: qsTrId("yubikey-menu-add_token")
                enabled: !NfcAdapter.targetPresent && _authorized
                onEnabledChanged: pulleyMenu.updateVisibility()
                onClicked: {
                    var page = pageStack.push("ScanPage.qml", {
                        "allowedOrientations": allowedOrientations
                    })
                    page.skip.connect(function() {
                        pageStack.push(addTokenDialogComponent)
                    })
                    page.tokenDetected.connect(function(token) {
                        pageStack.push(addTokenDialogComponent, {
                            "type": token.type,
                            "algorithm": token.algorithm,
                            "label": token.label,
                            "secret": token.secret,
                            "digits": token.digits,
                            "counter": token.counter
                        })
                    })
                    page.tokensDetected.connect(function(model) {
                        pageStack.push("YubiKeyImportDialog.qml", {
                            "allowedOrientations": allowedOrientations,
                            "model": model,
                            "acceptDestinationAction": PageStackAction.Push,
                            "acceptDestination": waitPageComponent,
                            "acceptDestinationProperties": {
                                "destinationPage": thisPage,
                                //: Status label
                                //% "Touch YubiKey to save the selected tokens"
                                "text":  qsTrId("yubikey-wait-put_selected_tokens"),
                                "complete": function(page,keyState,waitId,success) {
                                    if (keyState === YubiKeyCard.YubiKeyStateReady) {
                                        if (success) {
                                            pageStack.pop(thisPage)
                                        } else {
                                            var dialog = pageStack.previousPage(page)
                                            page.waitForId(yubiKey.putTokens(dialog.selectedTokens))
                                        }
                                    } else {
                                        page.tryAgain()
                                    }
                                }
                            }
                        })
                    })
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
                timeLeft: otpListModel.haveExpiringTotpCodes ? yubiKey.totpTimeLeft : 0
                visible: !yubiKeyAccessDenied
            }

            HarbourHighlightIcon {
                source: "images/yubikey-lock.svg"
                sourceSize.width: Theme.iconSizeExtraLarge
                anchors.centerIn: parent
                visible: (yubiKeyAccessDenied && isLandscape)
            }
        }

        SilicaFlickable {
            id: contentFlickable

            contentHeight: authDataColumn.y + authDataColumn.height + Theme.paddingLarge
            interactive: !yubiKeyAccessDenied
            clip: isPortrait
            anchors {
                bottom: parent.bottom
                right: parent.right
            }

            Column {
                id: authDataColumn

                width: parent.width
                visible: opacity > 0
                opacity: yubiKeyAccessDenied ? 0 : 1

                SilicaListView {
                    id: otpList

                    model: YubiKeyAuthListModel {
                        id: otpListModel

                        yubiKeyId: yubiKey.yubiKeyId
                        authList: yubiKey.yubiKeyOtpList
                        authData: yubiKey.yubiKeyOtpData
                        refreshableTokens: yubiKey.refreshableTokens
                    }
                    width: parent.width
                    height: Math.max(contentHeight, contentFlickable.height -
                        authDataColumn.y - (headerItem.visible ?  headerItem.height : 0) -
                        cardInfoColumn.height - 2 * Theme.paddingLarge)

                    header: Component {
                        ListSeparator {
                            visible: otpList.count > 0
                        }
                    }

                    delegate: YubiKeyAuthListItem {
                        id: delegate

                        readonly property string itemName: model.name
                        readonly property string itemPassword: model.password
                        readonly property bool itemFavorite: model.favorite
                        readonly property bool itemCanBeSteamToken: model.type === YubiKeyCard.TypeTOTP
                        readonly property bool itemMarkedForRefresh: model.markedForRefresh
                        readonly property bool itemMarkedForDeletion: model.markedForDeletion
                        readonly property bool itemCanCopyPassword: !itemMarkedForRefresh && !itemMarkedForDeletion && itemPassword !== ""
                        readonly property bool itemCanRename: !itemMarkedForRefresh && !itemMarkedForDeletion &&
                            yubiKey.yubiKeyVersion >= YubiKeyCard.Version_5_3_0

                        landscape: thisPage.isLandscape
                        name: itemName
                        type: model.type
                        password: itemPassword
                        steam: model.steam
                        favorite: itemFavorite
                        expired: model.expired
                        refreshable: model.refreshable
                        markedForRefresh: itemMarkedForRefresh
                        markedForDeletion: itemMarkedForDeletion
                        totpValid: yubiKey.totpValid
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
                                    onClicked: renameToken(itemName)
                                }
                                MenuItem {
                                    id: deleteMenuItem

                                    //: Generic menu item
                                    //% "Delete"
                                    text: qsTrId("yubikey-menu-delete")
                                    onEnabledChanged: contextMenu.updateVisibility()
                                    enabled: !delegate.itemMarkedForDeletion
                                    onClicked: delegate.markForDeletion()
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
                                    onClicked: delegate.cancelMarks()
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

                        onRequestRefresh: markForRefresh()

                        onCancel: cancelMarks()

                        onClicked: {
                            if (copyPassword()) {
                                clipboardNotification.publish()
                                buzz.play()
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

                        function cancelMarks() {
                            model.markedForRefresh = false
                            model.markedForDeletion = false
                        }

                        function markForRefresh() {
                            model.markedForRefresh = true
                        }

                        function markForDeletion() {
                            model.markedForDeletion = true
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

                VerticalPadding{ }

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
                        text: qsTrId("yubikey-info-serial").arg(yubiKey.yubiKeySerial)
                    }

                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        visible: yubiKey.yubiKeyVersionString !== ""
                        //: Card info label
                        //% "Version: %1"
                        text: qsTrId("yubikey-info-version").arg(yubiKey.yubiKeyVersionString)
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

        Loader {
            id: passwordInputLoader

            active: yubiKeyAccessDenied
            anchors {
                left: contentFlickable.left
                right: contentFlickable.right
                top: parent.top
                bottom: parent.bottom
            }

            sourceComponent: Component {
                YubiKeyAuthorizeView {
                    isLandscape: thisPage.isLandscape
                    invalidPassword: _invalidPassword
                    yubiKeyPresent: yubiKey.present
                    contentHeight: thisPage.height
                    onEnterPassword: {
                        if (yubiKey.submitPassword(password, savePassword)) {
                            if (yubiKey.present) {
                                thisPage.replace()
                            } else {
                                var yubiKeyPage = thisPage
                                var page = pageStack.push(waitPageComponent, {
                                    //: Status label
                                    //% "Touch the same YubiKey to validate the password"
                                    "text": qsTrId("yubikey-status-waiting_to_authorize"),
                                    "complete": function(page,keyState,waitId,success) {
                                        switch (keyState) {
                                        case YubiKeyCard.YubiKeyStateReady:
                                            yubiKeyPage.replace()
                                            break
                                        case YubiKeyCard.YubiKeyStateUnauthorized:
                                            page.backNavigation = true
                                            _invalidPassword = true
                                            break
                                        }
                                    }
                                })
                                page.statusChanged.connect(function() {
                                    if (page.status === PageStatus.Active) {
                                        _invalidPassword = false
                                    }
                                })
                            }
                        }
                    }
                }
            }
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
                    height: yubiKeyAccessDenied ? yubiKeyIconContainer.maxHeight :
                        Math.max(Math.min(yubiKeyIconContainer.maxHeight, thisPage.height - contentFlickable.contentHeight), yubiKeyIconContainer.minHeight)
                },
                PropertyChanges {
                    target: authDataColumn
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
                    height: thisPage.height
                },
                PropertyChanges {
                    target: authDataColumn
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
