import QtQuick
import QtQuick.Layouts
import Artilody.Backend

Item {
    id: page
    signal openDevices()
    signal editWorkout(int id)
    signal showDetails()
    signal openMusicLibrary()
    signal requestStop()
    signal openHistory()

    readonly property int target: Training.targetPower
    readonly property int hrZone: Training.heartRateZone
    readonly property int chartTotal: Training.freeRide
                                      ? Math.max(600, Math.ceil((Training.elapsedSec + 60) / 600) * 600)
                                      : Training.totalSec

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.bottomMargin: Theme.pageMargin
        spacing: Theme.spacing

        // ================= left column =================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing

            // header
            Column {
                Layout.topMargin: 46
                Layout.fillWidth: true
                spacing: 4
                Item {
                    width: parent.width
                    height: crumbs.height
                    Row {
                        id: crumbs
                        spacing: 14
                        Text {
                            text: "Indoor Cycling"
                            color: Theme.text
                            font.pixelSize: 16
                        }
                        Icon {
                            name: "chevronRight"
                            size: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: Training.freeRide ? "Free Ride" : (Training.focus.length > 0 ? Training.focus : Training.category)
                            color: Theme.text
                            font.pixelSize: 16
                        }
                    }

                    // armed via startWhenPedaling(): the clock starts with the first pedal strokes
                    Rectangle {
                        id: waitingPill
                        visible: Training.waiting
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 32
                        width: waitingRow.implicitWidth + 28
                        radius: height / 2
                        color: Theme.navSelected
                        border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.55)
                        border.width: 1
                        Row {
                            id: waitingRow
                            anchors.centerIn: parent
                            spacing: 9
                            Rectangle {
                                width: 9; height: 9; radius: 4.5
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.success
                                SequentialAnimation on opacity {
                                    running: waitingPill.visible
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.25; duration: 700 }
                                    NumberAnimation { to: 1; duration: 700 }
                                }
                            }
                            Text {
                                text: "Start pedaling to begin"
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
                Text {
                    text: Training.workoutName
                    color: Theme.text
                    font.pixelSize: 46
                    font.weight: Font.Black
                    width: parent.width
                    elide: Text.ElideRight
                }
                Text {
                    text: Training.summary
                    color: Theme.text
                    font.pixelSize: 20
                }
            }

            // live metrics
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 222
                Layout.topMargin: 2
                spacing: Theme.spacing

                MetricCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 306
                    Layout.fillHeight: true
                    label: "Power"
                    showPowerAverage: true
                    iconName: "bolt"
                    iconColor: Theme.danger
                    // display value: 3 s average when "Smooth power" is on
                    value: Sensors.displayPower
                    unit: "W"
                    valueSize: 84
                    fraction: Sensors.displayPower / Math.max(1, (page.target > 0 ? page.target : Profiles.ftp) * 1.3)
                    barColor: Theme.zoneColor(Profiles.zoneForWatts(Sensors.displayPower))
                    footerLeft: page.target > 0 ? "Target " + page.target + " W" : (Training.freeRide ? "Free ride" : "No target")
                    footerRight: page.target > 0 && Training.active
                                 ? ((Sensors.displayPower - page.target > 0 ? "+" : "") + (Sensors.displayPower - page.target) + " W") : ""
                }
                MetricCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 229
                    Layout.fillHeight: true
                    label: "Cadence"
                    iconName: "cadence"
                    iconColor: Theme.success
                    value: Training.cadence
                    unit: "RPM"
                    fraction: Training.cadence / 120
                    barColor: Theme.success
                    readonly property int targetCad: Training.targetCadence > 0 ? Training.targetCadence : Profiles.preferredCadence
                    footerLeft: "Target " + (targetCad - 5) + " – " + (targetCad + 5)
                }
                MetricCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 201
                    Layout.fillHeight: true
                    label: "Heart Rate"
                    iconName: "heart"
                    iconColor: Theme.danger
                    value: Training.heartRate > 0 ? Training.heartRate : "–"
                    unit: "BPM"
                    valueSize: 58
                    fraction: Training.heartRate / Math.max(1, Profiles.maxHr)
                    barColor: Theme.hrZoneColors[page.hrZone]
                    footerLeft: Training.heartRate > 0
                                ? (page.hrZone > 0 ? "Zone " + page.hrZone + "  (" + Profiles.heartRateZoneRange(page.hrZone) + ")" : "Below zone 1")
                                : "No sensor data"
                }
                MetricCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 191
                    Layout.fillHeight: true
                    label: "Elapsed Time"
                    iconName: "timer"
                    value: Theme.clock(Training.elapsedSec)
                    valueSize: 62
                    fraction: Training.freeRide ? (Training.elapsedSec % 3600) / 3600 : Training.progress
                    barColor: Theme.purple
                    footerLeft: Training.waiting ? "Waiting for pedaling"
                                                 : (Training.freeRide ? "Free ride" : "of " + Theme.clock(Training.totalSec))
                }
            }

            // workout profile chart
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 326
                title: "Workout Profile"
                headerRight: [ ZoneLegend {} ]

                WorkoutChart {
                    x: 6
                    y: 52
                    width: parent.width - 16
                    height: parent.height - 60
                    segments: Training.segments
                    totalSec: page.chartTotal
                    powerSeries: Training.powerSeries
                    hrSeries: Training.heartRateSeries
                    seriesStep: Training.seriesStep
                    ftp: Profiles.ftp
                    cursorSec: Training.active || Training.elapsedSec > 0 ? Training.elapsedSec : -1
                    showCursorLabel: Training.active
                    cursorWatts: Sensors.displayPower
                    hrMin: Math.max(40, Math.floor((Profiles.restHr + 10) / 20) * 20)
                    hrMax: Math.ceil(Profiles.maxHr / 20) * 20
                }
            }

            // timeline
            Card {
                id: timelineCard
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 180
                title: "Workout Timeline"

                headerRight: [
                    Row {
                        spacing: 10
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: Training.freeRide ? "No intervals" : Training.blockCount + " intervals  •  " + Theme.clock(Training.totalSec)
                            color: Theme.textSecondary
                            font.pixelSize: 15
                            rightPadding: 6
                        }
                        // intensity (ERG)
                        Rectangle {
                            visible: !Training.freeRide
                            height: 38
                            width: intensityRow.width + 16
                            radius: 8
                            color: Theme.cardStrong
                            border.color: Theme.fieldBorder
                            Row {
                                id: intensityRow
                                anchors.centerIn: parent
                                spacing: 4
                                IconButton { flat: true; iconName: "minus"; iconSize: 16; onClicked: Training.adjustIntensity(-5); tooltip: "Easier (−5%)" }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: (100 + Training.intensityOffset) + "%"
                                    color: Theme.text
                                    font.pixelSize: 15
                                    width: 46
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                IconButton { flat: true; iconName: "plus"; iconSize: 16; onClicked: Training.adjustIntensity(5); tooltip: "Harder (+5%)" }
                            }
                        }
                        AppButton {
                            visible: Training.active && !Training.freeRide
                            kind: "secondary"; height: 38; fontSize: 15; iconSize: 16
                            text: "Skip"; iconName: "skipNext"; bold: false
                            onClicked: Training.skipInterval()
                        }
                        AppButton {
                            visible: !Training.freeRide
                            kind: "secondary"; height: 38; fontSize: 15; iconSize: 16
                            text: "Edit Workout"; iconName: "edit"; bold: false
                            onClicked: page.editWorkout(Training.workoutId)
                        }
                        AppButton {
                            kind: "secondary"; height: 38; fontSize: 15; iconSize: 16
                            text: "Add Interval"; iconName: "plus"; bold: false
                            enabled: !Training.active
                            onClicked: page.editWorkout(Training.workoutId)
                        }
                    }
                ]

                Item {
                    id: blocksArea
                    x: 18
                    y: 58
                    width: parent.width - 36
                    height: 82

                    readonly property var blocks: Training.timeline
                    readonly property real minW: 64
                    function widthFor(i) {
                        var n = blocks.length
                        if (n === 0)
                            return 0
                        var gap = 4
                        var avail = width - gap * (n - 1)
                        if (n * minW >= avail)
                            return avail / n
                        var total = Training.totalSec
                        var rest = avail - n * minW
                        return minW + rest * blocks[i].duration / Math.max(1, total)
                    }

                    Row {
                        spacing: 4
                        Repeater {
                            model: blocksArea.blocks
                            Rectangle {
                                width: blocksArea.widthFor(index)
                                height: 82
                                radius: 5
                                color: Theme.zoneColor(modelData.zone)
                                border.width: index === Training.currentIndex && Training.active ? 3 : 0
                                border.color: "white"
                                Column {
                                    anchors.centerIn: parent
                                    visible: parent.width > 50
                                    spacing: 1
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.durationText
                                        color: Theme.textOnColor
                                        font.pixelSize: 16
                                        font.weight: Font.Bold
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.name
                                        color: Theme.textOnColor
                                        font.pixelSize: 15
                                        width: Math.min(implicitWidth, blocksArea.widthFor(index) - 8)
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.wattsText
                                        color: Theme.textOnColor
                                        font.pixelSize: 14
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        visible: Training.freeRide
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Free Ride – ride at your own pace without structured intervals"
                            color: Theme.textSecondary
                            font.pixelSize: 17
                        }
                    }
                }

                // progress
                AppSlider {
                    id: progressBar
                    x: 18
                    width: parent.width - 36 - 130
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 20
                    from: 0
                    to: 1
                    value: Training.freeRide ? 0 : Training.progress
                    enabled: false
                    trackHeight: 9
                    fillColor: Theme.danger
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: progressBar.verticalCenter
                    text: Theme.clock(Training.elapsedSec) + (Training.freeRide ? "" : " / " + Theme.clock(Training.totalSec))
                    color: Theme.text
                    font.pixelSize: 17
                }
            }
        }

        // ================= right column =================
        ColumnLayout {
            Layout.preferredWidth: 354
            Layout.maximumWidth: 354
            Layout.fillHeight: true
            spacing: 12

            Rectangle {
                Layout.topMargin: 64
                Layout.fillWidth: true
                Layout.preferredHeight: 54
                radius: 12
                color: detailsMouse.containsMouse ? Theme.cardStrong : Theme.card
                border.color: Theme.cardBorder
                Row {
                    x: 18
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 16
                    Icon { name: "document"; size: 26; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: "Workout Details"
                        color: Theme.text
                        font.pixelSize: 18
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: detailsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.showDetails()
                }
            }

            DevicesSummaryCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 186
                onOpenDevices: page.openDevices()
            }

            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 128
                Row {
                    x: 18
                    y: 16
                    spacing: 12
                    Icon { name: "pulse"; size: 26; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: "ERG Mode"
                        color: Theme.text
                        font.pixelSize: 19
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                ToggleSwitch {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    y: 14
                    checked: Training.ergMode
                    enabled: !Training.freeRide
                    onToggled: (on) => Training.ergMode = on
                }
                Text {
                    x: 18
                    y: 52
                    width: parent.width - 36
                    text: Training.freeRide
                          ? "Free ride: no automatic resistance control. Ride at your own pace."
                          : Training.ergMode
                            ? "Trainer will automatically adjust resistance to match the target power during the workout."
                            : "Free Ride mode: follow the targets at your own pace – the trainer will not change resistance."
                    color: Theme.textSecondary
                    font.pixelSize: 16
                    wrapMode: Text.WordWrap
                    lineHeight: 1.1
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                title: "Controls"
                RowLayout {
                    x: 16
                    y: 48
                    width: parent.width - 32
                    spacing: 12
                    AppButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        kind: Training.active ? "secondary" : "success"
                        iconName: Training.running ? "pause" : "play"
                        text: Training.running ? "Pause"
                                               : (Training.paused ? "Resume" : (Training.waiting ? "Start now" : "Start"))
                        onClicked: {
                            if (Training.active || Training.waiting)
                                Training.togglePause()   // pause/resume, or manual start while waiting
                            else
                                Training.startWhenPedaling()
                        }
                    }
                    AppButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        kind: "danger"
                        iconName: "stop"
                        text: "Stop"
                        enabled: Training.armedOrActive
                        onClicked: {
                            if (Training.waiting)
                                Training.stop()          // just cancels waiting, nothing to save
                            else
                                page.requestStop()
                        }
                    }
                }
            }

            MusicCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 300
                onOpenLibrary: page.openMusicLibrary()
            }
        }
    }
}
