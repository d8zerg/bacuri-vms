pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import Bacuri.Vms.Client.Ui

Popup {
    id: panel
    required property WindowCommands registry
    required property Workspace workspace
    property string mode: ""
    property var data: ({})
    property var results: []
    property var intervals: []
    readonly property var currentArgument: searching && results[list.currentIndex] ? registry.argument(results[list.currentIndex].key, query.text) : ({})
    readonly property alias previewHost: previewHost
    property bool showPreview: false
    property string hoveredPoint: ""
    signal inspectPoint(string point)
    Timer { id: hoverDelay; interval: 240; onTriggered: if (panel.visible && panel.searching) panel.inspectPoint(panel.hoveredPoint) }
    function hover(key) {
        hoveredPoint = !query.text.includes("@") && key?.startsWith("point/") ? key.slice(6) : "";
        if (panel.visible && panel.searching) hoverDelay.restart();
    }
    property var selectedPoints: []
    property string notice: ""
    property string lastAnnouncement: ""
    readonly property bool searching: mode === "search"
    readonly property bool editing: ["archive", "export", "bookmark", "ptz", "layout"].includes(mode)
    readonly property string title: searching ? registry.entries.palette.name : mode === "layout" ? qsTr("Apply layout") : mode === "intervals" ? registry.entries.archive.name : mode === "case" ? data.name || "" : registry.entries[mode]?.name || ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - Tokens.space3 * 2, Tokens.scaled(560))
    height: Math.min(parent.height - Tokens.space3 * 2, body.implicitHeight + padding * 2)
    padding: Tokens.space3
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    palette.window: Tokens.surface
    palette.windowText: Tokens.ink
    palette.base: Tokens.surface
    palette.alternateBase: Tokens.raised
    palette.text: Tokens.ink
    palette.placeholderText: Tokens.muted
    palette.button: Tokens.surface
    palette.buttonText: Tokens.ink
    palette.mid: Tokens.control
    palette.dark: Tokens.muted
    palette.highlight: Tokens.actionFill
    palette.highlightedText: Tokens.textOnAction
    palette.toolTipBase: Tokens.raised
    palette.toolTipText: Tokens.ink
    Overlay.modal: Rectangle { color: Tokens.canvas; opacity: 0.72 }
    background: Rectangle {
        color: Tokens.surface
        radius: Tokens.radius
        border.color: Tokens.control
    }

    onClosed: {
        hoverDelay.stop();
        registry.stopMotion();
        if (mode === "intervals") registry.cancelRead();
    }
    function show(kind, row) {
        if (kind === "dismiss") {
            close();
            mode = "";
            data = {};
            results = [];
            intervals = [];
            query.clear();
            start.clear();
            end.clear();
            argument.clear();
            notice = "";
            lastAnnouncement = "";
            return;
        }
        lastAnnouncement = "";
        selectedPoints = [];
        mode = kind;
        data = row;
        notice = "";
        query.clear();
        start.text = row.at || registry.archiveTime || row.last_recorded_at || registry.now();
        end.text = row.end || registry.intervalEnd(start.text, row.id || "");
        argument.clear();
        intervals = [];
        choice.currentIndex = 0;
        if (row.preset) {
            choice.currentIndex = choice.model.findIndex(item => item.command === "preset_goto" && item.target === row.preset);
        }
        if (searching)
            refresh(false);
        open();
        Qt.callLater(focusFirst);
    }
    function focusFirst() {
        if (!visible)
            return;
        (searching ? query : mode === "ptz" || mode === "layout" ? choice : editing ? start : dismiss).forceActiveFocus(Qt.TabFocusReason);
        if (searching) {
            announceSelection();
            hover(results[list.currentIndex]?.key);
        }
    }
    function refresh(preserve) {
        const previous = preserve ? results[list.currentIndex]?.key : "";
        results = registry.search(query.text);
        list.currentIndex = Math.max(0, results.findIndex(row => row.key === previous));
        if (results.length === 0)
            list.currentIndex = -1;
        if (visible && searching) hover(results[list.currentIndex]?.key);
        if (visible)
            announceSelection();
    }
    function announceSelection() {
        if (!searching || !visible)
            return;
        const message = results.length ? qsTr("%1 results. %2").arg(results.length).arg(results[list.currentIndex]?.name || "") : qsTr("No matches.");
        if (message !== lastAnnouncement) {
            body.Accessible.announce(message);
            lastAnnouncement = message;
        }
    }
    function togglePoint(index) {
        const key = results[index]?.key || "";
        if (!key.startsWith("point/")) return;
        if (selectedPoints.includes(key)) selectedPoints = selectedPoints.filter(item => item !== key);
        else if (selectedPoints.length < 4) selectedPoints = selectedPoints.concat(key);
    }
    function selectResult(index) {
        if (selectedPoints.length) {
            if (registry.temporaryLayout(selectedPoints, Number(screenChoice.currentValue))) close();
            return;
        }
        const row = results[index];
        if (!row)
            return;
        const parsed = index === list.currentIndex ? currentArgument : registry.argument(row.key, query.text);
        if (!parsed.valid) { notify(parsed.hint); return; }
        close();
        registry.choose(row.key, parsed.value);
    }
    function submit() {
        if (mode === "layout") {
            registry.applyLayout(data.key, choice.currentValue);
        } else {
            const action = mode === "ptz" ? choice.model[choice.currentIndex] : null;
            if (mode === "ptz" && !action) return;
            registry.pointAction(mode, data.id, action ? action.command : start.text, end.text, action ? action.target : argument.text);
        }
    }
    function notify(message) {
        notice = message;
        if (visible)
            body.Accessible.announce(message);
    }

    contentItem: ColumnLayout {
        id: body
        spacing: Tokens.space2
        Accessible.role: Accessible.Dialog
        Accessible.name: panel.title
        RowLayout {
            Layout.fillWidth: true
            KitLabel {
                text: panel.title
                Layout.fillWidth: true
                typeSize: Tokens.textPanelTitle
                font.bold: true
            }
            KitButton {
                id: dismiss
                text: qsTr("Close")
                symbol: "circle-x"
                onClicked: panel.close()
            }
        }
        KitField {
            id: query
            objectName: "commandQuery"
            visible: panel.searching
            Layout.fillWidth: true
            label: qsTr("Search commands and objects")
            placeholderText: label
            onTextChanged: if (panel.searching)
                panel.refresh(false)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space && event.modifiers === Qt.ControlModifier) {
                    panel.togglePoint(list.currentIndex);
                    event.accepted = true;
                }
            }
            onAccepted: panel.selectResult(list.currentIndex)
            Keys.onDownPressed: {
                list.currentIndex = Math.min(panel.results.length - 1, list.currentIndex + 1);
                panel.announceSelection();
            }
            Keys.onUpPressed: {
                list.currentIndex = Math.max(0, list.currentIndex - 1);
                panel.announceSelection();
            }
        }
        KitLabel {
            visible: panel.searching
            text: panel.currentArgument.hint || qsTr("Camera @ site time · Layout @ screen number")
            secondary: true
            Layout.fillWidth: true
        }
        ListView {
            id: list
            objectName: "commandResults"
            onCurrentIndexChanged: if(panel.visible) panel.hover(panel.results[currentIndex]?.key)
            visible: panel.searching && count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(count * Tokens.scaled(46), Tokens.scaled(300))
            Layout.minimumHeight: 0
            clip: true
            model: panel.results
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Results")
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: resultRow
                required property var modelData
                required property int index
                width: ListView.view.width
                height: Tokens.scaled(46)
                highlighted: ListView.isCurrentItem
                text: modelData.name
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.group + " · " + text + (modelData.hint ? " · " + modelData.hint : "")
                Accessible.selectable: true
                Accessible.selected: panel.selectedPoints.length ? panel.selectedPoints.includes(modelData.key) : highlighted
                Accessible.onPressAction: panel.selectResult(index)
                hoverEnabled: true
                onHoveredChanged: if (hovered) panel.hover(modelData.key)
                onClicked: panel.selectResult(index)
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: mouse => { mouse.accepted = !!(mouse.modifiers & Qt.ControlModifier); }
                    onClicked: panel.togglePoint(index)
                }
                background: Rectangle {
                    color: parent.highlighted ? Tokens.actionFill : parent.hovered ? Tokens.raised : Tokens.surface
                    radius: Tokens.radius / 2
                    border.color: parent.highlighted ? Tokens.focus : "transparent"
                }
                contentItem: RowLayout {
                    IconLabel {
                        icon.source: "qrc:/design/icons/" + modelData.icon + ".svg"
                        icon.width: Tokens.scaled(20)
                        icon.height: Tokens.scaled(20)
                        icon.color: resultRow.highlighted ? Tokens.textOnAction : Tokens.muted
                        Accessible.ignored: true
                    }
                    KitLabel {
                        text: (panel.selectedPoints.includes(modelData.key) ? "✓ " : "") + modelData.marked
                        textFormat: Text.StyledText
                        color: resultRow.highlighted ? Tokens.textOnAction : Tokens.ink
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        Accessible.ignored: true
                    }
                    KitLabel {
                        text: modelData.hint || modelData.group
                        color: resultRow.highlighted ? Tokens.textOnAction : Tokens.muted
                        Accessible.ignored: true
                    }
                }
            }
        }
        KitLabel {
            visible: panel.searching && !panel.results.length
            text: qsTr("No matches.")
        }
        Item {
            id: previewHost
            visible: panel.searching && panel.showPreview
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Math.min(Tokens.scaled(140),panel.parent.height/5) : 0
        }
        RowLayout {
            visible: panel.selectedPoints.length > 0
            Layout.fillWidth: true
            KitChoice {
                id: screenChoice
                Layout.fillWidth: true
                label: qsTr("Screen")
                symbol: "monitor"
                model: panel.selectedPoints.length ? panel.registry.screens() : []
            }
            KitButton {
                text: qsTr("Apply layout")
                onClicked: panel.selectResult(list.currentIndex)
            }
        }
        ScrollView {
            id: scroll
            visible: !panel.searching
            Layout.fillWidth: true
            Layout.preferredHeight: form.implicitHeight
            Layout.minimumHeight: 0
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: form
                width: scroll.availableWidth
                spacing: Tokens.space2
                KitLabel {
                    visible: !!panel.data.name && panel.mode !== "case"
                    text: panel.data.name || ""
                    Layout.fillWidth: true
                    font.bold: true
                }
                KitLabel {
                    visible: panel.mode === "case"
                    Layout.fillWidth: true
                    text: panel.data.data?.notes || qsTr("No notes.")
                }
                Repeater {
                    model: panel.mode === "exports" ? panel.workspace.exports.rows : []
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        KitLabel {
                            Layout.fillWidth: true
                            text: parent.modelData.point + " · " + parent.modelData.start + " · " +
                                (parent.modelData.error || parent.modelData.path || (parent.modelData.total > 0 ?
                                    Math.round(100 * parent.modelData.received / parent.modelData.total) + "%" : panel.jobState(parent.modelData.state)))
                        }
                        KitButton {
                            visible: ["failed", "cancelled", "queued", "running"].includes(parent.modelData.state)
                            readonly property bool running: ["running", "queued"].includes(parent.modelData.state)
                            text: running ? qsTr("Cancel") : qsTr("Retry")
                            symbol: running ? "circle-x" : "rotate-cw"
                            onClicked: running ? panel.workspace.exports.cancel(parent.modelData.id) : panel.workspace.exports.retry(parent.modelData.id)
                        }
                    }
                }
                KitButton {
                    visible: panel.mode === "publish-keymap"
                    text: qsTr("Publish site defaults")
                    onClicked: { panel.registry.publishKeymap(); panel.close(); }
                }
                KitLabel { visible: panel.mode === "exports" && !panel.workspace.exports.rows.length; text: qsTr("No export jobs") }
                Repeater {
                    model: panel.mode === "case" ? panel.data.data?.materials || [] : []
                    KitLabel {
                        required property var modelData
                        Layout.fillWidth: true
                        text: [modelData.point_id, modelData.interval?.start, modelData.interval?.end].filter(Boolean).join(" · ")
                    }
                }
                KitLabel {
                    visible: panel.mode === "intervals"
                    text: panel.data.at || ""
                    Layout.fillWidth: true
                }
                Repeater {
                    model: panel.intervals
                    KitLabel {
                        required property var modelData
                        Layout.fillWidth: true
                        text: [modelData.interval?.start, modelData.interval?.end, panel.archiveState(modelData.state)].join(" · ")
                    }
                }
                GridLayout {
                    visible: panel.mode === "ptz"
                    Layout.fillWidth: true
                    columns: 3
                    Repeater {
                        model: panel.mode === "ptz" ? (panel.data.ptz?.commands || []).filter(id => ["up","down","left","right","zoom_in","zoom_out"].includes(id)) : []
                        KitButton {
                            required property string modelData
                            objectName: "ptz-" + modelData
                            enabled: (panel.data.ptz?.commands || []).includes("stop")
                            text: panel.registry.ptzName(modelData)
                            onPressed: panel.registry.startMotion(panel.data.id,modelData)
                            onReleased: panel.registry.stopMotion()
                            onCanceled: panel.registry.stopMotion()
                            onActiveFocusChanged: if (!activeFocus && pressed) panel.registry.stopMotion()
                        }
                    }
                }
                KitChoice {
                    id: choice
                    objectName: "commandChoice"
                    Layout.fillWidth: true
                    visible: panel.mode === "layout" || (panel.mode === "ptz" && count > 0)
                    label: panel.mode === "layout" ? qsTr("Screen") : qsTr("Action")
                    symbol: panel.mode === "layout" ? "monitor" : "settings-2"
                    model: panel.mode === "layout" ? panel.registry.screens() : panel.mode === "ptz" ? panel.registry.ptzOptions : []
                }
                KitLabel {
                    text: qsTr("Start time")
                    visible: start.visible
                }
                KitField {
                    id: start
                    objectName: "commandStart"
                    visible: ["archive", "bookmark", "export"].includes(panel.mode)
                    Layout.fillWidth: true
                    label: qsTr("Start time")
                    onAccepted: panel.mode === "export" ? end.forceActiveFocus() : panel.submit()
                }
                KitLabel {
                    text: end.label
                    visible: end.visible
                }
                KitField {
                    id: end
                    visible: panel.mode === "export"
                    Layout.fillWidth: true
                    label: qsTr("End time")
                    onAccepted: panel.submit()
                }
                KitLabel {
                    text: argument.label
                    visible: argument.visible
                }
                KitField {
                    id: argument
                    visible: panel.mode === "bookmark"
                    label: panel.mode === "bookmark" ? qsTr("Temporary mark name") : qsTr("Target")
                    Layout.fillWidth: true
                    onAccepted: panel.submit()
                }
                KitButton {
                    visible: panel.editing && (panel.mode !== "ptz" || choice.count > 0)
                    text: qsTr("Run")
                    primary: true
                    onClicked: panel.submit()
                }
            }
        }
        KitLabel {
            Layout.fillWidth: true
            visible: text.length > 0
            text: panel.notice
            Accessible.role: Accessible.StatusBar
        }
    }
    function jobState(value) {
        switch (value) {
        case "queued": return qsTr("Queued");
        case "running": return qsTr("Downloading");
        case "complete": return qsTr("Completed");
        case "failed": return qsTr("Failed");
        case "cancelled": return qsTr("Cancelled");
        default: return value;
        }
    }
    function archiveState(value) {
        switch (value) {
        case "recorded":
            return qsTr("Recorded");
        case "gap":
            return qsTr("Gap");
        case "pending_catalog":
            return qsTr("Pending");
        default:
            return qsTr("Unavailable");
        }
    }
}
