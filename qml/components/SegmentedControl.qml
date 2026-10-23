import QtQuick

// Pill-style segmented selector.
//   SegmentedControl {
//       options: [{ value: "all", label: "All" },
//                 { value: "rowing", label: "Rowing", icon: "rowing", color: Theme.rowingColor, textColor: Theme.textOnColor }]
//       value: Workouts.sportFilter
//       onActivated: (v) => Workouts.sportFilter = v
//   }
Rectangle {
    id: seg
    property var options: []
    property string value
    property real fontSize: 16
    property real segmentPadding: 16
    property real iconSize: 18
    signal activated(string value)

    implicitWidth: segRow.implicitWidth + 8
    implicitHeight: 50
    radius: 10
    color: Theme.card
    border.color: Theme.cardBorder

    Row {
        id: segRow
        anchors.centerIn: parent
        spacing: 0
        Repeater {
            model: seg.options
            Rectangle {
                readonly property bool active: seg.value === modelData.value
                readonly property color tint: modelData.color !== undefined ? modelData.color : Theme.accent
                readonly property color fg: modelData.textColor !== undefined ? modelData.textColor : "white"
                width: segContent.implicitWidth + 2 * seg.segmentPadding
                height: seg.height - 8
                radius: 8
                color: active ? tint : (segMouse.containsMouse ? Theme.hover : "transparent")
                Row {
                    id: segContent
                    anchors.centerIn: parent
                    spacing: 7
                    Icon {
                        visible: modelData.icon !== undefined
                        name: modelData.icon !== undefined ? modelData.icon : ""
                        size: seg.iconSize
                        strokeWidth: 2
                        color: parent.parent.active ? parent.parent.fg : Theme.text
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.label
                        color: parent.parent.active ? parent.parent.fg : Theme.text
                        font.pixelSize: seg.fontSize
                        font.weight: parent.parent.active ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: segMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: seg.activated(modelData.value)
                }
            }
        }
    }
}
