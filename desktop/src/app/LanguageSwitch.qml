import Bacuri.Vms.Client.Settings
import Bacuri.Vms.Client.Ui

KitSegmentedControl {
    label: qsTr("Interface language")
    labels: ["EN", "RU"]
    accessibleLabels: ["English", "Русский"]
    currentIndex: Preferences.language === "ru" ? 1 : 0
    onSelected: index => Preferences.language = index === 1 ? "ru" : "en"
}
