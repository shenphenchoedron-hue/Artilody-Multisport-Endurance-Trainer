import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

// Training calendar: month grid with planned workouts, an "Upcoming" list and
// a panel for the selected day. All plan logic lives in Planner (C++).
Item {
    id: page

    property int viewYear: 2026
    property int viewMonth: 1                 // 1..12
    property string todayIso: ""
    property string selectedDate: ""
    property var cells: []                    // 42 day cells (6 weeks, Monday first)
    property var dayEntries: []               // entries of selectedDate
    property var upcomingList: []
    property var monthStats: ({ planned: 0, completed: 0, rowing: 0, cycling: 0, seconds: 0 })

    property var selectedEntryId: -1          // chip selected with a single click (side panel highlight)

    // drag & drop of chips between days
    property var dragEntry: null
    property string dropIso: ""
    property bool refreshPending: false       // a refresh() arrived while dragging

    // Modal dialogs are hosted by Main.qml's scaled root so they cover the whole
    // window (sidebar and top bar included), not just this page.
    readonly property Item dialogHost: ApplicationWindow.window && ApplicationWindow.window.dialogHost
                                       ? ApplicationWindow.window.dialogHost : page

    readonly property var monthNames: ["January", "February", "March", "April", "May", "June", "July",
                                       "August", "September", "October", "November", "December"]
    readonly property var weekdayNames: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

    // --- navigation / feedback (Main.qml: ApplicationWindow.window.showPage / showToast) -----
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

    // --- date helpers ("yyyy-MM-dd" strings = local calendar days) ---------------------------
    function pad(n) { return n < 10 ? "0" + n : "" + n }
    function isoOf(d) { return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) }
    function parseIso(s) {
        var p = String(s).split("-")
        return new Date(parseInt(p[0], 10), parseInt(p[1], 10) - 1, parseInt(p[2], 10))
    }
    function addDays(iso, n) {
        var d = parseIso(iso)
        return isoOf(new Date(d.getFullYear(), d.getMonth(), d.getDate() + n))
    }
    function longDate(iso) {
        if (!iso)
            return ""
        var d = parseIso(iso)
        return ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"][d.getDay()]
               + ", " + d.getDate() + " " + monthNames[d.getMonth()]
    }
    function shortDate(iso) {
        var d = parseIso(iso)
        return weekdayNames[(d.getDay() + 6) % 7] + " " + d.getDate() + " " + monthNames[d.getMonth()].substring(0, 3)
    }
    function relativeDate(iso) {
        if (iso === todayIso) return "Today"
        if (iso === addDays(todayIso, 1)) return "Tomorrow"
        if (iso === addDays(todayIso, -1)) return "Yesterday"
        return shortDate(iso)
    }
    function durationText(sec) {
        var m = Math.round((sec || 0) / 60)
        if (m < 60)
            return m + " min"
        return Math.floor(m / 60) + " h" + (m % 60 > 0 ? " " + pad(m % 60) : "")
    }
    function kmText(m) { return ((m || 0) / 1000).toFixed(1) + " km" }
    // "12.0 km  ·  154 m ↑  ·  0:52:10  ·  France"
    function routeDetails(distanceM, ascentM, sec, country) {
        var parts = [kmText(distanceM), Math.round(ascentM || 0) + " m ↑"]
        if (sec > 0)
            parts.push(durationText(sec))
        if (country && country.length > 0)
            parts.push(country)
        return parts.join("  ·  ")
    }
    function kindOf(e) { return e && e.kind ? e.kind : "workout" }
    function entryColor(e) { return Theme.kindColor(kindOf(e), e ? e.sport : "") }
    function entryIcon(e) { return Theme.kindIcon(kindOf(e), e ? e.sport : "") }
    function routeMissing(e) { return kindOf(e) === "rlv" && e.routeAvailable === false }
    function modeText(e) {
        var o = e.options || {}
        var k = kindOf(e)
        if (k === "freeride")
            return o.mode === "erg" ? "ERG" + (o.ergWatts > 0 ? " " + o.ergWatts + " W" : "") : "Ride"
        if (k === "rlv")
            return o.mode === "erg" ? "ERG" + (o.ergWatts > 0 ? " " + o.ergWatts + " W" : "")
                                    : "Simulation" + (o.difficulty !== undefined && o.difficulty < 100 ? " " + o.difficulty + " %" : "")
        return ""
    }
    // second line of a chip (short)
    function chipSubText(e) {
        if (kindOf(e) === "rlv" && !(e.durationSec > 0))
            return e.routeDistanceM > 0 ? kmText(e.routeDistanceM) : "Video Ride"
        if (kindOf(e) === "freeride" && !(e.durationSec > 0))
            return "Open"
        return durationText(e.durationSec)
    }
    // detail line of the side panel / upcoming list
    function entrySubtitle(e) {
        var k = kindOf(e)
        var parts = []
        if (k === "freeride") {
            parts.push("Free Ride")
            parts.push(e.plannedDurationSec > 0 ? durationText(e.plannedDurationSec) : "Open")
            parts.push(modeText(e))
        } else if (k === "rlv") {
            if (e.routeDistanceM > 0)
                parts.push(kmText(e.routeDistanceM) + "  " + Math.round(e.routeAscentM) + " m ↑")
            else
                parts.push("Video Ride")
            if (e.durationSec > 0)
                parts.push(durationText(e.durationSec))
            parts.push(e.workoutId >= 0 ? "Workout " + ((e.options || {}).workoutErg === false ? "(guide)" : "ERG") : modeText(e))
        } else {
            parts.push(Theme.sportName(e.sport))
            if (e.category)
                parts.push(e.category)
            parts.push(durationText(e.durationSec))
        }
        if (e.completed)
            parts.push("Done")
        return parts.join("  •  ")
    }
    // tooltip text with all details
    function entryDetails(e) {
        var k = kindOf(e)
        var lines = [e.name || ""]
        if (k === "freeride") {
            lines.push("Free Ride  ·  " + (e.plannedDurationSec > 0 ? "planned " + durationText(e.plannedDurationSec) : "no planned duration")
                       + "  ·  " + modeText(e))
        } else if (k === "rlv") {
            lines.push("Route: " + (e.routeName || "") + (e.routeDistanceM > 0 ? "  ·  " + routeDetails(e.routeDistanceM, e.routeAscentM, 0, "") : ""))
            if (routeMissing(e))
                lines.push("⚠ Route not found in the video library")
            if (e.workoutId >= 0)
                lines.push("Workout: " + e.workoutName + "  ·  " + durationText(e.durationSec)
                           + ((e.options || {}).workoutErg === false ? "  ·  targets as guidance" : "  ·  Workout ERG"))
            lines.push("Resistance: " + modeText(e))
        } else {
            lines[0] += "  ·  " + durationText(e.durationSec)
        }
        if (e.notes)
            lines.push("Notes: " + e.notes)
        if (e.completed)
            lines.push("Done")
        return lines.join("\n")
    }
    function startLabel(e) {
        var k = kindOf(e)
        return k === "freeride" ? "Start Free Ride" : k === "rlv" ? "Start Video Ride"
             : e.sport === "rowing" ? "Row this workout" : e.sport === "running" ? "Run this workout" : "Ride this workout"
    }

    // --- data ------------------------------------------------------------------------------------
    function refresh() {
        // Rebuilding the cells destroys the dragged chip (and with it the mouse
        // grab that would end the drag): wait until the drop.
        if (dragEntry !== null) {
            refreshPending = true
            return
        }
        refreshPending = false
        var now = new Date()
        todayIso = isoOf(now)
        if (!selectedDate)
            selectedDate = todayIso
        var first = new Date(viewYear, viewMonth - 1, 1)
        var offset = (first.getDay() + 6) % 7
        var start = new Date(viewYear, viewMonth - 1, 1 - offset)
        var end = new Date(viewYear, viewMonth - 1, 1 - offset + 41)
        var list = Planner.entriesBetween(isoOf(start), isoOf(end))
        var byDate = {}
        var stats = { planned: 0, completed: 0, rowing: 0, cycling: 0, seconds: 0 }
        for (var i = 0; i < list.length; ++i) {
            var e = list[i]
            if (!byDate[e.date])
                byDate[e.date] = []
            byDate[e.date].push(e)
            var ed = parseIso(e.date)
            if (ed.getMonth() === viewMonth - 1 && ed.getFullYear() === viewYear) {
                stats.planned++
                if (e.completed) stats.completed++
                if (e.sport === "rowing") stats.rowing++
                else if (e.sport === "running") stats.running = (stats.running || 0) + 1
                else stats.cycling++
                stats.seconds += e.durationSec
            }
        }
        var out = []
        for (var c = 0; c < 42; ++c) {
            var d = new Date(viewYear, viewMonth - 1, 1 - offset + c)
            var iso = isoOf(d)
            out.push({
                iso: iso,
                day: d.getDate(),
                inMonth: d.getMonth() === viewMonth - 1,
                isToday: iso === todayIso,
                isPast: iso < todayIso,
                entries: byDate[iso] || []
            })
        }
        cells = out
        monthStats = stats
        upcomingList = Planner.upcoming(8)
        dayEntries = Planner.entriesForDate(selectedDate)
    }

    function showMonth(year, month) {
        var m = month - 1
        viewYear = year + Math.floor(m / 12)
        viewMonth = ((m % 12) + 12) % 12 + 1
        refresh()
    }
    function stepMonth(delta) { showMonth(viewYear, viewMonth + delta) }
    function goToday() {
        var now = new Date()
        selectedDate = isoOf(now)
        showMonth(now.getFullYear(), now.getMonth() + 1)
    }
    // end a chip drag (drop, cancel, page hidden); runs a deferred refresh
    function endDrag() {
        dragEntry = null
        dropIso = ""
        if (refreshPending)
            Qt.callLater(refresh)
    }
    // single click on a chip: show it in the side panel (no month switch, so a
    // double-click on a chip of a neighbouring month still reaches the chip)
    function selectEntry(e) {
        if (!e)
            return
        selectedDate = e.date
        selectedEntryId = e.id
        dayEntries = Planner.entriesForDate(e.date)
    }
    function selectDay(iso) {
        if (iso !== selectedDate)
            selectedEntryId = -1
        selectedDate = iso
        var d = parseIso(iso)
        if (d.getFullYear() !== viewYear || d.getMonth() + 1 !== viewMonth)
            showMonth(d.getFullYear(), d.getMonth() + 1)
        else
            dayEntries = Planner.entriesForDate(iso)
    }

    // --- actions -----------------------------------------------------------------------------------
    // Rowing entries set up a rowing watt plan; cycling entries load the training page.
    // The planned entry is handed to the Planner first, so the backend ticks off
    // exactly this entry when the workout is completed (whatever its date).
    function markActive(e) {
        if (typeof Planner.setActiveEntry === "function")
            Planner.setActiveEntry(e.id)
    }
    function clearActive() {
        if (typeof Planner.clearActiveEntry === "function")
            Planner.clearActiveEntry()
    }
    function startEntry(e) {
        if (!e || e.workoutId === undefined)
            return
        if (e.kind !== undefined && e.kind !== "workout") {
            // Free Ride / Video Ride: the backend prepares the session
            var target = Planner.startEntry(e)
            if (target !== "") {
                goTo(target)
                // e.g. the attached workout of a Video Ride was deleted
                if (Planner.lastError.length > 0)
                    toast(Planner.lastError)
            } else {
                toast(Planner.lastError.length > 0 ? Planner.lastError : "Could not start \"" + e.name + "\"")
            }
            return
        }
        if (e.sport === "running") {
            if (Running.active) {
                toast("A treadmill run is in progress – finish it first")
                goTo("treadmill")
                return
            }
            markActive(e)
            if (Running.attachWorkout(e.workoutId))
                goTo("treadmill")
            else {
                clearActive()
                toast("Could not load \"" + e.name + "\" for a treadmill run")
            }
            return
        }
        if (Planner.isRowing(e)) {
            if (Rowing.active) {
                toast("A rowing session is in progress – finish it first")
                goTo("rowing")
                return
            }
            markActive(e)
            if (Rowing.setupPlan(e.workoutId))
                goTo("rowing")
            else {
                clearActive()
                toast("Could not load \"" + e.name + "\" as a rowing plan")
            }
        } else {
            if (Training.active) {
                toast("A workout is in progress – finish it first")
                goTo("dashboard")
                return
            }
            markActive(e)
            if (Training.loadWorkout(e.workoutId))
                goTo("dashboard")
            else {
                clearActive()
                toast("Could not load \"" + e.name + "\"")
            }
        }
    }
    function toggleCompleted(e) {
        Planner.setCompleted(e.id, !e.completed)
    }
    // asks first (the removal cannot be undone)
    function removeEntry(e) {
        if (!e || e.id === undefined)
            return
        removeConfirm.entry = e
        removeConfirm.message = "Remove \"" + e.name + "\" from " + longDate(e.date) + "?"
        removeConfirm.open()
    }
    function doRemoveEntry(e) {
        if (Planner.remove(e.id))
            toast("\"" + e.name + "\" removed from " + longDate(e.date))
    }
    function moveEntry(e, iso) {
        if (!e || !iso || iso === e.date)
            return
        if (Planner.move(e.id, iso))
            toast("\"" + e.name + "\" moved to " + longDate(iso))
    }
    function openEdit(e) {
        if (!e || kindOf(e) === "workout")
            return
        picker.openEdit(e)
    }
    function openMove(e) {
        movePicker.entry = e
        movePicker.subtitle = "Move \"" + e.name + "\" (" + longDate(e.date) + ")"
        movePicker.openFor(e.date)
    }
    function openMenu(e) {
        chipMenu.entry = e
        chipMenu.popup()
    }
    // screenshot aid (WATT_SCREENSHOT_MENU): menu of the first visible chip
    function openScreenshotMenu() {
        for (var i = 0; i < cells.length; ++i) {
            if (cells[i].entries && cells[i].entries.length > 0) {
                var p = calGrid.mapToItem(page, (i % 7 + 0.5) * calGrid.cellWidth, Math.floor(i / 7) * calGrid.cellHeight + 52)
                chipMenu.entry = cells[i].entries[0]
                chipMenu.popup(p.x, p.y)
                return chipMenu
            }
        }
        chipMenu.entry = { name: "Workout", sport: "cycling", date: selectedDate }
        chipMenu.popup(page.width / 3, page.height / 3)
        return chipMenu
    }

    // screenshot aid (WATT_SCREENSHOT_MENU=calendar-add, calendar-add-freeride, calendar-add-video)
    function openScreenshotAdd(kind) {
        if (kind === "edit") {   // the first Free Ride / Video Ride of the selected day
            var list = Planner.entriesForDate(selectedDate)
            for (var i = 0; i < list.length; ++i)
                if (kindOf(list[i]) !== "workout") {
                    openEdit(list[i])
                    return
                }
            console.warn("openScreenshotAdd: no Free Ride / Video Ride on", selectedDate)
            return
        }
        picker.openFor(selectedDate, kind)
        if (kind === "rlv" && picker.cyclingWorkouts.length > 1)
            picker.vrWorkoutId = picker.cyclingWorkouts[1].workoutId
    }
    function closeDialogs() {
        if (picker.visible) picker.close()
        if (movePicker.visible) movePicker.close()
        if (removeConfirm.visible) removeConfirm.close()
    }

    Component.onCompleted: goToday()
    onVisibleChanged: {
        if (visible) {
            refresh()
            return
        }
        // leaving the page: nothing of it may stay open over another page
        closeDialogs()
        chipMenu.close()
        if (dragEntry !== null)
            endDrag()
    }

    Connections {
        target: Planner
        function onPlanChanged() { page.refresh() }
    }
    Connections {
        target: Profiles
        function onProfileChanged() { page.refresh() }
    }
    // roll over to the next day while the app stays open
    Timer {
        interval: 60000
        repeat: true
        running: page.visible
        onTriggered: if (page.isoOf(new Date()) !== page.todayIso) page.refresh()
    }

    // --- header -----------------------------------------------------------------------------------
    PageHeader {
        x: Theme.pageMargin + 8
        y: 30
        title: "Calendar"
        subtitle: "Plan your rides and rows"
    }

    RowLayout {
        id: toolbar
        x: Theme.pageMargin
        y: 146
        width: parent.width - 2 * Theme.pageMargin
        height: 54
        spacing: 10

        IconButton {
            Layout.preferredWidth: 50
            Layout.fillHeight: true
            iconName: "chevronLeft"
            iconSize: 24
            tooltip: "Previous month"
            onClicked: page.stepMonth(-1)
        }
        Text {
            Layout.preferredWidth: 270
            horizontalAlignment: Text.AlignHCenter
            text: page.monthNames[page.viewMonth - 1] + " " + page.viewYear
            color: Theme.text
            font.pixelSize: 30
            font.weight: Font.Bold
        }
        IconButton {
            Layout.preferredWidth: 50
            Layout.fillHeight: true
            iconName: "chevronRight"
            iconSize: 24
            tooltip: "Next month"
            onClicked: page.stepMonth(1)
        }
        AppButton {
            Layout.fillHeight: true
            kind: "secondary"
            bold: false
            fontSize: 17
            horizontalPadding: 18
            text: "Today"
            onClicked: page.goToday()
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            text: page.monthStats.planned === 0 ? "Nothing planned this month"
                  : page.monthStats.planned + " planned  •  " + page.monthStats.completed + " done  •  "
                    + page.durationText(page.monthStats.seconds)
            color: Theme.textSecondary
            font.pixelSize: 16
            elide: Text.ElideRight
        }
        // legend
        Row {
            spacing: 16
            Repeater {
                model: [{ k: "workout", s: "cycling", t: "Cycling" }, { k: "workout", s: "rowing", t: "Rowing" },
                        { k: "workout", s: "running", t: "Running" },
                        { k: "freeride", s: "cycling", t: "Free Ride" }, { k: "rlv", s: "cycling", t: "Video" }]
                Row {
                    spacing: 6
                    Icon {
                        name: Theme.kindIcon(modelData.k, modelData.s)
                        size: 18
                        strokeWidth: 2
                        color: Theme.kindColor(modelData.k, modelData.s)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text { text: modelData.t; color: Theme.textSecondary; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Row {
                spacing: 6
                Icon { name: "check"; size: 18; strokeWidth: 2.4; color: Theme.successText; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Done"; color: Theme.textSecondary; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
            }
        }
        AppButton {
            Layout.fillHeight: true
            Layout.leftMargin: 8
            horizontalPadding: 18
            iconSpacing: 8
            iconName: "plus"
            text: "Add"
            onClicked: picker.openFor(page.selectedDate)
        }
    }

    // --- month grid ---------------------------------------------------------------------------------
    Card {
        id: calCard
        x: Theme.pageMargin
        y: 214
        width: parent.width - 2 * Theme.pageMargin - side.width - Theme.spacing
        height: parent.height - y - Theme.pageMargin
        padding: 10

        Row {
            id: weekdayRow
            x: 10
            y: 10
            Repeater {
                model: page.weekdayNames
                Text {
                    width: calGrid.cellWidth
                    height: 26
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    color: index >= 5 ? Theme.textMuted : Theme.textSecondary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
            }
        }

        Grid {
            id: calGrid
            x: 10
            y: 40
            width: parent.width - 20
            height: parent.height - y - 10
            columns: 7
            readonly property real cellWidth: width / 7
            readonly property real cellHeight: height / 6

            Repeater {
                model: page.cells
                Item {
                    id: dayCell
                    readonly property var cell: modelData
                    width: calGrid.cellWidth
                    height: calGrid.cellHeight
                    readonly property string iso: cell.iso || ""
                    readonly property var entries: cell.entries || []
                    readonly property bool selected: iso === page.selectedDate
                    readonly property bool dropTarget: page.dragEntry !== null && page.dropIso === iso
                    readonly property int chipRoom: Math.max(1, Math.floor((height - 34) / 39))
                    readonly property int shownChips: entries.length > chipRoom ? chipRoom - 1 : entries.length

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 8
                        color: dayCell.dropTarget ? Theme.navSelected
                             : dayCell.selected ? Theme.cardStrong
                             : (hover.hovered ? Theme.hover : (dayCell.cell.inMonth ? "transparent" : Qt.rgba(0, 0, 0, 0.08)))
                        border.width: dayCell.dropTarget || dayCell.selected ? 2 : 1
                        border.color: dayCell.dropTarget ? Theme.accentLight : (dayCell.selected ? Theme.accent : Theme.cardBorder)
                        clip: true

                        HoverHandler { id: hover }

                        // click: select the day (side panel); double-click / "+": add a workout
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.selectDay(dayCell.iso)
                            onDoubleClicked: picker.openFor(dayCell.iso)
                        }

                        // day number (today = filled circle)
                        Rectangle {
                            x: 6
                            y: 5
                            width: 28; height: 26; radius: 13
                            color: dayCell.cell.isToday ? Theme.accent : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: dayCell.cell.day || ""
                                color: dayCell.cell.isToday ? "white"
                                     : (dayCell.cell.inMonth ? (dayCell.cell.isPast ? Theme.textSecondary : Theme.text) : Theme.textMuted)
                                font.pixelSize: 15
                                font.weight: dayCell.cell.isToday ? Font.Bold : Font.DemiBold
                            }
                        }
                        Text {
                            visible: dayCell.cell.isToday && dayCell.width > 110
                            x: 40
                            y: 9
                            text: "Today"
                            color: Theme.accentText
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }

                        IconButton {
                            visible: hover.hovered || dayCell.selected
                            anchors.right: parent.right
                            anchors.rightMargin: 3
                            y: 3
                            flat: true
                            iconName: "plus"
                            iconSize: 16
                            tooltip: "Add to this day"
                            onClicked: picker.openFor(dayCell.iso)
                        }

                        Column {
                            x: 5
                            y: 33
                            width: parent.width - 10
                            spacing: 3
                            Repeater {
                                model: dayCell.entries.slice(0, dayCell.shownChips)
                                Rectangle {
                                    id: chip
                                    readonly property var entry: modelData
                                    width: parent.width
                                    readonly property bool rowing: entry.sport === "rowing"
                                    readonly property color sportColor: page.entryColor(entry)
                                    readonly property bool missing: page.routeMissing(entry)
                                    readonly property bool done: entry.completed === true
                                    readonly property bool chosen: entry.id === page.selectedEntryId
                                    property bool dragging: false
                                    property bool suppressClick: false   // the click that ends a drag
                                    property real pressX: 0
                                    property real pressY: 0

                                    height: 36
                                    radius: 6
                                    // completed: lighter fill + check + strike-through, but the
                                    // text keeps full contrast (no dimming of the whole chip)
                                    color: Qt.rgba(sportColor.r, sportColor.g, sportColor.b,
                                                   chipMouse.containsMouse ? 0.42 : (done ? 0.13 : 0.26))
                                    border.width: chosen ? 2 : 0
                                    border.color: Theme.accent
                                    opacity: page.dragEntry && page.dragEntry.id === entry.id ? 0.35 : 1

                                    Rectangle {
                                        width: 3
                                        height: parent.height
                                        radius: 1.5
                                        color: chip.sportColor
                                    }
                                    Icon {
                                        id: chipIcon
                                        x: 7
                                        y: 4
                                        name: chip.entry.completed ? "check" : page.entryIcon(chip.entry)
                                        size: 14
                                        strokeWidth: chip.entry.completed ? 2.6 : 2
                                        color: chip.done ? Theme.successText : chip.sportColor
                                    }
                                    Text {
                                        anchors.left: chipIcon.right
                                        anchors.leftMargin: 4
                                        anchors.right: chipWarn.visible ? chipWarn.left : parent.right
                                        anchors.rightMargin: 4
                                        y: 3
                                        text: chip.entry.name || ""
                                        color: Theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        font.strikeout: chip.entry.completed === true
                                        elide: Text.ElideRight
                                    }
                                    Icon {
                                        id: chipWarn
                                        visible: chip.missing
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        y: 4
                                        name: "warning"
                                        size: 14
                                        strokeWidth: 2.2
                                        color: Theme.warningText
                                    }
                                    Text {
                                        id: durLabel
                                        x: 9
                                        y: 19
                                        width: parent.width - 13
                                        elide: Text.ElideRight
                                        text: (chip.missing ? "Route not found" : page.chipSubText(chip.entry)) + (chip.done ? " · done" : "")
                                        color: chip.done ? Theme.successText : Theme.textSecondary
                                        font.pixelSize: 11
                                    }

                                    MouseArea {
                                        id: chipMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: chip.dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                        preventStealing: true
                                        pressAndHoldInterval: 600

                                        onPressed: (mouse) => {
                                            chip.dragging = false
                                            chip.suppressClick = false
                                            chip.pressX = mouse.x
                                            chip.pressY = mouse.y
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!(mouse.buttons & Qt.LeftButton))
                                                return
                                            if (!chip.dragging && Math.abs(mouse.x - chip.pressX) + Math.abs(mouse.y - chip.pressY) > 8) {
                                                chip.dragging = true
                                                page.dragEntry = chip.entry
                                                ghost.entry = chip.entry
                                                ghost.width = chip.width
                                            }
                                            if (chip.dragging) {
                                                var p = chipMouse.mapToItem(page, mouse.x, mouse.y)
                                                ghost.x = p.x - chip.pressX
                                                ghost.y = p.y - chip.pressY
                                                var g = chipMouse.mapToItem(calGrid, mouse.x, mouse.y)
                                                var target = calGrid.childAt(g.x, g.y)
                                                page.dropIso = target && target.iso !== undefined ? target.iso : ""
                                            }
                                        }
                                        onReleased: {
                                            if (chip.dragging) {
                                                chip.dragging = false
                                                chip.suppressClick = true
                                                var e = page.dragEntry
                                                var iso = page.dropIso
                                                page.endDrag()
                                                // after this handler: the move rebuilds the cells (and this chip)
                                                Qt.callLater(page.moveEntry, e, iso)
                                            }
                                        }
                                        onCanceled: {
                                            if (chip.dragging) {
                                                chip.dragging = false
                                                page.endDrag()
                                            }
                                        }
                                        // click: select (side panel); double-click: ride / row it
                                        onClicked: (mouse) => {
                                            if (chip.suppressClick) {
                                                chip.suppressClick = false
                                                return
                                            }
                                            if (mouse.button === Qt.RightButton)
                                                page.openMenu(chip.entry)
                                            else
                                                page.selectEntry(chip.entry)
                                        }
                                        onDoubleClicked: (mouse) => {
                                            if (mouse.button === Qt.LeftButton && !chip.suppressClick)
                                                page.startEntry(chip.entry)
                                        }
                                        onPressAndHold: (mouse) => {
                                            if (!chip.dragging)
                                                page.openMenu(chip.entry)
                                        }
                                    }

                                    ToolTip.visible: chipMouse.containsMouse && !chip.dragging && page.dragEntry === null
                                    ToolTip.delay: 700
                                    ToolTip.text: page.entryDetails(chip.entry)
                                                  + "\nClick to select · double-click to " + (chip.rowing ? "row" : "ride")
                                                  + " · right-click for options · drag to move"
                                }
                            }
                            Text {
                                visible: dayCell.entries.length > dayCell.shownChips
                                width: parent.width
                                height: 20
                                leftPadding: 4
                                verticalAlignment: Text.AlignVCenter
                                text: "+" + (dayCell.entries.length - dayCell.shownChips) + " more"
                                color: Theme.accentText
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
        }
    }

    // ghost chip that follows the pointer while dragging
    Rectangle {
        id: ghost
        property var entry: ({})
        z: 500
        visible: page.dragEntry !== null
        height: 24
        radius: 6
        color: page.entryColor(entry)
        opacity: 0.9
        Text {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 6
            verticalAlignment: Text.AlignVCenter
            text: ghost.entry.name || ""
            color: Theme.textOnColor
            font.pixelSize: 12
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
    }

    // --- side panel -------------------------------------------------------------------------------------
    Item {
        id: side
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin
        y: calCard.y
        width: 384
        height: calCard.height

        Card {
            id: dayCard
            width: parent.width
            height: Math.round(parent.height * 0.52)
            title: page.selectedDate === page.todayIso ? "Today" : page.longDate(page.selectedDate)
            titleSize: 20
            headerRight: [
                IconButton {
                    flat: true
                    iconName: "plus"
                    iconSize: 22
                    tooltip: "Add a workout, Free Ride or Video Ride to this day"
                    onClicked: picker.openFor(page.selectedDate)
                }
            ]

            Text {
                x: 18
                y: 46
                visible: page.selectedDate === page.todayIso
                text: page.longDate(page.selectedDate)
                color: Theme.textSecondary
                font.pixelSize: 14
            }

            ListView {
                id: dayList
                x: 10
                y: 72
                width: parent.width - 20
                height: parent.height - y - 10
                clip: true
                spacing: 8
                model: page.dayEntries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: dayRow
                    readonly property var entry: modelData
                    readonly property color sportColor: page.entryColor(entry)
                    readonly property bool chosen: entry.id === page.selectedEntryId
                    readonly property bool editable: page.kindOf(entry) !== "workout"
                    readonly property bool missing: page.routeMissing(entry)
                    width: ListView.view.width
                    height: 104
                    radius: 10
                    color: chosen ? Theme.navSelected : Theme.cardStrong
                    border.color: chosen ? Theme.accent : Theme.cardBorder
                    border.width: chosen ? 2 : 1

                    // click selects the entry; double-click starts it
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.selectedEntryId = dayRow.entry.id
                        onDoubleClicked: page.startEntry(dayRow.entry)
                    }

                    Rectangle {
                        x: 12
                        y: 12
                        width: 40; height: 40; radius: 10
                        color: Qt.rgba(dayRow.sportColor.r, dayRow.sportColor.g, dayRow.sportColor.b, 0.22)
                        Icon {
                            anchors.centerIn: parent
                            name: page.entryIcon(dayRow.entry)
                            size: 24
                            strokeWidth: 2
                            color: dayRow.sportColor
                        }
                    }
                    Text {
                        x: 64
                        y: 10
                        width: parent.width - 64 - 12
                        text: dayRow.entry.name
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.Bold
                        font.strikeout: dayRow.entry.completed === true
                        elide: Text.ElideRight
                    }
                    Row {
                        x: 64
                        y: 34
                        width: parent.width - 64 - 12
                        spacing: 6
                        // a Video Ride whose route is not in the video library
                        Rectangle {
                            id: missingBadge
                            visible: dayRow.missing
                            width: missingRow.implicitWidth + 12
                            height: 20
                            radius: 6
                            color: Qt.rgba(1, 0.82, 0.12, Theme.dark ? 0.18 : 0.22)
                            border.color: Theme.warningText
                            Row {
                                id: missingRow
                                anchors.centerIn: parent
                                spacing: 4
                                Icon { name: "warning"; size: 13; strokeWidth: 2.2; color: Theme.warningText; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "Route not found"; color: Theme.warningText; font.pixelSize: 12; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                        Text {
                            width: parent.width - (missingBadge.visible ? missingBadge.width + 6 : 0)
                            text: page.entrySubtitle(dayRow.entry)
                            color: dayRow.entry.completed ? Theme.successText : Theme.textSecondary
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                    }
                    ToolTip.visible: dayHover.hovered && page.dragEntry === null
                    ToolTip.delay: 900
                    ToolTip.text: page.entryDetails(dayRow.entry)
                    HoverHandler { id: dayHover }
                    Row {
                        x: 12
                        y: 60
                        spacing: 6
                        AppButton {
                            width: dayRow.editable ? 104 : 128
                            height: 36
                            fontSize: 15
                            iconSize: 16
                            iconSpacing: 8
                            horizontalPadding: 10
                            iconName: "play"
                            text: dayRow.entry.sport === "rowing" ? "Row" : dayRow.entry.sport === "running" ? "Run" : "Ride"
                            kind: dayRow.entry.completed ? "secondary" : "primary"
                            onClicked: page.startEntry(dayRow.entry)
                        }
                        IconButton {
                            width: 40; height: 36
                            iconName: "check"
                            iconSize: 18
                            iconColor: dayRow.entry.completed ? Theme.successText : Theme.text
                            tooltip: dayRow.entry.completed ? "Mark as not completed" : "Mark as completed"
                            onClicked: page.toggleCompleted(dayRow.entry)
                        }
                        IconButton {
                            visible: dayRow.editable
                            width: 40; height: 36
                            iconName: "edit"
                            iconSize: 18
                            tooltip: "Edit"
                            onClicked: page.openEdit(dayRow.entry)
                        }
                        IconButton {
                            width: 40; height: 36
                            iconName: "calendar"
                            iconSize: 18
                            tooltip: "Move to another day"
                            onClicked: page.openMove(dayRow.entry)
                        }
                        IconButton {
                            width: 40; height: 36
                            iconName: "trash"
                            iconSize: 18
                            tooltip: "Remove from calendar"
                            onClicked: page.removeEntry(dayRow.entry)
                        }
                    }
                }

                Column {
                    visible: dayList.count === 0
                    anchors.centerIn: parent
                    width: parent.width - 20
                    spacing: 12
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: page.selectedDate < page.todayIso ? "Nothing was planned for this day" : "Nothing planned yet"
                        color: Theme.textSecondary
                        font.pixelSize: 16
                    }
                    AppButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: 44
                        kind: "dashed"
                        fontSize: 16
                        iconSize: 18
                        iconSpacing: 8
                        iconName: "plus"
                        text: "Add to Calendar"
                        onClicked: picker.openFor(page.selectedDate)
                    }
                }
            }
        }

        Card {
            id: upcomingCard
            y: dayCard.height + Theme.spacing
            width: parent.width
            height: parent.height - y
            title: "Upcoming"
            titleSize: 20

            ListView {
                id: upList
                x: 10
                y: 52
                width: parent.width - 20
                height: parent.height - y - 10
                clip: true
                spacing: 4
                model: page.upcomingList
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: upRow
                    readonly property var entry: modelData
                    width: ListView.view.width
                    height: 52
                    radius: 8
                    color: upMouse.containsMouse ? Theme.hover : "transparent"

                    MouseArea {
                        id: upMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // click: show it in the day panel; double-click: start it
                        onClicked: {
                            page.selectDay(upRow.entry.date)
                            page.selectedEntryId = upRow.entry.id
                        }
                        onDoubleClicked: page.startEntry(upRow.entry)
                    }
                    Text {
                        x: 8
                        width: 86
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.relativeDate(upRow.entry.date)
                        color: upRow.entry.isToday ? Theme.accentText : Theme.textSecondary
                        font.pixelSize: 14
                        font.weight: upRow.entry.isToday ? Font.Bold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Icon {
                        x: 98
                        anchors.verticalCenter: parent.verticalCenter
                        name: page.routeMissing(upRow.entry) ? "warning" : page.entryIcon(upRow.entry)
                        size: 20
                        strokeWidth: 2
                        color: page.routeMissing(upRow.entry) ? Theme.warningText : page.entryColor(upRow.entry)
                    }
                    Column {
                        x: 128
                        width: parent.width - 128 - 48
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            width: parent.width
                            text: upRow.entry.name
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: page.routeMissing(upRow.entry) ? "Route not found" : page.entrySubtitle(upRow.entry)
                            color: Theme.textMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    IconButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        flat: true
                        iconName: "play"
                        iconSize: 18
                        iconColor: Theme.accentText
                        tooltip: page.startLabel(upRow.entry)
                        onClicked: page.startEntry(upRow.entry)
                    }
                }

                Text {
                    visible: upList.count === 0
                    anchors.centerIn: parent
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "Nothing planned yet.\nDouble-click a day to plan a workout, Free Ride or Video Ride."
                    color: Theme.textSecondary
                    font.pixelSize: 15
                }
            }
        }
    }

    // --- context menu for a planned workout ------------------------------------------------------------------
    AppMenu {
        id: chipMenu
        property var entry: ({})
        minWidth: 260
        AppMenuItem {
            text: page.startLabel(chipMenu.entry)
            iconName: "play"
            iconColor: page.entryColor(chipMenu.entry)
            onTriggered: page.startEntry(chipMenu.entry)
        }
        AppMenuItem {
            visible: page.kindOf(chipMenu.entry) !== "workout"
            text: "Edit…"
            iconName: "edit"
            onTriggered: page.openEdit(chipMenu.entry)
        }
        AppMenuItem {
            text: chipMenu.entry.completed ? "Mark as not completed" : "Mark as completed"
            iconName: chipMenu.entry.completed ? "close" : "check"
            onTriggered: page.toggleCompleted(chipMenu.entry)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Move to…"
            iconName: "calendar"
            onTriggered: page.openMove(chipMenu.entry)
        }
        AppMenuItem {
            text: "Move to next day"
            iconName: "chevronRight"
            onTriggered: page.moveEntry(chipMenu.entry, page.addDays(chipMenu.entry.date, 1))
        }
        AppMenuItem {
            text: "Show day"
            iconName: "calendarDays"
            onTriggered: page.selectDay(chipMenu.entry.date)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Remove from calendar"
            iconName: "trash"
            destructive: true
            onTriggered: page.removeEntry(chipMenu.entry)
        }
    }

    // --- move dialog ---------------------------------------------------------------------------------------
    DatePickerDialog {
        id: movePicker
        parent: page.dialogHost
        property var entry: ({})
        title: "Move entry"
        acceptText: "Move"
        onPicked: (iso) => page.moveEntry(movePicker.entry, iso)
    }

    // --- remove confirmation ---------------------------------------------------------------------------------
    ConfirmDialog {
        id: removeConfirm
        parent: page.dialogHost
        property var entry: ({})
        title: "Remove from calendar?"
        acceptText: "Remove"
        acceptKind: "danger"
        onAccepted: page.doRemoveEntry(removeConfirm.entry)
    }

    // --- add / edit dialog: Workout | Free Ride | Video Ride ------------------------------------------------
    AppDialog {
        id: picker
        parent: page.dialogHost
        property string targetIso
        property string kind: "workout"          // "workout" | "freeride" | "rlv"
        property var editEntry: null             // Free Ride / Video Ride entry being edited (null = add)
        readonly property bool editing: editEntry !== null
        // workout tab
        property string sport: "all"
        property string search: ""
        property var items: []
        property var chosenId: -1
        // free ride tab (duration / watts live in the spin boxes)
        property string frMode: "ride"
        // video ride tab
        property string routePath: ""
        property string vrMode: "sim"
        property var vrWorkoutId: -1
        property bool vrWorkoutErg: true
        property var cyclingWorkouts: []         // [{ workoutId, name, label }], first = none
        readonly property int routeRow: {
            var n = Videos.count               // re-evaluate when the library changes
            return routePath.length > 0 && n >= 0 ? Videos.indexOfPath(routePath) : -1
        }
        readonly property bool canSave: kind === "workout" ? chosenId >= 0
                                        : kind === "freeride" ? true
                                        : routePath.length > 0 && (routeRow >= 0 || editing)

        title: editing ? (kind === "freeride" ? "Edit Free Ride" : "Edit Video Ride") : "Add to Calendar"
        panelWidth: 840
        panelHeight: 740

        function defaultWatts() {
            var f = Profiles.ftp > 0 ? Profiles.ftp : 200
            return Math.max(25, Math.round(f * 0.6 / 5) * 5)
        }
        function firstValidRoute() {
            for (var i = 0; i < Videos.count; ++i) {
                var r = Videos.get(i)
                if (r.valid)
                    return r.path
            }
            return ""
        }
        function resetFields() {
            chosenId = -1
            searchField.text = ""
            notesField.text = ""
            frMode = "ride"
            frMinutes.value = 60
            frWatts.value = FreeRide.targetWatts > 0 ? FreeRide.targetWatts : defaultWatts()
            routePath = Rlv.hasRoute ? Rlv.routePath : firstValidRoute()
            vrMode = "sim"
            vrDifficulty.value = 100
            vrWatts.value = Rlv.targetWatts > 0 ? Rlv.targetWatts : defaultWatts()
            vrWorkoutId = -1
            vrWorkoutErg = true
        }
        function openFor(iso, k) {
            editEntry = null
            targetIso = iso || page.todayIso
            resetFields()
            kind = k === "freeride" || k === "rlv" ? k : "workout"
            reload()
            reloadCycling()
            open()
        }
        function openEdit(e) {
            resetFields()
            editEntry = e
            targetIso = e.date
            kind = page.kindOf(e)
            notesField.text = e.notes || ""
            var o = e.options || {}
            if (kind === "freeride") {
                frMinutes.value = Math.round((e.plannedDurationSec || 0) / 60)
                frMode = o.mode === "erg" ? "erg" : "ride"
                if (o.ergWatts > 0)
                    frWatts.value = o.ergWatts
            } else {
                routePath = e.routePath || ""
                vrMode = o.mode === "erg" ? "erg" : "sim"
                vrDifficulty.value = o.difficulty !== undefined ? o.difficulty : 100
                if (o.ergWatts > 0)
                    vrWatts.value = o.ergWatts
                vrWorkoutId = e.workoutId >= 0 ? e.workoutId : -1
                vrWorkoutErg = o.workoutErg !== false
            }
            reloadCycling()
            open()
        }
        function reload() {
            var all = Workouts.catalog(sport)
            var needle = search.trim().toLowerCase()
            var out = []
            var chosenFound = false
            for (var i = 0; i < all.length; ++i) {
                var w = all[i]
                if (w.workoutId === chosenId)
                    chosenFound = true
                if (needle.length === 0 || w.name.toLowerCase().indexOf(needle) >= 0
                        || w.category.toLowerCase().indexOf(needle) >= 0)
                    out.push(w)
            }
            items = out
            if (!chosenFound)
                chosenId = -1   // the chosen workout was deleted
        }
        // cycling workouts that can be ridden during a video
        function reloadCycling() {
            var all = Workouts.catalog("cycling")
            var out = [{ workoutId: -1, name: "", label: "None – just ride the route" }]
            var found = vrWorkoutId < 0
            for (var i = 0; i < all.length; ++i) {
                var w = all[i]
                if (!(w.totalSec > 0))
                    continue
                if (w.workoutId === vrWorkoutId)
                    found = true
                out.push({ workoutId: w.workoutId, name: w.name,
                           label: w.name + "  ·  " + page.durationText(w.totalSec) + (w.summary ? "  ·  " + w.summary : "") })
            }
            cyclingWorkouts = out
            if (!found)
                vrWorkoutId = -1
        }
        function workoutIndex(id) {
            for (var i = 0; i < cyclingWorkouts.length; ++i)
                if (cyclingWorkouts[i].workoutId === id)
                    return i
            return 0
        }
        function sameRoute(a, b) {
            if (!a || !b)
                return false
            if (a === b)
                return true
            var ia = Videos.indexOfPath(a)
            return ia >= 0 && ia === Videos.indexOfPath(b)
        }
        function add(workoutId) {
            if (workoutId === undefined || workoutId < 0)
                return
            var id = Planner.addWorkout(targetIso, workoutId, notesField.text.trim())
            if (id >= 0) {
                var w = Workouts.get(workoutId)
                close()
                page.selectDay(targetIso)
                page.toast("\"" + (w.name || "Workout") + "\" planned for " + page.longDate(targetIso))
            } else {
                page.toast("Could not add the workout to the calendar")
            }
        }
        function save() {
            if (kind === "workout") {
                add(chosenId)
                return
            }
            if (!canSave)
                return
            var notes = notesField.text.trim()
            var wasEditing = editing
            var entry = editEntry
            var id = wasEditing ? entry.id : -1
            var ok = false
            var label = kind === "freeride" ? "Free Ride" : "Video Ride"
            if (kind === "freeride") {
                var fo = { mode: frMode, ergWatts: frMode === "erg" ? Math.round(frWatts.value) : 0 }
                var minutes = Math.round(frMinutes.value)
                if (wasEditing)
                    ok = Planner.updateEntry(id, fo, minutes)
                else {
                    id = Planner.addFreeRide(targetIso, minutes, fo, notes)
                    ok = id >= 0
                }
            } else {
                var vo = { mode: vrMode, difficulty: Math.round(vrDifficulty.value),
                           ergWatts: vrMode === "erg" ? Math.round(vrWatts.value) : 0,
                           workoutId: vrWorkoutId, workoutErg: vrWorkoutErg }
                if (wasEditing && sameRoute(entry.routePath, routePath)) {
                    ok = Planner.updateEntry(id, vo)
                } else {
                    // a new route: replace the entry (the route of an entry is fixed)
                    var nid = Planner.addVideoRide(targetIso, routePath, vo, notes)
                    ok = nid >= 0
                    if (ok && wasEditing) {
                        if (entry.completed)
                            Planner.setCompleted(nid, true)
                        Planner.remove(entry.id)
                        wasEditing = false   // date and notes are already right
                    }
                    id = nid
                }
            }
            if (!ok) {
                page.toast(Planner.lastError.length > 0 ? Planner.lastError : "Could not save the " + label)
                return
            }
            if (wasEditing) {
                if (notes !== (entry.notes || ""))
                    Planner.setNotes(id, notes)
                if (targetIso !== entry.date)
                    Planner.move(id, targetIso)
            }
            close()
            page.selectDay(targetIso)
            page.selectedEntryId = id
            page.toast(entry ? label + " updated" : label + " planned for " + page.longDate(targetIso))
        }
        onSportChanged: reload()
        onSearchChanged: reload()
        onClosed: editEntry = null

        // workouts saved/imported/deleted (or the FTP changed) while the dialog is open
        Connections {
            target: Workouts
            enabled: picker.visible
            function onCountChanged() { picker.reload(); picker.reloadCycling() }
        }
        Connections {
            target: Profiles
            enabled: picker.visible
            function onProfileChanged() { picker.reload(); picker.reloadCycling() }
        }

        // what to plan
        SegmentedControl {
            visible: !picker.editing
            anchors.right: parent.right
            anchors.rightMargin: 72
            y: 20
            height: 48
            fontSize: 15
            segmentPadding: 13
            value: picker.kind
            options: [
                { value: "workout", label: "Workout", icon: "bike" },
                { value: "freeride", label: "Free Ride", icon: "freeRide", color: Theme.freeRideColor, textColor: Theme.textOnColor },
                { value: "rlv", label: "Video Ride", icon: "video", color: Theme.videoColor }
            ]
            onActivated: (v) => picker.kind = v
        }

        // target day
        Row {
            x: 26
            y: 70
            spacing: 6
            IconButton {
                flat: true
                iconName: "chevronLeft"
                iconSize: 20
                tooltip: "Previous day"
                onClicked: picker.targetIso = page.addDays(picker.targetIso, -1)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "for " + page.longDate(picker.targetIso) + " " + (picker.targetIso.length > 0 ? page.parseIso(picker.targetIso).getFullYear() : "")
                color: Theme.textSecondary
                font.pixelSize: 17
            }
            IconButton {
                flat: true
                iconName: "chevronRight"
                iconSize: 20
                tooltip: "Next day"
                onClicked: picker.targetIso = page.addDays(picker.targetIso, 1)
            }
        }

        // ---- workout ----
        Item {
            anchors.fill: parent
            visible: picker.kind === "workout"

            RowLayout {
                x: 28
                y: 112
                width: picker.panelWidth - 56
                height: 50
                spacing: 12
                AppTextField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    iconName: "search"
                    placeholderText: "Search workouts..."
                    onTextChanged: picker.search = text
                }
                SegmentedControl {
                    Layout.fillHeight: true
                    value: picker.sport
                    options: [
                        { value: "all", label: "All" },
                        { value: "cycling", label: "Cycling", icon: "bike" },
                        { value: "rowing", label: "Rowing", icon: "rowing", color: Theme.rowingColor, textColor: Theme.textOnColor },
                        { value: "running", label: "Running", icon: "run", color: Theme.runningColor, textColor: Theme.textOnColor }
                    ]
                    onActivated: (v) => picker.sport = v
                }
            }

            ListView {
                id: pickList
                x: 20
                y: 176
                width: picker.panelWidth - 40
                height: picker.panelHeight - y - 162
                clip: true
                spacing: 4
                model: picker.items
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: pickRow
                    readonly property bool chosen: modelData.workoutId === picker.chosenId
                    readonly property color sportColor: Theme.sportColor(modelData.sport)
                    width: ListView.view.width
                    height: 62
                    radius: 10
                    color: chosen ? Theme.navSelected : (pickMouse.containsMouse ? Theme.hover : "transparent")
                    border.width: chosen ? 2 : 0
                    border.color: Theme.accent

                    MouseArea {
                        id: pickMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: picker.chosenId = modelData.workoutId
                        onDoubleClicked: picker.add(modelData.workoutId)
                    }
                    Rectangle {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 42; height: 42; radius: 10
                        color: Qt.rgba(pickRow.sportColor.r, pickRow.sportColor.g, pickRow.sportColor.b, 0.22)
                        Icon {
                            anchors.centerIn: parent
                            name: Theme.sportIcon(modelData.sport)
                            size: 24
                            strokeWidth: 2
                            color: pickRow.sportColor
                        }
                    }
                    Column {
                        x: 64
                        width: parent.width - 64 - pickBadges.width - 24
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            width: parent.width
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 17
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.summary
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                    Row {
                        id: pickBadges
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        SportBadge {
                            sport: modelData.sport
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        CategoryBadge {
                            category: modelData.category
                            anchors.verticalCenter: parent.verticalCenter
                            implicitHeight: 24
                        }
                        Text {
                            width: 64
                            horizontalAlignment: Text.AlignRight
                            text: page.durationText(modelData.totalSec)
                            color: Theme.text
                            font.pixelSize: 15
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                Text {
                    visible: pickList.count === 0
                    anchors.centerIn: parent
                    text: picker.sport === "rowing" && picker.search.length === 0
                          ? "No rowing workouts yet – create one in the Workout Builder"
                          : "No workouts match your filter"
                    color: Theme.textSecondary
                    font.pixelSize: 16
                }
            }
        }

        // ---- free ride ----
        Item {
            anchors.fill: parent
            visible: picker.kind === "freeride"

            Column {
                x: 28
                y: 118
                width: picker.panelWidth - 56
                spacing: 12

                Text {
                    text: "Duration"
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.Bold
                }
                Row {
                    spacing: 10
                    UnitSpinBox {
                        id: frMinutes
                        width: 190
                        from: 0
                        to: 1440
                        stepSize: 5
                        unit: "min"
                        value: 60
                    }
                    Repeater {
                        model: [30, 45, 60, 90, 120]
                        AppButton {
                            height: 46
                            fontSize: 15
                            horizontalPadding: 14
                            bold: Math.round(frMinutes.value) === modelData
                            kind: Math.round(frMinutes.value) === modelData ? "primary" : "secondary"
                            text: modelData + " min"
                            onClicked: frMinutes.value = modelData
                        }
                    }
                    AppButton {
                        height: 46
                        fontSize: 15
                        horizontalPadding: 14
                        bold: Math.round(frMinutes.value) === 0
                        kind: Math.round(frMinutes.value) === 0 ? "primary" : "secondary"
                        text: "Open"
                        onClicked: frMinutes.value = 0
                    }
                }
                Text {
                    width: parent.width
                    text: Math.round(frMinutes.value) > 0
                          ? "A target, not a limit: the Free Ride page shows the time left, nothing stops. "
                            + "The entry is ticked off by a saved Free Ride of at least " + Math.ceil(frMinutes.value / 2) + " min."
                          : "No planned duration: a saved Free Ride of at least 10 min ticks the entry off."
                    color: Theme.textSecondary
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }

                Item { width: 1; height: 8 }
                Text {
                    text: "Resistance"
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.Bold
                }
                Row {
                    spacing: 16
                    SegmentedControl {
                        height: 48
                        fontSize: 15
                        value: picker.frMode
                        options: [
                            { value: "ride", label: "Ride", icon: "mountain" },
                            { value: "erg", label: "ERG", icon: "bolt" }
                        ]
                        onActivated: (v) => picker.frMode = v
                    }
                    Text {
                        visible: picker.frMode === "erg"
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Target"
                        color: Theme.textSecondary
                        font.pixelSize: 16
                    }
                    UnitSpinBox {
                        id: frWatts
                        visible: picker.frMode === "erg"
                        anchors.verticalCenter: parent.verticalCenter
                        width: 190
                        from: 25
                        to: 1500
                        stepSize: 5
                        unit: "W"
                        value: 150
                    }
                }
                Text {
                    width: parent.width
                    text: picker.frMode === "erg"
                          ? "ERG: the trainer holds the target watts – change them during the ride with ↑ / ↓ or the Zwift Click."
                          : "Ride: like on the road – shift virtual gears (Zwift Click / ↑ ↓) or set a gradient on the Free Ride page."
                    color: Theme.textSecondary
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---- video ride ----
        Item {
            anchors.fill: parent
            visible: picker.kind === "rlv"

            Text {
                x: 28
                y: 116
                text: "Route"
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.Bold
            }
            Text {
                x: 100
                y: 119
                width: 340
                visible: picker.editing && picker.routeRow < 0 && picker.routePath.length > 0
                text: "Planned route not in the library – choose another one or keep it"
                color: Theme.warningText
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            ListView {
                id: routeList
                x: 20
                y: 148
                width: 440
                height: picker.panelHeight - y - 162
                clip: true
                spacing: 4
                model: Videos
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: routeRowItem
                    readonly property bool chosen: index === picker.routeRow
                    width: ListView.view.width
                    height: 62
                    radius: 10
                    color: chosen ? Theme.navSelected : (routeMouse.containsMouse ? Theme.hover : "transparent")
                    border.width: chosen ? 2 : 0
                    border.color: Theme.videoColor
                    opacity: model.valid ? 1 : 0.6

                    MouseArea {
                        id: routeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: model.valid
                        cursorShape: Qt.PointingHandCursor
                        onClicked: picker.routePath = model.path
                        onDoubleClicked: {
                            picker.routePath = model.path
                            picker.save()
                        }
                    }
                    Rectangle {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 42; height: 42; radius: 10
                        color: Qt.rgba(Theme.videoColor.r, Theme.videoColor.g, Theme.videoColor.b, 0.22)
                        Icon {
                            anchors.centerIn: parent
                            name: "video"
                            size: 24
                            strokeWidth: 2
                            color: Theme.videoColor
                        }
                    }
                    Column {
                        x: 64
                        width: parent.width - 64 - 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            width: parent.width
                            text: model.displayName
                            color: Theme.text
                            font.pixelSize: 16
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: model.valid ? page.routeDetails(model.distanceM, model.ascentM, model.durationSec, model.country)
                                              : (model.error.length > 0 ? model.error : "Cannot be loaded")
                            color: model.valid ? Theme.textSecondary : Theme.dangerText
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                }

                // empty library
                Column {
                    visible: routeList.count === 0
                    anchors.centerIn: parent
                    width: parent.width - 30
                    spacing: 12
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "film"
                        size: 40
                        strokeWidth: 2
                        color: Theme.videoColor
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: Videos.scanning ? "Scanning the video library…" : "No videos in your library"
                        color: Theme.text
                        font.pixelSize: 18
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "A route is a folder with the video (.mp4) and its GPS track (.gpx / .xml). Put route folders into "
                              + Videos.libraryFolder + " or add folders on the Video Ride page, then rescan."
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                    AppButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: 42
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        iconSpacing: 8
                        bold: false
                        iconName: "refresh"
                        enabled: !Videos.scanning
                        text: Videos.scanning ? "Scanning…" : "Rescan"
                        onClicked: if (!Videos.scanning) Videos.rescan()
                    }
                }
            }

            Column {
                x: 486
                y: 116
                width: picker.panelWidth - x - 28
                spacing: 10

                Text {
                    text: "Resistance"
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.Bold
                }
                SegmentedControl {
                    width: parent.width
                    height: 46
                    fontSize: 15
                    segmentPadding: 14
                    value: picker.vrMode
                    options: [
                        { value: "sim", label: "Simulation", icon: "mountain" },
                        { value: "erg", label: "ERG", icon: "bolt" }
                    ]
                    onActivated: (v) => picker.vrMode = v
                }
                Item {
                    visible: picker.vrMode === "erg"
                    width: parent.width
                    height: 46
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Target"
                        color: Theme.textSecondary
                        font.pixelSize: 16
                    }
                    UnitSpinBox {
                        id: vrWatts
                        anchors.right: parent.right
                        width: 190
                        height: 46
                        from: 25
                        to: 1500
                        stepSize: 5
                        unit: "W"
                        value: 150
                    }
                }
                Item {
                    visible: picker.vrMode === "sim"
                    width: parent.width
                    height: 22
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Difficulty"
                        color: Theme.textSecondary
                        font.pixelSize: 16
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.round(vrDifficulty.value) + " %"
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.Bold
                    }
                }
                AppSlider {
                    id: vrDifficulty
                    visible: picker.vrMode === "sim"
                    width: parent.width
                    from: 0
                    to: 100
                    stepSize: 5
                    snapMode: Slider.SnapAlways
                    value: 100
                    fillColor: Theme.success
                }
                Text {
                    width: parent.width
                    text: picker.vrMode === "erg" ? "The trainer holds the watts; climbs make the video slower."
                                                  : "Climbs get harder like on the road (100 % = the real gradients)."
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }

                Rectangle { width: parent.width; height: 1; color: Theme.cardBorder }

                Text {
                    text: "Ride a workout during the video"
                    color: Theme.text
                    font.pixelSize: 17
                    font.weight: Font.Bold
                }
                AppComboBox {
                    id: vrWorkoutCombo
                    width: parent.width
                    height: 48
                    fontSize: 15
                    model: picker.cyclingWorkouts
                    textRole: "label"
                    currentIndex: picker.workoutIndex(picker.vrWorkoutId)
                    displayText: picker.vrWorkoutId >= 0 && currentIndex > 0 && currentIndex < picker.cyclingWorkouts.length
                                 ? picker.cyclingWorkouts[currentIndex].name : "None"
                    onActivated: (index) => {
                        picker.vrWorkoutId = picker.cyclingWorkouts[index].workoutId
                        currentIndex = Qt.binding(function() { return picker.workoutIndex(picker.vrWorkoutId) })
                    }
                }
                Item {
                    visible: picker.vrWorkoutId >= 0
                    width: parent.width
                    height: 34
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Workout ERG"
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    ToggleSwitch {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        accessibleName: "Workout ERG"
                        checked: picker.vrWorkoutErg
                        onToggled: (on) => picker.vrWorkoutErg = on
                    }
                }
                Text {
                    width: parent.width
                    text: picker.vrWorkoutId < 0
                          ? "Optional: the workout's intervals run on the ride's clock and are shown under the video."
                          : picker.vrWorkoutErg
                            ? "The trainer holds the workout's watts (ERG); the route just sets the video's speed. "
                              + "After the workout the ride continues in " + (picker.vrMode === "erg" ? "ERG." : "Simulation.")
                            : "The targets are a guide only; the resistance above applies."
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }
            }
        }

        AppTextField {
            id: notesField
            x: 28
            y: picker.panelHeight - 146
            width: picker.panelWidth - 56
            height: 46
            font.pixelSize: 16
            iconName: "edit"
            placeholderText: "Notes (optional)"
        }

        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: picker.kind === "workout" ? "Tip: double-click a workout to add it right away"
                      : picker.kind === "rlv" ? (picker.editing ? "Start it from the calendar on the day"
                                                                : "Tip: double-click a route to add it right away")
                      : "Start it from the calendar – the Free Ride page opens ready to go"
                color: Theme.textMuted
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            AppButton {
                Layout.preferredHeight: 52
                kind: "secondary"
                bold: false
                text: "Cancel"
                onClicked: picker.close()
            }
            AppButton {
                Layout.preferredHeight: 52
                iconName: picker.editing ? "save" : "calendar"
                text: picker.editing ? "Save" : "Add to Calendar"
                enabled: picker.canSave
                onClicked: if (picker.canSave) picker.save()
            }
        }
    }
}
