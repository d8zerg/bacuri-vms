pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Bacuri.Vms.Client.App
import Bacuri.Vms.Client.Ui

Shell {
    id: examples
    workspace: CatalogWorkspace {
        id: samples
    }
    overviewTitle: qsTranslate("Shell", "Component catalog")
    overviewTools: KitPanel {
        contentItem: ColumnLayout {
            spacing: Tokens.space2
            KitLabel {
                text: qsTr("Catalog state")
                font.bold: true
            }
            KitChoice {
                objectName: "catalogScenario"
                Layout.fillWidth: true
                Layout.maximumWidth: Math.max(260, 16 * font.pixelSize)
                label: qsTr("Catalog state")
                symbol: "layers"
                model: [
                    {
                        value: 0,
                        text: qsTr("Populated")
                    },
                    {
                        value: 1,
                        text: qsTr("Waiting")
                    },
                    {
                        value: 2,
                        text: qsTr("Empty")
                    },
                    {
                        value: 3,
                        text: qsTr("Failure")
                    },
                    {
                        value: 4,
                        text: qsTr("Recovered")
                    },
                    {
                        value: 5,
                        text: qsTr("No permission")
                    }
                ]
                currentIndex: samples.scenario
                onActivated: index => samples.scenario = index
            }
            KitButton {
                objectName: "componentPreview"
                text: qsTr("Component preview")
                onClicked: preview.open()
            }
            ComponentPreview {
                id: preview
            }
        }
    }
}
