import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

// Rowing on a Concept2 PM5 (or the simulated rower) in three modes:
// Free Row ("solo"), Watt Plan ("plan") and Ghost Race ("ghost").
// All logic lives in the Rowing / RowingRecords singletons.
Item {
    id: page
    signal openDevices()
    signal openBuilder()
    signal requestFinish()
    signal message(string text)

    // mode chosen in the UI; the backend mode wins once something is set up
    property string localMode: "ghost"
    // a workout / free ride / video ride IN PROGRESS on another page disables
    // Start (the reason card says which, with the fix; SessionGuard)
    readonly property var blocker: Sessions.rowingBlocker
    readonly property bool otherSession: !!page.blocker && ("" + (page.blocker.kind || "")).length > 0
    readonly property string uiMode: Rowing.mode !== "none" ? Rowing.mode : localMode
    property int soloDistance: 2000          // 0 = open-ended
    property int ghostDistance: 2000
    property int planWorkoutId: -1
    property var workouts: []
    property bool showCustom: false          // custom distance entry open (solo / ghost)
    readonly property var bests: { RowingRecords.revision; return RowingRecords.allBests() }
    readonly property var standard: Rowing.standardDistances
    readonly property bool locked: Rowing.active
    readonly property bool finished: Rowing.state === "finished"
    // distance of the set-up row, or of the selection before anything is set up
    readonly property int target: Rowing.targetDistanceM > 0 || Rowing.mode !== "none"
                                  ? Rowing.targetDistanceM
                                  : (uiMode === "ghost" ? ghostDistance : uiMode === "solo" ? soloDistance : 0)
    readonly property real distance: Rowing.distanceMeters
    readonly property real openScale: Math.max(1000, Math.ceil((distance + 250) / 500) * 500)

    // --- helpers -------------------------------------------------------------
    function meters(m) { return Theme.thousands(Math.floor(m)) }
    function paceText(sec) { return sec > 0 ? Theme.clock(sec) : "-:--" }
    function timeTenths(sec) {
        if (!(sec > 0))
            return "–"
        // integer tenths first, so 59.97 s reads "1:00.0" (never "0:59.10" / "1:60.0")
        var tenths = Math.round(sec * 10)
        return Theme.clock(Math.floor(tenths / 10)) + "." + (tenths % 10)
    }
    // a time difference: "4.3 s" below a minute, "1:02.5" (m:ss.s) above
    function diffText(sec) {
        sec = Math.abs(sec)
        return Math.round(sec * 10) < 600 ? (Math.round(sec * 10) / 10).toFixed(1) + " s" : timeTenths(sec)
    }
    function bestFor(d) {
        for (var i = 0; i < bests.length; ++i)
            if (bests[i].distanceM === d)
                return bests[i]
        return null
    }
    function refreshWorkouts() {
        workouts = Rowing.rowingWorkouts()
        if (planWorkoutId < 0 && workouts.length > 0)
            planWorkoutId = workouts[0].workoutId
    }
    function applySetup() {
        if (Rowing.active)
            return
        if (localMode === "solo")
            Rowing.setupSolo(soloDistance)
        else if (localMode === "ghost")
            Rowing.setupGhost(ghostDistance)
        else if (planWorkoutId < 0 || !Rowing.setupPlan(planWorkoutId))
            Rowing.clear()
    }
    function selectMode(m) {
        if (Rowing.active)
            return
        localMode = m
        showCustom = false
        if (m === "plan")
            refreshWorkouts()
        applySetup()
    }
    function syncFromBackend() {
        if (Rowing.mode === "none")
            return
        localMode = Rowing.mode
        if (Rowing.mode === "solo")
            soloDistance = Rowing.targetDistanceM
        else if (Rowing.mode === "ghost")
            ghostDistance = Rowing.targetDistanceM
        else if (Rowing.mode === "plan")
            planWorkoutId = Rowing.workoutId
    }
    function startNow() {
        if (Rowing.state === "idle")
            applySetup()
        Rowing.start()
    }
    function goTo(name) {
        var w = ApplicationWindow.window
        if (w && typeof w.showPage === "function")
            w.showPage(name)
    }

    Component.onCompleted: {
        syncFromBackend()
        refreshWorkouts()
    }
    onVisibleChanged: {
        if (!visible)
            return
        refreshWorkouts()
        // opening the page prepares the row ("ready"); it only starts with
        // Start / Space - strokes on the rower never start it
        if (Rowing.state === "idle" && !page.otherSession)
            applySetup()
    }

    Connections {
        target: Rowing
        function onSetupChanged() { page.syncFromBackend() }
        function onStartRefused(reason) { page.message(reason) }
        function onNewRecordSet(d, t, prev) {
            page.message(prev > 0
                         ? "New " + Theme.thousands(d) + " m record: " + page.timeTenths(t)
                           + " (" + page.diffText(prev - t) + " faster)"
                         : "First " + Theme.thousands(d) + " m record: " + page.timeTenths(t))
        }
        function onFinished() {
            if (Rowing.completed && Rowing.lastRideId >= 0 && !Rowing.newRecord)
                page.message("Row complete – saved to History")
        }
    }
    Connections {
        target: Workouts
        function onCountChanged() { page.refreshWorkouts() }
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

            // header
            Item {
                Layout.topMargin: 26
                Layout.fillWidth: true
                Layout.preferredHeight: header.implicitHeight
                Column {
                    id: header
                    spacing: 0
                    Text {
                        text: "Rowing"
                        color: Theme.text
                        font.pixelSize: 52
                        font.weight: Font.Black
                    }
                    Text {
                        text: page.uiMode === "plan" && Rowing.mode === "plan"
                              ? Rowing.workoutName + "  •  " + Theme.clock(Rowing.totalDurationSec) + " watt plan"
                              : "Row your distance. Race your past self."
                        color: Theme.text
                        font.pixelSize: 21
                    }
                }

                // status pill
                Rectangle {
                    id: statusPill
                    readonly property string st: Rowing.state
                    readonly property color dot: st === "ready" ? Theme.success
                                               : st === "running" ? Theme.danger
                                               : st === "paused" ? Theme.warning
                                               : st === "finished" ? Theme.accentLight : Theme.textMuted
                    visible: st !== "idle"
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
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
                                running: statusPill.st === "running"
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.25; duration: 700 }
                                NumberAnimation { to: 1; duration: 700 }
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: statusPill.st === "ready" ? "Ready – press Start"
                                  : statusPill.st === "running" ? "Rowing  •  " + Rowing.workoutName
                                  : statusPill.st === "paused" ? "Paused – press Resume"
                                  : (Rowing.completed ? "Finished" : "Stopped")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            // live metrics
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 136
                Layout.fillHeight: false   // children fill the row; the row itself never grows
                spacing: 12

                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 146
                    Layout.fillHeight: true
                    label: "Power"
                    iconName: "bolt"
                    iconColor: Theme.warningText
                    valueColor: Theme.warningText
                    value: Rowing.watts
                    unit: "W"
                    footer: Rowing.mode === "plan" && Rowing.targetWatts > 0
                            ? "Target: " + Rowing.targetWatts + " W"
                            : "Avg: " + Rowing.avgWatts + " W"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 213
                    Layout.fillHeight: true
                    label: "Distance"
                    iconName: "mapPin"
                    iconColor: Theme.accentLight
                    value: page.meters(page.distance) + (page.target > 0 ? " / " + page.meters(page.target) : "")
                    unit: "m"
                    progress: page.target > 0 ? Rowing.progress : -1
                    footer: Rowing.strokeCount + " strokes  •  " + Rowing.calories + " kcal"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 155
                    Layout.fillHeight: true
                    label: "Pace"
                    iconName: "gauge"
                    iconColor: Theme.pace
                    value: page.paceText(Rowing.pace500)
                    unit: "/500m"
                    footer: Rowing.mode === "ghost" && Rowing.hasGhost
                            ? "Ghost: " + page.paceText(Rowing.recordPace500)
                            : "Avg: " + page.paceText(Rowing.avgPace500)
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 152
                    Layout.fillHeight: true
                    label: "Stroke Rate"
                    iconName: "waves"
                    iconColor: Theme.cyan
                    value: Rowing.spm
                    unit: "SPM"
                    footer: "Avg: " + (Rowing.avgSpm > 0 ? Rowing.avgSpm : "–")   // stroke count: on the course below
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 174
                    Layout.fillHeight: true
                    label: "Heart Rate"
                    iconName: "heart"
                    iconColor: Theme.heartRate
                    value: Rowing.heartRate > 0 ? Rowing.heartRate : "–"
                    unit: "BPM"
                    footer: "Avg: " + (Rowing.avgHeartRate > 0 ? Rowing.avgHeartRate + " BPM" : "–")
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true
                    label: "Elapsed"
                    iconName: "timer"
                    value: Theme.clock(Rowing.elapsedSeconds)
                    footer: Rowing.mode === "plan"
                            ? "Left: " + Theme.clock(Rowing.remainingSec)
                            : (page.target > 0 && page.distance > 20 && Rowing.elapsedSeconds > 5
                               ? "Est. finish: " + Theme.clock(Rowing.elapsedSeconds * page.target / page.distance)
                               : (Rowing.mode === "ghost" && Rowing.hasGhost
                                  ? "Record: " + Theme.clock(Rowing.recordTimeSec) : "Avg: " + page.paceText(Rowing.avgPace500)))
                }
            }

            // power / pace / heart rate chart
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 318
                title: "Power, Pace & Heart Rate"
                headerRight: [
                    Row {
                        spacing: 22
                        Repeater {
                            model: [
                                { label: "Power (Watts)", color: Theme.warning },
                                { label: "Pace (/500m)", color: Theme.pace },
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

                RowingChart {
                    x: 6
                    y: 46
                    width: parent.width - 14
                    height: parent.height - 52
                    readonly property bool byTime: Rowing.mode === "plan"
                    xMode: byTime ? "time" : "distance"
                    xMax: byTime ? Math.max(60, Rowing.totalDurationSec) : (page.target > 0 ? page.target : page.openScale)
                    powerSeries: Rowing.powerSeries
                    paceSeries: Rowing.paceSeries
                    hrSeries: Rowing.heartRateSeries
                    distanceSeries: Rowing.distanceSeries
                    seriesStep: Rowing.seriesStep
                    targets: byTime ? Rowing.intervals : []
                    cursor: Rowing.active || page.finished ? (byTime ? Rowing.elapsedSeconds : page.distance) : -1
                    cursorLabel: byTime ? Theme.clock(Rowing.elapsedSeconds) : page.meters(page.distance) + " m"
                }
            }

            // course / plan
            Card {
                id: courseCard
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 230
                title: page.uiMode === "ghost"
                       ? "Ghost Race  •  " + page.meters(page.target > 0 ? page.target : page.ghostDistance) + " m"
                       : page.uiMode === "plan"
                         ? (Rowing.mode === "plan" ? "Watt Plan  •  " + Rowing.workoutName : "Watt Plan")
                         : (page.target > 0 ? "Distance  •  " + page.meters(page.target) + " m" : "Free Row")

                headerRight: [
                    Item {
                        // only the visible variant may define the width (childrenRect counts hidden items)
                        width: ghostHeader.visible ? ghostHeader.implicitWidth : infoHeader.implicitWidth
                        height: 24
                        // ghost: YOU | GHOST | gap
                        Row {
                            id: ghostHeader
                            visible: page.uiMode === "ghost"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 14
                            Text {
                                text: "<b>YOU</b>&nbsp; " + page.meters(page.distance) + " m"
                                textFormat: Text.StyledText
                                color: Theme.text
                                font.pixelSize: 16
                            }
                            Rectangle { width: 1; height: 20; color: Theme.fieldBorder; visible: Rowing.hasGhost }
                            Text {
                                visible: Rowing.hasGhost
                                text: "<b>GHOST</b>&nbsp; " + page.meters(Rowing.ghostDistanceM) + " m"
                                textFormat: Text.StyledText
                                color: Theme.text
                                font.pixelSize: 16
                            }
                            Rectangle { width: 1; height: 20; color: Theme.fieldBorder; visible: Rowing.hasGhost }
                            Text {
                                readonly property bool ahead: Rowing.ghostGapMeters >= 0
                                text: !Rowing.hasGhost
                                      ? "No record yet – set your first time"
                                      : (ahead ? "+" : "−") + page.meters(Math.abs(Rowing.ghostGapMeters)) + " m "
                                        + (ahead ? "ahead" : "behind") + "  ("
                                        + (Rowing.ghostGapSeconds >= 0 ? "+" : "−")
                                        + Math.abs(Rowing.ghostGapSeconds).toFixed(1) + " s)"
                                color: !Rowing.hasGhost ? Theme.textSecondary : (ahead ? Theme.successText : Theme.danger)
                                font.pixelSize: 16
                                font.weight: Font.Bold
                            }
                        }
                        // solo: rowed / to go; plan: current interval
                        Text {
                            id: infoHeader
                            visible: !ghostHeader.visible
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.uiMode === "solo"
                                  ? (page.target > 0
                                     ? page.meters(page.distance) + " m rowed  •  " + Theme.thousands(Math.max(0, page.target - Math.floor(page.distance))) + " m to go  •  " + Rowing.strokeCount + " strokes"
                                     : page.meters(page.distance) + " m rowed  •  " + Rowing.strokeCount + " strokes")
                                  : (Rowing.mode !== "plan" ? ""
                                     : Rowing.currentIntervalIndex >= 0 && Rowing.active
                                       ? "Interval " + (Rowing.currentIntervalIndex + 1) + "/" + Rowing.intervalCount
                                         + "  •  " + Rowing.currentIntervalName
                                       : Rowing.intervalCount + " intervals  •  " + Theme.clock(Rowing.totalDurationSec))
                            color: Theme.textSecondary
                            font.pixelSize: 16
                        }
                    }
                ]

                // ghost race course
                GhostRaceTrack {
                    visible: page.uiMode === "ghost"
                    x: 16
                    y: courseCard.contentTop
                    width: parent.width - 32
                    height: parent.height - y - 16
                    totalMeters: page.target > 0 ? page.target : page.ghostDistance
                    youMeters: page.distance
                    ghostMeters: Rowing.ghostDistanceM
                    ghostAvailable: Rowing.hasGhost
                    ghostRowing: Rowing.running
                    gapMeters: Rowing.ghostGapMeters
                    strokeCount: Rowing.strokeCount
                    strokeRate: Rowing.spm
                }

                // solo course
                RowerTrack {
                    visible: page.uiMode === "solo"
                    x: 16
                    y: courseCard.contentTop
                    width: parent.width - 32
                    height: parent.height - y - 16
                    totalMeters: page.target > 0 ? page.target : page.openScale
                    finishLabel: page.target > 0 ? page.meters(page.target) + " m" : ""
                    youMeters: page.distance
                    strokeCount: Rowing.strokeCount
                    strokeRate: Rowing.spm
                }

                // watt plan
                Item {
                    visible: page.uiMode === "plan"
                    x: 16
                    y: courseCard.contentTop
                    width: parent.width - 32
                    height: parent.height - y - 14

                    RowLayout {
                        id: planStats
                        visible: Rowing.mode === "plan"
                        width: parent.width
                        height: 46
                        spacing: 10
                        // static model (captions); the live values are bindings in
                        // the delegate, so the tiles are not rebuilt 4× per second
                        Repeater {
                            model: ["Target", "Current", "Interval left", "Next target", "Interval avg", "Compliance"]
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 10
                                color: Theme.cardStrong
                                border.color: Theme.cardBorder
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    Text {
                                        text: modelData
                                        color: Theme.textSecondary
                                        font.pixelSize: 14
                                        anchors.baseline: statValue.baseline
                                    }
                                    Text {
                                        id: statValue
                                        text: {
                                            switch (index) {
                                            case 0: return Rowing.targetWatts > 0 ? Rowing.targetWatts + " W" : "–"
                                            case 1: return Rowing.watts + " W"
                                            case 2: return Theme.clock(Rowing.intervalRemainingSec)
                                            case 3: return Rowing.nextTargetWatts > 0 ? Rowing.nextTargetWatts + " W" : "Finish"
                                            case 4: return Rowing.intervalAvgWatts + " W"
                                            default: return Rowing.compliance + "%"
                                            }
                                        }
                                        color: {
                                            if (index === 1)
                                                return Rowing.targetWatts <= 0 ? Theme.text
                                                     : Math.abs(Rowing.watts - Rowing.targetWatts) <= Rowing.targetWatts * 0.1 ? Theme.successText
                                                     : (Rowing.watts < Rowing.targetWatts ? Theme.warningText : Theme.danger)
                                            if (index === 5)
                                                return Rowing.compliance >= 80 ? Theme.successText
                                                     : (Rowing.compliance >= 50 ? Theme.warningText : Theme.danger)
                                            return Theme.text
                                        }
                                        font.pixelSize: 21
                                        font.weight: Font.Bold
                                    }
                                }
                            }
                        }
                    }

                    RowingIntervalChart {
                        visible: Rowing.mode === "plan"
                        y: planStats.height + 10
                        width: parent.width
                        height: parent.height - y
                        intervals: Rowing.intervals
                        totalSec: Rowing.totalDurationSec
                        elapsed: Rowing.elapsedSeconds
                        currentIndex: Rowing.active ? Rowing.currentIntervalIndex : -1
                        powerSeries: Rowing.powerSeries
                        seriesStep: Rowing.seriesStep
                        strokeCount: Rowing.strokeCount
                        strokeRate: Rowing.spm
                    }

                    Column {
                        visible: Rowing.mode !== "plan"
                        anchors.centerIn: parent
                        spacing: 14
                        Icon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: "signal"
                            size: 40
                            color: Theme.warning
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: page.workouts.length > 0 ? "Choose a rowing workout in the Session panel"
                                                           : "No rowing workouts yet – create one in the Builder"
                            color: Theme.textSecondary
                            font.pixelSize: 18
                        }
                    }
                }
            }
        }

        // ================= right column =================
        ColumnLayout {
            Layout.preferredWidth: 300
            Layout.maximumWidth: 300
            Layout.fillHeight: true
            spacing: 12

            // a ride in progress on another page: why Start is disabled, and
            // the fix (at the top: always in view)
            SessionBlockNotice {
                id: blockNotice
                Layout.topMargin: shown ? 64 : 0
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                namePrefix: "rowing"
                startWhat: "a row"
                blocker: !Rowing.active ? page.blocker : ({})
            }

            Card {
                id: sessionCard
                Layout.topMargin: blockNotice.shown ? 0 : 64
                Layout.fillWidth: true
                Layout.preferredHeight: sessionCol.y + sessionCol.implicitHeight + 16
                title: "Session"
                titleSize: 21

                Column {
                    id: sessionCol
                    x: 14
                    y: sessionCard.contentTop - 4
                    width: parent.width - 28
                    spacing: 8

                    Text { text: "Mode"; color: Theme.text; font.pixelSize: 16 }
                    ModeOption {
                        width: parent.width
                        title: "Free Row"
                        subtitle: "Row a distance. No targets."
                        iconName: "waves"
                        iconColor: Theme.cyan
                        checked: page.uiMode === "solo"
                        enabled: !page.locked
                        onClicked: page.selectMode("solo")
                    }
                    ModeOption {
                        width: parent.width
                        title: "Ghost Race"
                        subtitle: "Race your personal best."
                        iconName: "ghost"
                        iconColor: Theme.ghost
                        checked: page.uiMode === "ghost"
                        enabled: !page.locked
                        onClicked: page.selectMode("ghost")
                    }
                    ModeOption {
                        width: parent.width
                        title: "Watt Plan"
                        subtitle: "Follow a target power workout."
                        iconName: "signal"
                        iconColor: Theme.warning
                        checked: page.uiMode === "plan"
                        enabled: !page.locked
                        onClicked: page.selectMode("plan")
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.cardBorder }

                    // --- distance (solo + ghost) ---------------------------------
                    Text {
                        visible: page.uiMode !== "plan"
                        text: page.uiMode === "ghost" ? "Race Distance" : "Workout Distance"
                        color: Theme.text
                        font.pixelSize: 16
                    }
                    AppComboBox {
                        id: distanceCombo
                        visible: page.uiMode !== "plan"
                        enabled: !page.locked
                        width: parent.width
                        fontSize: 18
                        textRole: "text"
                        readonly property int current: page.uiMode === "ghost" ? page.ghostDistance : page.soloDistance
                        model: {
                            var a = []
                            var ghost = page.uiMode === "ghost"
                            if (!ghost)
                                a.push({ text: "Open (no distance)", value: 0 })
                            for (var i = 0; i < page.standard.length; ++i) {
                                var d = page.standard[i]
                                var b = ghost ? page.bestFor(d) : null
                                a.push({ text: Theme.thousands(d) + " m" + (b && b.hasRecord ? "   •  PB " + page.timeTenths(b.timeSec) : ""),
                                         value: d })
                            }
                            if (current > 0 && page.standard.indexOf(current) < 0)
                                a.push({ text: Theme.thousands(current) + " m (custom)", value: current })
                            a.push({ text: "Custom…", value: -1 })
                            return a
                        }
                        function indexOfValue(v) {
                            for (var i = 0; i < model.length; ++i)
                                if (model[i].value === v)
                                    return i
                            return 0
                        }
                        currentIndex: indexOfValue(current)
                        onActivated: (index) => {
                            var v = model[index].value
                            if (v === -1) {
                                page.showCustom = true
                                customField.forceActiveFocus()
                            } else {
                                page.showCustom = false
                                if (page.uiMode === "ghost")
                                    page.ghostDistance = v
                                else
                                    page.soloDistance = v
                                page.localMode = page.uiMode
                                page.applySetup()
                            }
                            currentIndex = Qt.binding(function() { return distanceCombo.indexOfValue(distanceCombo.current) })
                        }
                    }
                    Row {
                        id: customRow
                        visible: page.showCustom && page.uiMode !== "plan" && !page.locked
                        width: parent.width
                        spacing: 8
                        AppTextField {
                            id: customField
                            width: parent.width - setButton.width - 8
                            placeholderText: "Meters (100 – 42195)"
                            font.pixelSize: 16
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 42195 }
                            onAccepted: setButton.clicked()
                        }
                        AppButton {
                            id: setButton
                            text: "Set"
                            height: 50
                            fontSize: 16
                            horizontalPadding: 18
                            enabled: customField.acceptableInput && Number(customField.text) >= 100
                            onClicked: {
                                var v = parseInt(customField.text)
                                if (!(v >= 100))
                                    return
                                if (page.uiMode === "ghost")
                                    page.ghostDistance = v
                                else
                                    page.soloDistance = v
                                page.localMode = page.uiMode
                                page.showCustom = false
                                page.applySetup()
                            }
                        }
                    }

                    // solo info
                    Text {
                        visible: page.uiMode === "solo"
                        width: parent.width
                        text: page.soloDistance > 0
                              ? "Row " + Theme.thousands(page.soloDistance) + " m at your own pace – the row ends at the finish line."
                              : "Open row – no distance. Finish whenever you like."
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }

                    // ghost: personal bests per distance (hidden after the finish:
                    // the summary card below shows the result and the record)
                    Column {
                        visible: page.uiMode === "ghost" && !page.finished
                        width: parent.width
                        spacing: 2
                        Row {
                            width: parent.width
                            Text { width: parent.width * 0.34; text: "Personal bests"; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                            Text { width: parent.width * 0.33; text: "Time"; color: Theme.textMuted; font.pixelSize: 13; horizontalAlignment: Text.AlignRight }
                            Text { width: parent.width * 0.33; text: "/500m"; color: Theme.textMuted; font.pixelSize: 13; horizontalAlignment: Text.AlignRight }
                        }
                        Repeater {
                            model: page.bests
                            Rectangle {
                                readonly property bool sel: modelData.distanceM === (page.target > 0 && Rowing.mode === "ghost" ? page.target : page.ghostDistance)
                                visible: modelData.standard || modelData.hasRecord
                                width: parent.width
                                height: visible ? 24 : 0
                                radius: 6
                                color: sel ? Theme.navSelected : "transparent"
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width
                                    Text {
                                        width: parent.width * 0.34
                                        leftPadding: 6
                                        text: Theme.thousands(modelData.distanceM) + " m"
                                        color: Theme.text
                                        font.pixelSize: 14
                                    }
                                    Text {
                                        width: parent.width * 0.33
                                        text: modelData.hasRecord ? page.timeTenths(modelData.timeSec) : "–"
                                        color: modelData.hasRecord ? Theme.text : Theme.textMuted
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    Text {
                                        width: parent.width * 0.33
                                        rightPadding: 6
                                        text: modelData.hasRecord ? page.timeTenths(modelData.pace500Sec) : "–"
                                        color: Theme.textSecondary
                                        font.pixelSize: 14
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }
                        }
                    }

                    // --- watt plan ------------------------------------------------
                    Text {
                        visible: page.uiMode === "plan"
                        text: "Rowing Workout"
                        color: Theme.text
                        font.pixelSize: 16
                    }
                    AppComboBox {
                        id: planCombo
                        visible: page.uiMode === "plan" && page.workouts.length > 0
                        enabled: !page.locked
                        width: parent.width
                        fontSize: 16
                        textRole: "name"
                        model: page.workouts
                        function indexOfId(id) {
                            for (var i = 0; i < page.workouts.length; ++i)
                                if (page.workouts[i].workoutId === id)
                                    return i
                            return -1
                        }
                        currentIndex: indexOfId(Rowing.mode === "plan" ? Rowing.workoutId : page.planWorkoutId)
                        onActivated: (index) => {
                            page.planWorkoutId = page.workouts[index].workoutId
                            page.localMode = "plan"
                            page.applySetup()
                            currentIndex = Qt.binding(function() {
                                return planCombo.indexOfId(Rowing.mode === "plan" ? Rowing.workoutId : page.planWorkoutId)
                            })
                        }
                    }
                    Text {
                        visible: page.uiMode === "plan"
                        width: parent.width
                        text: page.workouts.length === 0
                              ? "No rowing workouts yet. Create one in the Builder with sport “Rowing”."
                              : (Rowing.mode === "plan"
                                 ? Rowing.intervalCount + " intervals  •  " + Theme.clock(Rowing.totalDurationSec)
                                   + "\nTargets from rowing FTP " + Rowing.rowingFtp + " W"
                                 : "Targets are a % of your rowing FTP.")
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                    AppButton {
                        visible: page.uiMode === "plan"
                        width: parent.width
                        height: 44
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        bold: false
                        iconName: "plus"
                        text: "New Rowing Workout"
                        enabled: !page.locked
                        onClicked: page.openBuilder()
                    }
                }
            }

            // no rower connected
            Card {
                visible: !Rowing.rowerConnected
                Layout.fillWidth: true
                Layout.preferredHeight: 142
                Row {
                    x: 16; y: 16
                    spacing: 10
                    Icon { name: "bluetooth"; size: 22; color: Theme.warning; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: "No rower connected"
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    x: 16; y: 46
                    width: parent.width - 32
                    text: "Connect a Concept2 PM5 (or the simulated rower) to start rowing."
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
                id: summary
                visible: page.finished
                Layout.fillWidth: true
                Layout.preferredHeight: summaryCol.implicitHeight + 32
                Column {
                    id: summaryCol
                    x: 16; y: 16
                    width: parent.width - 32
                    spacing: 10
                    Text {
                        text: Rowing.completed ? "Row complete" : "Row finished"
                        color: Theme.text
                        font.pixelSize: 19
                        font.weight: Font.Bold
                    }
                    Rectangle {
                        visible: Rowing.newRecord
                        width: parent.width
                        height: 50
                        radius: 10
                        color: Qt.rgba(1, 0.82, 0.12, 0.18)
                        border.color: Theme.warning
                        Row {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                            Icon { name: "trophy"; size: 24; color: Theme.warningText; anchors.verticalCenter: parent.verticalCenter }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    text: "New personal best!"
                                    color: Theme.warningText
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                }
                                Text {
                                    text: Rowing.previousRecordSec > 0
                                          ? "Previous " + page.timeTenths(Rowing.previousRecordSec) + "  •  "
                                            + page.diffText(Rowing.previousRecordSec - Rowing.finishTimeSec) + " faster"
                                          : "First record over " + page.meters(page.target) + " m"
                                    color: Theme.text
                                    font.pixelSize: 13
                                }
                            }
                        }
                    }
                    Grid {
                        columns: 2
                        width: parent.width
                        rowSpacing: 8
                        Repeater {
                            model: ["Time", "Distance", "Avg power", "Avg pace", "Avg stroke rate", "Avg heart rate"]
                            Column {
                                width: summaryCol.width / 2
                                Text { text: modelData; color: Theme.textSecondary; font.pixelSize: 13 }
                                Text {
                                    text: {
                                        switch (index) {
                                        case 0: return page.timeTenths(Rowing.finishTimeSec > 0 ? Rowing.finishTimeSec : Rowing.elapsedSeconds)
                                        case 1: return page.meters(page.distance) + " m"
                                        case 2: return Rowing.avgWatts + " W"
                                        case 3: return page.paceText(Rowing.avgPace500) + " /500m"
                                        case 4: return Rowing.avgSpm > 0 ? Rowing.avgSpm + " SPM" : "–"
                                        default: return Rowing.avgHeartRate > 0 ? Rowing.avgHeartRate + " BPM" : "–"
                                        }
                                    }
                                    color: Theme.text
                                    font.pixelSize: 19
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }
                    AppButton {
                        visible: Rowing.lastRideId >= 0
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

            // controls
            Card {
                id: controls
                Layout.fillWidth: true
                Layout.preferredHeight: controlsCol.implicitHeight + 32
                Column {
                    id: controlsCol
                    x: 14; y: 16
                    width: parent.width - 28
                    spacing: 10

                    AppButton {
                        visible: !Rowing.active
                        width: parent.width
                        height: 50
                        kind: "primary"
                        iconName: "play"
                        objectName: "rowingStartButton"
                        // the only way a row starts (strokes never do)
                        text: page.finished ? "Row Again" : "Start"
                        enabled: Rowing.rowerConnected && (page.uiMode !== "plan" || Rowing.mode === "plan") && !page.otherSession
                        onClicked: page.startNow()
                    }

                    AppButton {
                        visible: Rowing.active
                        width: parent.width
                        height: 50
                        kind: Rowing.paused ? "success" : "danger"
                        iconName: Rowing.paused ? "play" : "pause"
                        text: Rowing.paused ? "Resume" : "Pause Row"
                        onClicked: Rowing.togglePause()
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
                            enabled: Rowing.active
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
                            // discards the row: only while paused or after the finish
                            enabled: Rowing.paused || page.finished
                            onClicked: Rowing.reset()
                        }
                    }
                    Text {
                        // (not after the finish: keeps the right column within
                        // the window with the summary shown)
                        width: parent.width
                        visible: text.length > 0
                        text: page.finished || page.otherSession ? ""
                              : Rowing.state === "ready" && Rowing.rowerConnected
                                ? "Ready – press Start (or Space). Strokes alone never start the row."
                              : Rowing.paused ? "Paused – press Resume (or Space)." : ""
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
