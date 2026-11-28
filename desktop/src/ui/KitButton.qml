import QtQuick
import QtQuick.Controls.Basic
import Bacuri.Vms.Client.Settings

Button {
    id: control

    property bool primary: false
    property string shortcutHint: ""
    property string symbol: ""
    property bool focusIndicator: visualFocus

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: Tokens.space2
    horizontalPadding: symbol ? padding : Tokens.space3
    spacing: Tokens.space2
    font.pixelSize: Tokens.scaled(Tokens.textControl)
    font.weight: checked || primary ? Font.DemiBold : Font.Medium
    display: symbol ? AbstractButton.IconOnly : AbstractButton.TextOnly
    icon.source: symbol ? "qrc:/design/icons/" + symbol + ".svg" : ""
    readonly property int iconExtent: Tokens.scaled(20)
    icon.width: iconExtent
    icon.height: iconExtent
    readonly property color foreground: enabled ? (primary || checked ? Tokens.textOnAction : Tokens.ink) : Tokens.muted
    icon.color: foreground
    palette.brightText: foreground
    palette.buttonText: foreground
    Accessible.name: text
    ToolTip.visible: display === AbstractButton.IconOnly && (hovered || visualFocus)
    ToolTip.delay: 600
    ToolTip.text: shortcutHint ? text + " (" + shortcutHint + ")" : text
    background: Rectangle {
        implicitWidth: 40
        implicitHeight: 40
        radius: Tokens.radius / 2
        color: control.down ? Tokens.actionPressed
             : control.primary ? Tokens.actionFill : control.checked ? Tokens.selected : Tokens.surface
        border.color: control.focusIndicator ? Tokens.focus : control.primary ? Tokens.action : Tokens.line
        border.width: control.focusIndicator ? Tokens.lineFocus : Tokens.lineThin
        Rectangle {
            anchors.fill: parent
            anchors.margins: parent.border.width
            radius: parent.radius
            color: Tokens.actionHover
            opacity: control.hovered && !control.down ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: Preferences.motion ? Tokens.motionShort : 0 }
            }
        }
    }
}
