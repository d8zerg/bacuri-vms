import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

Control {
    id: chip
    required property string text
    required property string symbol
    property int severity: StatusChip.Neutral
    enum Severity { Neutral, Good, Caution, Critical }

    readonly property color foreground: [Tokens.info, Tokens.success, Tokens.warning, Tokens.danger][severity]
    padding: Tokens.space1
    horizontalPadding: Tokens.space2
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    contentItem: RowLayout {
        spacing: Tokens.space1
        IconLabel {
            icon.source: "qrc:/design/icons/" + chip.symbol + ".svg"
            icon.color: chip.foreground
            icon.width: Tokens.scaled(16)
            icon.height: Tokens.scaled(16)
            Accessible.ignored: true
        }
        KitLabel {
            text: chip.text
            typeSize: Tokens.textCaption
            color: chip.foreground
            Accessible.ignored: true
        }
    }
    background: Rectangle {
        color: [Tokens.infoBg, Tokens.successBg, Tokens.warningBg, Tokens.dangerBg][chip.severity]
        radius: Math.min(height / 2, Tokens.radius / 2)
    }
}
