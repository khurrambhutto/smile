import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root

    property var manager
    signal refreshRequested()
    signal settingsRequested()

    implicitHeight: 76
    padding: 14

    background: Rectangle {
        color: "#191c22"
        border.color: "#2a2f39"
        border.width: 1
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        Label {
            text: qsTr("Smile")
            color: "#f6f7fb"
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignVCenter
        }

        Rectangle {
            width: 1
            Layout.fillHeight: true
            color: "#2a2f39"
        }

        DeviceSelector {
            manager: root.manager
            Layout.preferredWidth: 360
            Layout.alignment: Qt.AlignVCenter
        }

        FormatSelector {
            manager: root.manager
            Layout.preferredWidth: 300
            Layout.alignment: Qt.AlignVCenter
        }

        Item { Layout.fillWidth: true }

        Button {
            text: qsTr("Refresh")
            icon.name: "view-refresh"
            onClicked: root.refreshRequested()
        }

        Button {
            text: qsTr("Settings")
            icon.name: "preferences-system"
            onClicked: root.settingsRequested()
        }
    }
}
