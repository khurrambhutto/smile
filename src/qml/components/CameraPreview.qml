import QtQuick
import QtQuick.Controls
import QtMultimedia

Item {
    id: root

    property var manager
    property bool mirrorPreview: false

    Rectangle {
        anchors.fill: parent
        color: "#050608"
    }

    VideoOutput {
        id: output
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
        transform: Scale {
            origin.x: output.width / 2
            xScale: root.mirrorPreview ? -1 : 1
        }

        Component.onCompleted: {
            if (root.manager)
                root.manager.videoSink = output.videoSink
        }

        Component.onDestruction: {
            if (root.manager && root.manager.videoSink === output.videoSink)
                root.manager.videoSink = null
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.manager && root.manager.errorMessage.length > 0
        color: "#cc050608"

        Label {
            anchors.centerIn: parent
            width: Math.min(root.width - 96, 620)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.manager ? root.manager.errorMessage : ""
            color: "#f5f6fb"
            font.pixelSize: 16
        }
    }
}
