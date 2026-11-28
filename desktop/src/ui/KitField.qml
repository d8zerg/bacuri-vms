import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: field
    required property string label
    font.pixelSize: Tokens.scaled(Tokens.textBody)
    color: Tokens.ink
    placeholderTextColor: Tokens.muted
    selectionColor: Tokens.actionFill
    selectedTextColor: Tokens.textOnAction
    padding: Tokens.space2
    selectByMouse: true
    Accessible.name: label
    background: Rectangle {
        implicitHeight: 42
        implicitWidth: 220
        radius: Tokens.radius / 2
        color: Tokens.surface
        border.color: field.activeFocus ? Tokens.focus : Tokens.control
        border.width: field.activeFocus ? Tokens.lineFocus : Tokens.lineThin
    }
}
