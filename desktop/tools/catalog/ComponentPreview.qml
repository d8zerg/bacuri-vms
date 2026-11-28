pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Bacuri.Vms.Client.Ui
import Bacuri.Vms.Client.App

Dialog {
    id: dialog
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 24, 560)
    title: qsTr("Component preview")
    footer: KitButton {
        text: qsTr("Close")
        symbol: "circle-x"
        onClicked: dialog.close()
    }
    readonly property var states: [qsTr("Ready"), qsTr("Loading"), qsTr("Empty"), qsTr("Failure"), qsTr("No permission")]
    contentItem: ColumnLayout {
        KitChoice {
            id: type
            objectName: "previewComponent"
            Layout.fillWidth: true
            label: qsTr("Component")
            symbol: "layers"
            model: ["KitButton", "KitField", "KitChoice", "KitSegmentedControl", "StatusChip", "StatePane", "KitFilterPanel", "KitLabel", "KitPanel", "PointRow"].map(x => ({
                        value: x,
                        text: x
                    }))
        }
        KitChoice {
            id: state
            objectName: "previewState"
            Layout.fillWidth: true
            label: qsTr("State")
            symbol: "list-filter"
            model: dialog.states.map((text, index) => ({
                        value: index,
                        text: text
                    }))
        }
        Loader {
            objectName: "previewItem"
            Layout.fillWidth: true
            enabled: state.currentIndex === 0
            sourceComponent: [button, field, choice, segments, chip, pane, filters, label, panel, row][type.currentIndex] ?? null
        }
        StatePane {
            Layout.fillWidth: true
            visible: state.currentIndex > 0 && type.currentText !== "StatePane"
            message: state.displayText
            failure: state.currentIndex >= 3
        }
    }
    Component {
        id: button
        KitButton {
            text: qsTranslate("Shell", "Refresh")
            symbol: "rotate-cw"
        }
    }
    Component {
        id: field
        KitField {
            label: qsTranslate("Shell", "Site address")
            placeholderText: label
        }
    }
    Component {
        id: choice
        KitChoice {
            label: qsTr("State")
            symbol: "layers"
            model: state.model
            currentIndex: state.currentIndex
        }
    }
    Component {
        id: segments
        KitSegmentedControl {
            label: qsTranslate("LanguageSwitch", "Interface language")
            labels: ["EN", "RU"]
            accessibleLabels: ["English", "Русский"]
            currentIndex: 0
            onSelected: index => currentIndex = index
        }
    }
    Component {
        id: chip
        StatusChip {
            text: state.displayText
            symbol: state.currentIndex >= 3 ? "circle-x" : "link-2"
            severity: state.currentIndex >= 3 ? StatusChip.Critical : StatusChip.Good
        }
    }
    Component {
        id: pane
        StatePane {
            message: state.displayText
            failure: state.currentIndex >= 3
        }
    }
    Component {
        id: filters
        KitFilterPanel {
            searchLabel: qsTranslate("PointFilters", "Search by name or ID")
            totalCount: 0
            resultCount: 0
            fields: []
        }
    }
    Component {
        id: label
        KitLabel {
            text: state.displayText
        }
    }
    Component {
        id: panel
        KitPanel {
            contentItem: KitLabel {
                text: state.displayText
            }
        }
    }
    Component {
        id: row
        PointRow {
            width: parent ? parent.width : 0
            index: 0
            point: ({
                    id: "sample",
                    name: qsTr("Observation point"),
                    connection: "online",
                    recording: "current"
                })
            checked: false
            focusIndicator: visualFocus
        }
    }
}
