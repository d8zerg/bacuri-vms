pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Bacuri.Vms.Client.Ui
import org.freedesktop.gstreamer.Qt6GLVideoItem

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
    readonly property AppearancePage appearance: settingsPage.item as AppearancePage
    property Item mediaFrame
    property Item mediaHost
    property LivePreview displayedStream
    readonly property LivePreview previewStream: displayedStream === firstStream ? secondStream : firstStream
    Item { id: parking; visible: false }
    component StreamSlot: LivePreview {
        id: slot
        parent: win.displayedStream === slot && win.mediaHost ? win.mediaHost
              : win.previewStream === slot && panel.visible && panel.searching ? panel.previewHost : parking
        anchors.fill: parent
        visible: !!point && parent !== parking
        surface: GstGLQt6VideoItem {
            parent: slot
            width: Math.max(1, slot.width)
            height: Math.max(1, slot.height)
        }
        Rectangle { anchors.fill: parent; color: Tokens.canvas; visible: !slot.ready }
    }
    StreamSlot { id: firstStream }
    StreamSlot { id: secondStream }
    function closeDisplayedStream() {
        if (displayedStream) {
            displayedStream.close();
            displayedStream = null;
        }
    }
    CommandPanel {
        id: panel
        objectName: "commandPanel"
        registry: commands
        workspace: win.workspace
        showPreview: !!win.previewStream.point
        onInspectPoint: id => win.previewStream.open(win.workspace, id)
        onClosed: Qt.callLater(() => { if (!panel.visible) win.previewStream.close(); })
    }
    CameraMenu { id: radial; parent: win.contentItem; objectName: "cameraMenu"; registry: commands }
    WindowCommands {
        id: commands
        objectName: "commandRegistry"
        modal: panel.visible || radial.visible || !!win.appearance?.capturing
        frame: win.mediaFrame
        onPointChosen: (id, live) => win.openPoint(id, live)
        onSubmitted: panel.close()
        onLayoutApplied: key => {
            panel.close();
            nav.push(Navigation.Layout, key, win.currentFocusTarget());
            backButton.forceActiveFocus();
        }
        onTool: (kind, data) => {
            if (radial.visible || radial.tracking) radial.hideMenu();
            if (kind === "intervals") win.closeDisplayedStream();
            panel.show(kind, data);
        }
        onResultsChanged: if (panel.visible && panel.searching) panel.refresh(true)
        onFeedback: message => {
            if (panel.visible) panel.notify(message);
            else { toast.text = message; toast.open(); scroll.announceStatus(message); }
        }
        onArchiveLoaded: (id, at, intervals) => {
            if (panel.visible && panel.mode === "intervals" && panel.data.id === id && panel.data.at === at) {
                panel.intervals = intervals;
                panel.notify(intervals.length ? qsTr("Archive intervals: %1").arg(intervals.length) : qsTr("No recording at this time."));
            }
        }
        Component.onCompleted: attach(win, nav, win.workspace)
        onTriggered: id => {
            switch (id) {
            case "bindings":
                win.settings();
                Qt.callLater(() => win.appearance?.openShortcuts());
                break;
            case "context":
                radial.begin(Qt.point(radial.width / 2, radial.height / 2), win.activeFocusItem);
                radial.finish(0, 0);
                break;
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
    KitPanel {
        parent: Overlay.overlay
        anchors.centerIn: parent
        visible: commands.hints
        width: Math.min(parent.width - 2 * Tokens.space3, Tokens.scaled(500))
        height: Math.min(parent.height - 2 * Tokens.space3, implicitHeight)
        z: 2
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            implicitHeight: hintsColumn.implicitHeight
            ColumnLayout {
                id: hintsColumn
                width: parent.width
                KitLabel { text: qsTr("Keyboard shortcuts"); font.bold: true }
                Repeater {
                    model: commands.hints ? commands.commandIds.filter(id => commands.entries[id].available && commands.entries[id].bindings.length) : []
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        KitLabel { Layout.fillWidth: true; text: commands.entries[parent.modelData].name }
                        KitLabel { text: commands.entries[parent.modelData].bindings.join(" · ") }
                    }
                }
            }
        }
    }
    ToolTip { id: toast; parent: Overlay.overlay; timeout: 4500; x: (parent.width - width) / 2; y: parent.height - height - Tokens.space3; width: Math.min(implicitWidth, parent.width - 2 * Tokens.space3) }
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
    readonly property string section: nav.page === Navigation.Settings ? qsTr("Settings") : nav.page === Navigation.Point ? qsTr("Observation point") : nav.page === Navigation.Layout ? commands.layout.name || qsTr("Layout") : overviewTitle

    width: 1000
    height: 720
    minimumWidth: 420
    minimumHeight: 350
    visible: true
    title: section + " | Bacuri VMS"
    color: Tokens.canvas

    Navigation {
        id: nav
        onChanged: if (win.displayedStream && (page !== Navigation.Point || pointId !== win.displayedStream.point))
            win.closeDisplayedStream()
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
    function openPoint(id, live = true) {
        const target = previewStream.point === id ? previewStream : displayedStream?.point === id ? displayedStream : previewStream;
        nav.push(Navigation.Point, id, {
            id: id,
            role: "point"
        });
        if (live) {
            if (displayedStream && displayedStream !== target) closeDisplayedStream();
            target.open(workspace, id);
            if (target.point === id) displayedStream = target;
        } else closeDisplayedStream();
        backButton.forceActiveFocus(Qt.TabFocusReason);
        viewport.contentY = 0;
    }

    Connections {
        target: win.workspace
        function onChanged() {
            if (!win.workspace.signedIn) {
                win.overviewContent?.filterPanel.reset();
                if (nav.containsPoint || nav.page === Navigation.Layout)
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
                objectName: "palette"
                text: (commands.entries.palette?.name || "")
                symbol: (commands.entries.palette?.icon || "")
                shortcutHint: (commands.entries.palette?.bindings || []).join(" · ")
                onClicked: commands.invoke("palette")
            }
            KitButton {
                objectName: "newWindow"
                text: (commands.entries["new"]?.name || "")
                symbol: (commands.entries["new"]?.icon || "")
                shortcutHint: (commands.entries["new"]?.bindings || []).join(" · ")
                onClicked: commands.invoke("new")
            }
            KitButton {
                id: settingsButton
                objectName: "settings"
                text: (commands.entries["settings"]?.name || "")
                symbol: (commands.entries["settings"]?.icon || "")
                shortcutHint: (commands.entries["settings"]?.bindings || []).join(" · ")
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
                text: (commands.entries["back"]?.name || "")
                symbol: (commands.entries["back"]?.icon || "")
                shortcutHint: (commands.entries["back"]?.bindings || []).join(" · ")
                onClicked: commands.invoke("back")
            }
            KitLabel {
                Layout.fillWidth: true
                text: win.section
                typeSize: Math.min(Tokens.textPageTitleMax, Math.max(Tokens.textPageTitleMin, win.width * 0.027))
                font.weight: Font.DemiBold
            }
            Loader {
                id: settingsPage
                Layout.fillWidth: true
                active: nav.page === Navigation.Settings
                visible: active
                sourceComponent: AppearancePage { registry: commands }
            }
            GridLayout {
                id: layoutGrid
                readonly property bool temporary: commands.layout.temporary === true
                visible: nav.page === Navigation.Layout
                Layout.fillWidth: true
                columns: temporary || win.width >= Tokens.scaled(650) ? 2 : 1
                Repeater {
                    model: layoutGrid.temporary ? 4 : (commands.layout.data?.points?.length || 0)
                    ColumnLayout {
                        id: cell
                        required property int index
                        readonly property string pointId: commands.layout.data?.points?.[index] || ""
                        readonly property var point: {
                            win.workspace.points.version;
                            return pointId ? win.workspace.points.lookup(pointId) : {};
                        }
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Loader {
                            Layout.fillWidth: true
                            Layout.preferredHeight: active ? width * 9 / 16 : 0
                            active: layoutGrid.temporary
                            visible: active
                            sourceComponent: Rectangle {
                                color: Tokens.canvas
                                border.color: Tokens.line
                                LivePreview {
                                    id: tile
                                    objectName: "layoutVideo-" + cell.index
                                    anchors.fill: parent
                                    preferMain: false
                                    surface: GstGLQt6VideoItem {
                                        parent: tile
                                        width: Math.max(1, tile.width)
                                        height: Math.max(1, tile.height)
                                    }
                                    Rectangle { anchors.fill: parent; color: Tokens.canvas; visible: !tile.ready }
                                    readonly property string camera: win.workspace.state === Workspace.Ready && cell.point.can_preview && cell.point.connection === "online" ? cell.pointId : ""
                                    onCameraChanged: open(win.workspace, camera)
                                    Component.onCompleted: open(win.workspace, camera)
                                }
                            }
                        }
                        KitButton {
                            Layout.fillWidth: true
                            visible: !!cell.pointId || !layoutGrid.temporary
                            text: cell.point.name || qsTr("This observation point is unavailable.")
                            enabled: !!cell.point.id
                            onClicked: win.openPoint(cell.pointId)
                        }
                    }
                }
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
                        id: cameraCard
                        objectName: "cameraCard"
                        property string commandScope: "video"
                        Item {
                            id: cameraInput
                            parent: cameraCard
                            objectName: "cameraInput"
                            activeFocusOnTab: true
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: cameraInput.forceActiveFocus(Qt.MouseFocusReason)
                        }
                        Layout.fillWidth: true
                        MouseArea {
                            objectName: "cameraContextArea"
                            parent: cameraCard
                            anchors.fill: parent
                            z: 1
                            acceptedButtons: Qt.RightButton
                            preventStealing: true
                            property point start
                            onPressed: mouse => {
                                start = Qt.point(mouse.x, mouse.y);
                                radial.begin(mapToItem(radial, mouse.x, mouse.y), win.activeFocusItem);
                            }
                            onPositionChanged: mouse => radial.hoverAt(mouse.x - start.x, mouse.y - start.y)
                            onReleased: mouse => radial.finish(mouse.x - start.x, mouse.y - start.y)
                            onCanceled: if (radial.tracking) radial.hideMenu()
                        }
                        visible: !!win.selected.id
                        contentItem: ColumnLayout {
                            spacing: Tokens.space3
                            RowLayout {
                                Layout.fillWidth: true
                                KitLabel {
                                    Layout.fillWidth: true
                                    text: win.selected.name ?? ""
                                    font.bold: true
                                    typeSize: Tokens.textPanelTitle
                                }
                                KitButton {
                                    objectName: "cameraActions"
                                    text: (commands.entries.context?.name || "")
                                    symbol: (commands.entries.context?.icon || "")
                                    shortcutHint: (commands.entries.context?.bindings || []).join(" · ")
                                    enabled: !!commands.entries.context?.available
                                    onClicked: commands.invoke("context")
                                }
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
                            KitButton {
                                objectName: "timelinePosition"
                                property string commandScope: "timeline"
                                onClicked: commands.invoke("archive")
                                visible: !!commands.archiveTime
                                text: commands.archiveTime ? (commands.entries.archive?.name || "") + " · " + commands.archiveTime : ""
                                Layout.fillWidth: true
                            }
                            Item {
                                objectName: "mediaSurface"
                                onChildrenChanged: win.mediaFrame = children.length ? children[0] : null
                                Component.onCompleted: win.mediaHost = this
                                Component.onDestruction: { win.mediaHost = null; win.mediaFrame = null; }
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
                text: (commands.entries["refresh"]?.name || "")
                symbol: (commands.entries["refresh"]?.icon || "")
                shortcutHint: (commands.entries["refresh"]?.bindings || []).join(" · ")
                enabled: !!commands.entries["refresh"]?.available
                onClicked: commands.invoke("refresh")
            }
            KitButton {
                text: (commands.entries["sign-out"]?.name || "")
                symbol: (commands.entries["sign-out"]?.icon || "")
                shortcutHint: (commands.entries["sign-out"]?.bindings || []).join(" · ")
                enabled: !!commands.entries["sign-out"]?.available
                onClicked: commands.invoke("sign-out")
            }
        }
    }
}
