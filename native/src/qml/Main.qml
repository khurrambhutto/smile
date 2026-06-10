import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: root

    width: 900
    height: 620
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: qsTr("smile")
    color: "#050608"

    Component.onCompleted: cameraManager.start()
    onClosing: cameraManager.stop()

    SettingsDialog {
        id: settingsDialog
        manager: cameraManager
    }

    CameraPreview {
        anchors.fill: parent
        manager: cameraManager
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.min(190, parent.height * 0.32)
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: "#cc050608" }
        }

    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        anchors.bottomMargin: 22
        height: 78

        RoundButton {
            id: settingsButton
            Layout.preferredWidth: 46
            Layout.preferredHeight: 46
            radius: 14
            text: qsTr("⚙")
            font.pixelSize: 20
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Settings")
            onClicked: settingsDialog.open()

            background: Rectangle {
                radius: settingsButton.radius
                color: settingsButton.down ? "#4dffffff" : "#26ffffff"
                border.color: "#40ffffff"
                border.width: 1
            }

            contentItem: Text {
                text: settingsButton.text
                color: "white"
                font: settingsButton.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Item { Layout.fillWidth: true }

        Button {
            id: captureButton
            Layout.preferredWidth: 76
            Layout.preferredHeight: 76
            enabled: cameraManager.streaming
            hoverEnabled: true
            onClicked: cameraManager.capturePhoto()
            ToolTip.visible: hovered && !enabled
            ToolTip.text: qsTr("Waiting for preview")

            background: Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.color: captureButton.enabled ? "#f4f6fb" : "#66ffffff"
                border.width: 4

                Rectangle {
                    anchors.centerIn: parent
                    width: captureButton.down ? 56 : 60
                    height: width
                    radius: width / 2
                    color: captureButton.enabled ? "#ef2f35" : "#777777"
                    border.color: captureButton.enabled ? "#ff6b6f" : "#999999"
                    border.width: 1

                    Behavior on width { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
                }
            }

            contentItem: Item {}
        }

        Item { Layout.fillWidth: true }

        Item {
            Layout.preferredWidth: 46
            Layout.preferredHeight: 46
        }
    }
}
