import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Artilody.Backend

// GPX Workshop: place sprints and KOMs on a GPX route and save them in the
// GPX file (GpxWorkshop). GPX Ride draws and times exactly these.
//   1. choose a route (or open a file)
//   2. choose Sprint or KOM, click the profile: a sprint is placed with the
//      length set; a KOM needs a second click for its top
//   3. edit names and distances in the list, then Save
Item {
    id: page
    signal message(string text)

    property string tool: "sprint"        // "sprint" / "kom"
    property real sprintLength: 200
    property real komStart: -1            // first click of a KOM, waiting for the top
    property int selected: -1             // index in GpxWorkshop.segments

    readonly property var segs: GpxWorkshop.segments
    readonly property color sprintColor: "#2fd35a"
    readonly property color komColor: "#ff5a4f"

    function km(m) { return GpxWorkshop.formatDistance(m) }
    function report(error, ok) {
        if (error.length > 0)
            page.message(error)
        else if (ok)
            page.message(ok)
        return error.length === 0
    }
    function clicked(meters) {
        if (!GpxWorkshop.loaded)
            return
        if (page.tool === "sprint") {
            if (report(GpxWorkshop.addSprint(meters, page.sprintLength, ""), ""))
                page.selected = indexAt(meters)
            return
        }
        if (page.komStart < 0) {
            page.komStart = meters
            return
        }
        const a = page.komStart
        page.komStart = -1
        if (report(GpxWorkshop.addKom(a, meters, ""), ""))
            page.selected = indexAt(Math.min(a, meters))
    }
    function indexAt(meters) {
        for (let i = 0; i < segs.length; ++i)
            if (meters >= segs[i].start - 1 && meters <= segs[i].finish + 1)
                return i
        return -1
    }
    onToolChanged: komStart = -1

    Connections {
        target: GpxWorkshop
        function onRouteChanged() { page.selected = -1; page.komStart = -1 }
        function onSaved() { page.message("Saved in " + GpxWorkshop.title + ". GPX Ride uses these sprints and KOMs.") }
    }

    FileDialog {
        id: openDialog
        title: "Open a GPX route"
        nameFilters: ["GPX routes (*.gpx *.GPX)"]
        currentFolder: GpxRoutes.libraryFolderUrl
        onAccepted: {
            if (GpxWorkshop.dirty) {   // unsaved sprints / KOMs / limit: ask first
                page.pendingFile = selectedFile.toString()
                page.pendingRow = -1
                discardDialog.open()
            } else if (!GpxWorkshop.open(selectedFile)) {
                page.message(GpxWorkshop.lastError)
            }
        }
    }

    PageHeader {
        id: header
        x: Theme.pageMargin + 8
        y: 30
        width: parent.width - 2 * Theme.pageMargin - 8
        title: "GPX Workshop"
        subtitle: "Place sprints and KOMs on a GPX route"
    }

    Flickable {
        anchors.fill: parent
        anchors.topMargin: 118
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.bottomMargin: Theme.pageMargin
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: parent.width
            spacing: Theme.spacing

            // route choice
            Card {
                width: parent.width
                height: 92
                Row {
                    x: 18
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    AppComboBox {
                        id: routeBox
                        width: Math.min(560, column.width - 420)
                        model: GpxRoutes
                        textRole: "displayName"
                        iconName: "mapPin"
                        currentIndex: GpxWorkshop.loaded ? GpxRoutes.indexOfPath(GpxWorkshop.path) : -1
                        displayText: GpxWorkshop.loaded ? GpxWorkshop.title : "Choose a GPX route…"
                        onActivated: (index) => {
                            if (GpxWorkshop.dirty) {
                                page.pendingRow = index
                                page.pendingFile = ""
                                discardDialog.open()
                            } else if (!GpxWorkshop.openRow(index)) {
                                page.message(GpxWorkshop.lastError)
                            }
                        }
                    }
                    AppButton {
                        text: "Open GPX File…"
                        iconName: "folder"
                        kind: "secondary"
                        fontSize: 16
                        onClicked: openDialog.open()
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: GpxWorkshop.loaded
                        text: page.km(GpxWorkshop.totalM) + "  ·  " + GpxWorkshop.sprintCount + " sprint"
                              + (GpxWorkshop.sprintCount === 1 ? "" : "s") + "  ·  " + GpxWorkshop.komCount + " KOM"
                              + (GpxWorkshop.komCount === 1 ? "" : "s")
                        color: Theme.textSecondary
                        font.pixelSize: 16
                    }
                }
            }

            // profile with the segments
            Card {
                width: parent.width
                height: 410
                title: "Route profile"
                headerRight: [
                    Row {
                        spacing: 12
                        SegmentedControl {
                            anchors.verticalCenter: parent.verticalCenter
                            options: [{ value: "sprint", label: "Sprint", icon: "bolt", color: page.sprintColor, textColor: "#0b2a14" },
                                      { value: "kom", label: "KOM", icon: "mountain", color: page.komColor }]
                            value: page.tool
                            fontSize: 15
                            onActivated: (v) => page.tool = v
                        }
                        Row {
                            visible: page.tool === "sprint"
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Length"
                                color: Theme.textSecondary
                                font.pixelSize: 15
                            }
                            SpinBox {
                                id: lengthBox
                                from: 50
                                to: 5000
                                stepSize: 50
                                editable: true
                                value: page.sprintLength
                                onValueModified: page.sprintLength = value
                                textFromValue: (v) => v + " m"
                                valueFromText: (t) => parseInt(t)
                            }
                        }
                        AppButton {
                            visible: page.tool === "kom"
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Longest climb"
                            iconName: "trendUp"
                            kind: "secondary"
                            fontSize: 15
                            enabled: GpxWorkshop.loaded
                            onClicked: {
                                const e = GpxWorkshop.suggestKom()
                                if (page.report(e, ""))
                                    page.selected = page.segs.length - 1
                            }
                        }
                    }
                ]

                Text {
                    visible: !GpxWorkshop.loaded
                    anchors.centerIn: parent
                    text: "Choose a GPX route above."
                    color: Theme.textMuted
                    font.pixelSize: 18
                }

                Item {
                    id: plot
                    visible: GpxWorkshop.loaded
                    x: 18
                    y: parent.contentTop + 62
                    width: parent.width - 36
                    height: parent.height - y - 18

                    RouteProfile {
                        id: profile
                        anchors.fill: parent
                        profile: GpxWorkshop.profile
                        total: GpxWorkshop.totalM
                        minEle: GpxWorkshop.minEle
                        maxEle: GpxWorkshop.maxEle
                        position: 0
                        startDistance: 0
                        elevation: 0
                        informations: []
                        currentInformation: -1
                        interactive: true
                        showPosition: false
                        showPositionLabel: false
                        hoverPrefix: page.tool === "sprint" ? "Sprint from "
                                     : page.komStart < 0 ? "KOM from " : "KOM to "
                        onStartChosen: (meters) => page.clicked(meters)
                    }
                    // the segments over the profile
                    Repeater {
                        model: page.segs
                        Item {
                            readonly property bool kom: modelData.kind === "kom"
                            readonly property color tint: kom ? page.komColor : page.sprintColor
                            x: profile.xOf(modelData.start)
                            width: Math.max(3, profile.xOf(modelData.finish) - x)
                            height: profile.plotH
                            Rectangle {
                                anchors.fill: parent
                                color: parent.tint
                                opacity: index === page.selected ? 0.32 : 0.18
                            }
                            Rectangle { width: 2; height: parent.height; color: parent.tint }
                            Rectangle { x: parent.width - 2; width: 2; height: parent.height; color: parent.tint }
                            Rectangle {
                                y: -30
                                x: Math.max(-parent.x, Math.min(plot.width - parent.x - width, (parent.width - width) / 2))
                                width: label.implicitWidth + 14
                                height: 24
                                radius: 12
                                color: parent.tint
                                Text {
                                    id: label
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: parent.parent.kom ? "white" : "#0b2a14"
                                    font.pixelSize: 13
                                    font.weight: Font.Bold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.selected = index
                                }
                            }
                        }
                    }
                    // a KOM waiting for its top
                    Rectangle {
                        visible: page.komStart >= 0
                        x: profile.xOf(page.komStart) - 1
                        width: 2
                        height: profile.plotH
                        color: page.komColor
                    }
                }
                Text {
                    visible: GpxWorkshop.loaded
                    x: 18
                    y: parent.contentTop
                    text: page.tool === "sprint" ? "Click the profile where a " + page.sprintLength + " m sprint should start."
                          : page.komStart < 0 ? "Click the profile at the foot of the climb."
                          : "Now click the top of the climb (Esc cancels)."
                    color: Theme.textSecondary
                    font.pixelSize: 15
                }
            }

            // elevation cleaning
            Card {
                width: parent.width
                height: contentTop + 46 + gradNote.implicitHeight + 16
                title: "Gradients"
                visible: GpxWorkshop.loaded
                Row {
                    x: 18
                    y: parent.contentTop
                    spacing: 14
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Limit gradients to"
                        color: Theme.text
                        font.pixelSize: 16
                    }
                    SpinBox {
                        anchors.verticalCenter: parent.verticalCenter
                        from: 5
                        to: 30
                        editable: true
                        value: Math.round(GpxWorkshop.maxGrade)
                        onValueModified: GpxWorkshop.maxGrade = value
                        textFromValue: (v) => v + " %"
                        valueFromText: (t) => parseInt(t)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Steepest in the file " + GpxWorkshop.rawMaxGrade.toFixed(1) + " %  →  ridden "
                              + GpxWorkshop.steepestGrade.toFixed(1) + " %"
                              + (Math.round(GpxWorkshop.maxGrade) === GpxWorkshop.defaultMaxGrade ? "   (default limit)" : "")
                        color: GpxWorkshop.rawMaxGrade > GpxWorkshop.maxGrade + 0.5 ? Theme.warning : Theme.textSecondary
                        font.pixelSize: 15
                    }
                }
                Text {
                    id: gradNote
                    x: 18
                    y: parent.contentTop + 46
                    width: parent.width - 36
                    wrapMode: Text.WordWrap
                    text: "Single bad elevation points are always removed. Steeper steps than the limit (typical at "
                          + "hairpins in GPS / map data) become even ramps – the height gained stays the same. The "
                          + "limit is saved with the route; the track in the file is not changed."
                    color: Theme.textMuted
                    font.pixelSize: 14
                }
            }

            // the list
            Card {
                width: parent.width
                height: Math.max(150, contentTop + listCol.height + 18)
                title: "Sprints and KOMs"
                headerRight: [
                    Row {
                        spacing: 10
                        AppButton {
                            text: "Revert"
                            iconName: "refresh"
                            kind: "secondary"
                            fontSize: 15
                            enabled: GpxWorkshop.dirty
                            onClicked: GpxWorkshop.revert()
                        }
                        AppButton {
                            text: "Save to GPX file"
                            iconName: "save"
                            fontSize: 15
                            enabled: GpxWorkshop.loaded && GpxWorkshop.dirty
                            onClicked: page.report(GpxWorkshop.save(), "")
                        }
                    }
                ]
                Column {
                    id: listCol
                    x: 18
                    y: parent.contentTop
                    width: parent.width - 36
                    spacing: 8
                    Text {
                        visible: GpxWorkshop.loaded && page.segs.length === 0
                        text: "None yet – this route has no sprint or KOM in GPX Ride."
                        color: Theme.textMuted
                        font.pixelSize: 16
                    }
                    Repeater {
                        model: page.segs
                        Rectangle {
                            id: rowItem
                            readonly property bool kom: modelData.kind === "kom"
                            width: listCol.width
                            height: 56
                            radius: 10
                            color: index === page.selected ? Theme.menuBackground : "transparent"
                            border.color: index === page.selected ? (kom ? page.komColor : page.sprintColor) : Theme.faintLine
                            MouseArea { anchors.fill: parent; onClicked: page.selected = index }
                            Row {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 12
                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: rowItem.kom ? "mountain" : "bolt"
                                    size: 22
                                    color: rowItem.kom ? page.komColor : page.sprintColor
                                }
                                AppTextField {
                                    width: 220
                                    anchors.verticalCenter: parent.verticalCenter
                                    placeholderText: modelData.label
                                    text: modelData.name
                                    onEditingFinished: if (text !== modelData.name)
                                        page.report(GpxWorkshop.setSegment(index, modelData.start, modelData.finish, text), "")
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "from"
                                    color: Theme.textSecondary
                                    font.pixelSize: 15
                                }
                                AppTextField {
                                    width: 110
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Math.round(modelData.start)
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    onEditingFinished: {
                                        const a = parseFloat(text)
                                        // moving the start keeps the length
                                        if (!isNaN(a) && Math.round(a) !== Math.round(modelData.start))
                                            page.report(GpxWorkshop.setSegment(index, a, a + modelData.length, modelData.name), "")
                                        text = Qt.binding(() => Math.round(modelData.start))
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "m,  length"
                                    color: Theme.textSecondary
                                    font.pixelSize: 15
                                }
                                AppTextField {
                                    width: 100
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Math.round(modelData.length)
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    onEditingFinished: {
                                        const l = parseFloat(text)
                                        if (!isNaN(l) && Math.round(l) !== Math.round(modelData.length))
                                            page.report(GpxWorkshop.setSegment(index, modelData.start, modelData.start + l, modelData.name), "")
                                        text = Qt.binding(() => Math.round(modelData.length))
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "m   (at " + page.km(modelData.start) + ")"
                                          + (rowItem.kom ? "  ·  " + modelData.grade.toFixed(1) + " %  ·  "
                                                           + Math.round(modelData.gain) + " m up" : "")
                                    color: Theme.textMuted
                                    font.pixelSize: 15
                                }
                            }
                            IconButton {
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "trash"
                                tooltip: "Remove"
                                onClicked: {
                                    GpxWorkshop.removeSegment(index)
                                    page.selected = -1
                                }
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Saving writes the sprints and KOMs into the GPX file as a GPX extension (other apps ignore it); "
                      + "the track itself is not changed, so your saved times on the route are kept. A segment that is "
                      + "moved or changed in length starts with a new best time."
                color: Theme.textMuted
                font.pixelSize: 14
            }
        }
    }

    property int pendingRow: -1
    property string pendingFile: ""
    ConfirmDialog {
        id: discardDialog
        title: "Discard changes?"
        message: "The sprints and KOMs of " + GpxWorkshop.title + " have not been saved."
        acceptText: "Discard"
        acceptKind: "danger"
        onAccepted: {
            const ok = page.pendingFile.length > 0 ? GpxWorkshop.open(page.pendingFile) : GpxWorkshop.openRow(page.pendingRow)
            if (!ok)
                page.message(GpxWorkshop.lastError)
        }
    }

    Keys.onEscapePressed: page.komStart = -1
}
