import Bacuri.Vms.Client.Ui

KitFilterPanel {
    required property PointFilterModel proxy
    onQueryChanged: proxy.query = query
    onValuesChanged: proxy.criteria = values
    searchLabel: qsTr("Search by name or ID")
    totalCount: (proxy.sourceModel as PointModel)?.count ?? 0
    resultCount: proxy.count
    fields: [
        { key: "connection", label: qsTr("Connection"), symbol: "link-2", options: [
            { value: "", text: qsTr("Any connection") },
            { value: "online", text: qsTranslate("PointStatus", "Online") },
            { value: "offline", text: qsTranslate("PointStatus", "Offline") },
            { value: "unknown", text: qsTranslate("PointStatus", "Connection unknown") }
        ] },
        { key: "recording", label: qsTr("Recording status"), symbol: "hard-drive", options: [
            { value: "", text: qsTr("Any recording status") },
            { value: "current", text: qsTranslate("PointStatus", "Recording") },
            { value: "delayed", text: qsTranslate("PointStatus", "Recording delayed") },
            { value: "stopped", text: qsTranslate("PointStatus", "Recording stopped") },
            { value: "unknown", text: qsTranslate("PointStatus", "Recording unknown") }
        ] }
    ]
}
