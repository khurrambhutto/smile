import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    property var manager

    model: manager ? manager.formatModel : null
    textRole: "description"
    currentIndex: manager ? manager.selectedFormatIndex : -1
    enabled: count > 0
    popup.height: Math.min(420, contentItem.implicitHeight + 24)

    onActivated: function(index) {
        if (manager)
            manager.selectedFormatIndex = index
    }

    ToolTip.visible: hovered && currentText.length > 0
    ToolTip.text: currentText
}
