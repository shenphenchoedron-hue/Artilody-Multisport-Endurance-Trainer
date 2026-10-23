import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Artilody.Backend

Item {
    id: page
    signal started()
    signal editWorkout(int id)
    signal newWorkout()
    signal message(string text)

    readonly property var sel: Workouts.selected
    readonly property bool selRowing: sel.sport === "rowing"
    property bool ergSelected: Profiles.defaultErg

    // Navigation lives in Main.qml (ApplicationWindow.window.showPage).
    function goTo(name) {
        var w = ApplicationWindow.window
        if (w && typeof w.showPage === "function")
            w.showPage(name)
    }
    function createWorkout(sport) {
        Builder.newWorkout(sport)
        goTo("builder")
    }
    function rowWorkout(id) {
        if (Rowing.active) {
            page.message("A rowing session is in progress – finish it first")
            goTo("rowing")
            return
        }
        if (Rowing.setupPlan(id))
            goTo("rowing")
        else
            page.message("Could not set up the rowing plan")
    }
    function planWorkout(id, name) {
        calendarPicker.workoutId = id
        calendarPicker.subtitle = "Plan \"" + name + "\" on…"
        calendarPicker.openFor("")
    }

    Connections {
        target: Profiles
        function onProfileChanged() { page.ergSelected = Profiles.defaultErg }
    }

    PageHeader {
        x: Theme.pageMargin + 8
        y: 30
        title: "Workouts"
        subtitle: "Choose your next ride or row"
    }

    SegmentedControl {
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin
        y: 76
        height: 50
        value: Workouts.sportFilter
        options: [
            { value: "all", label: "All sports" },
            { value: "cycling", label: "Cycling", icon: "bike" },
            { value: "rowing", label: "Rowing", icon: "rowing", color: Theme.rowingColor, textColor: Theme.textOnColor }
        ]
        onActivated: (v) => Workouts.sportFilter = v
    }

    // --- filter bar ------------------------------------------------------------
    RowLayout {
        id: filterBar
        x: Theme.pageMargin
        y: 146
        width: parent.width - 2 * Theme.pageMargin
        height: 54
        spacing: 8

        AppTextField {
            Layout.fillWidth: true
            Layout.minimumWidth: 170
            Layout.fillHeight: true
            iconName: "search"
            placeholderText: "Search workouts..."
            text: Workouts.searchText
            onTextChanged: Workouts.searchText = text
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: catRow.width + 8
            radius: 10
            color: Theme.card
            border.color: Theme.cardBorder
            Row {
                id: catRow
                anchors.centerIn: parent
                spacing: 0
                Repeater {
                    model: Workouts.categories
                    Rectangle {
                        width: catLabel.implicitWidth + 18
                        height: 44
                        radius: 8
                        color: Workouts.category === modelData ? Theme.accent : (catMouse.containsMouse ? Theme.hover : "transparent")
                        Text {
                            id: catLabel
                            anchors.centerIn: parent
                            text: modelData
                            color: Workouts.category === modelData ? "white" : Theme.text
                            font.pixelSize: 16
                        }
                        Rectangle {
                            visible: index > 0 && Workouts.category !== modelData && Workouts.category !== Workouts.categories[index - 1]
                            width: 1; height: 24
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.cardBorder
                        }
                        MouseArea {
                            id: catMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Workouts.category = modelData
                        }
                    }
                }
            }
        }

        AppComboBox {
            Layout.preferredWidth: 214
            Layout.fillHeight: true
            prefix: "Sort: "
            model: Workouts.sortModes
            currentIndex: Workouts.sortMode
            onActivated: (index) => Workouts.sortMode = index
        }

        AppButton {
            Layout.fillHeight: true
            horizontalPadding: 16
            iconSpacing: 8
            kind: "secondary"
            bold: false
            text: "Import"
            iconName: "import"
            onClicked: importDialog.open()
        }

        AppButton {
            id: exportButton
            Layout.fillHeight: true
            horizontalPadding: 16
            iconSpacing: 8
            kind: "secondary"
            bold: false
            text: "Export"
            iconName: "export"
            onClicked: exportMenu.popup()
            Menu {
                id: exportMenu
                y: exportButton.height + 4
                width: Math.max(implicitWidth, 300)
                MenuItem {
                    text: "Export selected workout…"
                    enabled: page.sel.workoutId !== undefined
                    onTriggered: page.startExport(false)
                }
                MenuItem {
                    text: "Export all my workouts…"
                    enabled: Workouts.customCount > 0
                    onTriggered: page.startExport(true)
                }
            }
        }

        AppButton {
            id: newButton
            Layout.fillHeight: true
            horizontalPadding: 18
            iconSpacing: 8
            text: "New Workout"
            iconName: "plus"
            onClicked: newMenu.popup()
            Menu {
                id: newMenu
                y: newButton.height + 4
                width: Math.max(implicitWidth, 260)
                MenuItem {
                    text: "New cycling workout"
                    onTriggered: page.createWorkout("cycling")
                }
                MenuItem {
                    text: "New rowing workout"
                    onTriggered: page.createWorkout("rowing")
                }
            }
        }
    }

    // --- sharing (export / import) -------------------------------------------
    function startExport(all) {
        if (all && Workouts.customCount === 0) {
            Workouts.exportAllCustom("")   // sets the "nothing to export" message
            page.message(Workouts.lastMessage)
            return
        }
        exportDialog.exportAll = all
        exportDialog.selectedFile = Workouts.suggestedExportUrl(all ? -1 : page.sel.workoutId, all ? "all" : "awt")
        exportDialog.open()
    }

    FileDialog {
        id: importDialog
        title: "Import workouts"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Workouts (*.awt *.json *.zwo)", "All files (*)"]
        onAccepted: {
            Workouts.importFiles(selectedFiles)
            page.message(Workouts.lastMessage)
        }
    }

    FileDialog {
        id: exportDialog
        property bool exportAll: false
        title: exportAll ? "Export all my workouts" : "Export workout"
        fileMode: FileDialog.SaveFile
        nameFilters: exportAll ? ["Artilody workout (*.awt)"]
                               : ["Artilody workout (*.awt)", "Zwift workout (*.zwo)"]
        onAccepted: {
            if (exportAll) {
                Workouts.exportAllCustom(selectedFile)
            } else {
                // the file extension decides; without one the chosen filter does
                var hint = selectedNameFilter.index === 1 ? "zwo" : "awt"
                Workouts.exportWorkout(page.sel.workoutId, selectedFile, hint)
            }
            page.message(Workouts.lastMessage)
        }
    }

    // --- workout grid --------------------------------------------------------
    GridView {
        id: grid
        x: Theme.pageMargin - 2
        y: 220
        width: parent.width - 2 * Theme.pageMargin - preview.width - 22
        height: parent.height - y - Theme.pageMargin + 4
        cellWidth: width / 2
        cellHeight: 266
        clip: true
        model: Workouts
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight
            readonly property bool isSelected: workoutId === Workouts.selectedId

            Rectangle {
                anchors.fill: parent
                anchors.margins: 6
                radius: Theme.radius
                color: isSelected ? Theme.cardStrong : (cardMouse.containsMouse ? Qt.lighter(Theme.card, 1.1) : Theme.card)
                border.width: isSelected ? 3 : 1
                border.color: isSelected ? Theme.accent : Theme.cardBorder

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Workouts.selectedId = workoutId
                    onDoubleClicked: page.editWorkout(workoutId)
                }

                Row {
                    x: 18
                    y: 14
                    spacing: 8
                    SportBadge {
                        sport: model.sport
                        compact: true
                        implicitHeight: 28
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    CategoryBadge {
                        category: model.category
                        label: model.builtIn ? model.category.toUpperCase() : "CUSTOM · " + model.category.toUpperCase()
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                // sport stripe so rowing and cycling stand apart at a glance
                Rectangle {
                    x: 1
                    y: 18
                    width: 4
                    height: parent.height - 36
                    radius: 2
                    color: Theme.sportColor(model.sport)
                }

                // selected check / favourite heart
                Rectangle {
                    visible: isSelected
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    y: 16
                    width: 34; height: 34; radius: 17
                    color: Theme.accent
                    Icon { anchors.centerIn: parent; name: "check"; color: "white"; size: 20; strokeWidth: 2.6 }
                }
                Icon {
                    visible: !isSelected
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    y: 20
                    name: favorite ? "heart" : "heartOutline"
                    color: favorite ? Theme.heartRate : Theme.text
                    size: 26
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Workouts.toggleFavorite(workoutId)
                    }
                }

                Text {
                    x: 18
                    y: 50
                    width: parent.width - 36
                    text: name
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Row {
                    x: 18
                    y: 82
                    spacing: 8
                    Icon { name: "clock"; size: 20; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: durationMin + " min"
                        color: Theme.text
                        font.pixelSize: 17
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                WorkoutChart {
                    x: 12
                    y: 108
                    width: parent.width - 24
                    height: 92
                    compact: true
                    showHeartRate: false
                    segments: model.segments
                    totalSec: model.totalSec
                    ftp: model.sport === "rowing" ? Profiles.rowingFtp : Profiles.ftp
                    barGaps: false
                    enabled: false
                }
                Text {
                    x: 18
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    width: parent.width - 36
                    text: summary + "  •  " + (focus.length > 0 ? focus : category)
                    color: Theme.textSecondary
                    font.pixelSize: 15
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            visible: grid.count === 0
            anchors.centerIn: parent
            text: "No workouts match your filter"
            color: Theme.textSecondary
            font.pixelSize: 18
        }
    }

    // --- preview panel ---------------------------------------------------------
    Card {
        id: preview
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin
        y: 224
        width: 506
        height: parent.height - y - Theme.pageMargin
        visible: page.sel.workoutId !== undefined

        Text {
            x: 20; y: 20
            text: "Workout Preview"
            color: Theme.text
            font.pixelSize: 19
            font.weight: Font.Bold
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            y: 14
            spacing: 4
            IconButton {
                flat: true
                iconName: page.sel.favorite ? "heart" : "heartOutline"
                iconColor: page.sel.favorite ? Theme.heartRate : Theme.text
                iconSize: 26
                onClicked: Workouts.toggleFavorite(page.sel.workoutId)
            }
            IconButton {
                flat: true
                iconName: "dots"
                iconSize: 24
                onClicked: moreMenu.popup()
                Menu {
                    id: moreMenu
                    width: Math.max(implicitWidth, 300)
                    MenuItem {
                        text: "Add to calendar…"
                        onTriggered: page.planWorkout(page.sel.workoutId, page.sel.name)
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: "Export selected workout…"
                        onTriggered: page.startExport(false)
                    }
                    MenuItem {
                        text: "Export all my workouts…"
                        enabled: Workouts.customCount > 0
                        onTriggered: page.startExport(true)
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: "Duplicate"
                        onTriggered: {
                            var id = Workouts.duplicate(page.sel.workoutId)
                            if (id >= 0) {
                                Workouts.selectedId = id
                                page.message("Workout duplicated")
                            }
                        }
                    }
                    MenuItem {
                        // built-in workouts cannot be deleted; the Ramp FTP Test says why
                        text: page.sel.requiredForFtp ? "Delete (required for FTP tests)" : "Delete"
                        enabled: page.sel.deletable === true
                        onTriggered: deleteDialog.open()
                    }
                }
            }
        }

        Text {
            x: 20; y: 50
            width: parent.width - 40
            text: page.sel.name || ""
            color: Theme.text
            font.pixelSize: 38
            font.weight: Font.Black
            elide: Text.ElideRight
        }
        Row {
            x: 20; y: 106
            spacing: 12
            SportBadge { sport: page.sel.sport || "cycling"; implicitHeight: 28; anchors.verticalCenter: parent.verticalCenter }
            CategoryBadge { category: page.sel.category || ""; anchors.verticalCenter: parent.verticalCenter }
            Icon { name: "timer"; size: 20; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: (page.sel.durationMin || 0) + " min" + (page.sel.focus ? "   •   " + page.sel.focus : "")
                color: Theme.text
                font.pixelSize: 17
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        WorkoutChart {
            x: 4
            y: 146
            width: parent.width - 18
            height: 186
            segments: page.sel.segments || []
            totalSec: page.sel.totalSec || 0
            showHeartRate: false
            showAxisTitles: true
            fontSize: 14
            ftp: page.selRowing ? Profiles.rowingFtp : Profiles.ftp
            barGaps: false
        }

        RowLayout {
            x: 16
            y: 346
            width: parent.width - 32
            height: 82
            spacing: 12
            StatTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                iconName: "timer"; valueSize: 21; caption: "Duration"; value: (page.sel.durationMin || 0) + " min"
            }
            StatTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                iconName: "bolt"; valueSize: 21; iconColor: Theme.warning; caption: "Intensity"; value: (page.sel.intensity || 0) + "% FTP"
            }
            StatTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                iconName: "dashboard"; valueSize: 21; iconColor: Theme.accentLight; caption: "Intervals"; value: page.sel.intervalsLabel || ""
            }
        }

        Text {
            x: 20
            y: 442
            width: parent.width - 40
            height: parent.height - y - 230
            text: page.sel.description || ""
            color: Theme.text
            font.pixelSize: 17
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            lineHeight: 1.1
        }

        Text {
            x: 20
            anchors.bottom: modeRow.top
            anchors.bottomMargin: 10
            visible: !page.selRowing
            text: "Ride Mode"
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.Bold
        }

        RowLayout {
            id: modeRow
            visible: !page.selRowing
            x: 20
            width: parent.width - 40
            height: 74
            anchors.bottom: actionRow.top
            anchors.bottomMargin: 22
            spacing: 12
            Repeater {
                model: [
                    { erg: true, title: "ERG Mode", icon: "pulse", sub: "Automatically match target power" },
                    { erg: false, title: "Free Ride", icon: "trendUp", sub: "Follow the workout at your own pace" }
                ]
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    readonly property bool active: page.ergSelected === modelData.erg
                    color: active ? Theme.navSelected : "transparent"
                    border.width: active ? 2 : 1
                    border.color: active ? Theme.accent : Theme.cardBorder
                    Rectangle {
                        x: 14; y: 14
                        width: 24; height: 24; radius: 12
                        color: "transparent"
                        border.width: 2
                        border.color: parent.active ? Theme.accent : Theme.textSecondary
                        Rectangle {
                            anchors.centerIn: parent
                            visible: parent.parent.active
                            width: 12; height: 12; radius: 6
                            color: Theme.accent
                        }
                    }
                    Row {
                        x: 50; y: 13
                        spacing: 10
                        Icon { name: modelData.icon; size: 22; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: modelData.title
                            color: Theme.text
                            font.pixelSize: 17
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Text {
                        x: 14
                        y: 46
                        width: parent.width - 20
                        text: modelData.sub
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.ergSelected = modelData.erg
                    }
                }
            }
        }

        Rectangle {
            visible: page.selRowing
            x: 20
            width: parent.width - 40
            height: 96
            anchors.bottom: actionRow.top
            anchors.bottomMargin: 22
            radius: 10
            color: Qt.rgba(Theme.rowingColor.r, Theme.rowingColor.g, Theme.rowingColor.b, 0.12)
            border.color: Qt.rgba(Theme.rowingColor.r, Theme.rowingColor.g, Theme.rowingColor.b, 0.55)
            Icon {
                x: 16
                y: 16
                name: "rowing"
                size: 30
                strokeWidth: 2
                color: Theme.rowingColor
            }
            Column {
                x: 60
                y: 12
                width: parent.width - 76
                spacing: 4
                Text {
                    text: "Rowing workout"
                    color: Theme.text
                    font.pixelSize: 17
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: "Watt targets use your rowing FTP (" + Profiles.rowingFtp + " W). Row it on a Concept2 PM5 (or the Virtual Rower) on the Rowing page."
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
        }

        RowLayout {
            id: actionRow
            x: 20
            width: parent.width - 40
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 22
            height: 62
            spacing: 14
            AppButton {
                Layout.fillWidth: true
                Layout.fillHeight: true
                kind: "danger"
                iconName: "play"
                // While waiting for pedaling another workout can be chosen; it replaces the armed one.
                readonly property bool armedHere: Training.waiting && Training.workoutId === page.sel.workoutId
                text: page.selRowing ? (Rowing.active ? "Rowing…" : "Row Workout")
                      : Training.active ? "In progress"
                                        : (armedHere ? "Waiting…" : "Start Workout")
                fontSize: 20
                enabled: page.selRowing ? !Rowing.active : (!Training.active && !armedHere)
                onClicked: {
                    if (page.selRowing) {
                        page.rowWorkout(page.sel.workoutId)
                        return
                    }
                    if (Training.loadWorkout(page.sel.workoutId)) {
                        Training.ergMode = page.ergSelected
                        Training.startWhenPedaling()
                        page.started()
                    }
                }
            }
            AppButton {
                Layout.preferredWidth: 112
                Layout.fillHeight: true
                kind: "secondary"
                iconName: "edit"
                iconSpacing: 8
                horizontalPadding: 12
                text: "Edit"
                bold: false
                onClicked: page.editWorkout(page.sel.workoutId)
            }
            IconButton {
                Layout.preferredWidth: 62
                Layout.fillHeight: true
                iconName: "calendar"
                iconSize: 26
                tooltip: "Add to calendar"
                onClicked: page.planWorkout(page.sel.workoutId, page.sel.name)
            }
            IconButton {
                Layout.preferredWidth: 62
                Layout.fillHeight: true
                iconName: "share"
                iconSize: 26
                tooltip: "Export / share workout"
                onClicked: page.startExport(false)
            }
        }

        // free ride shortcut
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 22
            anchors.bottom: modeRow.top
            anchors.bottomMargin: 12
            text: "Ride without workout ›"
            color: Theme.accentLight
            font.pixelSize: 14
            visible: !page.selRowing && !Training.active && !(Training.waiting && Training.freeRide)
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    Training.loadFreeRide()
                    Training.startWhenPedaling()
                    page.started()
                }
            }
        }
    }

    DatePickerDialog {
        id: calendarPicker
        property var workoutId: -1
        title: "Add to calendar"
        acceptText: "Add to Calendar"
        onPicked: (iso) => {
            if (Planner.addWorkout(iso, calendarPicker.workoutId) >= 0)
                page.message("Planned for " + calendarPicker.parseIso(iso).toLocaleDateString(Qt.locale("en_US"), "dddd, d MMMM")
                             + " – see Calendar")
            else
                page.message("Could not add the workout to the calendar")
        }
    }

    ConfirmDialog {
        id: deleteDialog
        title: "Delete workout?"
        message: "\"" + (page.sel.name || "") + "\" will be removed permanently. Recorded rides stay in History."
        acceptText: "Delete"
        acceptKind: "danger"
        onAccepted: {
            Workouts.remove(page.sel.workoutId)
            page.message("Workout deleted")
        }
    }

    // When a workout is chosen and nothing is riding, show it on the dashboard.
    Connections {
        target: Workouts
        function onSelectedChanged() {
            // an armed workout is only replaced by pressing Start Workout again
            // rowing workouts are set up on the Rowing page, not the cycling dashboard
            if (!Training.armedOrActive && Workouts.selectedId >= 0 && Workouts.selected.sport !== "rowing")
                Training.loadWorkout(Workouts.selectedId)
        }
    }
}
