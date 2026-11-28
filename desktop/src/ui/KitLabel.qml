import QtQuick
import QtQuick.Controls.Basic

Label {
    property real typeSize: Tokens.textBody
    property bool secondary: false

    font.pixelSize: Tokens.scaled(typeSize)
    color: secondary ? Tokens.muted : Tokens.ink
    wrapMode: Text.Wrap
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
