import QtQuick
import QtQuick.Controls.impl
import QtQuick.Layouts
import Bacuri.Vms.Client.Ui

KitButton {
    id: row
    required property var point
    required property int index

    width: ListView.view.width
    objectName: "point-" + point.id
    text: point.name
    checked: ListView.isCurrentItem
    focusIndicator: visualFocus || (checked && ListView.view.activeFocus)
    activeFocusOnTab: false
    Accessible.role: Accessible.ListItem
    Accessible.description: connection.text + ", " + recording.text
    contentItem: RowLayout {
        spacing: Tokens.space3
        IconLabel {
            icon.source: "qrc:/design/icons/cctv.svg"
            icon.width: Tokens.scaled(22)
            icon.height: Tokens.scaled(22)
            icon.color: Tokens.muted
            Layout.alignment: Qt.AlignTop
            Accessible.ignored: true
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Tokens.space1
            KitLabel {
                Layout.fillWidth: true
                text: row.text
                typeSize: Tokens.textControl
                font.weight: Font.DemiBold
                Accessible.ignored: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: Tokens.space1
                PointStatus { id: connection; recording: false; value: row.point.connection }
                PointStatus { id: recording; recording: true; value: row.point.recording }
            }
        }
    }
}
