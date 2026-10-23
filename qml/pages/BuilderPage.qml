import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

Item {
    id: page
    signal message(string text)

    readonly property var sel: Builder.selected
    readonly property int selIndex: Builder.selectedIndex
    readonly property bool hasSel: selIndex >= 0 && sel.index !== undefined
    readonly property bool showEndPower: hasSel && (sel.isRamp || sel.type === "ramp" || sel.type === "warmup" || sel.type === "cooldown")
    property bool percentView: false
    readonly property bool rowing: Builder.sport === "rowing"
    readonly property string ftpLabel: rowing ? "Rowing FTP" : "FTP"

    PageHeader {
        x: Theme.pageMargin + 8
        y: 30
        title: "Workout Builder"
        subtitle: page.rowing ? "Design your own rowing session" : "Design your own training session"
    }

    // FTP the watts are based on (rowing workouts use the rowing FTP)
    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin
        y: 90
        spacing: 8
        Icon {
            name: page.rowing ? "rowing" : "bike"
            size: 22
            strokeWidth: 2
            color: Theme.sportColor(Builder.sport)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: "Watts based on your " + page.ftpLabel + ": " + Builder.ftp + " W"
            color: Theme.textSecondary
            font.pixelSize: 17
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // --- name / actions ----------------------------------------------------
    RowLayout {
        x: Theme.pageMargin
        y: 144
        width: parent.width - 2 * Theme.pageMargin
        height: 56
        spacing: 14

        AppTextField {
            id: nameField
            Layout.fillWidth: true
            Layout.fillHeight: true
            font.pixelSize: 22
            text: Builder.name
            trailingIcon: "edit"
            placeholderText: "Workout name"
            onTextEdited: Builder.name = text
            onTrailingClicked: { nameField.forceActiveFocus(); nameField.selectAll() }
        }
        SegmentedControl {
            Layout.fillHeight: true
            fontSize: 16
            segmentPadding: 14
            value: Builder.sport
            options: [
                { value: "cycling", label: "Cycling", icon: "bike" },
                { value: "rowing", label: "Rowing", icon: "rowing", color: Theme.rowingColor, textColor: Theme.textOnColor }
            ]
            onActivated: (v) => Builder.sport = v
        }
        AppComboBox {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            model: Builder.categories
            currentIndex: Math.max(0, Builder.categories.indexOf(Builder.category))
            onActivated: (index) => Builder.category = Builder.categories[index]
        }
        Item { Layout.preferredWidth: 20 }
        AppButton {
            Layout.preferredWidth: 166
            Layout.fillHeight: true
            kind: "secondary"
            iconName: "play"
            text: "Preview"
            bold: false
            onClicked: previewDialog.open()
        }
        AppButton {
            Layout.preferredWidth: 216
            Layout.fillHeight: true
            iconName: "save"
            text: "Save Workout"
            onClicked: page.save()
        }
    }

    function save() {
        var id = Builder.save()
        if (id >= 0)
            page.message("Workout \"" + Builder.name + "\" saved")
        else
            page.message(Builder.lastError)
    }

    // --- timeline card -------------------------------------------------------
    Card {
        id: timelineCard
        x: Theme.pageMargin
        y: 218
        width: settingsCard.x - x - Theme.spacing
        height: addBlockCard.y - y - Theme.spacing
        title: "Workout Timeline"
        titleSize: 22

        Row {
            x: 18
            y: 54
            spacing: 8
            Icon { name: "clock"; size: 20; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: Math.round(Builder.totalDurationSec / 60) + " min   •   " + Builder.blockCount + " blocks"
                color: Theme.textSecondary
                font.pixelSize: 17
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: 14
            spacing: 14
            Text {
                text: "View:"
                color: Theme.textSecondary
                font.pixelSize: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            AppComboBox {
                width: 206
                height: 46
                model: ["Power (Watts)", "% " + page.ftpLabel]
                currentIndex: page.percentView ? 1 : 0
                onActivated: (index) => page.percentView = index === 1
            }
        }

        WorkoutChart {
            id: builderChart
            x: 4
            y: 88
            width: parent.width - 20
            height: 260
            segments: Builder.segments
            totalSec: Builder.totalDurationSec
            showHeartRate: false
            percentAxis: page.percentView
            ftp: Builder.ftp
            barGaps: false
            selectionStart: page.hasSel ? Builder.blockStartSec(page.selIndex, Builder.totalDurationSec) : -1
            selectionEnd: page.hasSel ? Builder.blockStartSec(page.selIndex, Builder.totalDurationSec) + page.sel.duration : -1
            onClickedAt: (second) => Builder.selectedIndex = Builder.blockAtSecond(second)
        }

        // block cards
        ListView {
            id: blockList
            x: 10
            y: 356
            width: parent.width - 20
            height: 166
            orientation: ListView.Horizontal
            spacing: 8
            clip: true
            model: Builder.intervals
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 6 }

            property int dragFrom: -1
            property int dropIndex: -1

            delegate: Item {
                id: blockDelegate
                width: 134
                height: 160
                readonly property bool isSel: index === Builder.selectedIndex

                Rectangle {
                    anchors.fill: parent
                    radius: 10
                    color: blockDelegate.isSel ? Theme.cardStrong : Theme.card
                    border.width: blockDelegate.isSel ? 3 : 1
                    border.color: blockDelegate.isSel ? Theme.accent : Theme.cardBorder
                    opacity: blockList.dragFrom === index ? 0.5 : 1

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Builder.selectedIndex = index
                    }

                    // grip (drag to reorder)
                    Icon {
                        x: 2
                        y: 40
                        name: "grip"
                        size: 18
                        color: Theme.textSecondary
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.SizeAllCursor
                            preventStealing: true
                            onPressed: blockList.dragFrom = index
                            onPositionChanged: (mouse) => {
                                var p = mapToItem(blockList.contentItem, mouse.x, mouse.y)
                                var i = blockList.indexAt(p.x, 50)
                                blockList.dropIndex = i < 0 ? (p.x < 0 ? 0 : blockList.count - 1) : i
                            }
                            onReleased: {
                                if (blockList.dropIndex >= 0 && blockList.dropIndex !== blockList.dragFrom)
                                    Builder.moveBlock(blockList.dragFrom, blockList.dropIndex)
                                blockList.dragFrom = -1
                                blockList.dropIndex = -1
                            }
                        }
                    }

                    Rectangle {
                        x: 18
                        y: 10
                        width: parent.width - 30
                        height: 12
                        radius: 6
                        color: Theme.zoneColor(zone)
                    }
                    Text {
                        x: 24
                        y: 32
                        width: parent.width - 30
                        text: name
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Column {
                        x: 24
                        y: 62
                        spacing: 6
                        Row {
                            spacing: 8
                            Icon { name: "clock"; size: 16; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: durationText; color: Theme.text; font.pixelSize: 15 }
                        }
                        Row {
                            spacing: 8
                            Icon { name: "bolt"; size: 16; color: zone === 3 ? Theme.warning : Theme.text; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                text: isRamp ? watts + "–" + endWatts + " W" : watts + " W"
                                color: Theme.text
                                font.pixelSize: 15
                            }
                        }
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.horizontalCenterOffset: 6
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 10
                        spacing: 16
                        IconButton { flat: true; iconName: "copy"; iconSize: 19; tooltip: "Duplicate"; onClicked: Builder.duplicateBlock(index) }
                        IconButton { flat: true; iconName: "trash"; iconSize: 19; tooltip: "Delete"; onClicked: Builder.removeBlock(index) }
                    }
                }

                Rectangle {
                    visible: blockList.dropIndex === index && blockList.dragFrom !== index
                    width: 4
                    height: parent.height
                    radius: 2
                    x: blockList.dragFrom > index ? -6 : parent.width + 2
                    color: Theme.accentLight
                }
            }
        }

        AppButton {
            x: 18
            width: parent.width - 36
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            height: 50
            kind: "dashed"
            iconName: "plus"
            text: "Add Interval"
            onClicked: Builder.addInterval()
        }
    }

    // --- add block --------------------------------------------------------------
    Card {
        id: addBlockCard
        x: Theme.pageMargin
        width: timelineCard.width
        height: 150
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.pageMargin
        title: "Add Block"
        titleSize: 22

        RowLayout {
            x: 18
            y: 58
            width: parent.width - 36
            height: 78
            spacing: 12
            Repeater {
                model: [
                    { type: "warmup", label: "Warm Up", bars: [0.4, 0.6, 0.8], color: "#3d86fb" },
                    { type: "steady", label: "Steady", bars: [0.7, 0.7, 0.7, 0.7], color: "#25d3e3" },
                    { type: "intervals", label: "Intervals", bars: [0.5, 0.8, 0.6, 1.0], color: "#ffd21f" },
                    { type: "recovery", label: "Recovery", bars: [0.6, 0.9, 0.6], color: "#45d659" },
                    { type: "cooldown", label: "Cool Down", bars: [0.9, 0.6, 0.4], color: "#3d86fb" },
                    { type: "ramp", label: "Ramp", bars: [0.4, 0.6, 0.8, 1.0], color: "#ff3b30" }
                ]
                Rectangle {
                    id: addTile
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: blockMouse.containsMouse ? Theme.cardStrong : Theme.card
                    border.color: Theme.cardBorder
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 12
                        height: 26
                        spacing: 3
                        Repeater {
                            model: modelData.bars
                            Rectangle {
                                width: 8
                                height: 26 * modelData
                                anchors.bottom: parent.bottom
                                radius: 1
                                color: addTile.blockColor
                            }
                        }
                    }
                    readonly property color blockColor: modelData.color
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 10
                        text: modelData.label
                        color: Theme.text
                        font.pixelSize: 16
                    }
                    MouseArea {
                        id: blockMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Builder.addBlock(modelData.type)
                    }
                }
            }
        }
    }

    // --- interval settings ------------------------------------------------------
    Card {
        id: settingsCard
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin
        y: 218
        width: 372
        height: parent.height - y - Theme.pageMargin
        title: "Interval Settings"
        titleSize: 22

        Item {
            x: 20
            y: 58
            width: parent.width - 40
            height: parent.height - 58 - 20
            visible: page.hasSel

            Rectangle {
                width: 8
                height: 52
                radius: 4
                color: Theme.zoneColor(page.sel.zone || 0)
            }
            TextField {
                id: blockName
                x: 22
                y: -6
                width: parent.width - 22
                height: 38
                text: page.sel.name || ""
                color: Theme.text
                font.pixelSize: 21
                font.weight: Font.Bold
                leftPadding: 4
                selectByMouse: true
                background: Rectangle {
                    color: blockName.activeFocus ? Theme.field : "transparent"
                    radius: 6
                    border.width: blockName.activeFocus ? 1 : 0
                    border.color: Theme.accentLight
                }
                onEditingFinished: Builder.setBlockName(page.selIndex, text)
            }
            Text {
                x: 26
                y: 30
                text: {
                    switch (page.sel.type) {
                    case "warmup": return "Gradually raise the effort"
                    case "cooldown": return "Bring the effort down gently"
                    case "recovery": return page.rowing ? "Easy paddling between efforts" : "Easy spinning between efforts"
                    case "ramp": return "Steadily increasing power"
                    case "interval": return "Build sustainable power"
                    default: return "Constant effort"
                    }
                }
                color: Theme.textSecondary
                font.pixelSize: 15
            }

            ColumnLayout {
                y: 80
                width: parent.width
                spacing: 14

                StepperField {
                    Layout.fillWidth: true
                    iconName: "clock"
                    label: "Duration"
                    value: Theme.clock(page.sel.duration || 0)
                    onDecrement: Builder.setDuration(page.selIndex, page.sel.duration - (page.sel.duration > 120 ? 30 : 15))
                    onIncrement: Builder.setDuration(page.selIndex, page.sel.duration + (page.sel.duration >= 120 ? 30 : 15))
                    onEdited: (text) => { if (!Builder.setDurationText(page.selIndex, text)) page.message("Use mm:ss, e.g. 12:00") }
                }
                StepperField {
                    Layout.fillWidth: true
                    iconName: "bolt"
                    iconColor: Theme.warning
                    label: page.showEndPower ? "Start Power (W)" : "Target Power (W)"
                    value: page.sel.watts !== undefined ? page.sel.watts : ""
                    onDecrement: Builder.setWatts(page.selIndex, page.sel.watts - 5)
                    onIncrement: Builder.setWatts(page.selIndex, page.sel.watts + 5)
                    onEdited: (text) => { var v = parseInt(text); if (!isNaN(v)) Builder.setWatts(page.selIndex, v) }
                }
                StepperField {
                    Layout.fillWidth: true
                    visible: page.showEndPower
                    iconName: "trendUp"
                    iconColor: Theme.warning
                    label: "End Power (W)"
                    value: page.sel.endWatts !== undefined ? page.sel.endWatts : ""
                    onDecrement: Builder.setEndWatts(page.selIndex, page.sel.endWatts - 5)
                    onIncrement: Builder.setEndWatts(page.selIndex, page.sel.endWatts + 5)
                    onEdited: (text) => { var v = parseInt(text); if (!isNaN(v)) Builder.setEndWatts(page.selIndex, v) }
                }
                StepperField {
                    Layout.fillWidth: true
                    iconName: "dashboard"
                    iconColor: Theme.accentLight
                    label: "% " + page.ftpLabel
                    unit: "%"
                    value: page.sel.percent !== undefined ? page.sel.percent : ""
                    onDecrement: Builder.setPercent(page.selIndex, page.sel.percent - 1)
                    onIncrement: Builder.setPercent(page.selIndex, page.sel.percent + 1)
                    onEdited: (text) => { var v = parseInt(text); if (!isNaN(v)) Builder.setPercent(page.selIndex, v) }
                }
                StepperField {
                    Layout.fillWidth: true
                    iconName: "target"
                    label: page.rowing ? "Stroke Rate (SPM)" : "Cadence (RPM)"
                    unit: page.rowing ? "SPM" : "RPM"
                    value: page.sel.cadence > 0 ? page.sel.cadence : "–"
                    onDecrement: Builder.setCadence(page.selIndex, page.sel.cadence > (page.rowing ? 16 : 40) ? page.sel.cadence - 1 : 0)
                    onIncrement: Builder.setCadence(page.selIndex, page.sel.cadence > 0 ? page.sel.cadence + 1 : (page.rowing ? 22 : Profiles.preferredCadence))
                    onEdited: (text) => { var v = parseInt(text); Builder.setCadence(page.selIndex, isNaN(v) ? 0 : v) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Icon { name: "heart"; color: Theme.danger; size: 26 }
                    Column {
                        Layout.preferredWidth: 86
                        Text { text: "RPE"; color: Theme.textSecondary; font.pixelSize: 16 }
                        Text { text: "(Perceived Effort)"; color: Theme.textMuted; font.pixelSize: 11 }
                    }
                    AppComboBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        model: Builder.rpeOptions
                        currentIndex: (page.sel.rpe !== undefined ? page.sel.rpe : -1) + 1
                        displayText: page.sel.rpe >= 0 ? Builder.rpeOptions[page.sel.rpe + 1]
                                                       : "Auto · RPE " + Theme.rpeLabels[page.sel.zone || 0]
                        accentColor: Theme.zoneColor(page.sel.zone || 0)
                        fontSize: 16
                        onActivated: (index) => Builder.setRpe(page.selIndex, index - 1)
                    }
                }
            }
        }

        Text {
            visible: !page.hasSel
            anchors.centerIn: parent
            text: "Select a block to edit it"
            color: Theme.textSecondary
            font.pixelSize: 17
        }

        // summary
        Rectangle {
            x: 20
            width: parent.width - 40
            height: 1
            anchors.bottom: summaryTitle.top
            anchors.bottomMargin: 14
            color: Theme.cardBorder
        }
        Text {
            id: summaryTitle
            x: 20
            anchors.bottom: summaryRow.top
            anchors.bottomMargin: 12
            text: "Workout Summary"
            color: Theme.text
            font.pixelSize: 19
            font.weight: Font.Bold
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.baseline: summaryTitle.baseline
            text: Builder.intensity + "% " + page.ftpLabel + "  •  TSS " + Builder.tss
            color: Theme.textSecondary
            font.pixelSize: 14
        }
        RowLayout {
            id: summaryRow
            x: 20
            width: parent.width - 40
            height: 78
            anchors.bottom: saveButton.top
            anchors.bottomMargin: 20
            spacing: 10
            StatTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                iconName: "clock"
                caption: "Total duration"
                value: Math.round(Builder.totalDurationSec / 60) + " min"
            }
            StatTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                iconName: "dashboard"
                iconColor: Theme.accentLight
                caption: "Blocks"
                value: Builder.blockCount + " blocks"
            }
        }
        AppButton {
            id: saveButton
            x: 20
            width: parent.width - 40
            height: 62
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            iconName: "save"
            text: Builder.dirty ? "Save Workout •" : "Save Workout"
            fontSize: 20
            onClicked: page.save()
        }
    }

    // --- preview dialog -------------------------------------------------------
    AppDialog {
        id: previewDialog
        title: Builder.name
        panelWidth: 980
        panelHeight: 560

        Text {
            x: 28
            y: 72
            text: Math.round(Builder.totalDurationSec / 60) + " min  •  " + Builder.blockCount + " blocks  •  "
                  + Builder.intensity + "% " + page.ftpLabel + "  •  estimated TSS " + Builder.tss + "  •  " + page.ftpLabel + " " + Builder.ftp + " W"
            color: Theme.textSecondary
            font.pixelSize: 17
        }
        ZoneLegend {
            x: 28
            y: 108
            showHeartRate: false
        }
        WorkoutChart {
            x: 14
            y: 146
            width: previewDialog.panelWidth - 40
            height: 300
            segments: Builder.segments
            totalSec: Builder.totalDurationSec
            showHeartRate: false
            ftp: Builder.ftp
        }
        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            Item { Layout.fillWidth: true }
            AppButton {
                kind: "secondary"
                text: "Close"
                onClicked: previewDialog.close()
            }
            AppButton {
                iconName: "save"
                text: "Save Workout"
                onClicked: {
                    previewDialog.close()
                    page.save()
                }
            }
        }
    }
}
