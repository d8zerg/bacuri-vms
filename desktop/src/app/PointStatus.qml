import QtQuick
import Bacuri.Vms.Client.Ui

StatusChip {
    required property string value
    required property bool recording
    readonly property bool unknown: !["online", "offline", "current", "delayed", "stopped"].includes(value)
    text: StatusText.describe(value,recording)
    symbol: recording ? (value === "current" ? "hard-drive" : "archive-x")
         : value === "online" ? "link-2" : "unplug"
    severity: unknown ? StatusChip.Neutral : value === "offline" ? StatusChip.Critical
            : value === "delayed" || value === "stopped" ? StatusChip.Caution : StatusChip.Good
}
