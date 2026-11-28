pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Bacuri.Vms.Client.Settings
import Bacuri.Vms.Client.Ui

KitPanel {
    id: page
    readonly property var themes: ["system", "light", "dark"]
    readonly property var scales: [1, 1.25, 1.5, 2]

    component SettingLabel: KitLabel {
        required property KitSegmentedControl selector

        objectName: selector.objectName + "Label"
        text: selector.label + ":"
        typeSize: Tokens.textControl
        font.weight: Font.DemiBold
        Layout.fillWidth: true
        Accessible.ignored: true
    }

    contentItem: Item {
        implicitWidth: Math.ceil(grid.pairedWidth)
        implicitHeight: grid.implicitHeight

        GridLayout {
            id: grid
            readonly property real pairedWidth: Math.max(languageLabel.implicitWidth, themeLabel.implicitWidth, scaleLabel.implicitWidth)
                + columnSpacing + Math.max(language.implicitWidth, theme.implicitWidth, scale.implicitWidth)
            width: Math.min(page.availableWidth, implicitWidth)
            columns: page.availableWidth >= pairedWidth ? 2 : 1
            columnSpacing: Tokens.space2
            rowSpacing: columns === 1 ? Tokens.space1 : Tokens.space3

            SettingLabel { id: languageLabel; selector: language }
            LanguageSwitch {
                id: language
                objectName: "languageSetting"
                Layout.minimumWidth: implicitWidth
            }
            SettingLabel {
                id: themeLabel
                selector: theme
                Layout.topMargin: grid.columns === 1 ? Tokens.space2 : 0
            }
            KitSegmentedControl {
                id: theme
                objectName: "themeSetting"
                Layout.minimumWidth: implicitWidth
                label: qsTr("Theme")
                labels: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                icons: ["qrc:/design/icons/monitor.svg", "qrc:/design/icons/sun.svg", "qrc:/design/icons/moon.svg"]
                currentIndex: page.themes.indexOf(Preferences.theme)
                onSelected: index => Preferences.theme = page.themes[index]
            }
            SettingLabel {
                id: scaleLabel
                selector: scale
                Layout.topMargin: grid.columns === 1 ? Tokens.space2 : 0
            }
            KitSegmentedControl {
                id: scale
                objectName: "scaleSetting"
                Layout.fillWidth: true
                Layout.minimumWidth: segmentWidth + 2 * inset
                Layout.maximumWidth: implicitWidth
                label: qsTr("Text size")
                labels: page.scales.map(value => (100 * value) + "%")
                currentIndex: page.scales.indexOf(Preferences.textScale)
                onSelected: index => Preferences.textScale = page.scales[index]
            }
        }
    }
}
