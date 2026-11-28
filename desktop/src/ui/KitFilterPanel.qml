pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Layouts

ColumnLayout {
    id: panel

    required property string searchLabel
    required property var fields
    required property int totalCount
    required property int resultCount
    readonly property string query: search.text.trim()
    property var values: ({})
    readonly property bool active: query.length > 0 || Object.keys(values).length > 0

    spacing: Tokens.space2
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Filters")

    function reset() {
        search.clear()
        values = ({})
    }

    function focusSearch() {
        search.forceActiveFocus(Qt.ShortcutFocusReason)
        search.selectAll()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Tokens.space1
        KitField {
            id: search
            objectName: "filter"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            label: panel.searchLabel
            placeholderText: label
            leftPadding: magnifier.width + 2 * Tokens.space2
            IconLabel {
                id: magnifier
                anchors.left: parent.left
                anchors.leftMargin: Tokens.space2
                anchors.verticalCenter: parent.verticalCenter
                icon.source: "qrc:/design/icons/search.svg"
                icon.width: Tokens.scaled(18)
                icon.height: Tokens.scaled(18)
                icon.color: Tokens.muted
                Accessible.ignored: true
            }
        }
        KitButton {
            id: disclosure
            objectName: "filterOptions"
            visible: panel.fields.length > 0
            text: qsTr("Filter options")
            symbol: "list-filter"
            checkable: true
        }
        KitButton {
            objectName: "filterReset"
            visible: panel.active || search.text.length > 0
            text: qsTr("Reset filters")
            symbol: "circle-x"
            onClicked: {
                panel.reset()
                search.forceActiveFocus(Qt.TabFocusReason)
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        visible: disclosure.checked
        columns: Math.max(1, Math.min(panel.fields.length, Math.floor((width + columnSpacing) / (13 * search.font.pixelSize + columnSpacing))))
        uniformCellWidths: true
        rowSpacing: Tokens.space2
        columnSpacing: Tokens.space3

        Repeater {
            // Keep keyboard focus on the same field when its labels are translated.
            model: panel.fields.length
            ColumnLayout {
                id: field
                required property int index
                readonly property var definition: panel.fields[index]
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: Tokens.space1

                KitLabel {
                    text: field.definition.label
                    typeSize: Tokens.textControl
                    font.weight: Font.DemiBold
                    Accessible.ignored: true
                }
                KitChoice {
                    objectName: "filter-" + field.definition.key
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    label: field.definition.label
                    symbol: field.definition.symbol
                    model: field.definition.options
                    currentIndex: Math.max(0, field.definition.options.findIndex(option =>
                        option.value === (panel.values[field.definition.key] ?? "")))
                    onActivated: index => {
                        const updated = Object.assign({}, panel.values)
                        const value = field.definition.options[index].value
                        if (value) updated[field.definition.key] = value
                        else delete updated[field.definition.key]
                        panel.values = updated
                    }
                }
            }
        }
    }
    KitLabel {
        Layout.fillWidth: true
        visible: panel.active
        secondary: true
        typeSize: Tokens.textCaption
        text: {
            const parts = [qsTr("%1 of %2").arg(panel.resultCount).arg(panel.totalCount)]
            if (!disclosure.checked) {
                for (const field of panel.fields) {
                    const option = field.options.find(item => item.value === panel.values[field.key])
                    if (option) parts.push(field.label + ": " + option.text)
                }
            }
            return parts.join(" · ")
        }
        Accessible.role: Accessible.StatusBar
    }
}
