pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Bacuri.Vms.Client.Ui

FocusScope {
    id: menu
    required property WindowCommands registry
    property point origin
    property bool tracking: false
    property string selection: ""
    property Item returnFocus
    property bool presets: false
    property bool ptzStroke: false
    property point strokePivot
    property point strokeDirection
    property point firstOrigin
    readonly property var actions: presets ? Array.from({length: 8}, (_,i) => ({
        id: "preset-" + (i+1), number: i+1, name: qsTr("Preset %1").arg(i+1), description: registry.presets[i]?.label || qsTr("Go to preset %1").arg(i+1),
        available: i < registry.presets.length, bindings: [], startAngle: -112.5 + i*45, endAngle: -67.5 + i*45
    })) : registry.radialActions
    readonly property var selectedAction: actions.find(row => row.id === selection)
    readonly property real outerRadius: Math.max(0, Math.min(Tokens.scaled(145), Math.min(width, height) / 2 - Tokens.space2))
    readonly property real innerRadius: outerRadius * 0.52
    readonly property real cx: Math.max(outerRadius, Math.min(width - outerRadius, origin.x))
    readonly property real cy: Math.max(outerRadius, Math.min(height - outerRadius, origin.y))
    anchors.fill: parent
    z: 1
    visible: false
    Accessible.role: Accessible.PopupMenu
    Accessible.name: (registry.entries.context?.name || "")

    function begin(point, focusItem) {
        presets = false;
        ptzStroke = false;
        firstOrigin = point;
        origin = point;
        returnFocus = focusItem;
        selection = "";
        tracking = true;
        delay.restart();
    }
    function openMenu() {
        visible = true;
        forceActiveFocus(Qt.PopupFocusReason);
        if (!selection)
            step(1);
    }
    function hideMenu() {
        delay.stop();
        presetDelay.stop();
        tracking = false;
        visible = false;
        if (returnFocus && returnFocus.visible)
            returnFocus.forceActiveFocus(Qt.PopupFocusReason);
    }
    function sectorAt(x, y) {
        const dx = x - cx;
        const dy = y - cy;
        const distance = Math.hypot(dx, dy);
        if (distance < innerRadius || distance > outerRadius) return "";
        if (presets) return "preset-" + (1 + (Math.round((Math.atan2(dy,dx)*180/Math.PI+90)/45)+8)%8);
        return registry.direction(dx, dy);
    }
    function hoverAt(dx, dy) {
        if (!tracking) return;
        if (ptzStroke && !presets) {
            const forward = (dx - strokePivot.x) * strokeDirection.x + (dy - strokePivot.y) * strokeDirection.y;
            const lateral = Math.abs(dx * strokeDirection.y - dy * strokeDirection.x);
            if (forward > 0 && lateral < 3)
                strokePivot = Qt.point(dx, dy);
            else if (Math.hypot(dx - strokePivot.x, dy - strokePivot.y) >= 24)
                showPresets();
        }
        if (presets) {
            selection = strokePreset(dx, dy);
            return;
        }
        selection = visible ? sectorAt(firstOrigin.x + dx, firstOrigin.y + dy) : registry.direction(dx, dy);
        if (!ptzStroke && selection === "ptz" && registry.hasPresets() && dx < -32) {
            const length = Math.hypot(dx, dy);
            strokeDirection = Qt.point(dx / length, dy / length);
            ptzStroke = true;
            strokePivot = Qt.point(dx, dy);
        }
    }
    function strokePreset(dx, dy) {
        const x = dx - strokePivot.x;
        const y = dy - strokePivot.y;
        if (Math.hypot(x, y) < 24) return "";
        const sector = Math.round((Math.atan2(y, x) * 180 / Math.PI + 90) / 45);
        return "preset-" + (1 + (sector + 8) % 8);
    }
    function finish(dx, dy) {
        if (!tracking) return;
        if (ptzStroke) hoverAt(dx, dy);
        const gesture = registry.direction(dx, dy);
        const id = presets ? strokePreset(dx, dy) : visible && gesture ? sectorAt(firstOrigin.x + dx, firstOrigin.y + dy) : gesture;
        tracking = false;
        delay.stop();
        if (id && actions.some(row => row.id === id && row.available))
            run(id, !visible || (presets && ptzStroke));
        else
            openMenu();
    }
    function showPresets() {
        if (presets || !registry.hasPresets()) return;
        origin = tracking && ptzStroke ? Qt.point(firstOrigin.x + strokePivot.x, firstOrigin.y + strokePivot.y)
                                     : Qt.point(cx - (innerRadius + outerRadius) / 2, cy);
        presets = true;
        openMenu();
        selection = "preset-1";
    }
    function run(id, stroke = false) {
        if (!id || !actions.some(row => row.id === id && row.available))
            return;
        hideMenu();
        registry.recordGesture(id, stroke);
        if (presets) registry.confirmPreset(Number(id.slice(7)));
        else registry.invoke(id);
    }
    function step(delta) {
        const enabled = actions.filter(row => row.available);
        if (!enabled.length) {
            hideMenu();
            return;
        }
        let index = enabled.findIndex(row => row.id === selection);
        if (index < 0)
            index = delta > 0 ? -1 : 0;
        selection = enabled[(index + enabled.length + delta) % enabled.length].id;
    }
    onSelectionChanged: {
        presetDelay.stop();
        if (selection === "ptz" && registry.hasPresets()) presetDelay.start();
        const row = actions.find(item => item.id === selection);
        if (visible && row) Accessible.announce(row.name);
    }
    Timer { id: presetDelay; interval: 400; onTriggered: menu.showPresets() }
    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Down:
        case Qt.Key_Right:
            if (selection === "ptz" && registry.hasPresets()) { showPresets(); break; }
        case Qt.Key_Tab:
            step(1);
            break;
        case Qt.Key_Up:
        case Qt.Key_Left:
        case Qt.Key_Backtab:
            step(-1);
            break;
        case Qt.Key_Enter:
        case Qt.Key_Return:
        case Qt.Key_Space:
            run(selection);
            break;
        case Qt.Key_Escape:
            hideMenu();
            break;
        default:
            return;
        }
        event.accepted = true;
    }
    Timer {
        id: delay
        interval: 180
        onTriggered: if (menu.tracking) menu.openMenu()
    }

    Item {
        id: wheel
        x: menu.cx - menu.outerRadius
        y: menu.cy - menu.outerRadius
        width: 2 * menu.outerRadius
        height: width
        readonly property real radius: menu.outerRadius
        readonly property real hub: menu.innerRadius
        Rectangle {
            anchors.centerIn: parent
            width: wheel.hub * 2
            height: width
            radius: wheel.hub
            color: Tokens.raised
            border.color: Tokens.control
        }
        Canvas {
            id: ring
            objectName: "cameraActionRing"
            anchors.fill: parent
            readonly property var sectors: menu.actions
            readonly property string selection: menu.selection
            readonly property color surface: Tokens.surface
            readonly property color highlight: Tokens.actionFill
            readonly property color outline: Tokens.control
            readonly property color focusOutline: Tokens.focus
            onSectorsChanged: requestPaint()
            onSelectionChanged: requestPaint()
            onSurfaceChanged: requestPaint()
            onHighlightChanged: requestPaint()
            onOutlineChanged: requestPaint()
            onFocusOutlineChanged: requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                const cx = width / 2, cy = height / 2;
                const outer = wheel.radius - 1, inner = wheel.hub + 2;
                if (outer <= inner) return;
                for (const sector of sectors) {
                    const start = (sector.startAngle + 0.7) * Math.PI / 180;
                    const end = (sector.endAngle - 0.7) * Math.PI / 180;
                    const active = sector.id === selection && sector.available;
                    ctx.beginPath();
                    ctx.arc(cx, cy, outer, start, end, false);
                    ctx.arc(cx, cy, inner, end, start, true);
                    ctx.closePath();
                    ctx.fillStyle = active ? highlight : surface;
                    ctx.fill();
                    ctx.lineWidth = active ? Tokens.lineFocus : Tokens.lineThin;
                    ctx.strokeStyle = active ? focusOutline : outline;
                    ctx.stroke();
                    if (active) {
                        const angle = (start + end) / 2;
                        const dx = Math.cos(angle), dy = Math.sin(angle);
                        const base = inner - wheel.hub * 0.17;
                        const halfWidth = wheel.hub * 0.1;
                        const baseX = cx + dx * base, baseY = cy + dy * base;
                        ctx.beginPath();
                        ctx.moveTo(cx + dx * inner, cy + dy * inner);
                        ctx.lineTo(baseX - dy * halfWidth, baseY + dx * halfWidth);
                        ctx.lineTo(baseX + dy * halfWidth, baseY - dx * halfWidth);
                        ctx.closePath();
                        ctx.fillStyle = focusOutline;
                        ctx.fill();
                    }
                }
            }
        }
        Repeater {
            model: menu.actions
            Item {
                id: sector
                required property var modelData
                anchors.fill: parent
                readonly property real start: modelData.startAngle
                readonly property real sweep: modelData.endAngle - start
                readonly property real angle: (start + sweep / 2) * Math.PI / 180
                readonly property real iconRadius: (wheel.hub + wheel.radius) / 2
                readonly property bool selected: menu.selection === modelData.id
                readonly property bool highlighted: selected && modelData.available
                AbstractButton {
                    id: action
                    objectName: "radial-" + sector.modelData.id
                    readonly property string shortcutHint: sector.modelData.bindings.join(" · ")
                    readonly property int iconExtent: Math.min(Tokens.scaled(29), width)
                    width: Math.min(Tokens.scaled(46), wheel.radius - wheel.hub)
                    height: width
                    x: wheel.radius + Math.cos(sector.angle) * sector.iconRadius - width / 2
                    y: wheel.radius + Math.sin(sector.angle) * sector.iconRadius - height / 2
                    text: sector.modelData.name
                    enabled: sector.modelData.available
                    focusPolicy: Qt.NoFocus
                    Accessible.role: Accessible.MenuItem
                    Accessible.name: text
                    Accessible.description: [sector.modelData.description, shortcutHint].filter(Boolean).join(". ")
                    Accessible.selectable: true
                    Accessible.selected: sector.selected
                    onClicked: menu.run(sector.modelData.id)
                    contentItem: IconLabel {
                        text: menu.presets ? String(sector.modelData.number) : ""
                        color: sector.highlighted ? Tokens.textOnAction : Tokens.ink
                        icon.source: menu.presets ? "" : "qrc:/design/icons/" + sector.modelData.icon + ".svg"
                        icon.width: action.iconExtent
                        icon.height: action.iconExtent
                        icon.color: sector.highlighted ? Tokens.textOnAction : action.enabled ? Tokens.ink : Tokens.muted
                        opacity: action.enabled ? 1 : 0.65
                        Accessible.ignored: true
                    }
                }
            }
        }
        KitLabel {
            objectName: "radialDescription"
            anchors.centerIn: parent
            width: wheel.hub * 1.55
            height: wheel.hub
            text: menu.selectedAction?.description || (menu.registry.entries.context?.name || "")
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            typeSize: Tokens.textBody
            font.weight: Font.DemiBold
            fontSizeMode: Text.Fit
            minimumPixelSize: Tokens.textCaption
            wrapMode: Text.Wrap
            color: Tokens.ink
            Accessible.ignored: true
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onPositionChanged: event => {
            const id = menu.sectorAt(event.x, event.y);
            if (!menu.tracking && id) menu.selection = id;
            else if (!id) presetDelay.stop();
        }
        onClicked: event => {
            const id = menu.sectorAt(event.x, event.y);
            if (id)
                menu.run(id);
            else
                menu.hideMenu();
        }
    }
}
