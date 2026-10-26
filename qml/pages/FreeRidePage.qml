import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

// Free ride without a workout: virtual gears (shift to change power and
// speed), ERG (hold a set wattage) or free (no resistance control).
// All logic lives in the FreeRide singleton; Zwift Click buttons arrive there
// directly from Devices.
Item {
    id: page
    signal openDevices()
    signal openGearSetup()
    signal requestFinish()

    readonly property string st: FreeRide.state
    readonly property bool finished: st === "finished"
    // only one session at a time (FreeRide refuses to start otherwise)
    readonly property bool otherSession: Training.armedOrActive || Rowing.active
    readonly property var setup: FreeRide.gearSetup
    readonly property var rings: setup.chainrings || []
    readonly property var cogs: setup.cogs || []
    readonly property int chartTotal: Math.max(600, Math.ceil((FreeRide.elapsedSeconds + 60) / 600) * 600)
    property bool plusDown: false
    property bool minusDown: false

    function kmText(m) { return (m / 1000).toFixed(m < 100000 ? 2 : 1) }
    function ringsText() {
        var a = []
        for (var i = rings.length - 1; i >= 0; --i)
            a.push(rings[i])
        return a.join("/")
    }
    function cassetteText() {
        return cogs.length > 0 ? cogs[0] + "–" + cogs[cogs.length - 1] : ""
    }
    function gradeText(g) {
        return (g > 0 ? "+" : g < 0 ? "−" : "") + Math.abs(g).toFixed(1) + " %"
    }
    function modeName(m) { return m === "erg" ? "ERG" : m === "free" ? "Free" : "Gears" }
    function goTo(name) {
        var w = ApplicationWindow.window
        if (w && typeof w.showPage === "function")
            w.showPage(name)
    }

    Connections {
        target: FreeRide
        function onShifted(direction) {
            if (FreeRide.mode === "gears") {
                gearDisplay.flash(direction)
                var b = direction > 0 ? harderButton : easierButton
                b.pulse()
            } else if (FreeRide.mode === "erg") {
                ergControl.pulse(direction)
            }
        }
    }
    Connections {
        target: Devices
        function onControllerButtonsChanged(plus, minus) {
            page.plusDown = plus
            page.minusDown = minus
        }
    }

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

            // header: title, status, mode
            Item {
                Layout.topMargin: 26
                Layout.fillWidth: true
                Layout.preferredHeight: header.implicitHeight
                Column {
                    id: header
                    spacing: 0
                    Text {
                        text: "Free Ride"
                        color: Theme.text
                        font.pixelSize: 52
                        font.weight: Font.Black
                    }
                    Text {
                        text: FreeRide.mode === "erg" ? "Hold a set wattage in ERG mode – as long as you like."
                              : FreeRide.mode === "free" ? "Ride without resistance control."
                              : "Ride without a workout. Shift gears for more watts and speed."
                        color: Theme.text
                        font.pixelSize: 21
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    spacing: 14

                    Rectangle {
                        id: statusPill
                        readonly property color dot: page.st === "ready" ? Theme.success
                                                   : page.st === "running" ? Theme.danger
                                                   : page.st === "paused" ? Theme.warning
                                                   : page.st === "finished" ? Theme.accentLight : Theme.textMuted
                        visible: page.st !== "idle"
                        anchors.verticalCenter: parent.verticalCenter
                        height: 34
                        width: pillRow.implicitWidth + 28
                        radius: height / 2
                        color: Theme.navSelected
                        border.color: Qt.rgba(dot.r, dot.g, dot.b, 0.6)
                        Row {
                            id: pillRow
                            anchors.centerIn: parent
                            spacing: 9
                            Rectangle {
                                width: 9; height: 9; radius: 4.5
                                anchors.verticalCenter: parent.verticalCenter
                                color: statusPill.dot
                                SequentialAnimation on opacity {
                                    running: page.st === "ready" || page.st === "running"
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.25; duration: 700 }
                                    NumberAnimation { to: 1; duration: 700 }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.st === "ready" ? "Waiting for pedaling…"
                                      : page.st === "running" ? (FreeRide.coasting ? "Coasting" : "Riding") + "  •  " + page.modeName(FreeRide.mode)
                                      : page.st === "paused" ? "Paused" : "Finished"
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    SegmentedControl {
                        id: modeSelector
                        anchors.verticalCenter: parent.verticalCenter
                        height: 46
                        options: [
                            { value: "gears", label: "Gears", icon: "cog" },
                            { value: "erg", label: "ERG", icon: "target" },
                            { value: "free", label: "Free", icon: "freeRide" }
                        ]
                        value: FreeRide.mode
                        onActivated: (v) => FreeRide.mode = v
                    }
                }
            }

            // live metrics
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 132
                Layout.fillHeight: false
                spacing: 12

                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    Layout.fillHeight: true
                    label: "Power"
                    iconName: "bolt"
                    iconColor: Theme.warningText
                    valueColor: Theme.warningText
                    showPowerAverage: true
                    value: Sensors.displayPower
                    unit: "W"
                    footer: FreeRide.mode === "erg" ? "Target: " + FreeRide.targetWatts + " W"
                            : FreeRide.mode === "gears" && FreeRide.gearTargetWatts > 0
                              ? "Gear: " + FreeRide.gearTargetWatts + " W"
                              : "Avg: " + FreeRide.avgWatts + " W"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 170
                    Layout.fillHeight: true
                    label: "Speed"
                    iconName: "gauge"
                    iconColor: Theme.speed
                    value: FreeRide.speedKmh.toFixed(1)
                    unit: "km/h"
                    footer: "Avg: " + FreeRide.avgSpeedKmh.toFixed(1) + " km/h"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true
                    label: "Cadence"
                    iconName: "cadence"
                    iconColor: Theme.cyan
                    value: FreeRide.cadence
                    unit: "RPM"
                    footer: FreeRide.active && FreeRide.coasting ? "Coasting"
                            : FreeRide.mode === "gears" ? "Gear " + FreeRide.gearLabel : "Max: " + FreeRide.maxWatts + " W"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    Layout.fillHeight: true
                    label: "Heart Rate"
                    iconName: "heart"
                    iconColor: Theme.heartRate
                    value: FreeRide.heartRate > 0 ? FreeRide.heartRate : "–"
                    unit: "BPM"
                    footer: FreeRide.heartRate > 0 && Profiles.maxHr > 0
                            ? Math.round(FreeRide.heartRate * 100 / Profiles.maxHr) + "% of max" : "No sensor"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true
                    label: "Time"
                    iconName: "timer"
                    value: Theme.clock(FreeRide.elapsedSeconds)
                    footer: "Avg: " + FreeRide.avgWatts + " W"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    Layout.fillHeight: true
                    label: "Distance"
                    iconName: "mapPin"
                    iconColor: Theme.accentLight
                    value: page.kmText(FreeRide.distanceMeters)
                    unit: "km"
                    footer: Math.round(FreeRide.kilojoules) + " kJ"
                }
            }

            // gears / ERG / free panel
            Card {
                id: controlCard
                Layout.fillWidth: true
                Layout.preferredHeight: 362
                title: FreeRide.mode === "erg" ? "ERG – Target Power"
                       : FreeRide.mode === "free" ? "Free Mode" : "Virtual Gears"
                headerRight: [
                    Text {
                        visible: FreeRide.mode === "gears"
                        text: (FreeRide.sequential ? "Sequential" : "Front + rear") + "  •  "
                              + page.ringsText() + " × " + page.cassetteText()
                              + "  •  " + FreeRide.gearCount + " gears"
                        color: Theme.textSecondary
                        font.pixelSize: 15
                    }
                ]

                // --- gears ---------------------------------------------------------
                RowLayout {
                    id: gearsRow
                    visible: FreeRide.mode === "gears"
                    x: 16
                    y: controlCard.contentTop - 4
                    width: parent.width - 32
                    height: parent.height - y - 42
                    spacing: 16

                    ShiftButton {
                        id: easierButton
                        Layout.preferredWidth: 132
                        Layout.fillHeight: true
                        iconName: "minus"
                        label: "Easier"
                        sublabel: "↓  or  −"
                        tint: Theme.accent
                        enabled: FreeRide.canShiftDown
                        onClicked: FreeRide.shiftDown()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 300   // the rest of the row goes here
                        Layout.fillHeight: true
                        spacing: 6
                        GearDisplay {
                            id: gearDisplay
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            frontTeeth: FreeRide.frontTeeth
                            rearTeeth: FreeRide.rearTeeth
                            gearIndex: FreeRide.gearIndex
                            gearCount: FreeRide.gearCount
                            ratio: FreeRide.gearRatio
                            chainrings: page.rings
                            cogs: page.cogs
                            frontIndex: FreeRide.frontIndex
                            rearIndex: FreeRide.rearIndex
                            sequential: FreeRide.sequential
                        }
                        Item { Layout.fillHeight: true }
                        Row {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 10
                            visible: FreeRide.sequential || page.rings.length > 1
                            AppButton {
                                height: 40
                                kind: "secondary"
                                fontSize: 15
                                iconSize: 16
                                iconSpacing: 8
                                horizontalPadding: 14
                                bold: false
                                iconName: "chevronLeft"
                                text: FreeRide.sequential ? "Jump 3 easier" : "Small ring"
                                enabled: FreeRide.canShiftFrontDown
                                onClicked: FreeRide.shiftFrontDown()
                            }
                            AppButton {
                                height: 40
                                kind: "secondary"
                                fontSize: 15
                                iconSize: 16
                                iconSpacing: 8
                                horizontalPadding: 14
                                bold: false
                                iconName: "chevronRight"
                                text: FreeRide.sequential ? "Jump 3 harder" : "Big ring"
                                enabled: FreeRide.canShiftFrontUp
                                onClicked: FreeRide.shiftFrontUp()
                            }
                        }
                    }

                    ShiftButton {
                        id: harderButton
                        Layout.preferredWidth: 132
                        Layout.fillHeight: true
                        iconName: "plus"
                        label: "Harder"
                        sublabel: "↑  or  +"
                        tint: Theme.orange
                        enabled: FreeRide.canShiftUp
                        onClicked: FreeRide.shiftUp()
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        color: Theme.cardBorder
                    }

                    // grade
                    ColumnLayout {
                        Layout.preferredWidth: 230
                        Layout.maximumWidth: 230
                        Layout.fillHeight: true
                        spacing: 6
                        Row {
                            spacing: 8
                            Icon { name: "mountain"; size: 22; color: Theme.successText; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "Grade"; color: Theme.text; font.pixelSize: 17; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Text {
                            text: page.gradeText(FreeRide.gradePercent)
                            color: FreeRide.gradePercent > 0 ? Theme.warningText : FreeRide.gradePercent < 0 ? Theme.successText : Theme.text
                            font.pixelSize: 46
                            font.weight: Font.Black
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            IconButton {
                                Layout.preferredWidth: 52
                                Layout.preferredHeight: 44
                                iconName: "minus"
                                tooltip: "Grade −0.5 % (PgDn)"
                                enabled: FreeRide.gradePercent > FreeRide.minGrade
                                onClicked: FreeRide.adjustGrade(-0.5)
                            }
                            IconButton {
                                Layout.preferredWidth: 52
                                Layout.preferredHeight: 44
                                iconName: "plus"
                                tooltip: "Grade +0.5 % (PgUp)"
                                enabled: FreeRide.gradePercent < FreeRide.maxGrade
                                onClicked: FreeRide.adjustGrade(0.5)
                            }
                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 44
                                kind: "secondary"
                                fontSize: 15
                                bold: false
                                text: "Flat"
                                enabled: FreeRide.gradePercent !== 0
                                onClicked: FreeRide.setGrade(0)
                            }
                        }
                        AppSlider {
                            id: gradeSlider
                            Layout.fillWidth: true
                            from: FreeRide.minGrade
                            to: FreeRide.maxGrade
                            stepSize: 0.5
                            snapMode: Slider.SnapAlways
                            value: FreeRide.gradePercent
                            fillColor: Theme.success
                            onMoved: FreeRide.setGrade(value)
                        }
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 16
                            Text { text: page.gradeText(FreeRide.minGrade); color: Theme.textMuted; font.pixelSize: 12 }
                            Text {
                                x: gradeSlider.width * (0 - FreeRide.minGrade) / (FreeRide.maxGrade - FreeRide.minGrade) - width / 2
                                text: "0"
                                color: Theme.textMuted
                                font.pixelSize: 12
                            }
                            Text { anchors.right: parent.right; text: page.gradeText(FreeRide.maxGrade); color: Theme.textMuted; font.pixelSize: 12 }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            text: FreeRide.gearTargetWatts > 0
                                  ? "This gear at " + FreeRide.cadence + " rpm needs ≈ " + FreeRide.gearTargetWatts + " W"
                                  : "Shift harder for more watts and speed at the same cadence."
                            color: Theme.textSecondary
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                Text {
                    visible: gearsRow.visible
                    x: 16
                    width: parent.width - 32
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: "Keys:   ↑ / +  harder   •   ↓ / −  easier   •   Shift + ↑ / ↓  "
                          + (FreeRide.sequential ? "jump 3 gears" : "front shift") + "   •   PgUp / PgDn  grade   •   Space  pause / resume"
                    color: Theme.textMuted
                    font.pixelSize: 13
                }

                // --- ERG -------------------------------------------------------------
                ErgControl {
                    id: ergControl
                    visible: FreeRide.mode === "erg"
                    x: 18
                    y: controlCard.contentTop
                    width: parent.width - 36
                    height: parent.height - y - 18
                }

                // --- free ------------------------------------------------------------
                Column {
                    visible: FreeRide.mode === "free"
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 14
                    width: Math.min(parent.width - 60, 640)
                    spacing: 12
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "freeRide"
                        size: 46
                        strokeWidth: 2.2
                        color: Theme.accentLight
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "No resistance control"
                        color: Theme.text
                        font.pixelSize: 24
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "The trainer stays in its own free-ride mode. Speed and distance are calculated "
                              + "from your power on a flat road. Zwift Click buttons are inactive – "
                              + "choose Gears to shift or ERG to hold a wattage."
                        color: Theme.textSecondary
                        font.pixelSize: 16
                        wrapMode: Text.WordWrap
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 12
                        AppButton {
                            height: 44
                            kind: "secondary"
                            fontSize: 15
                            iconSize: 18
                            bold: false
                            iconName: "cog"
                            text: "Use Gears"
                            onClicked: FreeRide.mode = "gears"
                        }
                        AppButton {
                            height: 44
                            kind: "secondary"
                            fontSize: 15
                            iconSize: 18
                            bold: false
                            iconName: "target"
                            text: "Use ERG"
                            onClicked: FreeRide.mode = "erg"
                        }
                    }
                }
            }

            // power / speed / heart rate chart
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 200
                title: "Power, Speed & Heart Rate"
                headerRight: [
                    Row {
                        spacing: 22
                        Repeater {
                            model: [
                                { label: "Power (W)", color: Theme.warning },
                                { label: "Speed (km/h)", color: Theme.speed },
                                { label: "Heart Rate (BPM)", color: Theme.heartRate }
                            ]
                            Row {
                                spacing: 8
                                Rectangle {
                                    width: 13; height: 13; radius: 6.5
                                    color: modelData.color
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: modelData.label
                                    color: Theme.textSecondary
                                    font.pixelSize: 15
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                ]

                FreeRideChart {
                    x: 6
                    y: 44
                    width: parent.width - 14
                    height: parent.height - 48
                    xMax: page.chartTotal
                    powerSeries: FreeRide.powerSeries
                    speedSeries: FreeRide.speedSeries
                    hrSeries: FreeRide.heartRateSeries
                    seriesStep: FreeRide.seriesStep
                    targetWatts: FreeRide.mode === "erg" && (FreeRide.active || page.st === "idle") ? FreeRide.targetWatts : 0
                    cursor: FreeRide.active || page.finished ? FreeRide.elapsedSeconds : -1
                }
            }
        }

        // ================= right column =================
        // (a Column, not a ColumnLayout: hidden cards must not leave gaps in Qt 6.4)
        Column {
            Layout.preferredWidth: 300
            Layout.maximumWidth: 300
            Layout.alignment: Qt.AlignTop
            topPadding: 64
            spacing: 12

            // controls
            Card {
                id: controls
                width: parent.width
                height: controlsCol.implicitHeight + 32
                Column {
                    id: controlsCol
                    x: 14; y: 16
                    width: parent.width - 28
                    spacing: 10

                    AppButton {
                        visible: !FreeRide.active
                        width: parent.width
                        height: 50
                        kind: "primary"
                        iconName: "play"
                        text: page.finished ? "Ride Again" : "Start Now"
                        enabled: FreeRide.trainerConnected && !page.otherSession
                        onClicked: FreeRide.start()
                    }
                    AppButton {
                        visible: !FreeRide.active && !page.finished
                        width: parent.width
                        height: 44
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        bold: false
                        iconName: FreeRide.waiting ? "close" : "cadence"
                        text: FreeRide.waiting ? "Cancel Auto-Start" : "Start When Pedaling"
                        enabled: FreeRide.waiting || (FreeRide.trainerConnected && !page.otherSession)
                        onClicked: FreeRide.waiting ? FreeRide.reset() : FreeRide.startWhenPedaling()
                    }
                    AppButton {
                        visible: FreeRide.active
                        width: parent.width
                        height: 50
                        kind: FreeRide.paused ? "success" : "danger"
                        iconName: FreeRide.paused ? "play" : "pause"
                        text: FreeRide.paused ? "Resume" : "Pause Ride"
                        onClicked: FreeRide.togglePause()
                    }
                    RowLayout {
                        width: parent.width
                        spacing: 10
                        AppButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            kind: "secondary"
                            fontSize: 15
                            iconSize: 18
                            iconName: "stop"
                            text: "Finish"
                            enabled: FreeRide.active
                            onClicked: page.requestFinish()
                        }
                        AppButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            kind: "secondary"
                            fontSize: 15
                            iconSize: 18
                            iconName: "refresh"
                            text: "Reset"
                            enabled: page.finished
                            onClicked: FreeRide.reset()
                        }
                    }
                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: FreeRide.waiting ? "Start pedaling – the ride begins with the first pedal strokes."
                              : !FreeRide.active && page.otherSession
                                ? (Rowing.active ? "A row is in progress – finish it to start a free ride."
                                                 : "A workout is running – finish it to start a free ride.")
                              : page.st === "idle" && FreeRide.trainerConnected ? "Space starts and pauses the ride." : ""
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // no trainer connected
            Card {
                visible: !FreeRide.trainerConnected
                width: parent.width
                height: 142
                Row {
                    x: 16; y: 16
                    spacing: 10
                    Icon { name: "bluetooth"; size: 22; color: Theme.warning; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: "No trainer connected"
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    x: 16; y: 46
                    width: parent.width - 32
                    text: "Connect a smart trainer (or the virtual trainer) to ride with gears or ERG."
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }
                AppButton {
                    x: 16
                    width: parent.width - 32
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    height: 40
                    fontSize: 15
                    iconSize: 18
                    iconName: "bluetooth"
                    text: "Open Devices"
                    onClicked: page.openDevices()
                }
            }

            // finished summary
            Card {
                visible: page.finished
                width: parent.width
                height: summaryCol.implicitHeight + 32
                Column {
                    id: summaryCol
                    x: 16; y: 16
                    width: parent.width - 32
                    spacing: 10
                    Text {
                        text: "Ride finished"
                        color: Theme.text
                        font.pixelSize: 19
                        font.weight: Font.Bold
                    }
                    Grid {
                        columns: 2
                        width: parent.width
                        rowSpacing: 8
                        Repeater {
                            model: ["Time", "Distance", "Avg power", "Max power", "Avg speed", "Energy"]
                            Column {
                                width: summaryCol.width / 2
                                Text { text: modelData; color: Theme.textSecondary; font.pixelSize: 13 }
                                Text {
                                    text: {
                                        switch (index) {
                                        case 0: return Theme.clock(FreeRide.elapsedSeconds)
                                        case 1: return page.kmText(FreeRide.distanceMeters) + " km"
                                        case 2: return FreeRide.avgWatts + " W"
                                        case 3: return FreeRide.maxWatts + " W"
                                        case 4: return FreeRide.avgSpeedKmh.toFixed(1) + " km/h"
                                        default: return Math.round(FreeRide.kilojoules) + " kJ"
                                        }
                                    }
                                    color: Theme.text
                                    font.pixelSize: 19
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }
                    Text {
                        visible: FreeRide.lastRideId < 0
                        width: parent.width
                        text: "Not saved (discarded or shorter than 30 s)."
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    AppButton {
                        visible: FreeRide.lastRideId >= 0
                        width: parent.width
                        height: 40
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        bold: false
                        iconName: "history"
                        text: "View in History"
                        onClicked: page.goTo("history")
                    }
                }
            }

            // Zwift Click
            Card {
                id: clickCard
                width: parent.width
                height: clickCol.y + clickCol.implicitHeight + 16
                title: "Zwift Click"
                titleSize: 18
                headerRight: [
                    Row {
                        spacing: 6
                        Repeater {
                            model: ["−", "+"]
                            Rectangle {
                                readonly property bool down: index === 0 ? page.minusDown : page.plusDown
                                width: 26; height: 26; radius: 13
                                color: down ? (index === 0 ? Theme.accent : Theme.orange) : "transparent"
                                border.color: down ? "transparent" : Theme.fieldBorder
                                opacity: Devices.controllerConnected ? 1 : 0.4
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: parent.down ? "white" : Theme.textSecondary
                                    font.pixelSize: 16
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }
                ]
                Column {
                    id: clickCol
                    x: 16
                    y: clickCard.contentTop - 4
                    width: parent.width - 32
                    spacing: 8
                    Row {
                        spacing: 10
                        Icon {
                            name: "controller"
                            size: 30
                            strokeWidth: 2
                            color: Devices.controllerConnected ? Theme.successText : Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: Devices.controllerConnected
                                      ? (Devices.controllerCount > 1 ? Devices.controllerCount + " pods connected" : "Connected")
                                      : Devices.controllerState === 1 ? "Connecting…" : "Not connected"
                                color: Devices.controllerConnected ? Theme.successText : Theme.text
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: clickCol.width - 40
                                text: Devices.controllerConnected
                                      ? Devices.controllerName
                                        + (Devices.controllerBattery >= 0 ? "  •  Battery " + Devices.controllerBattery + " %" : "")
                                      : "Virtual shifting with Zwift Click v1 / v2"
                                color: Theme.textSecondary
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: FreeRide.mode === "erg" ? "+ / −  change the ERG target by 10 W"
                              : FreeRide.mode === "free" ? "Buttons are inactive in Free mode"
                              : "+  shift harder   •   −  shift easier"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                    AppButton {
                        visible: !Devices.controllerConnected
                        width: parent.width
                        height: 40
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        bold: false
                        iconName: "bluetooth"
                        text: "Connect in Devices"
                        onClicked: page.openDevices()
                    }
                }
            }

            // bike & gears
            Card {
                id: bikeCard
                visible: !page.finished
                width: parent.width
                height: bikeCol.y + bikeCol.implicitHeight + 16
                title: "Bike & Gears"
                titleSize: 18
                Column {
                    id: bikeCol
                    x: 16
                    y: bikeCard.contentTop - 4
                    width: parent.width - 32
                    spacing: 6
                    Repeater {
                        model: ["Chainrings", "Cassette", "Wheel", "Shifting"]
                        Item {
                            width: bikeCol.width
                            height: 22
                            Text { text: modelData; color: Theme.textSecondary; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: {
                                    switch (index) {
                                    case 0: return page.ringsText() + (page.rings.length === 1 ? " (1×)" : "")
                                    case 1: return page.cassetteText() + "  (" + page.cogs.length + ")"
                                    case 2: return (page.setup.circumferenceMm || 0) + " mm"
                                    default: return (FreeRide.sequential ? "Sequential" : "Front + rear") + "  •  " + FreeRide.gearCount + " gears"
                                    }
                                }
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                    Item { width: 1; height: 2 }
                    AppButton {
                        width: parent.width
                        height: 40
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        bold: false
                        iconName: "cog"
                        text: "Gear Setup…"
                        onClicked: page.openGearSetup()
                    }
                }
            }

        }
    }
}
