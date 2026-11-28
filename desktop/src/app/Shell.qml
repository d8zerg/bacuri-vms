pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Bacuri.Vms.Client.Ui

ApplicationWindow {
    id: win
    required property Workspace workspace
    readonly property alias navigation: nav
    readonly property OverviewContent overviewContent: overviewPage.item as OverviewContent
    property Component overviewTools
    property string overviewTitle: connected ? qsTr("Observation points") : qsTr("Site sign-in")
    readonly property Flickable viewport: scroll.contentItem as Flickable
    signal newWindow
    property string draftAddress: workspace.address
    PointFilterModel {
        id: pointProxy
        sourceModel: win.workspace.points
    }
    CommandRegistry {
        id: commands
        Component.onCompleted: attach(win, nav, win.workspace)
        onTriggered: id => {
            switch (id) {
            case "new":
                win.newWindow();
                break;
            case "close":
                win.close();
                break;
            case "back":
                win.back();
                break;
            case "settings":
                if (nav.page === Navigation.Settings)
                    win.back();
                else
                    win.settings();
                break;
            case "find":
                win.overviewContent?.filterPanel.focusSearch();
                break;
            }
        }
    }
    onActiveFocusItemChanged: {
        const target = activeFocusItem;
        if (!target || !viewport)
            return;
        for (let ancestor = target.parent; ancestor; ancestor = ancestor.parent) {
            if (ancestor !== viewport.contentItem)
                continue;
            const y = target.mapToItem(ancestor, 0, 0).y;
            if (y < viewport.contentY)
                viewport.contentY = y;
            else if (y + target.height > viewport.contentY + viewport.height)
                viewport.contentY = Math.max(0, y + Math.min(target.height, viewport.height) - viewport.height);
            break;
        }
    }
    readonly property int stationState: workspace.state
    readonly property bool connected: workspace.signedIn
    readonly property var selected: {
        workspace.points.version;
        return workspace.points.lookup(nav.pointId);
    }
    readonly property bool overview: nav.page === Navigation.Overview
    readonly property string section: nav.page === Navigation.Settings ? qsTr("Settings") : nav.page === Navigation.Point ? qsTr("Observation point") : overviewTitle

    width: 1000
    height: 720
    minimumWidth: 420
    minimumHeight: 350
    visible: true
    title: section + " | Bacuri VMS"
    color: Tokens.canvas

    Navigation {
        id: nav
    }
    function back() {
        if (overview)
            return;
        nav.back();
        restoreFocus();
    }
    function restoreFocus() {
        const focus = nav.focusTarget;
        if (overview && focus.role === "point" && overviewContent) {
            const list = overviewContent.list;
            list.currentIndex = pointProxy.rowFor(focus.id);
            if (list.currentIndex >= 0)
                list.positionViewAtIndex(list.currentIndex, ListView.Contain);
            list.forceActiveFocus(Qt.BacktabFocusReason);
        } else {
            const target = focus.id ? nav.findTarget(win.contentItem, focus.id) : null;
            (target && target.visible ? target : settingsButton).forceActiveFocus(Qt.BacktabFocusReason);
        }
    }
    function currentFocusTarget():var {
        const list = overviewContent?.list
        if (overview && list && activeFocusItem === list && list.currentIndex >= 0)
            return {id: pointProxy.idAt(list.currentIndex), role: "point"}
        return {id: activeFocusItem?.objectName || (overview ? "settings" : "back"), role: "control"}
    }
    function saveNavigation():var { return nav.save(currentFocusTarget()) }
    function settings() {
        if (nav.page === Navigation.Settings)
            return;
        nav.push(Navigation.Settings, "", currentFocusTarget());
        backButton.forceActiveFocus(Qt.TabFocusReason);
        viewport.contentY = 0;
    }
    function openPoint(id) {
        nav.push(Navigation.Point, id, {
            id: id,
            role: "point"
        });
        backButton.forceActiveFocus(Qt.TabFocusReason);
        viewport.contentY = 0;
    }

    Connections {
        target: win.workspace
        function onChanged() {
            if (!win.workspace.signedIn) {
                win.overviewContent?.filterPanel.reset();
                if (nav.containsPoint)
                    nav.reset();
            }
        }
    }
    Connections {
        target: win.workspace.points
        function onStatusChanged(id, name, connection, recording) {
            if (!win.active || nav.page === Navigation.Settings || (nav.page === Navigation.Point && nav.pointId !== id))
                return;
            const parts = [name];
            if (connection)
                parts.push(StatusText.describe(connection, false));
            if (recording)
                parts.push(StatusText.describe(recording, true));
            scroll.announceStatus(parts.join(". "));
        }
    }
    header: Rectangle {
        implicitHeight: headerRow.implicitHeight + 2 * Tokens.space2
        color: Tokens.surface
        RowLayout {
            id: headerRow
            anchors.fill: parent
            anchors.margins: Tokens.space2
            spacing: Tokens.space2
            Image {
                source: "qrc:/design/brand/mark.svg"
                readonly property int rasterExtent: Math.ceil(Tokens.scaled(32) * Screen.devicePixelRatio)
                sourceSize: Qt.size(rasterExtent, rasterExtent)
                Layout.preferredWidth: Tokens.scaled(32)
                Layout.preferredHeight: Tokens.scaled(32)
                Accessible.role: Accessible.Graphic
                Accessible.name: "Bacuri VMS"
                Accessible.ignored: brand.visible
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: brand.implicitHeight
                KitLabel {
                    id: brand
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Bacuri VMS"
                    font.weight: Font.DemiBold
                    typeSize: Tokens.textPanelTitle
                    wrapMode: Text.NoWrap
                    visible: implicitWidth <= parent.width
                }
            }
            KitButton {
                objectName: "newWindow"
                text: commands.entries["new"].name
                symbol: commands.entries["new"].icon
                onClicked: commands.invoke("new")
            }
            KitButton {
                id: settingsButton
                objectName: "settings"
                text: commands.entries["settings"].name
                symbol: commands.entries["settings"].icon
                checked: nav.page === Navigation.Settings
                onClicked: commands.invoke("settings")
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Tokens.line
        }
    }

    ScrollView {
        id: scroll
        function announceStatus(message: string) {
            Accessible.announce(message);
        }
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            x: Math.max(Tokens.space3, (scroll.availableWidth - width) / 2)
            readonly property real preferredWidth: 880
            width: Math.min(preferredWidth, scroll.availableWidth - 2 * Tokens.space3)
            spacing: Tokens.space3
            Item {
                Layout.preferredHeight: Tokens.space1
            }
            KitButton {
                id: backButton
                objectName: "back"
                visible: !win.overview
                text: commands.entries["back"].name
                symbol: commands.entries["back"].icon
                onClicked: commands.invoke("back")
            }
            KitLabel {
                Layout.fillWidth: true
                text: win.section
                typeSize: Math.min(Tokens.textPageTitleMax, Math.max(Tokens.textPageTitleMin, win.width * 0.027))
                font.weight: Font.DemiBold
            }
            Loader {
                Layout.fillWidth: true
                active: nav.page === Navigation.Settings
                visible: active
                sourceComponent: AppearancePage {}
            }
            Loader {
                id: overviewPage
                Layout.fillWidth: true
                visible: win.overview
                sourceComponent: OverviewContent {}
            }
            Loader {
                Layout.fillWidth: true
                active: nav.page === Navigation.Point || nav.containsPoint
                visible: nav.page === Navigation.Point
                sourceComponent: ColumnLayout {
                    KitPanel {
                        Layout.fillWidth: true
                        visible: !!win.selected.id
                        contentItem: ColumnLayout {
                            spacing: Tokens.space3
                            KitLabel {
                                Layout.fillWidth: true
                                text: win.selected.name ?? ""
                                font.bold: true
                                typeSize: Tokens.textPanelTitle
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: Tokens.space2
                                PointStatus {
                                    recording: false
                                    value: win.selected?.connection ?? "unknown"
                                }
                                PointStatus {
                                    recording: true
                                    value: win.selected?.recording ?? "unknown"
                                }
                                StatusChip {
                                    visible: !!win.selected?.alarm
                                    text: qsTr("Alarm")
                                    severity: StatusChip.Critical
                                    symbol: "bell-ring"
                                }
                            }
                            Item {
                                objectName: "mediaSurface"
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? width * 9 / 16 : 0
                                visible: children.length > 0
                            }
                        }
                    }
                    StatePane {
                        Layout.fillWidth: true
                        visible: !win.selected.id
                        message: qsTr("This observation point is unavailable.")
                    }
                }
            }
            Item {
                Layout.preferredHeight: Tokens.space3
            }
        }
    }
    component OverviewContent: ColumnLayout {
        property alias list: pointList
        property alias filterPanel: filters
        spacing: Tokens.space3
        Loader {
            Layout.fillWidth: true
            visible: !!item
            sourceComponent: win.overviewTools
        }
        KitPanel {
            Layout.fillWidth: true
            visible: !win.connected && win.overview
            contentItem: ColumnLayout {
                spacing: Tokens.space3
                KitLabel {
                    text: qsTr("Site address")
                    font.bold: true
                }
                KitField {
                    id: origin
                    objectName: "origin"
                    Layout.fillWidth: true
                    label: qsTr("Site address")
                    placeholderText: "https://vms.example"
                    Component.onCompleted: text = win.draftAddress
                    onTextEdited: win.draftAddress = text
                    readOnly: win.stationState === Workspace.Authorizing
                    onAccepted: win.workspace.signIn(text)
                }
                KitLabel {
                    Layout.fillWidth: true
                    text: qsTr("Confirm sign-in in your system browser.")
                    secondary: true
                }
                KitButton {
                    visible: win.stationState !== Workspace.Authorizing
                    text: qsTr("Sign in")
                    symbol: "log-in"
                    primary: true
                    onClicked: win.workspace.signIn(origin.text)
                }
            }
        }
        StatePane {
            objectName: "statePane"
            Layout.fillWidth: true
            visible: win.overview && (win.stationState === Workspace.Authorizing || (win.stationState === Workspace.Loading && !win.workspace.points.count) || win.stationState === Workspace.SigningOut || failure || (win.connected && win.workspace.points.count === 0))
            failure: win.stationState === Workspace.Failed
            message: {
                if (failure) {
                    return win.workspace.failureMessage;
                }
                if (win.stationState === Workspace.Authorizing)
                    return qsTr("Waiting for browser confirmation.");
                if (win.stationState === Workspace.Loading)
                    return qsTr("Loading observation points…");
                if (win.stationState === Workspace.SigningOut)
                    return qsTr("Signing out…");
                return qsTr("No observation points are available.");
            }
            action: win.stationState === Workspace.Authorizing ? qsTr("Cancel") : failure && win.connected ? qsTr("Retry") : ""
            actionSymbol: win.stationState === Workspace.Authorizing ? "circle-x" : "rotate-cw"
            onTriggered: {
                if (!win.connected)
                    win.workspace.cancel();
                else if (win.workspace.retrySignOut)
                    win.workspace.signOut();
                else
                    win.workspace.refresh();
            }
        }
        KitPanel {
            Layout.fillWidth: true
            visible: win.overview && win.connected && win.workspace.points.count > 0
            contentItem: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    visible: win.workspace.sessionActions || win.overviewTitle !== qsTr("Observation points")
                    KitLabel {
                        text: qsTr("Observation points")
                        font.bold: true
                        visible: win.overviewTitle !== text
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    KitLabel {
                        readonly property bool updating: win.stationState === Workspace.Loading
                        text: qsTr("Updating observation points…")
                        visible: win.workspace.sessionActions
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignRight
                        opacity: updating ? 1 : 0
                        Accessible.ignored: !updating
                        Accessible.role: Accessible.StatusBar
                        onUpdatingChanged: if (updating && win.active)
                            Accessible.announce(text)
                    }
                }
                PointFilters {
                    id: filters
                    Layout.fillWidth: true
                    proxy: pointProxy
                }
                KitLabel {
                    Layout.fillWidth: true
                    visible: filters.resultCount === 0
                    text: qsTr("No observation points match these filters.")
                    Accessible.role: Accessible.StatusBar
                }
                ListView {
                    id: pointList
                    objectName: "pointList"
                    model: pointProxy
                    visible: count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, Math.max(160, win.height * 0.43))
                    spacing: Tokens.space1
                    clip: true
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Observation points")
                    Keys.onReturnPressed: if (currentIndex >= 0)
                        win.openPoint(pointProxy.idAt(currentIndex))
                    Keys.onEnterPressed: if (currentIndex >= 0)
                        win.openPoint(pointProxy.idAt(currentIndex))
                    delegate: PointRow {
                        onClicked: {
                            pointList.currentIndex = index;
                            win.openPoint(point.id);
                        }
                    }
                }
            }
        }
        Flow {
            visible: win.workspace.sessionActions && win.connected && win.overview
            Layout.fillWidth: true
            spacing: Tokens.space2
            KitButton {
                text: commands.entries["refresh"].name
                symbol: commands.entries["refresh"].icon
                enabled: commands.entries["refresh"].available
                onClicked: commands.invoke("refresh")
            }
            KitButton {
                text: commands.entries["sign-out"].name
                symbol: commands.entries["sign-out"].icon
                enabled: commands.entries["sign-out"].available
                onClicked: commands.invoke("sign-out")
            }
        }
    }
}
