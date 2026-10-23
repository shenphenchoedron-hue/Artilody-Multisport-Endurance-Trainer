import QtQuick

// Radio-style option card (rowing mode selector).
Rectangle {
    id: opt
    property string title
    property string subtitle
    property string iconName
    property color iconColor: Theme.cyan
    property bool checked: false
    property bool enabled: true
    signal clicked()

    implicitHeight: 56
    radius: 10
    opacity: enabled || checked ? 1 : 0.5
    color: checked ? Theme.navSelected : (mouse.containsMouse && enabled ? Theme.hover : Theme.cardStrong)
    border.width: 1
    border.color: checked ? Theme.accentLight : Theme.cardBorder

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
        onClicked: if (opt.enabled) opt.clicked()
    }
}
