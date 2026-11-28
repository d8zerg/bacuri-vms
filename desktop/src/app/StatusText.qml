pragma Singleton
import QtQml

QtObject {
    function describe(value: string, recording: bool): string {
        return value === "online" ? qsTranslate("PointStatus", "Online") : value === "offline" ? qsTranslate("PointStatus", "Offline")
        : value === "current" ? qsTranslate("PointStatus", "Recording") : value === "delayed" ? qsTranslate("PointStatus", "Recording delayed")
        : value === "stopped" ? qsTranslate("PointStatus", "Recording stopped")
        : recording ? qsTranslate("PointStatus", "Recording unknown") : qsTranslate("PointStatus", "Connection unknown")
    }
}
