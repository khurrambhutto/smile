import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property bool recording: false
    property string durationText: "00:00"

    visible: recording
    radius: 999
    color: "#d9b91c1c"
    border.color: "#ff6b6b"
    border.width: 1
    implicitWidth: row.implicitWidth + 24
    implicitHeight: 34

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        Rectangle {
            width: 9
            height: 9
            radius: 9
            color: "#ff6b6b"
            anchors.verticalCenter: parent.verticalCenter
        }

        Label {
            text: qsTr("REC %1").arg(root.durationText)
            color: "white"
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }
}
