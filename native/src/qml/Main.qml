import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: root

    width: 980
    height: 680
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    title: qsTr("Smile")
    color: theme.window

    QtObject {
        id: theme
        readonly property bool dark: darkMode.checked
        readonly property color window: dark ? "#111216" : "#f1f3f4"
        readonly property color panel: dark ? "#202228" : "#ffffff"
        readonly property color panelSoft: dark ? "#2a2d34" : "#e9ecef"
        readonly property color text: dark ? "#f7f7f8" : "#1f2328"
        readonly property color muted: dark ? "#a8adb7" : "#65707d"
        readonly property color border: dark ? "#3a3f49" : "#d8dde3"
        readonly property color accent: "#e7363f"
        readonly property color accentPressed: "#b91f28"
        readonly property color glass: dark ? "#cc17191f" : "#ddffffff"
    }

    Component.onCompleted: cameraManager.start()
    onClosing: cameraManager.stop()

    SettingsDialog {
        id: settingsDialog
        manager: cameraManager
    }

    CameraPreview {
        anchors.fill: parent
        manager: cameraManager
        mirrorPreview: true
    }

    Rectangle {
        anchors.fill: parent
        visible: flashOverlay.opacity > 0
        color: "white"
        opacity: 0
        id: flashOverlay

        Behavior on opacity {
            NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: effectsOpen.checked ? 216 : 132
        color: "#00000000"

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: theme.dark ? "#f0101114" : "#e8f1f3f4" }
        }

        Behavior on height {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 22
        spacing: 14

        RowLayout {
            visible: effectsOpen.checked
            Layout.fillWidth: true
            spacing: 10

            ListModel {
                id: effectsModel
                ListElement { label: "Natural"; value: 0 }
                ListElement { label: "Blur"; value: 1 }
                ListElement { label: "B&W"; value: 2 }
                ListElement { label: "Comic"; value: 3 }
            }

            Repeater {
                model: effectsModel

                delegate: Button {
                    Layout.preferredWidth: 118
                    Layout.preferredHeight: 54
                    text: model.label
                    checkable: true
                    checked: cameraManager.filterMode === model.value
                    onClicked: cameraManager.filterMode = model.value

                    background: Rectangle {
                        radius: 14
                        color: parent.checked ? theme.accent : theme.glass
                        border.color: parent.checked ? "#00ffffff" : theme.border
                        border.width: 1
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.checked ? "white" : theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            radius: 24
            color: theme.glass
            border.color: theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 18

                RowLayout {
                    Layout.preferredWidth: 210
                    spacing: 8

                    Button {
                        id: photoMode
                        Layout.preferredWidth: 94
                        Layout.preferredHeight: 46
                        text: qsTr("Photo")
                        checkable: true
                        checked: cameraManager.captureMode === 0
                        enabled: !cameraManager.recording
                        onClicked: cameraManager.captureMode = 0
                        background: Rectangle {
                            radius: 14
                            color: photoMode.checked ? theme.panelSoft : "#00000000"
                            border.color: photoMode.checked ? theme.border : "#00000000"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: photoMode.text
                            color: theme.text
                            font.pixelSize: 14
                            font.weight: photoMode.checked ? Font.DemiBold : Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Button {
                        id: videoMode
                        Layout.preferredWidth: 94
                        Layout.preferredHeight: 46
                        text: qsTr("Video")
                        checkable: true
                        checked: cameraManager.captureMode === 1
                        onClicked: cameraManager.captureMode = 1
                        background: Rectangle {
                            radius: 14
                            color: videoMode.checked ? theme.panelSoft : "#00000000"
                            border.color: videoMode.checked ? theme.border : "#00000000"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: videoMode.text
                            color: theme.text
                            font.pixelSize: 14
                            font.weight: videoMode.checked ? Font.DemiBold : Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: shutter
                    Layout.preferredWidth: 74
                    Layout.preferredHeight: 74
                    enabled: cameraManager.streaming
                    onClicked: {
                        if (cameraManager.captureMode === 1) {
                            cameraManager.toggleRecording()
                        } else {
                            countdown.begin()
                        }
                    }

                    background: Rectangle {
                        radius: width / 2
                        color: "transparent"
                        border.width: 4
                        border.color: shutter.enabled ? "#ffffff" : "#88ffffff"

                        Rectangle {
                            anchors.centerIn: parent
                            width: cameraManager.recording ? 34 : (shutter.down ? 52 : 58)
                            height: cameraManager.recording ? 34 : width
                            radius: cameraManager.recording ? 9 : width / 2
                            color: shutter.enabled ? theme.accent : "#888888"
                            border.color: shutter.enabled ? "#ff8d92" : "#aaaaaa"
                            border.width: 1

                            Behavior on width { NumberAnimation { duration: 100 } }
                            Behavior on radius { NumberAnimation { duration: 100 } }
                        }
                    }

                    contentItem: Item {}
                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    Layout.preferredWidth: 210
                    spacing: 8

                    Button {
                        id: effectsOpen
                        Layout.preferredWidth: 102
                        Layout.preferredHeight: 46
                        text: qsTr("Effects")
                        checkable: true
                        background: Rectangle {
                            radius: 14
                            color: effectsOpen.checked ? theme.panelSoft : "#00000000"
                            border.color: effectsOpen.checked ? theme.border : "#00000000"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: effectsOpen.text
                            color: theme.text
                            font.pixelSize: 14
                            font.weight: effectsOpen.checked ? Font.DemiBold : Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Button {
                        id: darkMode
                        Layout.preferredWidth: 86
                        Layout.preferredHeight: 46
                        text: checked ? qsTr("Dark") : qsTr("Light")
                        checkable: true
                        checked: true
                        background: Rectangle {
                            radius: 14
                            color: darkMode.checked ? theme.panelSoft : "#00000000"
                            border.color: darkMode.checked ? theme.border : "#00000000"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: darkMode.text
                            color: theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }

    Label {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 18
        padding: 10
        leftPadding: 16
        rightPadding: 16
        text: cameraManager.recording ? qsTr("Recording") : cameraManager.statusMessage
        color: "white"
        visible: text.length > 0
        background: Rectangle {
            radius: 16
            color: cameraManager.recording ? "#ccad1720" : "#99000000"
            border.color: "#33ffffff"
        }
    }

    Label {
        anchors.centerIn: parent
        text: countdown.remaining > 0 ? countdown.remaining : ""
        visible: countdown.remaining > 0
        color: "white"
        font.pixelSize: 112
        font.weight: Font.Bold
        style: Text.Outline
        styleColor: "#77000000"
    }

    Timer {
        id: countdown
        property int remaining: 0
        interval: 1000
        repeat: true

        function begin() {
            remaining = 3
            restart()
        }

        onTriggered: {
            remaining -= 1
            if (remaining <= 0) {
                stop()
                flashOverlay.opacity = 0.92
                flashReset.start()
                cameraManager.capturePhoto()
            }
        }
    }

    Timer {
        id: flashReset
        interval: 120
        onTriggered: flashOverlay.opacity = 0
    }

}
