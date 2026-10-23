import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Artilody.Backend

Item {
    id: page
    signal message(string text)

    readonly property var ride: History.selectedRide
    readonly property bool hasRide: ride.rideId !== undefined
    readonly property bool rideRowing: ride.sport === "rowing"
    property string exportKind: "csv"   // "csv" | "summary" | "fit"
    property bool showAll: false

    PageHeader {
        x: Theme.pageMargin + 4
        y: 30
        title: "History & Analysis"
        subtitle: "Your rides, progress and performance"
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin
        y: 76
        spacing: 14
        SegmentedControl {
            height: 50
            fontSize: 15
            segmentPadding: 12
            value: History.sportFilter
            options: [
                { value: "all", label: "All" },
                { value: "cycling", label: "Cycling", icon: "bike" },
                { value: "rowing", label: "Rowing", icon: "rowing", color: Theme.rowingColor, textColor: Theme.textOnColor }
            ]
            onActivated: (v) => History.sportFilter = v
        }
        AppComboBox {
            width: 222
            height: 50
            iconName: "calendar"
            model: History.rangeOptions
            textRole: "label"
            currentIndex: {
                for (var i = 0; i < History.rangeOptions.length; ++i)
                    if (History.rangeOptions[i].days === History.rangeDays)
                        return i
                return 1
            }
            onActivated: (index) => History.rangeDays = History.rangeOptions[index].days
        }
        AppButton {
            width: 212
            height: 50
            iconName: "download"
            text: "Export"
            onClicked: exportMenu.popup()
            AppMenu {
                id: exportMenu
                y: 54
                AppMenuItem {
                    text: "Selected ride – FIT (Garmin, Strava, TrainingPeaks…)"
                    iconName: "download"
                    enabled: page.hasRide
                    onTriggered: page.startExport("fit")
                }
                AppMenuItem {
                    text: "Selected ride – CSV (per second)"
                    iconName: "chart"
                    enabled: page.hasRide
                    onTriggered: page.startExport("csv")
                }
                AppMenuItem {
                    text: "All rides in range – CSV summary"
                    iconName: "document"
                    enabled: History.totalRides > 0
                    onTriggered: page.startExport("summary")
                }
                AppMenuItem {
                    text: "All rides in range – FIT files…"
                    iconName: "folder"
                    enabled: History.totalRides > 0
                    onTriggered: {
                        exportFolderDialog.currentFolder = History.suggestedExportUrl("folder")
                        exportFolderDialog.open()
                    }
                }
            }
        }
    }

    // screenshot aid (WATT_SCREENSHOT_MENU)
    function openScreenshotMenu() {
        exportMenu.popup(0, exportMenu.parent.height + 4)
        return exportMenu
    }

    function startExport(kind) {
        page.exportKind = kind
        exportDialog.selectedFile = History.suggestedExportUrl(kind)
        exportDialog.open()
    }

    FileDialog {
        id: exportDialog
        title: page.exportKind === "fit" ? "Export FIT activity" : "Export training data"
        fileMode: FileDialog.SaveFile
        nameFilters: page.exportKind === "fit" ? ["FIT activity files (*.fit)"] : ["CSV files (*.csv)"]
        defaultSuffix: page.exportKind === "fit" ? "fit" : "csv"
        onAccepted: {
            if (page.exportKind === "fit")
                History.exportSelectedFit(selectedFile)
            else if (page.exportKind === "summary")
                History.exportSummaryCsv(selectedFile)
            else
                History.exportSelectedCsv(selectedFile)
            page.message(History.lastMessage)
        }
    }

    FolderDialog {
        id: exportFolderDialog
        title: "Export all rides in range as FIT files"
        onAccepted: {
            History.exportAllFit(selectedFolder)
            page.message(History.lastMessage)
        }
    }

    // --- totals -----------------------------------------------------------------
    RowLayout {
        x: Theme.pageMargin
        y: 144
        width: parent.width - 2 * Theme.pageMargin
        height: 104
        spacing: Theme.spacing
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true; large: true
            iconName: History.sportFilter === "rowing" ? "rowing" : History.sportFilter === "cycling" ? "bike" : "pulse"
            iconColor: History.sportFilter === "rowing" ? Theme.rowingColor : Theme.accentLight
            caption: History.sportFilter === "rowing" ? "Total Rows" : History.sportFilter === "cycling" ? "Total Rides" : "Total Sessions"
            value: History.totalRides
        }
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true; large: true
            iconName: "clock"; iconColor: Theme.cyan; caption: "Training Time"; value: History.totalTimeText
        }
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true; large: true
            iconName: "bolt"; iconColor: Theme.warning; caption: "Avg Power"; value: History.avgPower + " W"
        }
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true; large: true
            iconName: "flame"; iconColor: Theme.orange; caption: "Total Energy"; value: Theme.thousands(History.totalKj) + " kJ"
        }
    }

    // --- selected ride chart --------------------------------------------------
    Card {
        id: chartCard
        x: Theme.pageMargin
        y: 264
        width: recentCard.x - x - Theme.spacing
        height: 386

        Text {
            x: 18; y: 16
            width: parent.width - legend.width - 50
            text: page.hasRide ? "Selected Workout · " + page.ride.name : "Selected Workout"
            color: Theme.text
            font.pixelSize: 23
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        Row {
            id: legend
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: 24
            spacing: 18
            Repeater {
                model: [
                    { label: "Target Power", color: Theme.warning },
                    { label: "Actual Power", color: Theme.actualPower },
                    { label: "Heart Rate", color: Theme.heartRate }
                ]
                Row {
                    spacing: 8
                    Rectangle { width: 24; height: 8; radius: 4; color: modelData.color; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: modelData.label; color: Theme.text; font.pixelSize: 14 }
                }
            }
        }
        Row {
            x: 18; y: 54
            spacing: 10
            visible: page.hasRide
            Icon { name: "calendar"; size: 20; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: page.ride.date + "  •  " + page.ride.durationMin + " min  •  "
                      + (page.rideRowing ? "Rowing  •  " + Theme.thousands(page.ride.distanceM || 0) + " m"
                                         : page.ride.blocks + " blocks  •  " + page.ride.mode)
                color: Theme.text
                font.pixelSize: 17
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        WorkoutChart {
            x: 2
            y: 86
            width: parent.width - 10
            height: parent.height - 92
            visible: page.hasRide
            targetSeries: History.selectedTarget
            powerSeries: History.selectedPower
            hrSeries: History.selectedHeartRate
            showActualLine: true
            ftp: page.ride.ftp || (page.rideRowing ? Profiles.rowingFtp : Profiles.ftp)
            minPowerAxis: 400
            hrMin: Math.max(40, Math.floor((Profiles.restHr + 10) / 20) * 20)
            hrMax: Math.ceil(Profiles.maxHr / 20) * 20
        }
        Column {
            visible: !page.hasRide
            anchors.centerIn: parent
            spacing: 12
            Icon { name: "chart"; size: 56; color: Theme.textMuted; anchors.horizontalCenter: parent.horizontalCenter }
            Text {
                text: "No rides in this period yet"
                color: Theme.textSecondary
                font.pixelSize: 19
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: "Finish a workout and it will appear here with a full analysis."
                color: Theme.textMuted
                font.pixelSize: 15
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    // --- recent workouts ---------------------------------------------------------
    Card {
        id: recentCard
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin
        y: 264
        width: 432
        height: page.showAll ? parent.height - y - Theme.pageMargin : 300
        z: page.showAll ? 5 : 0
        title: page.showAll ? "All Workouts" : "Recent Workouts"
        titleSize: 21
        headerRight: [
            Text {
                text: page.showAll ? "Show Less" : "View All"
                color: Theme.accentLight
                font.pixelSize: 16
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.showAll = !page.showAll
                }
            }
        ]

        Row {
            id: headerRow
            x: 18
            y: 58
            Repeater {
                model: [{ t: "Date", w: 84 }, { t: "Workout", w: 166 }, { t: "Duration", w: 82 }, { t: "Avg Power", w: 70 }]
                Text {
                    width: modelData.w
                    text: modelData.t
                    color: Theme.textSecondary
                    font.pixelSize: 15
                }
            }
        }

        ListView {
            x: 6
            y: 86
            width: parent.width - 12
            height: parent.height - y - 8
            clip: true
            model: History.rides
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                width: ListView.view.width
                height: 50
                radius: 8
                readonly property bool isSel: modelData.rideId === History.selectedRideId
                color: isSel ? Theme.navSelected : (index % 2 === 1 ? Theme.cardStrong : "transparent")
                border.width: isSel ? 1.5 : 0
                border.color: Theme.accentLight
                Row {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    Text { width: 84; text: modelData.date; color: Theme.text; font.pixelSize: 16; font.weight: isSel ? Font.Bold : Font.Normal }
                    Item {
                        width: 166
                        height: 22
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: modelData.sport === "rowing" ? "rowing" : "bike"
                            size: 16
                            strokeWidth: 2
                            color: Theme.sportColor(modelData.sport)
                        }
                        Text {
                            x: 22
                            width: parent.width - 22
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 16
                            elide: Text.ElideRight
                            rightPadding: 8
                            font.weight: isSel ? Font.Bold : Font.Normal
                        }
                    }
                    Text { width: 82; text: modelData.durationMin + " min"; color: Theme.text; font.pixelSize: 16; font.weight: isSel ? Font.Bold : Font.Normal }
                    Text { width: 70; text: modelData.avgPower + " W"; color: Theme.text; font.pixelSize: 16; font.weight: isSel ? Font.Bold : Font.Normal }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: History.selectRide(modelData.rideId)
                }
            }
            Text {
                visible: History.totalRides === 0
                anchors.centerIn: parent
                text: "No rides yet"
                color: Theme.textSecondary
                font.pixelSize: 16
            }
        }
    }

    // --- weekly training time ---------------------------------------------------
    Card {
        id: weeklyCard
        x: Theme.pageMargin
        y: chartCard.y + chartCard.height + Theme.spacing
        width: (chartCard.width - Theme.spacing) * 0.49
        height: parent.height - y - Theme.pageMargin
        title: "Weekly Training Time"
        titleSize: 21
        BarChart {
            x: 8
            y: 54
            width: parent.width - 20
            height: parent.height - 60
            values: History.weeklyHours
            labels: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
        }
    }

    // --- power zones -------------------------------------------------------------
    Card {
        id: zonesCard
        readonly property real maxFraction: {
            var m = 0
            var d = History.zoneDistribution
            for (var i = 0; i < d.length; ++i)
                m = Math.max(m, d[i].fraction)
            return m
        }
        x: weeklyCard.x + weeklyCard.width + Theme.spacing
        y: weeklyCard.y
        width: chartCard.width - weeklyCard.width - Theme.spacing
        height: weeklyCard.height

        Row {
            x: 18; y: 18
            spacing: 8
            Text { text: "Power Zones"; color: Theme.text; font.pixelSize: 21; font.weight: Font.Bold }
            Text {
                text: "(" + (History.rangeDays > 0 ? "last " + History.rangeDays + " days" : "all time") + ")"
                color: Theme.text
                font.pixelSize: 18
                anchors.baseline: parent.children[0].baseline
            }
        }
        Column {
            x: 18
            y: 62
            width: parent.width - 36
            spacing: (parent.height - 80 - 6 * 26) / 5
            Repeater {
                model: History.zoneDistribution
                Item {
                    width: parent.width
                    height: 26
                    Rectangle {
                        width: 16; height: 16; radius: 8
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.zoneColor(modelData.zone)
                    }
                    Text {
                        x: 28
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: Theme.text
                        font.pixelSize: 15
                    }
                    Rectangle {
                        x: 136
                        width: parent.width - 136 - 118
                        height: 22
                        radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.cardStrong
                        border.color: Theme.cardBorder
                        Rectangle {
                            width: Math.max(modelData.fraction > 0 ? 4 : 0, parent.width * (zonesCard.maxFraction > 0 ? modelData.fraction / zonesCard.maxFraction * 0.7 : 0))
                            height: parent.height
                            radius: 4
                            color: Theme.zoneColor(modelData.zone)
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 44
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.timeText
                        color: Theme.text
                        font.pixelSize: 14
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.round(modelData.fraction * 100) + "%"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }
            }
        }
    }

    // --- selected details -------------------------------------------------------
    Card {
        id: detailsCard
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin
        y: recentCard.y + 300 + Theme.spacing
        width: recentCard.width
        height: parent.height - y - Theme.pageMargin
        visible: !page.showAll
        title: "Selected Workout Details"
        titleSize: 21

        Text {
            x: 18; y: 50
            width: parent.width - 36
            text: page.hasRide ? page.ride.name : "–"
            color: Theme.text
            font.pixelSize: 21
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        Row {
            x: 18; y: 86
            spacing: 10
            visible: page.hasRide
            Icon {
                name: page.rideRowing ? "rowing" : "calendar"
                size: 18
                color: page.rideRowing ? Theme.rowingColor : Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: page.ride.date + "  •  " + page.ride.durationMin + " min  •  "
                      + (page.rideRowing ? Theme.thousands(page.ride.distanceM || 0) + " m rowed" : page.ride.blocks + " blocks")
                color: Theme.text
                font.pixelSize: 16
            }
        }
        GridLayout {
            x: 14
            y: 120
            width: parent.width - 28
            height: parent.height - y - 90
            columns: 2
            rowSpacing: 10
            columnSpacing: 10
            StatTile { Layout.fillWidth: true; Layout.fillHeight: true; iconName: "heart"; iconColor: Theme.danger; iconSize: 30; caption: "Avg HR"; value: (page.ride.avgHr || 0) + " BPM" }
            StatTile { Layout.fillWidth: true; Layout.fillHeight: true; iconName: "heart"; iconColor: Theme.danger; iconSize: 30; caption: "Max HR"; value: (page.ride.maxHr || 0) + " BPM" }
            StatTile { Layout.fillWidth: true; Layout.fillHeight: true; iconName: "bolt"; iconColor: Theme.warning; iconSize: 30; caption: "Normalized Power"; value: (page.ride.np || 0) + " W" }
            StatTile { Layout.fillWidth: true; Layout.fillHeight: true; iconName: "cadence"; iconColor: Theme.accentLight; iconSize: 30; caption: page.rideRowing ? "Stroke Rate" : "Cadence"; value: (page.ride.cadence || 0) + (page.rideRowing ? " SPM" : " RPM") }
        }
        AppButton {
            x: 14
            width: parent.width - 28
            height: 60
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            iconName: "document"
            text: "View Details"
            enabled: page.hasRide
            onClicked: rideDialog.open()
        }
    }

    // --- ride details dialog -------------------------------------------------------
    AppDialog {
        id: rideDialog
        title: page.ride.name || ""
        panelWidth: page.ride.isFtpTest ? 860 : 760   // FTP tests show an extra "Use as FTP" button
        panelHeight: page.ride.isFtpTest ? 646 : 560

        Text {
            x: 28; y: 72
            text: (page.ride.date || "") + " at " + (page.ride.time || "") + "  •  " + Theme.clock(page.ride.durationSec || 0)
                  + (page.rideRowing ? "  •  Rowing" + (page.ride.distanceM > 0 ? " " + Theme.thousands(page.ride.distanceM) + " m" : "")
                                     : (page.ride.mode ? "  •  " + page.ride.mode : ""))
                  + "  •  " + (page.rideRowing ? "Rowing FTP " : "FTP ")
                  + (page.ride.ftp || (page.rideRowing ? Profiles.rowingFtp : Profiles.ftp)) + " W"
            color: Theme.textSecondary
            font.pixelSize: 16
        }
        GridLayout {
            x: 24
            y: 110
            width: rideDialog.panelWidth - 48
            columns: 4
            rowSpacing: 10
            columnSpacing: 10
            Repeater {
                model: [
                    { c: "Avg Power", v: (page.ride.avgPower || 0) + " W", i: "bolt" },
                    { c: "Max Power", v: (page.ride.maxPower || 0) + " W", i: "bolt" },
                    { c: "Normalized", v: (page.ride.np || 0) + " W", i: "bolt" },
                    { c: "Energy", v: Theme.thousands(page.ride.kj || 0) + " kJ", i: "flame" },
                    { c: "Intensity (IF)", v: (page.ride.if || 0).toFixed(2), i: "trendUp" },
                    { c: "TSS", v: "" + (page.ride.tss || 0), i: "chart" },
                    { c: "Avg / Max HR", v: (page.ride.avgHr || 0) + " / " + (page.ride.maxHr || 0), i: "heart" },
                    page.rideRowing ? { c: "Avg Stroke Rate", v: (page.ride.cadence || 0) + " SPM", i: "waves" }
                                    : { c: "Avg Cadence", v: (page.ride.cadence || 0) + " RPM", i: "cadence" }
                ].concat(page.ride.isFtpTest ? [
                    { c: "Best 1 min", v: (page.ride.best1min || 0) + " W", i: "bolt" },
                    { c: "Estimated FTP", v: page.ride.estimatedFtp > 0 ? page.ride.estimatedFtp + " W" : "–", i: "target" }
                ] : [])
                StatTile {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    iconName: modelData.i
                    iconColor: modelData.i === "heart" ? Theme.danger : (modelData.i === "flame" ? Theme.orange : Theme.warning)
                    caption: modelData.c
                    value: modelData.v
                    valueSize: 20
                }
            }
        }
        Text {
            id: zonesTitle
            x: 28; y: page.ride.isFtpTest ? 376 : 290
            text: "Time in zones"
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.Bold
        }
        Row {
            x: 28; y: zonesTitle.y + 34
            width: rideDialog.panelWidth - 56
            height: 30
            Repeater {
                model: History.selectedZones
                Rectangle {
                    width: parent.width * modelData.fraction
                    height: 30
                    color: Theme.zoneColor(modelData.zone)
                    visible: modelData.fraction > 0
                    Text {
                        anchors.centerIn: parent
                        visible: parent.width > 40
                        text: Math.round(modelData.fraction * 100) + "%"
                        color: Theme.textOnColor
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
        Grid {
            x: 28; y: zonesTitle.y + 76
            columns: 3
            columnSpacing: 30
            rowSpacing: 6
            Repeater {
                model: History.selectedZones
                Row {
                    spacing: 8
                    Rectangle { width: 12; height: 12; radius: 6; color: Theme.zoneColor(modelData.zone); anchors.verticalCenter: parent.verticalCenter }
                    Text { text: modelData.name + "  " + modelData.timeText; color: Theme.textSecondary; font.pixelSize: 14 }
                }
            }
        }
        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            AppButton {
                kind: "secondary"
                iconName: "trash"
                text: "Delete ride"
                bold: false
                onClicked: deleteRideDialog.open()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                visible: page.ride.isFtpTest === true
                enabled: page.ride.estimatedFtp > 0
                kind: "secondary"
                iconName: "target"
                text: "Use as FTP"
                onClicked: ftpResultDialog.openWith(page.ride.estimatedFtp, Profiles.ftp, page.ride.best1min)
            }
            AppButton {
                kind: "secondary"
                iconName: "download"
                text: "Export FIT"
                onClicked: page.startExport("fit")
            }
            AppButton {
                kind: "secondary"
                iconName: "download"
                text: "Export CSV"
                onClicked: page.startExport("csv")
            }
            AppButton {
                text: "Close"
                onClicked: rideDialog.close()
            }
        }
    }

    ConfirmDialog {
        id: deleteRideDialog
        title: "Delete ride?"
        message: "This ride and all its recorded data will be removed permanently."
        acceptText: "Delete"
        acceptKind: "danger"
        onAccepted: {
            History.deleteRide(History.selectedRideId)
            rideDialog.close()
            page.message("Ride deleted")
        }
    }

    FtpResultDialog {
        id: ftpResultDialog
        z: 1001
        title: "FTP Test Result"
        onMessage: (text) => page.message(text)
    }
}
