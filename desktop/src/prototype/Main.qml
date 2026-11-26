import QtQuick
import QtQuick.Window
import org.freedesktop.gstreamer.Qt6GLVideoItem

// Measurement window of the video prototype: a grid of video cells and a bar that moves
// continuously, so the scene graph renders at the display rate and the frame time of the
// interface is measured independently of the video frame rate.
Window {
    id: root

    required property int cellCount
    readonly property int columns: Math.ceil(Math.sqrt(cellCount))
    readonly property int rows: Math.ceil(cellCount / columns)
    readonly property int gap: 4

    width: 1600
    height: 900
    visible: true
    color: "#12110f"
    title: "Bacuri desktop prototype"

    Grid {
        id: grid

        anchors.fill: parent
        anchors.margins: 8
        anchors.bottomMargin: 28
        columns: root.columns
        spacing: root.gap

        Repeater {
            model: root.cellCount

            Rectangle {
                required property int index

                width: (grid.width - (root.columns - 1) * root.gap) / root.columns
                height: (grid.height - (root.rows - 1) * root.gap) / root.rows
                color: "#0c0b0a"

                GstGLQt6VideoItem {
                    objectName: "video" + parent.index
                    anchors.fill: parent
                }
            }
        }
    }

    Rectangle {
        width: 32
        height: 4
        radius: 2
        color: "#bbb08e"
        y: root.height - 16

        NumberAnimation on x {
            from: 8
            to: root.width - 40
            duration: 2000
            loops: Animation.Infinite
        }
    }
}
