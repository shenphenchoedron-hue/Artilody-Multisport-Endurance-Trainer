import QtQuick

// Radio-style option card (rowing mode selector). Uses Item.enabled (not a
// redeclared property) so disabling also blocks input of the children.
// Keyboard: Tab to focus, Space / Enter to choose.
Rectangle {
    id: opt
    property string title
    property string subtitle
    property string iconName
    property color iconColor: Theme.cyan
    property bool checked: false
    signal clicked()

    activeFocusOnTab: true
    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.description: subtitle
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onPressAction: if (enabled) clicked()
    Keys.onSpacePressed: if (enabled) clicked()
    Keys.onReturnPressed: if (enabled) clicked()
    Keys.onEnterPressed: if (enabled) clicked()

    implicitHeight: 56
    radius: 10
    opacity: enabled || checked ? 1 : 0.5
    color: checked ? Theme.navSelected : (mouse.containsMouse && enabled ? Theme.hover : Theme.cardStrong)
    border.color: checked || activeFocus ? Theme.accentLight : Theme.cardBorder
    border.width: activeFocus ? 2 : 1

    // radio
    Rectangle {
        id: radio
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 20; height: 20; radius: 10
        color: "transparent"
        border.width: 2
        border.color: opt.checked ? Theme.accentLight : Theme.textSecondary
        Rectangle {
            anchors.centerIn: parent
            width: 10; height: 10; radius: 5
            visible: opt.checked
            color: Theme.accentLight
        }
    }
    Icon {
        id: icon
        anchors.left: radio.right
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        name: opt.iconName
        color: opt.iconColor
        size: 26
        strokeWidth: 2.2
    }
    Column {
        anchors.left: icon.right
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Text {
            width: parent.width
            text: opt.title
            color: Theme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: opt.subtitle
            color: Theme.textSecondary
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: opt.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: opt.clicked()   // a disabled item gets no mouse events
    }
}
