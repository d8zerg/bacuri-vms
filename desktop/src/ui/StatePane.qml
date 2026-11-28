import QtQuick
import QtQuick.Layouts

KitPanel {
    id: pane
    required property string message
    property string action: ""
    property string actionSymbol: ""
    property bool failure: false
    signal triggered()
    function announceState() {
        if (visible && Window.window?.active && message) Accessible.announce(message)
    }
    onVisibleChanged: Qt.callLater(announceState)
    onMessageChanged: Qt.callLater(announceState)
    contentItem: ColumnLayout {
        spacing: Tokens.space3
        StatusChip {
            visible: pane.failure
            text: qsTr("Unavailable")
            severity: StatusChip.Critical
            symbol: "circle-x"
        }
        KitLabel {
            Layout.fillWidth: true
            text: pane.message
            Accessible.role: Accessible.StatusBar
        }
        KitButton {
            objectName: "stateAction"
            visible: pane.action !== ""
            text: pane.action
            symbol: pane.actionSymbol
            onClicked: pane.triggered()
        }
    }
}
