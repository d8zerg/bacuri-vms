pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import Bacuri.Vms.Client.Ui

ColumnLayout {
    id: section
    required property WindowCommands registry
    readonly property bool capturing: editingId !== ""
    property string editingId: ""
    property string feedbackId: ""
    property string feedbackText: ""
    spacing: Tokens.space1

    function openSection() {
        disclosure.checked = true;
        disclosure.forceActiveFocus(Qt.ShortcutFocusReason);
    }
    function clearFeedback() {
        feedbackId = "";
        feedbackText = "";
    }
    function save(id: string, sequence: string, restore: bool) {
        const error = registry.setBinding(id, sequence, restore);
        feedbackId = id;
        feedbackText = error;
        if (!error)
            editingId = "";
        disclosure.Accessible.announce(error || qsTr("Shortcut updated."));
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Tokens.lineThin
        color: Tokens.line
    }
    ItemDelegate {
        id: disclosure
        objectName: "shortcutDisclosure"
        Layout.fillWidth: true
        checkable: true
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        text: (section.registry.entries.bindings?.name || "")
        padding: Tokens.space1
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: checked ? qsTr("Expanded") : qsTr("Collapsed")
        Accessible.onPressAction: disclosure.toggle()
        onCheckedChanged: {
            section.editingId = "";
            section.clearFeedback();
        }
        contentItem: RowLayout {
            KitLabel {
                Layout.fillWidth: true
                typeSize: Tokens.textControl
                font.weight: Font.DemiBold
                text: disclosure.text
                Accessible.ignored: true
            }
            IconLabel {
                icon.source: "qrc:/design/icons/chevron-down.svg"
                icon.width: Tokens.scaled(20)
                icon.height: Tokens.scaled(20)
                icon.color: Tokens.muted
                rotation: disclosure.checked ? 0 : -90
                Accessible.ignored: true
            }
        }
        background: Rectangle {
            implicitHeight: Tokens.scaled(40)
            radius: Tokens.radius / 2
            color: disclosure.hovered ? Tokens.raised : Tokens.surface
            border.color: disclosure.visualFocus ? Tokens.focus : "transparent"
            border.width: Tokens.lineFocus
        }
    }

    ColumnLayout {
        id: table
        objectName: "shortcutTable"
        visible: disclosure.checked
        Layout.fillWidth: true
        spacing: 0
        readonly property real actionWidth: Math.floor((section.width - Tokens.space2) * 0.42)
        Accessible.role: Accessible.Table
        Accessible.name: disclosure.text
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Tokens.space1
            spacing: Tokens.space2
            KitLabel {
                Layout.preferredWidth: table.actionWidth
                Layout.minimumWidth: 0
                typeSize: Tokens.textMeta
                secondary: true
                text: qsTr("Action")
                Accessible.role: Accessible.ColumnHeader
            }
            KitLabel {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                typeSize: Tokens.textMeta
                secondary: true
                text: qsTr("Shortcut")
                Accessible.role: Accessible.ColumnHeader
            }
        }
        Repeater {
            model: disclosure.checked ? section.registry.commandIds : []
            ColumnLayout {
                id: row
                required property string modelData
                readonly property var command: section.registry.entries[modelData]
                readonly property bool editing: section.editingId === modelData
                readonly property string bindingText: command.bindings.join(" · ") || qsTr("Unassigned")
                Layout.fillWidth: true
                spacing: 0
                Accessible.role: Accessible.Row
                Accessible.name: command.name
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Tokens.lineThin
                    color: Tokens.line
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Tokens.space1
                    spacing: Tokens.space2
                    KitLabel {
                        Layout.preferredWidth: table.actionWidth
                        Layout.minimumWidth: 0
                        typeSize: Tokens.textControl
                        text: row.command.name
                        Accessible.role: Accessible.Cell
                    }
                    ItemDelegate {
                        id: editor
                        objectName: "shortcut-" + row.modelData
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        padding: Tokens.space1
                        text: row.editing ? qsTr("Press a shortcut") : row.bindingText
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Change shortcut for %1").arg(row.command.name)
                        Accessible.description: text
                        Accessible.onPressAction: editor.clicked()
                        onClicked: {
                            section.clearFeedback();
                            section.editingId = row.editing ? "" : row.modelData;
                            forceActiveFocus();
                        }
                        onActiveFocusChanged: if (!activeFocus && row.editing) section.editingId = ""
                        Keys.onShortcutOverride: event => { if (row.editing) event.accepted = true; }
                        Keys.onPressed: event => {
                            if (!row.editing)
                                return;
                            if (event.key === Qt.Key_Escape && !event.modifiers) {
                                event.accepted = true;
                                section.editingId = "";
                                section.clearFeedback();
                                return;
                            }
                            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                                if (!(event.modifiers & ~Qt.ShiftModifier)) {
                                    section.editingId = "";
                                    return;
                                }
                            }
                            event.accepted = true;
                            const sequence = section.registry.keyName(event.key, event.modifiers);
                            if (sequence && !event.isAutoRepeat)
                                section.save(row.modelData, sequence, false);
                        }
                        contentItem: KitLabel {
                            typeSize: Tokens.textControl
                            text: editor.text
                            wrapMode: Text.Wrap
                            color: row.editing ? Tokens.textOnAction : row.command.bindings.length ? Tokens.ink : Tokens.muted
                            Accessible.ignored: true
                        }
                        background: Rectangle {
                            implicitHeight: Tokens.scaled(34)
                            radius: Tokens.radius / 2
                            color: row.editing ? Tokens.actionFill : editor.hovered ? Tokens.raised : Tokens.surface
                            border.color: editor.visualFocus || row.editing ? Tokens.focus : Tokens.line
                            border.width: editor.visualFocus || row.editing ? Tokens.lineFocus : Tokens.lineThin
                        }
                    }
                    KitButton {
                        objectName: "restore-" + row.modelData
                        visible: row.command.customized
                        text: qsTr("Restore default for %1").arg(row.command.name)
                        symbol: "rotate-cw"
                        onClicked: {
                            editor.forceActiveFocus(Qt.TabFocusReason);
                            section.save(row.modelData, "", true);
                        }
                    }
                }
                KitLabel {
                    Layout.fillWidth: true
                    Layout.leftMargin: Tokens.space1
                    Layout.bottomMargin: Tokens.space1
                    visible: section.feedbackId === row.modelData && section.feedbackText.length > 0
                    text: visible ? section.feedbackText : ""
                    color: Tokens.danger
                    typeSize: Tokens.textMeta
                    Accessible.role: Accessible.StatusBar
                }
            }
        }
    }
}
