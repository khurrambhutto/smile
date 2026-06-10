import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    property var manager

    padding: 18

    background: Rectangle {
        color: "#191c22"
        border.color: "#2a2f39"
        border.width: 1
    }

    RowLayout {
        anchors.fill: parent
        spacing: 18

        Label {
            text: root.manager ? root.manager.statusMessage : ""
            color: root.manager && root.manager.errorMessage.length > 0 ? "#fca5a5" : "#d7dbe6"
            elide: Text.ElideMiddle
            Layout.fillWidth: true
            font.pixelSize: 14
        }

        Button {
            text: root.manager && root.manager.streaming ? qsTr("Stop Preview") : qsTr("Start Preview")
            enabled: root.manager && root.manager.selectedDeviceIndex >= 0
            onClicked: {
                if (root.manager.streaming)
                    root.manager.stop()
                else
                    root.manager.start()
            }
        }

        Button {
            id: captureButton
            text: qsTr("Take Photo")
            enabled: root.manager && root.manager.streaming
            highlighted: true
            icon.name: "camera-photo"
            onClicked: root.manager.capturePhoto()
        }
    }
}
