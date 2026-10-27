import QtQuick
import QtQuick.Controls

Rectangle {
    id: btn
    property string iconName
    property real iconSize: 20
    property color iconColor: Theme.text
    property bool flat: false
    property bool enabled: true
    property string tooltip
    signal clicked()

    property string accessibleName: tooltip.length > 0 ? tooltip : iconName

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: if (btn.enabled) btn.clicked()

    implicitWidth: flat ? iconSize + 12 : 46
    implicitHeight: flat ? iconSize + 12 : 46
    radius: 10
    opacity: enabled ? 1 : 0.4
    color: flat ? (mouse.containsMouse ? Theme.hover : "transparent")
                : (mouse.pressed ? Theme.hover : Theme.cardStrong)
    border.width: flat ? 0 : 1
    border.color: Theme.fieldBorder

    Icon {
        anchors.centerIn: parent
        name: btn.iconName
        size: btn.iconSize
        color: btn.iconColor
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        // flat icon buttons are small: a slightly larger touch target
        anchors.margins: btn.flat ? -4 : 0
        hoverEnabled: true
        cursorShape: btn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (btn.enabled) btn.clicked()
    }

    ToolTip.visible: tooltip.length > 0 && mouse.containsMouse
    ToolTip.delay: 600
    ToolTip.text: tooltip
}
