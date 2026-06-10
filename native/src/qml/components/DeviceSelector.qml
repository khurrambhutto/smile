import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    property var manager

    model: manager ? manager.deviceModel : null
    textRole: "displayName"
    valueRole: "devicePath"
    currentIndex: manager ? manager.selectedDeviceIndex : -1
    enabled: count > 0
    popup.height: Math.min(360, contentItem.implicitHeight + 24)

    onActivated: function(index) {
        if (manager)
            manager.selectedDeviceIndex = index
    }

    ToolTip.visible: hovered && currentText.length > 0
    ToolTip.text: currentText
}
