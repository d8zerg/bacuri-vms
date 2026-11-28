pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

ComboBox {
    id: choice

    required property string label
    required property string symbol

    textRole: "text"
    valueRole: "value"
    Accessible.name: label
    Accessible.description: displayText
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    font.pixelSize: Tokens.scaled(Tokens.textControl)
    padding: Tokens.space2
    spacing: Tokens.space2
    palette.window: Tokens.surface
    palette.button: Tokens.surface
    palette.buttonText: Tokens.ink
    palette.text: Tokens.ink
    palette.windowText: Tokens.ink
    palette.dark: Tokens.muted
    palette.mid: Tokens.control
    palette.highlight: Tokens.actionFill
    palette.highlightedText: Tokens.textOnAction
    contentItem: RowLayout {
        spacing: Tokens.space2
        IconLabel {
            icon.source: "qrc:/design/icons/" + choice.symbol + ".svg"
            icon.width: Tokens.scaled(18)
            icon.height: Tokens.scaled(18)
            icon.color: Tokens.muted
            Accessible.ignored: true
        }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: choice.displayText
            font: choice.font
            color: Tokens.ink
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: ListView.view.width
        text: modelData.text
        highlighted: choice.highlightedIndex === index
        font.pixelSize: choice.font.pixelSize
        font.weight: choice.currentIndex === index ? Font.DemiBold : Font.Normal
        padding: Tokens.space2
        contentItem: Text {
            text: option.text
            font: option.font
            color: Tokens.ink
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }
    }
    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 42
        radius: Tokens.radius / 2
        color: choice.down ? Tokens.raised : Tokens.surface
        border.color: choice.visualFocus ? Tokens.focus : Tokens.control
        border.width: choice.visualFocus ? Tokens.lineFocus : Tokens.lineThin
    }
}
