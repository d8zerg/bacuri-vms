pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

Rectangle {
    id: control

    required property string label
    required property list<string> labels
    required property int currentIndex
    property list<url> icons: []
    property list<string> accessibleLabels: labels
    signal selected(int index)

    readonly property int inset: Tokens.space1 / 2
    readonly property int segmentWidth: {
        let widest = 0
        for (let i = 0; i < segments.count; ++i) {
            const segment = segments.itemAt(i)
            if (segment)
                widest = Math.max(widest, segment.implicitWidth)
        }
        return Math.ceil(widest)
    }
    readonly property bool vertical: width < implicitWidth

    implicitWidth: 2 * inset + labels.length * segmentWidth
    implicitHeight: layout.implicitHeight + 2 * inset
    color: Tokens.raised
    radius: Tokens.radius
    border.color: Tokens.control
    border.width: Tokens.lineThin
    Accessible.role: Accessible.Grouping
    Accessible.name: label

    GridLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: control.inset
        columns: control.vertical ? 1 : control.labels.length
        rowSpacing: 0
        columnSpacing: 0

        Repeater {
            id: segments
            // Keep delegates and keyboard focus when translated labels change.
            model: control.labels.length

            RadioButton {
                id: segment
                required property int index

                objectName: control.objectName + "-" + index
                Layout.fillWidth: true
                Layout.preferredWidth: control.segmentWidth
                implicitWidth: display === AbstractButton.IconOnly ? implicitHeight : measure.advanceWidth + 2 * Tokens.space2
                implicitHeight: Math.max(40, contentItem.implicitHeight + 2 * Tokens.space1)
                padding: Tokens.space1
                text: control.labels[index]
                checked: index === control.currentIndex
                focusPolicy: checked ? Qt.StrongFocus : Qt.ClickFocus
                font.pixelSize: Tokens.scaled(Tokens.textControl)
                font.weight: checked ? Font.DemiBold : Font.Normal
                display: control.icons.length ? AbstractButton.IconOnly : AbstractButton.TextOnly
                icon.source: control.icons.length ? control.icons[index] : ""
                readonly property int iconExtent: Tokens.scaled(24)
                icon.width: iconExtent
                icon.height: iconExtent
                icon.color: foreground
                readonly property color foreground: checked ? Tokens.textOnAction : Tokens.ink
                Accessible.name: control.accessibleLabels[index]
                hoverEnabled: true
                ToolTip.visible: (display === AbstractButton.IconOnly || text !== Accessible.name) && (hovered || visualFocus)
                ToolTip.delay: 600
                ToolTip.text: Accessible.name
                TextMetrics {
                    id: measure
                    text: segment.text
                    font.family: segment.font.family
                    font.pixelSize: segment.font.pixelSize
                    font.weight: Font.DemiBold
                }
                indicator: null
                contentItem: IconLabel {
                    display: segment.display
                    icon: segment.icon
                    text: segment.text
                    font: segment.font
                    color: segment.foreground
                    Accessible.ignored: true
                }
                background: Rectangle {
                    radius: control.radius - control.inset
                    color: segment.checked ? Tokens.actionFill : segment.hovered ? Tokens.surface : "transparent"
                    border.color: segment.visualFocus ? Tokens.focus : segment.checked ? Tokens.action : "transparent"
                    border.width: segment.visualFocus ? Tokens.lineFocus : Tokens.lineThin
                }
                onClicked: control.selected(index)
                Keys.onPressed: event => {
                    const count = segments.count
                    let next
                    switch (event.key) {
                    case Qt.Key_Left:
                    case Qt.Key_Up: next = (index + count - 1) % count; break
                    case Qt.Key_Right:
                    case Qt.Key_Down: next = (index + 1) % count; break
                    case Qt.Key_Home: next = 0; break
                    case Qt.Key_End: next = count - 1; break
                    default: return
                    }
                    event.accepted = true
                    segments.itemAt(next).forceActiveFocus(Qt.TabFocusReason)
                    control.selected(next)
                }
            }
        }
    }
}
