import QtQuick

// Dashboard live metric (Power, Cadence, Heart Rate, Elapsed Time)
Card {
    id: metric
    property string label
    property bool showPowerAverage: false   // cycling power tile: "3 s avg" toggle
    property string iconName
    property color iconColor: Theme.text
    property string value: "0"
    property string unit
    property real fraction: 0
    property color barColor: Theme.warning
    property string footerLeft
    property string footerRight
    property real valueSize: 66

    Row {
        x: 22
        y: 20
        spacing: 12
        Icon {
            name: metric.iconName
            color: metric.iconColor
            size: 30
            strokeWidth: 2.4
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: metric.label
            color: Theme.text
            font.pixelSize: 19
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    PowerAvgBadge {
        visible: metric.showPowerAverage
        anchors.right: parent.right
        anchors.rightMargin: 22
        y: 20 + (30 - height) / 2
    }

    Row {
        x: 20
        y: 58
        spacing: 10
        Text {
            id: valueText
            text: metric.value
            color: Theme.text
            font.pixelSize: metric.valueSize
            font.weight: Font.Black
            font.letterSpacing: -1.5
        }
        Text {
            visible: metric.unit.length > 0
            text: metric.unit
            color: Theme.text
            font.pixelSize: metric.valueSize * 0.55
            font.weight: Font.Bold
            anchors.baseline: valueText.baseline
        }
    }

    Rectangle {
        id: track
        x: 22
        width: parent.width - 44
        y: parent.height - 72
        height: 16
        radius: 8
        color: Theme.fieldBorder
        Rectangle {
            height: parent.height
            radius: parent.radius
            width: Math.max(height, parent.width * Math.max(0, Math.min(1, metric.fraction)))
            visible: metric.fraction > 0
            color: metric.barColor
            Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
        }
    }

    Text {
        x: 22
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        text: metric.footerLeft
        color: Theme.textSecondary
        font.pixelSize: 17
        width: parent.width - 44 - rightText.width
        elide: Text.ElideRight
    }
    Text {
        id: rightText
        anchors.right: parent.right
        anchors.rightMargin: 22
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        text: metric.footerRight
        color: Theme.textSecondary
        font.pixelSize: 17
    }
}
