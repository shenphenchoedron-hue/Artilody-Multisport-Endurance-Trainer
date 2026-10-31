import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Artilody.Backend

// Treadmill Run: run on a Bluetooth FTMS treadmill through the 3D world of
// the GPX Ride. The treadmill is only READ (belt speed + incline) - the
// runner sets the belt, the app never controls it. All logic lives in the
// Running (RunSession) and RunLibrary singletons; this page shows them.
//   * Route: the standard 10 km loop (GPX off) or a GPX track (GPX on).
//   * Workouts: speed as % of the AnT speed or km/h + suggested incline.
//   * AnT test: built-in workout, evaluated at the end of its test block.
Item {
    id: page
    signal openDevices()
    signal requestFinish()
    signal message(string text)

    readonly property string st: Running.state
    readonly property bool locked: Running.active
    readonly property var blocker: Sessions.runBlocker
    readonly property bool otherSession: !!page.blocker && ("" + (page.blocker.kind || "")).length > 0
    readonly property bool canStart: Running.hasRoute && Running.treadmillConnected && !page.otherSession
    readonly property string startBlockedReason: Running.active || page.canStart ? ""
        : page.otherSession ? page.blocker.text
        : !Running.treadmillConnected ? "No treadmill connected" : ""
    readonly property bool workoutLive: Running.hasWorkout && !Running.workoutFinished
    readonly property real ant: Running.antSpeedKmh

    function toast(text) { page.message(text) }
    function kmText(m, digits) { return (m / 1000).toFixed(digits === undefined ? 2 : digits) }
    function gradeText(g) { return (g > 0.05 ? "+" : g < -0.05 ? "−" : "") + Math.abs(g).toFixed(1) + " %" }
    function gradeColor(g) { return g <= -2 ? "#3ddc5c" : g < 2 ? "#4da3ff" : g < 5 ? "#ffd21f" : g < 8 ? "#ff8a26" : "#f4513b" }
    // ±3 % of the target: green, ±6 %: yellow, else orange
    function complianceColor(actual, target) {
        if (actual <= 0 || target <= 0)
            return Theme.textSecondary
        var d = Math.abs(actual - target) / target
        return d <= 0.03 ? Theme.successText : d <= 0.06 ? Theme.warningText : Theme.orangeText
    }
    function selectWorkout(id) {
        if (!Running.attachWorkout(id))
            toast(Running.lastError.length > 0 ? Running.lastError : "Could not load the workout")
    }
    // Running workouts are made and edited in the Workout Builder (sport
    // "running") like every workout; they also show on the Workouts page.
    function newWorkout() {
        Builder.newWorkout("running")
        var w = ApplicationWindow.window
        if (w && typeof w.showPage === "function")
            w.showPage("builder")
    }
    function editWorkout(id) {
        var w = ApplicationWindow.window
        if (w && typeof w.editWorkout === "function")
            w.editWorkout(id)
    }
    function startAntTest() {
        if (Running.active) {
            toast("Finish the current run first")
            return
        }
        if (Running.setupAntTest())
            toast("AnT test loaded – warm up 10 min, then 30 min at an even, hard pace")
    }

    FileDialog {
        id: gpxFileDialog
        title: "Open a GPX route to run"
        nameFilters: ["GPX routes (*.gpx *.GPX)"]
        currentFolder: GpxRoutes.libraryFolderUrl
        onAccepted: {
            if (!Running.selectGpx(selectedFile))
                page.toast(Running.lastError.length > 0 ? Running.lastError : "Could not load the route")
        }
    }
    AntDialog {
        id: antDialog
        parent: ApplicationWindow.window ? ApplicationWindow.window.dialogHost : page
        onMessage: (text) => page.toast(text)
        onStartTest: page.startAntTest()
    }

    TapHandler { onTapped: page.forceActiveFocus() }

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

            // header: title + route choice
            Item {
                Layout.topMargin: 26
                Layout.fillWidth: true
                Layout.preferredHeight: header.implicitHeight
                Column {
                    id: header
                    width: parent.width - routeRow.width - 20
                    Text {
                        width: parent.width
                        text: "Treadmill Run"
                        color: Theme.text
                        font.pixelSize: 52
                        font.weight: Font.Black
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 30
                    }
                    Text {
                        width: parent.width
                        text: "Run in 3D – the road follows your treadmill's speed."
                        color: Theme.text
                        font.pixelSize: 21
                        elide: Text.ElideRight
                    }
                }
                Row {
                    id: routeRow
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    spacing: 10

                    // GPX on / off
                    Rectangle {
                        width: gpxRow.implicitWidth + 24
                        height: 58
                        radius: 10
                        color: Theme.cardStrong
                        border.color: Theme.fieldBorder
                        Row {
                            id: gpxRow
                            anchors.centerIn: parent
                            spacing: 10
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "GPX"
                                color: Theme.text
                                font.pixelSize: 17
                                font.weight: Font.Bold
                            }
                            ToggleSwitch {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: Running.gpxEnabled
                                enabled: !page.locked
                                onColor: Theme.runningColor
                                accessibleName: "GPX route"
                                onToggled: (on) => {
                                    if (on && !Running.hasGpx)
                                        gpxFileDialog.open()
                                    else
                                        Running.gpxEnabled = on
                                }
                            }
                        }
                    }
                    AppComboBox {
                        id: routeCombo
                        width: Math.max(240, Math.min(380, header.parent.width - 520 - 210))
                        height: 58
                        enabled: !page.locked && Running.gpxEnabled && GpxRoutes.count > 0
                        focusPolicy: Qt.NoFocus
                        Accessible.name: "GPX route"
                        model: GpxRoutes
                        textRole: "displayName"
                        currentIndex: GpxRoutes.indexOfPath(Running.gpxPath)
                        displayText: Running.routeName
                        onActivated: (index) => {
                            if (!Running.selectGpx(GpxRoutes.pathAt(index)))
                                page.toast(Running.lastError.length > 0 ? Running.lastError : "Could not load the route")
                            currentIndex = Qt.binding(function() { return GpxRoutes.indexOfPath(Running.gpxPath) })
                        }
                        contentItem: Row {
                            leftPadding: 14
                            spacing: 12
                            Icon {
                                name: Running.gpxEnabled ? "mapPin" : "flag"
                                size: 24
                                color: Running.gpxEnabled ? Theme.textSecondary : Theme.runningColor
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: routeCombo.width - 100
                                Text {
                                    width: parent.width
                                    text: Running.routeName
                                    color: Theme.text
                                    font.pixelSize: 17
                                    font.weight: Font.Bold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: (Running.routeLengthM / 1000).toFixed(1) + " km  ·  " + Math.round(Running.routeAscentM) + " m ↑  ·  "
                                          + (Running.routeIsLoop ? "loop" : "point to point")
                                          + (Running.gpxEnabled ? "" : "  ·  standard route")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.small(13)
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    IconButton {
                        width: 58
                        height: 58
                        iconName: "folder"
                        iconSize: 24
                        tooltip: "Open a GPX file"
                        enabled: !page.locked
                        onClicked: gpxFileDialog.open()
                    }
                }
            }

            // live metrics
            RowLayout {
                Layout.fillWidth: true
                // fixed height (the tiles' fillHeight would otherwise let the
                // row take space from the 3D view)
                Layout.preferredHeight: 108
                Layout.minimumHeight: 108
                Layout.maximumHeight: 108
                spacing: 12
                RunMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 170
                    Layout.fillHeight: true
                    label: "Speed"
                    iconName: "gauge"
                    iconColor: Theme.runningColor
                    valueColor: page.workoutLive && Running.active ? page.complianceColor(Running.speedKmh, Running.targetSpeedKmh) : Theme.text
                    value: Running.speedKmh.toFixed(1)
                    unit: "km/h"
                    footer: page.workoutLive && Running.targetSpeedKmh > 0 ? "Target " + Running.targetSpeedKmh.toFixed(1) + " km/h"
                            : Running.active ? "Avg " + Running.avgSpeedKmh.toFixed(1) + " km/h" : "Belt speed"
                }
                RunMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true
                    label: "Pace"
                    iconName: "timer"
                    iconColor: Theme.cyan
                    value: Running.pace.length > 0 ? Running.pace : "–"
                    unit: "/km"
                    footer: page.workoutLive && Running.targetPace.length > 0 ? "Target " + Running.targetPace + " /km"
                            : Running.active && Running.avgPace.length > 0 ? "Avg " + Running.avgPace + " /km"
                            : "Z" + (Running.speedZone + 1) + " · " + Math.round(Running.speedKmh / Math.max(1, page.ant) * 100) + " % AnT"
                }
                RunMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true
                    label: "Incline"
                    iconName: "mountain"
                    iconColor: page.gradeColor(Running.inclinePercent)
                    value: Running.inclineKnown ? Running.inclinePercent.toFixed(1) : "–"
                    unit: "%"
                    footer: page.workoutLive ? "Set " + Running.targetInclinePercent.toFixed(1) + " %"
                            : "Route " + page.gradeText(Running.routeGradePercent)
                }
                RunMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true
                    label: "Heart Rate"
                    iconName: "heart"
                    iconColor: Theme.heartRate
                    value: Running.heartRate > 0 ? Running.heartRate : "–"
                    unit: "BPM"
                    footer: Running.heartRate > 0 && RunLibrary.antHeartRate > 0
                            ? Math.round(Running.heartRate * 100 / RunLibrary.antHeartRate) + "% of AnT HR"
                            : Running.heartRate > 0 && Profiles.maxHr > 0 ? Math.round(Running.heartRate * 100 / Profiles.maxHr) + "% of max"
                            : "No sensor"
                }
                RunMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 170
                    Layout.fillHeight: true
                    label: "Time"
                    iconName: "clock"
                    value: Theme.clock(Running.elapsedSeconds)
                    footer: page.kmText(Running.runDistanceM) + " km  •  " + Math.round(Running.calories) + " kcal"
                }
            }

            // 3D view
            Rectangle {
                id: sceneCard
                readonly property var scene: sceneLoader.item
                readonly property bool showScene: Running.hasRoute && !!scene && scene.supported && scene.hasScene && scene.error.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 260
                Layout.preferredHeight: 2000
                radius: Theme.radius
                color: showScene ? "#04060d" : Theme.card
                border.color: Theme.cardBorder
                border.width: 1
                clip: true

                Loader {
                    id: sceneLoader
                    active: true
                    visible: sceneCard.showScene
                    anchors.fill: parent
                    anchors.margins: 1
                    sourceComponent: Route3DView { session: Running }
                }
                Text {
                    visible: !sceneCard.showScene
                    anchors.centerIn: parent
                    width: parent.width - 60
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: sceneCard.scene && sceneCard.scene.error.length > 0 ? sceneCard.scene.error : "Preparing the 3D view…"
                    color: Theme.textSecondary
                    font.pixelSize: 17
                }

                // status chip
                Row {
                    visible: sceneCard.showScene
                    x: 16
                    y: 16
                    spacing: 8
                    Rectangle {
                        id: statusChip
                        readonly property color dot: page.st === "running" ? Theme.danger
                                                   : page.st === "paused" ? Theme.warning
                                                   : page.st === "finished" ? Theme.accentLight : "#9aa3c0"
                        height: 34
                        width: statusRow.implicitWidth + 26
                        radius: height / 2
                        color: Qt.rgba(0, 0, 0, 0.55)
                        border.color: Qt.rgba(dot.r, dot.g, dot.b, 0.7)
                        Row {
                            id: statusRow
                            anchors.centerIn: parent
                            spacing: 9
                            Rectangle {
                                width: 9; height: 9; radius: 4.5
                                anchors.verticalCenter: parent.verticalCenter
                                color: statusChip.dot
                                SequentialAnimation on opacity {
                                    running: page.st === "running"
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.25; duration: 700 }
                                    NumberAnimation { to: 1; duration: 700 }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.st === "running" ? (Running.speedKmh < 0.5 ? "Belt stopped" : Running.speedKmh < 6.5 ? "Walking" : "Running")
                                                              + (Running.antTestBlockActive ? "  •  AnT test" : "")
                                      : page.st === "paused" ? "Paused"
                                      : page.st === "finished" ? (Running.completed ? (Running.hasWorkout ? "Workout complete" : "Route complete") : "Finished")
                                      : Running.treadmillConnected && !page.otherSession ? "Ready – press Start" : "Preview"
                                color: "white"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                    Rectangle {
                        visible: Running.routeIsLoop && (Running.active || Running.lap > 1)
                        height: 34
                        width: lapText.implicitWidth + 26
                        radius: 17
                        color: Qt.rgba(0, 0, 0, 0.55)
                        Text {
                            id: lapText
                            anchors.centerIn: parent
                            text: "Lap " + Running.lap + "  •  " + page.kmText(Running.distanceMeters, 1) + " / " + page.kmText(Running.routeLengthM, 1) + " km"
                            color: "white"
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                    }
                }

                // route grade: what the incline would be outside
                Rectangle {
                    visible: sceneCard.showScene
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    y: 16
                    width: Math.max(gradeCol.implicitWidth + 28, 120)
                    height: gradeCol.implicitHeight + 14
                    radius: 12
                    color: Qt.rgba(0, 0, 0, 0.58)
                    border.width: 2
                    border.color: page.gradeColor(Running.routeGradePercent)
                    Column {
                        id: gradeCol
                        anchors.centerIn: parent
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 6
                            Icon { name: "mountain"; size: 20; color: page.gradeColor(Running.routeGradePercent); anchors.verticalCenter: parent.verticalCenter }
                            Text { text: page.gradeText(Running.routeGradePercent); color: "white"; font.pixelSize: 28; font.weight: Font.Black }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "route  •  treadmill " + (Running.inclineKnown ? Running.inclinePercent.toFixed(1) + " %" : "–")
                            color: Qt.rgba(1, 1, 1, 0.75)
                            font.pixelSize: Theme.small(13)
                        }
                    }
                }

                // workout target overlay
                Rectangle {
                    visible: sceneCard.showScene && page.workoutLive
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    width: targetRow.implicitWidth + 36
                    height: 74
                    radius: 14
                    color: Qt.rgba(0, 0, 0, 0.62)
                    border.width: 2
                    border.color: Theme.zoneColor(Running.workoutSegments.length > Running.currentStepIndex && Running.currentStepIndex >= 0
                                                  ? Running.workoutSegments[Running.currentStepIndex].zone : 1)
                    Row {
                        id: targetRow
                        anchors.centerIn: parent
                        spacing: 22
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: Running.currentStepName.length > 0 ? Running.currentStepName : "Get ready"
                                color: Qt.rgba(1, 1, 1, 0.8)
                                font.pixelSize: 14
                            }
                            Text {
                                text: Theme.clock(Running.stepRemainingSec)
                                color: "white"
                                font.pixelSize: 30
                                font.weight: Font.Black
                            }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text { text: "Target"; color: Qt.rgba(1, 1, 1, 0.8); font.pixelSize: 14 }
                            Text {
                                text: Running.targetSpeedKmh.toFixed(1) + " km/h"
                                color: "white"
                                font.pixelSize: 30
                                font.weight: Font.Black
                            }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text { text: Running.targetPercent + " % AnT"; color: Qt.rgba(1, 1, 1, 0.8); font.pixelSize: 14 }
                            Text {
                                text: "incline " + Running.targetInclinePercent.toFixed(1) + " %"
                                color: "white"
                                font.pixelSize: 20
                                font.weight: Font.Bold
                            }
                        }
                        Column {
                            visible: Running.active
                            anchors.verticalCenter: parent.verticalCenter
                            Text { text: "Belt"; color: Qt.rgba(1, 1, 1, 0.8); font.pixelSize: 14 }
                            Text {
                                readonly property real d: Running.speedKmh - Running.targetSpeedKmh
                                text: (d >= 0 ? "+" : "−") + Math.abs(d).toFixed(1)
                                color: Math.abs(d) <= Running.targetSpeedKmh * 0.03 ? "#3ddc5c" : Math.abs(d) <= Running.targetSpeedKmh * 0.06 ? "#ffd21f" : "#ff8a26"
                                font.pixelSize: 30
                                font.weight: Font.Black
                            }
                        }
                    }
                }

                // paused / finished veil
                Rectangle {
                    visible: sceneCard.showScene && (page.st === "paused" || page.st === "finished")
                    anchors.centerIn: parent
                    width: veilCol.implicitWidth + 48
                    height: veilCol.implicitHeight + 32
                    radius: 16
                    color: Qt.rgba(0, 0, 0, 0.6)
                    Column {
                        id: veilCol
                        anchors.centerIn: parent
                        spacing: 4
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: page.st === "paused" ? "Paused" : "Run finished"
                            color: "white"
                            font.pixelSize: 34
                            font.weight: Font.Black
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: page.kmText(Running.runDistanceM) + " km  •  " + Theme.clock(Running.elapsedSeconds) + "  •  "
                                  + (Running.avgPace.length > 0 ? Running.avgPace + " /km" : "–")
                                  + (Running.avgHeartRate > 0 ? "  •  " + Running.avgHeartRate + " bpm" : "")
                            color: Qt.rgba(1, 1, 1, 0.85)
                            font.pixelSize: 17
                        }
                    }
                }
            }

            // route profile + workout
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: Running.hasWorkout ? 176 : 116
                Layout.maximumHeight: Running.hasWorkout ? 176 : 116
                RouteProfile {
                    x: 16
                    y: 12
                    width: parent.width - 32
                    height: 80
                    profile: Running.profile
                    total: Running.routeLengthM
                    minEle: Running.minElevationM
                    maxEle: Running.maxElevationM
                    position: Running.distanceMeters
                    startDistance: 0
                    elevation: Running.elevationM
                    informations: []
                    currentInformation: -1
                }
                RunWorkoutStrip {
                    visible: Running.hasWorkout
                    x: 16
                    y: 102
                    width: parent.width - 32
                    height: 60
                    segments: Running.workoutSegments
                    totalSec: Running.workoutTotalSec
                    elapsed: Running.active || page.st === "finished" ? Running.elapsedSeconds : -1
                    currentIndex: Running.currentStepIndex
                }
            }
        }

        // ================= right column =================
        ColumnLayout {
            Layout.preferredWidth: 360
            Layout.maximumWidth: 360
            Layout.fillHeight: true
            Layout.topMargin: 64
            spacing: Theme.spacing

            // controls
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: controlsCol.implicitHeight + 32
                Column {
                    id: controlsCol
                    x: 16
                    y: 16
                    width: parent.width - 32
                    spacing: 10
                    Row {
                        width: parent.width
                        spacing: 10
                        Icon {
                            name: "treadmill"
                            size: 30
                            color: Running.treadmillConnected ? Theme.runningColor : Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 40 - devButton.width
                            Text {
                                width: parent.width
                                text: Running.treadmillConnected ? Running.treadmillName : "No treadmill connected"
                                color: Theme.text
                                font.pixelSize: 17
                                font.weight: Font.Bold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: Running.treadmillConnected ? "Read only – set speed and incline on the treadmill"
                                                                 : "Connect an FTMS treadmill on the Devices page"
                                color: Theme.textSecondary
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                        AppButton {
                            id: devButton
                            visible: !Running.treadmillConnected
                            anchors.verticalCenter: parent.verticalCenter
                            height: 40
                            fontSize: 14
                            horizontalPadding: 12
                            kind: "secondary"
                            text: "Devices"
                            onClicked: page.openDevices()
                        }
                    }
                    SessionBlockNotice {
                        width: parent.width
                        blocker: page.blocker
                        startWhat: "a run"
                        namePrefix: "run"
                    }
                    Row {
                        width: parent.width
                        spacing: 10
                        AppButton {
                            width: Running.active ? (parent.width - 10) / 2 : parent.width
                            height: 56
                            enabled: Running.active || page.canStart
                            iconName: Running.running ? "pause" : "play"
                            text: Running.running ? "Pause" : Running.paused ? "Resume" : page.st === "finished" ? "Run Again" : "Start Run"
                            onClicked: page.st === "finished" ? Running.start() : Running.togglePause()
                        }
                        AppButton {
                            visible: Running.active
                            width: (parent.width - 10) / 2
                            height: 56
                            kind: "danger"
                            iconName: "stop"
                            text: "Finish"
                            onClicked: page.requestFinish()
                        }
                    }
                    Text {
                        visible: page.startBlockedReason.length > 0 && !page.otherSession
                        width: parent.width
                        text: page.startBlockedReason
                        color: Theme.warningText
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // AnT
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                title: "Anaerobic Threshold"
                titleSize: 17
                headerRight: [
                    Text {
                        text: RunLibrary.antTested ? "" : "default"
                        color: Theme.textMuted
                        font.pixelSize: 13
                    }
                ]
                Column {
                    x: 16
                    y: 46
                    width: parent.width - 32
                    spacing: 6
                    Row {
                        spacing: 10
                        Text {
                            text: RunLibrary.antSpeedKmh.toFixed(1) + " km/h"
                            color: Theme.text
                            font.pixelSize: 26
                            font.weight: Font.Black
                        }
                        Text {
                            anchors.baseline: parent.children[0].baseline
                            text: RunLibrary.antPace + " /km" + (RunLibrary.antHeartRate > 0 ? "  •  " + RunLibrary.antHeartRate + " bpm" : "")
                            color: Theme.textSecondary
                            font.pixelSize: 15
                        }
                    }
                    Text {
                        width: parent.width
                        text: RunLibrary.testStatusText
                        color: RunLibrary.testDue ? Theme.warningText : Theme.textSecondary
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Row {
                        spacing: 8
                        AppButton {
                            height: 36
                            fontSize: 14
                            iconSize: 16
                            iconSpacing: 7
                            horizontalPadding: 12
                            kind: RunLibrary.testDue ? "primary" : "secondary"
                            iconName: "flag"
                            text: "Run AnT Test"
                            enabled: !Running.active
                            onClicked: page.startAntTest()
                        }
                        AppButton {
                            height: 36
                            fontSize: 14
                            iconSize: 16
                            iconSpacing: 7
                            horizontalPadding: 12
                            kind: "secondary"
                            iconName: "chart"
                            text: "Zones & History"
                            onClicked: antDialog.openDialog()
                        }
                    }
                }
            }

            // workouts
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 200
                title: "Running Workouts"
                titleSize: 17
                headerRight: [
                    IconButton {
                        width: 34; height: 34
                        flat: true
                        iconName: "plus"
                        iconSize: 18
                        tooltip: "New running workout (Workout Builder)"
                        onClicked: page.newWorkout()
                    }
                ]
                ListView {
                    id: workoutList
                    x: 10
                    y: 50
                    width: parent.width - 20
                    height: parent.height - 60
                    clip: true
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    model: [{ workoutId: -1, name: "Free run", summary: "No workout – run as you like", totalSec: 0, builtIn: true, antTest: false, segments: [] }]
                           .concat(RunLibrary.workouts)
                    delegate: Rectangle {
                        readonly property bool selected: modelData.workoutId === Running.workoutId
                        width: workoutList.width - 8
                        height: 64
                        radius: 10
                        color: selected ? Theme.navSelected : wMouse.containsMouse ? Theme.hover : "transparent"
                        border.width: selected ? 1.5 : 0
                        border.color: Theme.runningColor
                        MouseArea {
                            id: wMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: page.locked ? Qt.ArrowCursor : Qt.PointingHandCursor
                            onClicked: if (!page.locked) page.selectWorkout(modelData.workoutId)
                        }
                        Icon {
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            name: modelData.antTest ? "flag" : modelData.workoutId < 0 ? "run" : "document"
                            size: 22
                            color: modelData.antTest ? Theme.runningColor : Theme.textSecondary
                        }
                        Column {
                            x: 42
                            y: 8
                            width: parent.width - 42 - (modelData.workoutId >= 0 ? 76 : 8)
                            spacing: 2
                            Text {
                                width: parent.width
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: (modelData.totalSec > 0 ? Theme.clock(modelData.totalSec) + "  ·  " : "") + modelData.summary
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            RunWorkoutStrip {
                                visible: modelData.segments.length > 0
                                width: parent.width
                                height: 12
                                segments: modelData.segments
                                totalSec: modelData.totalSec
                            }
                        }
                        Row {
                            visible: modelData.workoutId >= 0
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            IconButton {
                                width: 34; height: 34
                                flat: true
                                iconName: "edit"
                                iconSize: 16
                                tooltip: modelData.builtIn ? "Edit a copy in the Workout Builder" : "Edit in the Workout Builder"
                                onClicked: page.editWorkout(modelData.workoutId)
                            }
                            IconButton {
                                width: 34; height: 34
                                flat: true
                                iconName: modelData.builtIn ? "copy" : "trash"
                                iconSize: 16
                                iconColor: modelData.builtIn ? Theme.text : Theme.dangerText
                                tooltip: modelData.builtIn ? "Duplicate" : "Delete"
                                enabled: !(selected && page.locked)
                                onClicked: {
                                    if (modelData.builtIn) {
                                        if (Workouts.duplicate(modelData.workoutId) >= 0)
                                            page.toast("Copy of \"" + modelData.name + "\" created")
                                    } else if (Workouts.remove(modelData.workoutId)) {
                                        page.toast("\"" + modelData.name + "\" deleted")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // chart
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 190
                title: "Speed & Heart Rate"
                titleSize: 17
                headerRight: [
                    Text {
                        visible: Running.hasWorkout && Running.active
                        text: "On target " + Running.compliance + " %"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                ]
                RunChart {
                    x: 10
                    y: 48
                    width: parent.width - 20
                    height: parent.height - 58
                    speed: Running.speedSeries
                    target: Running.targetSeries
                    heartRate: Running.heartRateSeries
                    step: Running.seriesStep
                    antSpeed: page.ant
                }
                Text {
                    visible: Running.speedSeries.length < 2
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 14
                    text: "Start the run to see your speed"
                    color: Theme.textMuted
                    font.pixelSize: 14
                }
            }
        }
    }
}
