import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import Artilody.Backend

// Real Life Video ride: ride a filmed route; the video follows your position
// on its GPS track. All logic lives in the Rlv (session) and Videos (library)
// singletons; this page only shows the video and keeps it in sync:
//   * source = Rlv.videoUrl
//   * player.position is set ONLY on Rlv.seekRequested(ms) (never bound)
//   * player.playbackRate is bound to Rlv.playbackRate
//   * play() while Rlv.videoShouldPlay, else pause()
//   * Rlv.reportVideoPosition(player.position) every 500 ms while playing
// Before the start (route chosen / start point moved) the frame at the start
// point is shown as a poster (play() + pause(), some backends render no frame
// for a seek in the stopped state).
Item {
    id: page
    signal openDevices()
    signal requestFinish()

    readonly property string st: Rlv.state
    readonly property bool finished: st === "finished"
    readonly property bool locked: Rlv.active || Rlv.waiting
    // only one session at a time (Rlv refuses to start otherwise)
    readonly property bool otherSession: (Training.armedOrActive && !Training.waiting) || Rowing.active || FreeRide.active
    // resistance mode: "sim" (the route as a simulated road, default) or
    // "erg" (fixed watts). gears: in "sim" the virtual gear sets the
    // resistance (virtual gears on in Devices / Zwift Click, or the fallback
    // on a trainer without gradient simulation); otherwise the rider shifts
    // the real bike and the trainer gets the grade.
    readonly property bool gears: Rlv.virtualGears
    readonly property bool simMode: Rlv.mode === "sim"
    readonly property bool ergMode: Rlv.mode === "erg"
    function shiftingText() {
        if (!Rlv.virtualGears)
            return "your bike's gears"
        if (Rlv.simulationFallback)
            return "Virtual gears"
        return Devices.controllerConnected ? "Virtual gears (Zwift Click)" : "Virtual gears (on-screen)"
    }
    readonly property bool canStart: Rlv.hasRoute && Rlv.trainerConnected && !page.otherSession
    property bool audioOn: false
    property bool plusDown: false
    property bool minusDown: false

    function kmText(m, digits) { return (m / 1000).toFixed(digits === undefined ? 2 : digits) }
    function gradeText(g) {
        return (g > 0.05 ? "+" : g < -0.05 ? "−" : "") + Math.abs(g).toFixed(1) + " %"
    }
    function gradeColor(g) {
        return g <= -2 ? "#3ddc5c" : g < 2 ? "#4da3ff" : g < 5 ? "#ffd21f" : g < 8 ? "#ff8a26" : "#f4513b"
    }
    function routeDetails(distanceM, ascentM, sec, country) {
        var parts = [(distanceM / 1000).toFixed(1) + " km", Math.round(ascentM) + " m ↑"]
        if (sec > 0)
            parts.push(Theme.clock(sec))
        if (country && country.length > 0)
            parts.push(country)
        return parts.join("  ·  ")
    }
    function goTo(name) {
        var w = ApplicationWindow.window
        if (w && typeof w.showPage === "function")
            w.showPage(name)
    }
    function toast(text) {
        var w = ApplicationWindow.window
        if (w && typeof w.showToast === "function")
            w.showToast(text)
    }
    function openLibraryFolder() {
        var f = Videos.ensureLibraryFolder()
        if (f.length > 0)
            Qt.openUrlExternally("file://" + (f.charAt(0) === "/" ? "" : "/") + f)
    }

    // ------------------------------------------------------------------ video
    // (a function, not only a binding: inside onMediaStatusChanged a binding
    // on mediaStatus may not have been re-evaluated yet)
    function isMediaReady() {
        var st = player.mediaStatus
        return st === MediaPlayer.LoadedMedia || st === MediaPlayer.BufferingMedia
               || st === MediaPlayer.BufferedMedia || st === MediaPlayer.EndOfMedia
    }
    readonly property bool mediaReady: player.mediaStatus === MediaPlayer.LoadedMedia
                                       || player.mediaStatus === MediaPlayer.BufferingMedia
                                       || player.mediaStatus === MediaPlayer.BufferedMedia
                                       || player.mediaStatus === MediaPlayer.EndOfMedia
    readonly property bool videoError: Rlv.videoAvailable && player.error !== MediaPlayer.NoError
    readonly property bool wantPlay: Rlv.videoShouldPlay
    // A backend can open a file but fail to decode its video (e.g. AV1 on a
    // computer without AV1 hardware decoding): no frame arrives at all.
    property bool gotFrame: false
    property bool noPicture: false
    readonly property string videoCodec: {
        try {
            return mediaReady ? player.metaData.stringValue(MediaMetaData.VideoCodec) : ""
        } catch (e) {
            return ""
        }
    }
    property real pendingSeekMs: -1
    property bool posterPending: false
    property real posterMs: 0

    function seekVideo(ms) {
        if (!Rlv.videoAvailable)
            return
        if (!isMediaReady()) {
            pendingSeekMs = ms      // applied once the media is loaded
            return
        }
        pendingSeekMs = -1
        player.position = ms
        if (!Rlv.videoShouldPlay)
            showPoster(ms)
    }
    function showPoster(ms) {
        posterMs = ms
        if (player.playbackState === MediaPlayer.PausedState && !posterPending)
            return                  // a paused player renders the frame of a seek
        posterPending = true
        player.play()
        posterTimer.restart()
    }
    function syncPlay() {
        if (!Rlv.videoAvailable || !isMediaReady())
            return
        if (Rlv.videoShouldPlay) {
            posterPending = false
            posterTimer.stop()
            if (player.playbackState !== MediaPlayer.PlayingState)
                player.play()
        } else if (!posterPending && player.playbackState === MediaPlayer.PlayingState) {
            player.pause()
        }
    }
    function handleMediaStatus() {
        if (!isMediaReady())
            return
        if (!gotFrame && !frameWatchdog.running && !noPicture)
            frameWatchdog.restart()
        if (pendingSeekMs >= 0) {
            var ms = pendingSeekMs
            pendingSeekMs = -1
            player.position = ms
            if (!Rlv.videoShouldPlay)
                showPoster(ms)
        }
        syncPlay()
    }
    onWantPlayChanged: syncPlay()

    // How to convert a video the computer cannot play (AV1 …) to H.264:
    // HandBrake steps, the ffmpeg alternative and quick action buttons.
    // Used on the video ("No picture", light on dark) and on the card
    // ("This video cannot be played", theme colours).
    component ConvertHelp: Column {
        id: help
        property color textColor: Theme.text
        property color mutedColor: Theme.textSecondary
        property color linkColor: Theme.accentText
        property color panelColor: Theme.field
        property color panelBorder: Theme.fieldBorder
        property bool onVideo: false
        property bool compact: false
        readonly property real stepSize: compact ? 13 : 14
        readonly property string ffmpegLine: "ffmpeg -i video.mp4 -c:v libx264 -crf 21 -an video-h264.mp4"
        readonly property var steps: [
            "Open the video in HandBrake and choose the preset <b>General → Fast 720p30</b> (or HQ 720p30 Surround).",
            "<b>Video</b> tab: Video Encoder <b>H.264 (x264)</b>, Framerate <b>Same as source</b> and "
                + "<b>Constant Framerate</b> – keeps the video in sync with the GPS route.",
            "Save it as <b>.mp4</b> in the route folder with the <b>same file name</b> as the original, "
                + "replacing it (keep the original somewhere else).",
            "Press <b>Rescan Library</b>."
        ]
        function plainSteps() {
            var t = "Convert the video to H.264 with HandBrake (free: https://handbrake.fr)\n"
            for (var i = 0; i < steps.length; ++i)
                t += (i + 1) + ". " + steps[i].replace(/<[^>]+>/g, "") + "\n"
            return t + "Or with ffmpeg (then give the new file the original name):\n" + ffmpegLine + "\n"
        }
        function toast(text) {
            var w = ApplicationWindow.window
            if (w && typeof w.showToast === "function")
                w.showToast(text)
        }
        spacing: compact ? 8 : 10

        Rectangle {
            width: help.width
            height: stepsCol.implicitHeight + (help.compact ? 18 : 24)
            radius: 10
            color: help.panelColor
            border.color: help.panelBorder
            Column {
                id: stepsCol
                x: help.compact ? 12 : 16
                y: help.compact ? 9 : 12
                width: parent.width - 2 * x
                spacing: help.compact ? 4 : 6
                Text {
                    width: parent.width
                    text: "<b>Convert it with HandBrake</b> – free: <a href=\"https://handbrake.fr\">handbrake.fr</a>"
                    textFormat: Text.StyledText
                    color: help.textColor
                    linkColor: help.linkColor
                    font.pixelSize: help.stepSize + 1
                    wrapMode: Text.WordWrap
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    HoverHandler {
                        enabled: parent.hoveredLink.length > 0
                        cursorShape: Qt.PointingHandCursor
                    }
                }
                Repeater {
                    model: help.steps
                    Row {
                        width: stepsCol.width
                        spacing: 8
                        Text {
                            id: stepNo
                            text: (index + 1) + "."
                            color: help.linkColor
                            font.pixelSize: help.stepSize
                            font.weight: Font.Bold
                        }
                        Text {
                            width: parent.width - stepNo.width - parent.spacing
                            text: modelData
                            textFormat: Text.StyledText
                            color: help.mutedColor
                            font.pixelSize: help.stepSize
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Text {
                    width: parent.width
                    topPadding: 2
                    text: "Or with ffmpeg (then give the new file the original name):"
                    color: help.mutedColor
                    font.pixelSize: help.stepSize - 1
                    wrapMode: Text.WordWrap
                }
                TextEdit {
                    width: parent.width
                    text: help.ffmpegLine
                    readOnly: true
                    selectByMouse: true
                    color: help.textColor
                    selectionColor: Theme.accent
                    selectedTextColor: "white"
                    font.family: Qt.platform.os === "osx" ? "Menlo" : Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono"
                    font.pixelSize: help.stepSize - 1
                    wrapMode: TextEdit.WrapAnywhere
                }
            }
        }
        Flow {
            width: help.width
            spacing: 8
            AppButton {
                height: help.compact ? 34 : 38
                kind: help.onVideo ? "primary" : "secondary"
                fontSize: 14
                iconSize: 16
                iconSpacing: 8
                horizontalPadding: 14
                bold: false
                iconName: "link"
                text: "HandBrake Website"
                onClicked: Qt.openUrlExternally("https://handbrake.fr")
            }
            AppButton {
                height: help.compact ? 34 : 38
                kind: "secondary"
                fontSize: 14
                iconSize: 16
                iconSpacing: 8
                horizontalPadding: 14
                bold: false
                iconName: "copy"
                text: "Copy Steps"
                onClicked: {
                    Devices.copyToClipboard(help.plainSteps())
                    help.toast("Conversion steps copied to the clipboard")
                }
            }
            AppButton {
                height: help.compact ? 34 : 38
                kind: "secondary"
                fontSize: 14
                iconSize: 16
                iconSpacing: 8
                horizontalPadding: 14
                bold: false
                iconName: "refresh"
                text: "Rescan Library"
                onClicked: {
                    Videos.rescan()
                    help.toast(Videos.count + (Videos.count === 1 ? " video" : " videos") + " in the library")
                }
            }
        }
    }

    MediaPlayer {
        id: player
        source: Rlv.videoUrl
        videoOutput: videoOut
        // RLVs are usually silent: no audio output unless switched on
        audioOutput: page.audioOn ? audioOut : null
        playbackRate: Rlv.playbackRate
        onMediaStatusChanged: page.handleMediaStatus()
        onSourceChanged: {
            page.posterPending = false
            posterTimer.stop()
            page.gotFrame = false
            page.noPicture = false
            frameWatchdog.stop()
        }
    }
    AudioOutput {
        id: audioOut
        volume: 0.7
    }
    Connections {
        target: videoOut.videoSink
        function onVideoFrameChanged() {
            if (!page.gotFrame) {
                page.gotFrame = true
                page.noPicture = false
                frameWatchdog.stop()
            }
        }
    }
    Timer {
        id: frameWatchdog
        interval: 6000
        onTriggered: page.noPicture = !page.gotFrame && page.isMediaReady() && Rlv.videoAvailable
    }
    Timer {
        id: posterTimer
        interval: 350
        onTriggered: {
            if (!page.posterPending)
                return
            page.posterPending = false
            if (!Rlv.videoShouldPlay) {
                player.pause()
                player.position = page.posterMs
            }
        }
    }
    Timer {
        interval: 500
        repeat: true
        running: player.playbackState === MediaPlayer.PlayingState && !page.posterPending && Rlv.active
        onTriggered: Rlv.reportVideoPosition(player.position)
    }
    Connections {
        target: Rlv
        function onSeekRequested(positionMs) { page.seekVideo(positionMs) }
        function onInformationChanged() {
            if (Rlv.currentInformation.length > 0) {
                infoBox.text = Rlv.currentInformation
                infoBox.shown = true
                infoTimer.restart()
            } else {
                infoBox.shown = false
            }
        }
        function onShifted(direction) {   // ERG steps (Zwift Click ±10 W)
            var b = page.ergMode ? (direction > 0 ? resistanceCard.ergPlus10 : resistanceCard.ergMinus10) : null
            if (b)
                b.pulse()
        }
    }
    Connections {
        target: Gears
        function onShifted(direction) {
            if (page.gears)
                (direction > 0 ? harderButton : easierButton).pulse()
        }
    }
    Connections {
        target: Devices
        function onControllerButtonsChanged(plus, minus) {
            page.plusDown = plus
            page.minusDown = minus
        }
    }
    Component.onCompleted: {
        // a route restored at startup was sought before this page existed
        if (Rlv.videoAvailable)
            pendingSeekMs = Rlv.videoPosition
        handleMediaStatus()
    }

    FolderDialog {
        id: addFolderDialog
        title: "Add a folder with Real Life Videos"
        onAccepted: {
            if (Videos.addFolder(selectedFolder))
                page.toast(Videos.count > 0 ? Videos.count + (Videos.count === 1 ? " video" : " videos") + " in the library"
                                            : "No videos found in that folder")
            else
                page.toast(Videos.lastError.length > 0 ? Videos.lastError : "Could not add the folder")
        }
    }
    FolderDialog {
        id: libraryFolderDialog
        title: "Choose the videos folder"
        onAccepted: Videos.libraryFolder = selectedFolder
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

            // header: title + route drop-down
            Item {
                Layout.topMargin: 26
                Layout.fillWidth: true
                Layout.preferredHeight: header.implicitHeight
                Column {
                    id: header
                    width: parent.width - routeRow.width - 20
                    spacing: 0
                    Text {
                        text: "Video Ride"
                        color: Theme.text
                        font.pixelSize: 52
                        font.weight: Font.Black
                    }
                    Text {
                        width: parent.width
                        text: "Ride a filmed route – the video follows your speed."
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

                    AppComboBox {
                        id: routeCombo
                        width: 470
                        height: 58
                        enabled: !page.locked && Videos.count > 0
                        model: Videos
                        textRole: "displayName"
                        currentIndex: Rlv.routeIndex
                        displayText: Videos.count === 0 ? "No videos in the library"
                                     : Rlv.hasRoute ? (Rlv.routeTitle.length > 0 ? Rlv.routeTitle : Rlv.routeName) : "Choose a video…"
                        onActivated: (index) => {
                            if (!Rlv.select(index))
                                page.toast(Rlv.lastError.length > 0 ? Rlv.lastError : "Could not load the video")
                            currentIndex = Qt.binding(function() { return Rlv.routeIndex })
                        }
                        contentItem: Row {
                            leftPadding: 14
                            spacing: 12
                            Icon {
                                name: "film"
                                size: 24
                                color: Theme.textSecondary
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: routeCombo.width - 100
                                Text {
                                    width: parent.width
                                    text: routeCombo.displayText
                                    color: Theme.text
                                    font.pixelSize: 17
                                    font.weight: Rlv.hasRoute ? Font.Bold : Font.Normal
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: Rlv.hasRoute
                                    width: parent.width
                                    text: page.routeDetails(Rlv.totalDistanceM, Rlv.totalAscentM, Rlv.routeDurationSec, Rlv.routeCountry)
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        delegate: ItemDelegate {
                            width: routeCombo.width - 12
                            height: 58
                            leftPadding: 12
                            rightPadding: 12
                            highlighted: routeCombo.highlightedIndex === index
                            enabled: model.valid
                            contentItem: Column {
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: model.displayName
                                    color: Theme.text
                                    font.pixelSize: Theme.menuFontSize
                                    font.weight: Font.Bold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: model.valid ? page.routeDetails(model.distanceM, model.ascentM, model.durationSec, model.country)
                                                      : (model.error.length > 0 ? model.error : "Cannot be loaded")
                                    color: model.valid ? Theme.textSecondary : Theme.danger
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                }
                            }
                            background: Rectangle {
                                color: parent.highlighted ? Qt.rgba(0.13, 0.47, 1.0, Theme.dark ? 0.30 : 0.16)
                                       : routeCombo.currentIndex === index ? Theme.navSelected : "transparent"
                                radius: 8
                            }
                        }
                    }
                    IconButton {
                        width: 58
                        height: 58
                        iconName: "folder"
                        iconSize: 24
                        tooltip: "Video library"
                        onClicked: libraryMenu.open()

                        AppMenu {
                            id: libraryMenu
                            y: parent.height + 4
                            AppMenuItem {
                                text: "Add Folder…"
                                iconName: "plus"
                                onTriggered: addFolderDialog.open()
                            }
                            AppMenuItem {
                                text: "Choose Videos Folder…"
                                iconName: "folder"
                                onTriggered: libraryFolderDialog.open()
                            }
                            AppMenuItem {
                                text: "Open Videos Folder"
                                iconName: "export"
                                onTriggered: page.openLibraryFolder()
                            }
                            AppMenuItem {
                                text: "Rescan Library"
                                iconName: "refresh"
                                onTriggered: {
                                    Videos.rescan()
                                    page.toast(Videos.count + (Videos.count === 1 ? " video" : " videos") + " in the library")
                                }
                            }
                        }
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
                    value: Rlv.displayWatts
                    unit: "W"
                    footer: page.ergMode ? "Target: " + Rlv.targetWatts + " W"
                            : page.gears && Rlv.gearTargetWatts > 0 && Rlv.active ? "Target: " + Rlv.gearTargetWatts + " W"
                            : "Avg: " + Rlv.avgWatts + " W"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true
                    label: "Cadence"
                    iconName: "cadence"
                    iconColor: Theme.cyan
                    value: Rlv.cadence
                    unit: "RPM"
                    footer: Rlv.active && Rlv.coasting ? "Coasting"
                            : page.gears ? "Gear " + Gears.gearLabel
                            : page.ergMode ? "ERG mode" : "Shift on your bike"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    Layout.fillHeight: true
                    label: "Heart Rate"
                    iconName: "heart"
                    iconColor: Theme.heartRate
                    value: Rlv.heartRate > 0 ? Rlv.heartRate : "–"
                    unit: "BPM"
                    footer: Rlv.heartRate > 0 && Profiles.maxHr > 0
                            ? Math.round(Rlv.heartRate * 100 / Profiles.maxHr) + "% of max" : "No sensor"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 170
                    Layout.fillHeight: true
                    label: "Speed"
                    iconName: "gauge"
                    iconColor: Theme.speed
                    value: Rlv.speedKmh.toFixed(1)
                    unit: "km/h"
                    footer: "Avg: " + Rlv.avgSpeedKmh.toFixed(1) + " km/h"
                }
                RowingMetric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    Layout.fillHeight: true
                    label: "Time"
                    iconName: "timer"
                    value: Theme.clock(Rlv.elapsedSeconds)
                    footer: page.kmText(Rlv.riddenMeters) + " km  •  " + Math.round(Rlv.kilojoules) + " kJ"
                }
            }

            // video window
            Rectangle {
                id: videoCard
                readonly property bool showVideo: Rlv.hasRoute && Rlv.videoAvailable && !page.videoError
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 200
                radius: Theme.radius
                color: showVideo ? "#04060d" : Theme.card
                border.color: Theme.cardBorder
                border.width: 1
                clip: true

                VideoOutput {
                    id: videoOut
                    visible: videoCard.showVideo
                    anchors.fill: parent
                    anchors.margins: 1
                    fillMode: VideoOutput.PreserveAspectFit
                }

                // ---- overlays on the video (light on dark in both themes) ----
                Row {
                    visible: videoCard.showVideo
                    x: 16
                    y: 16
                    spacing: 8
                    Rectangle {
                        id: statusChip
                        readonly property color dot: page.st === "ready" ? Theme.success
                                                   : page.st === "running" ? Theme.danger
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
                                    running: page.st === "ready" || page.st === "running"
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.25; duration: 700 }
                                    NumberAnimation { to: 1; duration: 700 }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.st === "ready" ? "Waiting for pedaling…"
                                      : page.st === "running" ? (Rlv.coasting ? "Coasting" : "Riding") + "  •  " + (page.gears ? "Virtual gears" : page.ergMode ? "ERG " + Rlv.targetWatts + " W" : "Simulation")
                                      : page.st === "paused" ? "Paused"
                                      : page.st === "finished" ? (Rlv.completed ? "Route complete" : "Finished")
                                      : "Preview"
                                color: "white"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                // grade badge
                Rectangle {
                    visible: videoCard.showVideo
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    y: 16
                    width: Math.max(gradeCol.implicitWidth + 28, 110)
                    height: gradeCol.implicitHeight + 14
                    radius: 12
                    color: Qt.rgba(0, 0, 0, 0.58)
                    border.width: 2
                    border.color: page.gradeColor(Rlv.effectiveGradePercent)
                    Column {
                        id: gradeCol
                        anchors.centerIn: parent
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 6
                            Icon {
                                name: "mountain"
                                size: 20
                                color: page.gradeColor(Rlv.effectiveGradePercent)
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                // the grade you ride (route × difficulty)
                                text: page.gradeText(Rlv.effectiveGradePercent)
                                color: "white"
                                font.pixelSize: 28
                                font.weight: Font.Black
                            }
                        }
                        Text {
                            visible: Math.abs(Rlv.effectiveGradePercent - Rlv.gradePercent) >= 0.05
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "route " + page.gradeText(Rlv.gradePercent)
                            color: Qt.rgba(1, 1, 1, 0.75)
                            font.pixelSize: 13
                        }
                    }
                }

                // information text (lower third)
                Rectangle {
                    id: infoBox
                    property string text
                    property bool shown: false
                    visible: videoCard.showVideo && opacity > 0.01
                    // the "No picture" help would be covered by the text
                    opacity: shown && text.length > 0 && !page.noPicture ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 600 } }
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: chipRow.top
                    anchors.bottomMargin: 14
                    width: Math.min(parent.width - 80, infoText.implicitWidth + 44)
                    height: infoText.implicitHeight + 20
                    radius: 10
                    color: Qt.rgba(0, 0, 0, 0.62)
                    Rectangle {
                        width: 4
                        height: parent.height - 16
                        anchors.verticalCenter: parent.verticalCenter
                        x: 10
                        radius: 2
                        color: Theme.accentLight
                    }
                    Text {
                        id: infoText
                        x: 24
                        width: parent.width - 36
                        anchors.verticalCenter: parent.verticalCenter
                        text: infoBox.text
                        color: "white"
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    Timer {
                        id: infoTimer
                        interval: 12000
                        onTriggered: infoBox.shown = false
                    }
                }

                // bottom chips: gear, difficulty, playback rate
                Row {
                    id: chipRow
                    visible: videoCard.showVideo
                    x: 16
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    spacing: 8
                    Repeater {
                        model: [
                            { icon: page.gears ? "cog" : page.ergMode ? "bolt" : "mountain",
                              text: page.gears ? Gears.gearLabel + " · " + (Gears.gearIndex + 1) + "/" + Gears.gearCount
                                    : page.ergMode ? "ERG " + Rlv.targetWatts + " W" : "Simulation",
                              show: true },
                            { icon: "mountain", text: "Difficulty " + Rlv.difficultyPercent + " %", show: !page.ergMode },
                            { icon: "play", text: "Video × " + Rlv.playbackRate.toFixed(2), show: Rlv.active }
                        ]
                        Rectangle {
                            visible: modelData.show
                            height: 32
                            width: chipContent.implicitWidth + 24
                            radius: 16
                            color: Qt.rgba(0, 0, 0, 0.55)
                            Row {
                                id: chipContent
                                anchors.centerIn: parent
                                spacing: 7
                                Icon {
                                    name: modelData.icon
                                    size: 16
                                    color: "white"
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: modelData.text
                                    color: "white"
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }

                // audio toggle (RLVs are usually silent)
                Rectangle {
                    visible: videoCard.showVideo
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    width: 36
                    height: 32
                    radius: 16
                    color: audioMouse.containsMouse ? Qt.rgba(0, 0, 0, 0.75) : Qt.rgba(0, 0, 0, 0.55)
                    Icon {
                        anchors.centerIn: parent
                        name: page.audioOn ? "volume" : "mute"
                        size: 18
                        color: "white"
                    }
                    MouseArea {
                        id: audioMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.audioOn = !page.audioOn
                    }
                    ToolTip.visible: audioMouse.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: page.audioOn ? "Mute the video" : "Play the video's sound"
                }

                // the file opened but no frame could be decoded
                Flickable {
                    id: noPicFlick
                    visible: videoCard.showVideo && page.noPicture
                    readonly property bool compact: height < 420 || width < 560
                    anchors.fill: parent
                    anchors.topMargin: compact ? 58 : 64
                    anchors.bottomMargin: compact ? 56 : 60
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    clip: true
                    contentWidth: width
                    contentHeight: Math.max(height, noPicPanel.height + 8)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: noPicFlick.contentHeight > noPicFlick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                    Rectangle {
                        id: noPicPanel
                        width: Math.min(noPicFlick.width - 16, 680)
                        x: (noPicFlick.width - width) / 2
                        y: Math.max(4, (noPicFlick.height - height) / 2)
                        height: noPicCol.implicitHeight + (noPicFlick.compact ? 24 : 36)
                        radius: 14
                        color: Qt.rgba(0.02, 0.03, 0.07, 0.86)
                        border.color: Qt.rgba(1, 1, 1, 0.12)
                        Column {
                            id: noPicCol
                            x: noPicFlick.compact ? 14 : 22
                            y: noPicFlick.compact ? 12 : 18
                            width: parent.width - 2 * x
                            spacing: noPicFlick.compact ? 6 : 10
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter
                                spacing: 10
                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "video"
                                    size: noPicFlick.compact ? 24 : 32
                                    strokeWidth: 2
                                    color: Theme.warning
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "No picture"
                                    color: "white"
                                    font.pixelSize: noPicFlick.compact ? 19 : 24
                                    font.weight: Font.Bold
                                }
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: "This computer cannot decode the video"
                                      + (page.videoCodec.length > 0 ? " (" + page.videoCodec + ")" : "")
                                      + ". AV1 videos need AV1 hardware decoding – convert the file once to H.264 (720p). "
                                      + "The ride itself works without the picture."
                                color: Qt.rgba(1, 1, 1, 0.82)
                                font.pixelSize: noPicFlick.compact ? 13 : 15
                                wrapMode: Text.WordWrap
                            }
                            ConvertHelp {
                                width: parent.width
                                onVideo: true
                                compact: noPicFlick.compact
                                textColor: "white"
                                mutedColor: Qt.rgba(1, 1, 1, 0.80)
                                linkColor: Theme.accentLight
                                panelColor: Qt.rgba(1, 1, 1, 0.07)
                                panelBorder: Qt.rgba(1, 1, 1, 0.14)
                            }
                        }
                    }
                }

                // paused / waiting / finished
                Rectangle {
                    visible: videoCard.showVideo && !page.noPicture
                             && (page.st === "paused" || page.st === "ready" || page.finished)
                    anchors.centerIn: parent
                    width: centerCol.implicitWidth + 56
                    height: centerCol.implicitHeight + 32
                    radius: 16
                    color: Qt.rgba(0, 0, 0, 0.6)
                    Column {
                        id: centerCol
                        anchors.centerIn: parent
                        spacing: 6
                        Icon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: page.st === "paused" ? "pause" : page.st === "ready" ? "cadence" : "flag"
                            size: 34
                            strokeWidth: 2.2
                            color: "white"
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: page.st === "paused" ? "Paused" : page.st === "ready" ? "Waiting for pedaling…"
                                  : Rlv.completed ? "Route complete" : "Ride finished"
                            color: "white"
                            font.pixelSize: 26
                            font.weight: Font.Bold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: page.st === "paused" ? "Press Space or Resume to continue"
                                  : page.st === "ready" ? "The ride starts with your first pedal strokes"
                                  : page.kmText(Rlv.riddenMeters) + " km in " + Theme.clock(Rlv.elapsedSeconds)
                            color: Qt.rgba(1, 1, 1, 0.75)
                            font.pixelSize: 15
                        }
                    }
                }

                // ---- placeholders ---------------------------------------------
                // empty library
                Column {
                    visible: Videos.count === 0 && !Rlv.hasRoute
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 60, 720)
                    spacing: 12
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "film"
                        size: 46
                        strokeWidth: 2
                        color: Theme.accentLight
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "No Real Life Videos yet"
                        color: Theme.text
                        font.pixelSize: 26
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "Each route is a folder with the video (.mp4) and its GPS track (.gpx), "
                              + "optionally with a route file (.xml) for the title and on-screen texts:"
                        color: Theme.textSecondary
                        font.pixelSize: 16
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: layoutText.implicitWidth + 40
                        height: layoutText.implicitHeight + 24
                        radius: 10
                        color: Theme.field
                        border.color: Theme.fieldBorder
                        Text {
                            id: layoutText
                            anchors.centerIn: parent
                            text: "Artilody Videos/\n   FR_Le_petit_Menglonnais/\n      FR_Le_petit_Menglonnais.mp4\n"
                                  + "      FR_Le_petit_Menglonnais.gpx\n      FR_Le_petit_Menglonnais.xml"
                            color: Theme.text
                            font.family: Qt.platform.os === "osx" ? "Menlo" : Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono"
                            font.pixelSize: 14
                        }
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "Put route folders into  " + Videos.libraryFolder + "  or add any other folder."
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WrapAnywhere
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 12
                        AppButton {
                            height: 46
                            fontSize: 16
                            iconSize: 20
                            iconName: "plus"
                            text: "Add Folder…"
                            onClicked: addFolderDialog.open()
                        }
                        AppButton {
                            height: 46
                            kind: "secondary"
                            fontSize: 16
                            iconSize: 20
                            bold: false
                            iconName: "folder"
                            text: "Choose Videos Folder…"
                            onClicked: libraryFolderDialog.open()
                        }
                        AppButton {
                            height: 46
                            kind: "secondary"
                            fontSize: 16
                            iconSize: 20
                            bold: false
                            iconName: "refresh"
                            text: "Rescan"
                            onClicked: Videos.rescan()
                        }
                    }
                }

                // library but no route chosen
                Column {
                    visible: Videos.count > 0 && !Rlv.hasRoute
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 60, 560)
                    spacing: 12
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "film"
                        size: 46
                        strokeWidth: 2
                        color: Theme.accentLight
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Choose a video"
                        color: Theme.text
                        font.pixelSize: 26
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: Videos.count + (Videos.count === 1 ? " route" : " routes")
                              + " in your library. Pick one from the list above – it shows the distance and the climbing of each route."
                        color: Theme.textSecondary
                        font.pixelSize: 16
                        wrapMode: Text.WordWrap
                    }
                    AppButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: 46
                        fontSize: 16
                        iconSize: 20
                        iconName: "film"
                        text: "Choose Video…"
                        onClicked: routeCombo.popup.open()
                    }
                }

                // route without a playable video
                Flickable {
                    id: noVideoFlick
                    visible: Rlv.hasRoute && !videoCard.showVideo
                    readonly property bool compact: height < 420 || width < 560
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: width
                    contentHeight: Math.max(height, noVideoCol.implicitHeight + 16)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: noVideoFlick.contentHeight > noVideoFlick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                    Column {
                        id: noVideoCol
                        width: Math.min(noVideoFlick.width - 24, page.videoError ? 680 : 600)
                        x: (noVideoFlick.width - width) / 2
                        y: Math.max(8, (noVideoFlick.height - implicitHeight) / 2)
                        spacing: noVideoFlick.compact ? 7 : 10
                        Icon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: !noVideoFlick.compact || !page.videoError
                            name: "video"
                            size: 46
                            strokeWidth: 2
                            color: Theme.warning
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: page.videoError ? "This video cannot be played" : "Video file missing"
                            color: Theme.text
                            font.pixelSize: noVideoFlick.compact ? 20 : 24
                            font.weight: Font.Bold
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: page.videoError
                                  ? player.errorString + (page.videoCodec.length > 0 ? " (" + page.videoCodec + ")" : "")
                                    + "\nAV1 videos need AV1 hardware decoding – convert them to H.264 (720p). "
                                    + "On a Raspberry Pi install gstreamer1.0-libav."
                                  : (Rlv.lastError.length > 0 ? Rlv.lastError + "\n" : "")
                                    + "Put the .mp4 next to the route's .gpx / .xml file. You can still ride the route – "
                                    + "distance, gradient and the profile work without the video."
                            color: Theme.textSecondary
                            font.pixelSize: noVideoFlick.compact ? 13 : 15
                            wrapMode: Text.WordWrap
                        }
                        ConvertHelp {
                            visible: page.videoError
                            width: parent.width
                            compact: noVideoFlick.compact
                        }
                    }
                }
            }

            // distance line: elevation profile of the route
            Card {
                id: profileCard
                Layout.fillWidth: true
                Layout.preferredHeight: 178
                Row {
                    id: statsRow
                    x: 18
                    y: 14
                    spacing: 22
                    Repeater {
                        model: [
                            { label: "Distance", value: page.kmText(Rlv.distanceMeters, 2) + " / " + page.kmText(Rlv.totalDistanceM, 1) + " km" },
                            { label: "To go", value: page.kmText(Rlv.remainingMeters, 2) + " km" },
                            { label: "Elevation", value: Math.round(Rlv.elevationM) + " m" },
                            { label: "Climbed", value: Math.round(Rlv.ascentDoneM) + " / " + Math.round(Rlv.totalAscentM) + " m" },
                            { label: "Grade", value: page.gradeText(Rlv.gradePercent) }
                        ]
                        Row {
                            spacing: 7
                            Text {
                                text: modelData.label
                                color: Theme.textSecondary
                                font.pixelSize: 15
                                anchors.baseline: valueLabel.baseline
                            }
                            Text {
                                id: valueLabel
                                text: Rlv.hasRoute ? modelData.value : "–"
                                color: index === 4 && Rlv.hasRoute ? page.gradeColor(Rlv.gradePercent) : Theme.text
                                font.pixelSize: 18
                                font.weight: Font.Bold
                            }
                        }
                    }
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    y: 17
                    visible: Rlv.hasRoute && !page.locked && statsRow.width + implicitWidth + 60 < profileCard.width
                    text: "Click the profile to choose the start point"
                    color: Theme.textMuted
                    font.pixelSize: 13
                }
                RouteProfile {
                    id: routeProfile
                    x: 18
                    y: 48
                    width: parent.width - 36
                    height: parent.height - y - 10
                    interactive: Rlv.hasRoute && !page.locked
                    onStartChosen: (meters) => Rlv.seekToDistance(meters)
                }
                Text {
                    visible: !Rlv.hasRoute
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 14
                    text: "The route profile appears here once you choose a video."
                    color: Theme.textMuted
                    font.pixelSize: 15
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
                width: parent.width
                height: controlsCol.implicitHeight + 32
                Column {
                    id: controlsCol
                    x: 14; y: 16
                    width: parent.width - 28
                    spacing: 10

                    AppButton {
                        visible: !Rlv.active
                        width: parent.width
                        height: 50
                        kind: "primary"
                        iconName: "play"
                        text: page.finished ? "Ride Again" : "Start Now"
                        enabled: page.canStart
                        onClicked: Rlv.start()
                    }
                    AppButton {
                        visible: !Rlv.active && !page.finished
                        width: parent.width
                        height: 44
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        bold: false
                        iconName: Rlv.waiting ? "close" : "cadence"
                        text: Rlv.waiting ? "Cancel Auto-Start" : "Start When Pedaling"
                        enabled: Rlv.waiting || page.canStart
                        onClicked: Rlv.waiting ? Rlv.finish(false) : Rlv.startWhenPedaling()
                    }
                    AppButton {
                        visible: Rlv.active
                        width: parent.width
                        height: 50
                        kind: Rlv.paused ? "success" : "danger"
                        iconName: Rlv.paused ? "play" : "pause"
                        text: Rlv.paused ? "Resume" : "Pause Ride"
                        onClicked: Rlv.togglePause()
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
                            enabled: Rlv.active
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
                            onClicked: Rlv.reset()
                        }
                    }
                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: Rlv.waiting ? "Start pedaling – the ride begins with the first pedal strokes."
                              : !Rlv.active && page.otherSession
                                ? (Rowing.active ? "A row is in progress – finish it to start a video ride."
                                   : FreeRide.active ? "A free ride is in progress – finish it to start a video ride."
                                   : "A workout is running – finish it to start a video ride.")
                              : !Rlv.hasRoute && !Rlv.active ? "Choose a video to start."
                              : page.st === "idle" && Rlv.trainerConnected
                                ? "Starts at " + page.kmText(Rlv.startDistanceM, 2) + " km. Space starts and pauses the ride." : ""
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // no trainer connected
            Card {
                visible: !Rlv.trainerConnected
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
                    text: "Connect a smart trainer (or the virtual trainer) to ride the video."
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
                        text: Rlv.completed ? "Route complete" : "Ride finished"
                        color: Theme.text
                        font.pixelSize: 19
                        font.weight: Font.Bold
                    }
                    Grid {
                        columns: 2
                        width: parent.width
                        rowSpacing: 8
                        Repeater {
                            model: ["Time", "Distance", "Avg power", "Climbed", "Avg speed", "Energy"]
                            Column {
                                width: summaryCol.width / 2
                                Text { text: modelData; color: Theme.textSecondary; font.pixelSize: 13 }
                                Text {
                                    text: {
                                        switch (index) {
                                        case 0: return Theme.clock(Rlv.elapsedSeconds)
                                        case 1: return page.kmText(Rlv.riddenMeters) + " km"
                                        case 2: return Rlv.avgWatts + " W"
                                        case 3: return Math.round(Rlv.ascentDoneM) + " m"
                                        case 4: return Rlv.avgSpeedKmh.toFixed(1) + " km/h"
                                        default: return Math.round(Rlv.kilojoules) + " kJ"
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
                        visible: Rlv.lastRideId < 0
                        width: parent.width
                        text: "Not saved (discarded or shorter than 30 s)."
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    AppButton {
                        visible: Rlv.lastRideId >= 0
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

            // resistance: simulation (real or virtual gears) / ERG
            Card {
                id: resistanceCard
                property Item ergPlus10: null
                property Item ergMinus10: null
                width: parent.width
                height: resCol.y + resCol.implicitHeight + 16
                title: "Resistance"
                titleSize: 18
                Column {
                    id: resCol
                    x: 16
                    y: resistanceCard.contentTop - 4
                    width: parent.width - 32
                    spacing: 10

                    SegmentedControl {
                        width: parent.width
                        height: 44
                        fontSize: 14
                        segmentPadding: 9
                        options: [
                            { value: "sim", label: "Simulation" },
                            { value: "erg", label: "ERG" }
                        ]
                        value: Rlv.mode
                        onActivated: (v) => Rlv.mode = v
                    }
                    Text {
                        width: parent.width
                        text: page.ergMode ? "The trainer holds your watts. Climbs make the video slower, not the pedals heavier."
                              : "Climbs get harder like on the road."
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }

                    // how the rider shifts (set up on the Devices page)
                    Text {
                        visible: page.simMode
                        width: parent.width
                        text: "Shifting: " + page.shiftingText()
                              + "   <a href=\"devices\" style=\"text-decoration:none\">Devices ›</a>"
                        textFormat: Text.StyledText
                        linkColor: Theme.accentLight
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        onLinkActivated: page.openDevices()
                        HoverHandler { cursorShape: parent.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    }

                    // simulation not possible on this trainer
                    Rectangle {
                        visible: Rlv.simulationFallback
                        width: parent.width
                        height: fallbackRow.implicitHeight + 16
                        radius: 8
                        color: Theme.cardStrong
                        border.color: Theme.warning
                        Row {
                            id: fallbackRow
                            x: 10; y: 8
                            width: parent.width - 20
                            spacing: 8
                            Icon { name: "info"; size: 18; color: Theme.warningText }
                            Text {
                                width: parent.width - 26
                                text: "Your trainer can't simulate climbs – using virtual gears instead."
                                color: Theme.text
                                font.pixelSize: 13
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // simulation: what the trainer gets
                    Item {
                        visible: page.simMode && Rlv.resistancePath !== "gearErg"
                        width: parent.width
                        height: 26
                        Text {
                            text: "Grade sent to trainer"
                            color: Theme.textSecondary
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: Rlv.simulationActive ? page.gradeText(Rlv.simulationGradePercent) : "–"
                            color: Rlv.simulationActive ? page.gradeColor(Rlv.simulationGradePercent) : Theme.textMuted
                            font.pixelSize: 18
                            font.weight: Font.Bold
                        }
                    }

                    // virtual gears
                    RowLayout {
                        visible: page.gears
                        width: parent.width
                        height: 76
                        spacing: 8
                        ShiftButton {
                            id: easierButton
                            Layout.preferredWidth: 64
                            Layout.fillHeight: true
                            iconName: "minus"
                            iconSize: 26
                            Accessible.name: "Shift easier"
                            tint: Theme.accent
                            enabled: Gears.canShiftDown
                            onClicked: Gears.shiftDown()
                        }
                        Column {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 2
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: Gears.gearLabel.replace("×", " × ")
                                color: Theme.text
                                font.pixelSize: 30
                                font.weight: Font.Black
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Gear " + (Gears.gearIndex + 1) + " / " + Gears.gearCount
                                color: Theme.textSecondary
                                font.pixelSize: 13
                            }
                        }
                        ShiftButton {
                            id: harderButton
                            Layout.preferredWidth: 64
                            Layout.fillHeight: true
                            iconName: "plus"
                            iconSize: 26
                            Accessible.name: "Shift harder"
                            tint: Theme.orange
                            enabled: Gears.canShiftUp
                            onClicked: Gears.shiftUp()
                        }
                    }
                    Text {
                        visible: page.gears && Rlv.resistancePath === "gearSimulation"
                        width: parent.width
                        text: "Leave your bike in " + Gears.referenceLabel + " on the trainer."
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: page.gears && Rlv.active && Rlv.cadence === 0 && Rlv.watts > 0
                        width: parent.width
                        text: "No cadence from your trainer: it rides without virtual gears, and your speed follows your power."
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    // ERG: target watts
                    Item {
                        visible: page.ergMode
                        width: parent.width
                        height: ergTarget.implicitHeight
                        Row {
                            id: ergTarget
                            spacing: 6
                            Text {
                                id: ergValue
                                text: Rlv.targetWatts
                                color: Theme.warningText
                                font.pixelSize: 54
                                font.weight: Font.Black
                                font.letterSpacing: -1
                            }
                            Text {
                                text: "W"
                                color: Theme.warningText
                                font.pixelSize: 26
                                font.weight: Font.Bold
                                anchors.baseline: ergValue.baseline
                            }
                        }
                        Column {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                anchors.right: parent.right
                                text: "Target power"
                                color: Theme.textSecondary
                                font.pixelSize: 13
                            }
                            Text {
                                readonly property int diff: Rlv.displayWatts - Rlv.targetWatts
                                anchors.right: parent.right
                                text: Rlv.displayWatts > 0 ? "Actual " + Rlv.displayWatts + " W" : "Actual – W"
                                color: Rlv.displayWatts <= 0 ? Theme.textSecondary
                                       : Math.abs(diff) <= Math.max(5, Rlv.targetWatts * 0.05) ? Theme.successText : Theme.warningText
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                    GridLayout {
                        visible: page.ergMode
                        width: parent.width
                        columns: 3
                        rowSpacing: 8
                        columnSpacing: 8
                        Repeater {
                            model: [-25, -10, -5, 5, 10, 25]
                            ShiftButton {
                                id: ergStep
                                Layout.fillWidth: true
                                Layout.preferredHeight: 50
                                label: (modelData > 0 ? "+" : "−") + Math.abs(modelData)
                                labelSize: 20
                                Accessible.name: (modelData > 0 ? "Increase target by " : "Decrease target by ") + Math.abs(modelData) + " watts"
                                tint: modelData > 0 ? Theme.orange : Theme.accent
                                enabled: modelData > 0 ? Rlv.targetWatts < Rlv.maxTargetWatts : Rlv.targetWatts > Rlv.minTargetWatts
                                onClicked: Rlv.adjustTargetWatts(modelData)
                                Component.onCompleted: {
                                    if (modelData === 10) resistanceCard.ergPlus10 = ergStep
                                    else if (modelData === -10) resistanceCard.ergMinus10 = ergStep
                                }
                            }
                        }
                    }
                    RowLayout {
                        visible: page.ergMode
                        width: parent.width
                        spacing: 8
                        AppTextField {
                            id: ergEntry
                            Layout.fillWidth: true
                            placeholderText: "Exact target, " + Rlv.minTargetWatts + "–" + Rlv.maxTargetWatts + " W"
                            font.pixelSize: 15
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 9999 }
                            onAccepted: ergSet.clicked()
                        }
                        AppButton {
                            id: ergSet
                            Layout.preferredHeight: 44
                            text: "Set"
                            fontSize: 15
                            horizontalPadding: 16
                            enabled: ergEntry.text.length > 0
                            onClicked: {
                                var v = parseInt(ergEntry.text)
                                if (isNaN(v))
                                    return
                                Rlv.targetWatts = v
                                ergEntry.text = ""
                                ergEntry.focus = false
                            }
                        }
                    }

                    // difficulty (resistance modes only)
                    Rectangle { visible: !page.ergMode; width: parent.width; height: 1; color: Theme.cardBorder }
                    Item {
                        visible: !page.ergMode
                        width: parent.width
                        height: 24
                        Text {
                            text: "Difficulty"
                            color: Theme.text
                            font.pixelSize: 16
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: Rlv.difficultyPercent + " %"
                            color: Theme.text
                            font.pixelSize: 18
                            font.weight: Font.Bold
                        }
                    }
                    AppSlider {
                        visible: !page.ergMode
                        width: parent.width
                        from: 0
                        to: 100
                        stepSize: 5
                        snapMode: Slider.SnapAlways
                        value: Rlv.difficultyPercent
                        fillColor: Theme.success
                        onMoved: Rlv.difficultyPercent = Math.round(value)
                    }
                    Text {
                        visible: !page.ergMode
                        width: parent.width
                        text: Rlv.difficultyPercent >= 100
                              ? "100 % = the real gradients of the route. Lower values flatten climbs and descents."
                              : "A " + page.gradeText(Math.max(Rlv.gradePercent, 6)).replace("+", "") + " climb feels like "
                                + page.gradeText(Math.max(Rlv.gradePercent, 6) * Rlv.difficultyPercent / 100).replace("+", "") + "."
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        width: parent.width
                        text: page.ergMode ? "Keys: ↑ / ↓ ±5 W  •  PgUp / PgDn ±10 W  •  Space pause"
                              : page.gears ? "Keys: ↑ / ↓ shift  •  PgUp / PgDn difficulty  •  Space pause"
                              : "Keys: PgUp / PgDn difficulty  •  Space pause"
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Zwift Click
            Card {
                id: clickCard
                visible: !page.finished && (Rlv.trainerConnected || Devices.controllerConnected)
                width: parent.width
                height: clickCol.y + clickCol.implicitHeight + 14
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
                    y: clickCard.contentTop - 6
                    width: parent.width - 32
                    spacing: 6
                    Row {
                        spacing: 10
                        Icon {
                            name: "controller"
                            size: 26
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
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: clickCol.width - 36
                                text: page.ergMode ? "+ / − target ±10 W" : "+ harder gear  •  − easier gear"
                                color: Theme.textSecondary
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
