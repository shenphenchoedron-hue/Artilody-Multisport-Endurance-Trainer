import QtQuick

// Low live metric of the Treadmill Run page (so the 3D view gets the height):
// icon + label on the first line, the big value, and a footer line - all in
// ~108 px.
Card {
    id: metric
    property string label
    property string iconName
    property color iconColor: Theme.text
    property color valueColor: Theme.text
    property string value: "0"
    property string unit
    property real valueSize: 36
    property string footer

    implicitHeight: 108

    Row {
        x: 14
        y: 10
        spacing: 8
        Icon {
            name: metric.iconName
            color: metric.iconColor
            size: 20
            strokeWidth: 2.2
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: metric.label
            color: Theme.text
            font.pixelSize: 15
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Item {
        id: valueBox
        x: 14
        y: 32
        width: parent.width - 24
        height: valueText.implicitHeight
        Row {
            spacing: 5
            // long values shrink instead of being clipped
            scale: Math.min(1, valueBox.width / Math.max(1, implicitWidth))
            transformOrigin: Item.Left
            Text {
                id: valueText
                text: metric.value
                color: metric.valueColor
                font.pixelSize: metric.valueSize
                font.weight: Font.Black
                font.letterSpacing: -1
            }
            Text {
                visible: metric.unit.length > 0
                text: metric.unit
                color: metric.valueColor
                font.pixelSize: metric.valueSize * 0.55
                font.weight: Font.Bold
                anchors.baseline: valueText.baseline
            }
        }
    }

    Text {
        x: 14
        width: parent.width - 24
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 9
        text: metric.footer
        color: Theme.textSecondary
        font.pixelSize: 14
        elide: Text.ElideRight
    }
}
