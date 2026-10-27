import QtQuick

Rectangle {
    id: item
    property string label
    property string iconName
    property bool selected: false
    signal clicked()

    height: 52
    radius: 10
    color: selected ? Theme.navSelected : (mouse.containsMouse ? Theme.hover : "transparent")
    Behavior on color { ColorAnimation { duration: 120 } }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        x: 18
        spacing: 16
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: item.iconName
            size: 26
            color: item.selected ? Theme.accentLight : Theme.text
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            // long labels ("Watt Training") shrink a little instead of
            // touching the edge of the highlight
            width: item.width - 18 - 26 - 16 - 10
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 15
            elide: Text.ElideRight
            text: item.label
            font.pixelSize: 18
            font.weight: item.selected ? Font.DemiBold : Font.Normal
            color: item.selected ? Theme.accentLight : Theme.text
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: item.clicked()
    }
}
