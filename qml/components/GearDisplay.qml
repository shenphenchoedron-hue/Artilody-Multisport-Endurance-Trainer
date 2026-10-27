import QtQuick

// Current virtual gear (Zwift's 24 gears): the big gear number, "Gear 12 / 24
// • ratio 2.40", a position bar and the shifting status line ("Virtual
// shifting: Zwift protocol"). Pure presentation: every value comes from Gears.
Item {
    id: gd
    property int gearIndex: 0          // 0 = easiest
    property int gearCount: 24
    property real ratio: 0
    property string status: ""         // Gears.shiftingStatus

    // Short highlight after a shift (+1 harder, -1 easier).
    function flash(direction) {
        flashArrow.text = direction > 0 ? "▲ Harder" : "▼ Easier"
        flashArrow.color = direction > 0 ? Theme.warningText : Theme.accentText
        flashAnim.restart()
    }

    implicitHeight: col.implicitHeight

    Column {
        id: col
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        spacing: 4

        Item {
            width: parent.width
            height: gearRow.height
            Row {
                id: gearRow
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10
                transformOrigin: Item.Center
                Text {
                    id: gearNumber
                    objectName: "gearNumber"
                    text: gd.gearIndex + 1
                    color: Theme.accentText
                    font.pixelSize: 84
                    font.weight: Font.Black
                    font.letterSpacing: -2
                }
                Text {
                    text: "/ " + gd.gearCount
                    color: Theme.textMuted
                    font.pixelSize: 34
                    font.weight: Font.Bold
                    anchors.baseline: gearNumber.baseline
                }
            }
            Text {
                id: flashArrow
                anchors.left: gearRow.right
                anchors.leftMargin: 14
                anchors.verticalCenter: gearRow.verticalCenter
                opacity: 0
                font.pixelSize: 17
                font.weight: Font.Bold
                color: Theme.warningText
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Gear " + (gd.gearIndex + 1) + "/" + gd.gearCount + " (" + gd.ratio.toFixed(2) + ")"
            color: Theme.textSecondary
            font.pixelSize: 17
        }

        // gear position bar
        Item {
            width: parent.width
            height: 18
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 20, 420)
                height: 6
                radius: 3
                color: Theme.fieldBorder
                Rectangle {
                    height: parent.height
                    radius: parent.radius
                    width: parent.width * (gd.gearIndex + 1) / Math.max(1, gd.gearCount)
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                }
            }
        }

        Text {
            objectName: "gearShiftingStatus"
            visible: gd.status.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: gd.status
            color: Theme.orange
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
    }

    SequentialAnimation {
        id: flashAnim
        ParallelAnimation {
            NumberAnimation { target: gearRow; property: "scale"; from: 1; to: 1.07; duration: 90; easing.type: Easing.OutCubic }
            NumberAnimation { target: flashArrow; property: "opacity"; to: 1; duration: 90 }
        }
        NumberAnimation { target: gearRow; property: "scale"; to: 1; duration: 200; easing.type: Easing.OutCubic }
        PauseAnimation { duration: 500 }
        NumberAnimation { target: flashArrow; property: "opacity"; to: 0; duration: 350 }
    }
}
