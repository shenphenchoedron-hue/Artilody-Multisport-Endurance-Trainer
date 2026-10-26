import QtQuick

// Current virtual gear of the free ride: "50 × 17", its position
// ("Gear 9 / 16") and a drivetrain sketch (chainrings + cassette, the gear in
// use highlighted). Pure presentation: every value comes from FreeRide.
Item {
    id: gd
    property int frontTeeth: 0
    property int rearTeeth: 0
    property int gearIndex: 0          // 0 = easiest
    property int gearCount: 1
    property real ratio: 0
    property var chainrings: []        // ascending teeth
    property var cogs: []              // ascending teeth (index 0 = hardest)
    property int frontIndex: 0
    property int rearIndex: 0
    property bool sequential: true

    // Short highlight after a shift (+1 harder, -1 easier).
    function flash(direction) {
        flashArrow.text = direction > 0 ? "▲ Harder" : "▼ Easier"
        flashArrow.color = direction > 0 ? Theme.warningText : Theme.accentText
        flashAnim.restart()
    }

    readonly property int minCog: cogs.length > 0 ? Number(cogs[0]) : 11
    readonly property int maxCog: cogs.length > 0 ? Number(cogs[cogs.length - 1]) : 34
    readonly property int maxRing: chainrings.length > 0 ? Number(chainrings[chainrings.length - 1]) : 50

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
                    id: frontText
                    text: gd.frontTeeth
                    color: Theme.accentText
                    font.pixelSize: 78
                    font.weight: Font.Black
                    font.letterSpacing: -2
                }
                Text {
                    text: "×"
                    color: Theme.textMuted
                    font.pixelSize: 52
                    font.weight: Font.Bold
                    anchors.baseline: frontText.baseline
                }
                Text {
                    text: gd.rearTeeth
                    color: Theme.text
                    font.pixelSize: 78
                    font.weight: Font.Black
                    font.letterSpacing: -2
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
            text: "Gear " + (gd.gearIndex + 1) + " / " + gd.gearCount + "   •   ratio " + gd.ratio.toFixed(2)
            color: Theme.textSecondary
            font.pixelSize: 17
        }

        // gear position bar
        Item {
            width: parent.width
            height: 18
            Rectangle {
                id: posTrack
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

        // drivetrain: chainrings (left) and cassette (right, easiest cog first)
        Item {
            width: parent.width
            height: 80
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                spacing: 34

                Row {
                    anchors.bottom: parent.bottom
                    spacing: 8
                    Repeater {
                        model: gd.chainrings
                        Column {
                            anchors.bottom: parent.bottom
                            spacing: 4
                            readonly property bool current: index === gd.frontIndex
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 22 + 34 * Number(modelData) / Math.max(1, gd.maxRing)
                                height: width
                                radius: width / 2
                                color: parent.current ? Qt.rgba(0.13, 0.47, 1.0, 0.25) : "transparent"
                                border.width: parent.current ? 3 : 2
                                border.color: parent.current ? Theme.accent : Theme.fieldBorder
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 7; height: 7; radius: 3.5
                                    color: parent.parent.current ? Theme.accent : Theme.fieldBorder
                                }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData
                                color: parent.current ? Theme.text : Theme.textMuted
                                font.pixelSize: 12
                                font.weight: parent.current ? Font.Bold : Font.Normal
                            }
                        }
                    }
                }

                Row {
                    anchors.bottom: parent.bottom
                    spacing: 5
                    Repeater {
                        model: gd.cogs.length
                        Column {
                            // easiest (largest) cog on the left, like the Shift Easier button
                            readonly property int cogIndex: gd.cogs.length - 1 - index
                            readonly property int teeth: Number(gd.cogs[cogIndex])
                            readonly property bool current: cogIndex === gd.rearIndex
                            anchors.bottom: parent.bottom
                            spacing: 4
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 13
                                height: 16 + 40 * (parent.teeth - gd.minCog) / Math.max(1, gd.maxCog - gd.minCog)
                                radius: 4
                                color: parent.current ? Theme.accent : Theme.fieldBorder
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: parent.teeth
                                color: parent.current ? Theme.text : Theme.textMuted
                                font.pixelSize: 12
                                font.weight: parent.current ? Font.Bold : Font.Normal
                            }
                        }
                    }
                }
            }
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
