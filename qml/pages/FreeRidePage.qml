import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

// Free ride without a workout: "Ride" (a simulated road with a grade; shift
// on your bike, or virtually with the gears set up on the Devices page) or
// ERG (hold a set wattage). All logic lives in the FreeRide and Gears
// singletons; Zwift Click buttons are routed by Gears.
Item {
    id: page
    signal openDevices()
    signal requestFinish()

    readonly property string st: FreeRide.state
    readonly property bool finished: st === "finished"
    // only one session at a time (FreeRide refuses to start otherwise)
    readonly property bool otherSession: Training.armedOrActive || Rowing.active
    readonly property var setup: Gears.gearSetup
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
    function modeName(m) { return m === "erg" ? "ERG" : "Ride" }
    // "Shifting: …" status line (also on the Video Ride page)
    function shiftingText() {
        if (!FreeRide.virtualGears)
            return "your bike's gears"
        if (FreeRide.simulationFallback)
            return "Virtual gears"
        return Devices.controllerConnected ? "Virtual gears (Zwift Click)" : "Virtual gears (on-screen)"
    }
    function goTo(name) {
        var w = ApplicationWindow.window
        if (w && typeof w.showPage === "function")
            w.showPage(name)
    }

    Connections {
        target: FreeRide
        function onShifted(direction) {   // ERG steps (Zwift Click ±10 W)
            if (FreeRide.mode === "erg")
                ergControl.pulse(direction)
        }
    }
    Connections {
        target: Gears
        function onShifted(direction) {
            if (FreeRide.virtualGears) {
                gearDisplay.flash(direction)
                var b = direction > 0 ? harderButton : easierButton
                b.pulse()
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
                              : "Ride a road without a workout. Climbs get harder like outdoors."
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
                        readonly property color dot: page.st === "idle" && FreeRide.ergHolding ? Theme.warning
                                                   : page.st === "ready" ? Theme.success
                                                   : page.st === "running" ? Theme.danger
                                                   : page.st === "paused" ? Theme.warning
                                                   : page.st === "finished" ? Theme.accentLight : Theme.textMuted
                        visible: page.st !== "idle" || FreeRide.ergHolding
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
                                text: page.st === "idle" ? "ERG on  •  " + FreeRide.targetWatts + " W"
                                      : page.st === "ready" ? "Waiting for pedaling…"
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
                        objectName: "freeRideModeSelector"
                        anchors.verticalCenter: parent.verticalCenter
                        height: 46
                        options: [
                            { value: "ride", label: "Ride", icon: "mountain" },
                            { value: "erg", label: "ERG", icon: "target" }
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
                            : FreeRide.virtualGears && FreeRide.gearTargetWatts > 0
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
                            : FreeRide.virtualGears ? "Gear " + Gears.gearLabel : "Max: " + FreeRide.maxWatts + " W"
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

            // ride (road + shifting) / ERG panel
            Card {
                id: controlCard
                Layout.fillWidth: true
                Layout.preferredHeight: 362
                title: FreeRide.mode === "erg" ? "ERG – Target Power" : "Road"
                headerRight: [
                    Text {
                        visible: FreeRide.mode !== "erg"
                        text: "Shifting: " + page.shiftingText()
                        color: Theme.textSecondary
                        font.pixelSize: 15
                    }
                ]

                // --- ride ----------------------------------------------------------
                RowLayout {
                    id: rideRow
                    visible: FreeRide.mode !== "erg"
                    x: 16
                    y: controlCard.contentTop - 4
                    width: parent.width - 32
                    height: parent.height - y - 42
                    spacing: 16

                    // virtual gears: small on-screen shifter around the gear (read-only setup)
                    ShiftButton {
                        id: easierButton
                        visible: FreeRide.virtualGears
                        Layout.preferredWidth: 84
                        Layout.preferredHeight: 150
                        Layout.alignment: Qt.AlignVCenter
                        iconName: "minus"
                        iconSize: 30
                        label: "Easier"
                        labelSize: 15
                        sublabel: "↓"
                        tint: Theme.accent
                        enabled: Gears.canShiftDown
                        onClicked: Gears.shiftDown()
                    }
                    ColumnLayout {
                        visible: FreeRide.virtualGears
                        Layout.fillWidth: true
                        Layout.preferredWidth: 300
                        Layout.fillHeight: true
                        spacing: 6
                        GearDisplay {
                            id: gearDisplay
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            frontTeeth: Gears.frontTeeth
                            rearTeeth: Gears.rearTeeth
                            gearIndex: Gears.gearIndex
                            gearCount: Gears.gearCount
                            ratio: Gears.gearRatio
                            chainrings: page.rings
                            cogs: page.cogs
                            frontIndex: Gears.frontIndex
                            rearIndex: Gears.rearIndex
                            sequential: Gears.sequential
                        }
                        Item { Layout.fillHeight: true }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: FreeRide.simulationFallback
                                  ? "Your trainer can't simulate climbs – using virtual gears."
                                  : FreeRide.resistancePath === "gearSimulation"
                                    ? "Leave your bike in " + Gears.referenceLabel + " on the trainer."
                                    : "The trainer sets the watts your gear needs."
                            color: FreeRide.simulationFallback ? Theme.warningText : Theme.textSecondary
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                    }
                    ShiftButton {
                        id: harderButton
                        visible: FreeRide.virtualGears
                        Layout.preferredWidth: 84
                        Layout.preferredHeight: 150
                        Layout.alignment: Qt.AlignVCenter
                        iconName: "plus"
                        iconSize: 30
                        label: "Harder"
                        labelSize: 15
                        sublabel: "↑"
                        tint: Theme.orange
                        enabled: Gears.canShiftUp
                        onClicked: Gears.shiftUp()
                    }

                    // real gears: the trainer simulates the grade
                    ColumnLayout {
                        visible: !FreeRide.virtualGears
                        Layout.fillWidth: true
                        Layout.preferredWidth: 300
                        Layout.fillHeight: true
                        spacing: 10
                        Item { Layout.fillHeight: true }
                        Icon {
                            Layout.alignment: Qt.AlignHCenter
                            name: "bike"
                            size: 46
                            strokeWidth: 2
                            color: Theme.accentLight
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "Shift on your bike"
                            color: Theme.text
                            font.pixelSize: 24
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: "The trainer gets the grade and makes climbs harder, like on the road."
                            color: Theme.textSecondary
                            font.pixelSize: 15
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "Grade sent to trainer: " + (FreeRide.simulationActive ? page.gradeText(FreeRide.simulationGradePercent) : "–")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        AppButton {
                            Layout.alignment: Qt.AlignHCenter
                            height: 40
                            kind: "secondary"
                            fontSize: 15
                            iconSize: 18
                            iconSpacing: 8
                            horizontalPadding: 14
                            bold: false
                            iconName: "cog"
                            text: "Virtual gears on Devices"
                            onClicked: page.openDevices()
                        }
                        Item { Layout.fillHeight: true }
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
                            text: FreeRide.virtualGears
                                  ? (FreeRide.gearTargetWatts > 0
                                     ? "This gear at " + FreeRide.cadence + " rpm needs ≈ " + FreeRide.gearTargetWatts + " W"
                                     : "Harder gear = more speed and more watts at the same cadence.")
                                  : "Climbs get harder like on the road."
                            color: Theme.textSecondary
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                Text {
                    visible: rideRow.visible
                    x: 16
                    width: parent.width - 32
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: FreeRide.virtualGears
                          ? "Keys:   ↑ / ↓  shift   •   Shift + ↑ / ↓  " + (Gears.sequential ? "jump 3 gears" : "front shift")
                            + "   •   PgUp / PgDn  grade   •   Space  pause / resume"
                          : "Keys:   PgUp / PgDn  grade   •   Space  pause / resume"
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

            // shifting (set up on the Devices page) + Zwift Click
            Card {
                id: clickCard
                width: parent.width
                height: clickCol.y + clickCol.implicitHeight + 16
                title: "Shifting"
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
                            name: FreeRide.virtualGears ? "cog" : "bike"
                            size: 28
                            strokeWidth: 2
                            color: FreeRide.virtualGears ? Theme.orange : Theme.accentLight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                width: clickCol.width - 40
                                text: FreeRide.mode === "erg" ? "ERG – no shifting needed"
                                      : page.shiftingText().charAt(0).toUpperCase() + page.shiftingText().slice(1)
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: clickCol.width - 40
                                text: Devices.controllerConnected
                                      ? Devices.controllerName
                                        + (Devices.controllerBattery >= 0 ? "  •  Battery " + Devices.controllerBattery + " %" : "")
                                      : Gears.summary
                                color: Theme.textSecondary
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: FreeRide.mode === "erg" ? "Click + / −  target ±10 W"
                              : "Click +  harder gear   •   −  easier gear"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                    AppButton {
                        width: parent.width
                        height: 40
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        bold: false
                        iconName: Devices.controllerConnected ? "cog" : "bluetooth"
                        text: Devices.controllerConnected ? "Virtual gears on Devices" : "Zwift Click & gears on Devices"
                        onClicked: page.openDevices()
                    }
                }
            }
        }
    }
}
