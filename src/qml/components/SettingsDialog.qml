import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property var manager

    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Close
    width: 560

    ColumnLayout {
        width: parent.width
        spacing: 16

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 12
            columnSpacing: 12

            Label {
                text: qsTr("Camera")
                Layout.alignment: Qt.AlignVCenter
            }

            DeviceSelector {
                manager: root.manager
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Quality")
                Layout.alignment: Qt.AlignVCenter
            }

            FormatSelector {
                manager: root.manager
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                text: qsTr("Refresh Cameras")
                onClicked: root.manager.refreshDevices()
            }

            Button {
                text: root.manager && root.manager.streaming ? qsTr("Restart Preview") : qsTr("Start Preview")
                onClicked: {
                    if (!root.manager)
                        return
                    if (root.manager.streaming)
                        root.manager.stop()
                    root.manager.start()
                }
            }

            Item { Layout.fillWidth: true }
        }

        Frame {
            Layout.fillWidth: true
            visible: root.manager && root.manager.lastPhotoPath.length > 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: qsTr("Last photo: %1").arg(root.manager ? root.manager.lastPhotoPath : "")
                    wrapMode: Text.WrapAnywhere
                    color: "#4b5563"
                    Layout.fillWidth: true
                }
            }
        }
    }
}
