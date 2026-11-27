import QtQuick
import QtQuick.Window
import org.freedesktop.gstreamer.Qt6GLVideoItem
import Bacuri.Vms.Client.Map

// Measurement window of the video and map bench of Bacuri VMS Client: a grid of video cells and a bar that moves
// continuously, so the scene graph renders at the display rate and the frame time of the
// interface is measured independently of the video frame rate. With a map style the map is
// shown next to the grid and turns slowly, so it renders continuously beside the video.
Window {
    id: root

    required property int cellCount
    required property string mapStyle
    readonly property int columns: Math.ceil(Math.sqrt(cellCount))
    readonly property int rows: Math.ceil(cellCount / columns)
    readonly property int gap: 4

    width: 1600
    height: 900
    visible: true
    color: "#12110f"
    title: "Bacuri VMS Client bench"

    Grid {
        id: grid

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.mapStyle === "" ? root.width - 16 : root.width * 0.66
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

    Loader {
        active: root.mapStyle !== ""
        anchors.left: grid.right
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.bottom: grid.bottom

        sourceComponent: MapView {
            objectName: "map"
            styleUrl: root.mapStyle
            latitude: 43.7375
            longitude: 7.4225
            zoom: 15.5

            NumberAnimation on bearing {
                from: 0
                to: 360
                duration: 60000
                loops: Animation.Infinite
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
