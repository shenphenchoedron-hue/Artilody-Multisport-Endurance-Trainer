import QtQuick

// Compact live metric of the rowing page: icon + label, big value with an
// optional suffix/unit, and a footer line (or a progress bar).
Card {
    id: metric
    property string label
    property bool showPowerAverage: false   // cycling power tile: "3 s avg" toggle
    property string iconName
    property color iconColor: Theme.text
    property color valueColor: Theme.text
    property string value: "0"
    property string unit
    property real valueSize: 40
    property string footer
    property real progress: -1        // >= 0 shows a progress bar instead of the footer text
    property color barColor: Theme.accent

    Row {
        x: 16
        y: 16
        spacing: 9
        Icon {
            name: metric.iconName
            color: metric.iconColor
            size: 24
            strokeWidth: 2.2
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: metric.label
            color: Theme.text
            font.pixelSize: 16
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Item {
        id: valueBox
        x: 14
        y: 46
        width: parent.width - 24
        height: valueText.implicitHeight
        Row {
            id: valueRow
            spacing: 6
            // shrink long values (e.g. 10,000 / 10,000 m) instead of clipping
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

    Item {
        visible: metric.progress >= 0
        x: 16
        width: parent.width - 32
        height: 22
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        Rectangle {
            id: track
            anchors.left: parent.left
            anchors.right: pct.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: 10
            radius: 5
            color: Theme.fieldBorder
            Rectangle {
                height: parent.height
                radius: parent.radius
                width: Math.max(metric.progress > 0 ? height : 0, parent.width * Math.max(0, Math.min(1, metric.progress)))
                color: metric.barColor
                Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
            }
        }
        Text {
            id: pct
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(Math.max(0, Math.min(1, metric.progress)) * 100) + "%"
            color: Theme.textSecondary
            font.pixelSize: 15
        }
    }

    Text {
        visible: metric.progress < 0
        x: 16
        width: parent.width - 28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        text: metric.footer
        color: Theme.textSecondary
        font.pixelSize: 15
        elide: Text.ElideRight
    }

    // narrow tile: compact "3 s" toggle at the right of the header
    PowerAvgBadge {
        visible: metric.showPowerAverage
        compact: true
        fontSize: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        y: 16 + (24 - height) / 2
    }
}
