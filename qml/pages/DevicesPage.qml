import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

Item {
    id: page
    signal openGearSetup()   // the advanced bike dialog (wheel, weight, air drag)
    signal openProfile()
    property int ergTarget: 150
    property bool trainersOpen: true
    property bool hrOpen: true
    property bool rowersOpen: true
    property bool othersOpen: true
    property bool controllersOpen: true
    // live Zwift Click buttons (Devices.controllerButtonsChanged), for testing
    property bool clickPlus: false
    property bool clickMinus: false
    readonly property var sectionTrainer: ({ title: "Smart Trainers", icon: "bike", kind: "trainer" })
    readonly property var sectionHr: ({ title: "Heart Rate Sensors", icon: "heart", kind: "hr" })
    readonly property var sectionRower: ({ title: "Rowers (Concept2 PM5)", icon: "rowing", kind: "rower" })
    readonly property var sectionOther: ({ title: "Other Bluetooth devices", icon: "bluetooth", kind: "other" })
    readonly property var sectionController: ({ title: "Controllers (Zwift Click)", icon: "controller", kind: "controller" })

    Connections {
        target: Devices
        function onControllerButtonsChanged(plus, minus) {
            page.clickPlus = plus
            page.clickMinus = minus
        }
    }

    // the controllers section (set by its delegate), for revealControllers()
    property Item controllerSection: null
    function revealControllers() {
        page.controllersOpen = true
        Qt.callLater(function() {
            if (page.controllerSection)
                devFlick.contentY = Math.max(0, Math.min(page.controllerSection.y,
                                                         devFlick.contentHeight - devFlick.height))
        })
    }

    function paceText(sec) {
        if (!(sec > 0) || sec > 3600)
            return "–"
        // round to tenths first, so 119.97 s reads "2:00.0" (not "1:60.0")
        var tenths = Math.round(sec * 10)
        var m = Math.floor(tenths / 600)
        var t = tenths - m * 600            // tenths of a second within the minute
        var s = Math.floor(t / 10)
        return m + ":" + (s < 10 ? "0" : "") + s + "." + (t % 10)
    }

    PageHeader {
        x: Theme.pageMargin + 4
        y: 30
        title: "Devices"
        subtitle: "Connect and manage your training equipment"
    }

    // --- status row ----------------------------------------------------------------
    RowLayout {
        x: Theme.pageMargin - 6
        y: 138
        width: parent.width - 2 * Theme.pageMargin + 6
        height: 112
        spacing: Theme.spacing

        Card {
            Layout.preferredWidth: 438
            Layout.fillHeight: true
            Icon {
                x: 30
                anchors.verticalCenter: parent.verticalCenter
                name: "bluetooth"
                size: 52
                strokeWidth: 2.4
                color: Devices.bluetoothAvailable ? Theme.accentLight : Theme.textMuted
            }
            Column {
                x: 116
                anchors.verticalCenter: parent.verticalCenter
                Text { text: "Bluetooth"; color: Theme.textSecondary; font.pixelSize: 17 }
                Text { text: Devices.adapterStatus; color: Theme.text; font.pixelSize: 32; font.weight: Font.Bold }
                Text {
                    text: Devices.scanError.length > 0 && !Devices.scanning ? Devices.scanStatus || "Bluetooth problem"
                          : Devices.scanning ? "Scanning for nearby devices…" : (Devices.scanStatus || (Devices.bluetoothAvailable ? "Ready to scan" : "Virtual devices available"))
                    color: Devices.scanError.length > 0 && !Devices.scanning ? Theme.danger : Theme.textSecondary
                    font.pixelSize: 15
                    width: 300
                    elide: Text.ElideRight
                }
            }
        }
        Card {
            Layout.preferredWidth: 420
            Layout.fillHeight: true
            Icon {
                x: 30
                anchors.verticalCenter: parent.verticalCenter
                name: "link"
                size: 52
                strokeWidth: 2.4
                color: Theme.cyan
            }
            Column {
                x: 116
                anchors.verticalCenter: parent.verticalCenter
                Text { text: Devices.connectedCount + (Devices.connectedCount === 1 ? " Device" : " Devices"); color: Theme.textSecondary; font.pixelSize: 17 }
                Text { text: Devices.connectedCount > 0 ? "Connected" : "Not connected"; color: Theme.text; font.pixelSize: 32; font.weight: Font.Bold }
                Text { text: Devices.connectedSummary; color: Theme.textSecondary; font.pixelSize: 15 }
            }
        }
        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            AppButton {
                x: 16
                y: 16
                width: parent.width - 32 - 66
                height: 56
                iconName: "search"
                text: Devices.scanning ? "Scanning…" : "Scan for Devices"
                fontSize: 19
                onClicked: Devices.scanning ? Devices.stopScan() : Devices.startScan()
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 16
                y: 20
                width: 52; height: 48
                iconName: "refresh"
                iconSize: 24
                tooltip: "Refresh adapter"
                onClicked: Devices.refreshAdapter()
                RotationAnimation on rotation {
                    running: Devices.scanning
                    loops: Animation.Infinite
                    from: 0; to: 360; duration: 1200
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                text: "Click to scan for nearby Bluetooth devices"
                color: Theme.textSecondary
                font.pixelSize: 15
            }
        }
    }

    // --- available devices --------------------------------------------------------
    Card {
        id: availableCard
        x: Theme.pageMargin - 6
        y: 264
        width: 636
        height: 470
        title: "Available Devices"
        titleSize: 22
        headerRight: [
            Row {
                spacing: 8
                Row {
                    spacing: 8
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "Show all devices"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ToggleSwitch {
                        width: 42; height: 24
                        anchors.verticalCenter: parent.verticalCenter
                        onColor: Theme.accent
                        checked: Devices.showAllDevices
                        onToggled: (on) => Devices.showAllDevices = on
                    }
                    Item { width: 10; height: 1 }
                }
                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: rescanRow.width
                    height: rescanRow.height
                    Row {
                        id: rescanRow
                        spacing: 8
                        Icon { name: "refresh"; size: 20; color: Theme.accentLight; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "Rescan"; color: Theme.accentLight; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Devices.startScan()
                    }
                }
            }
        ]

        Flickable {
            id: devFlick
            x: 12
            y: 58
            width: parent.width - 24
            height: parent.height - 70
            contentHeight: devCol.height + 12
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: devCol
                width: parent.width
                spacing: 12

                // Bluetooth problem (scan error, adapter off, not a PM5, ...)
                Rectangle {
                    visible: Devices.scanError.length > 0
                    width: devCol.width
                    height: visible ? errText.implicitHeight + 24 : 0
                    radius: 10
                    color: Qt.rgba(0.95, 0.3, 0.35, 0.12)
                    border.color: Theme.danger
                    Icon { x: 12; y: 12; name: "info"; size: 20; color: Theme.danger }
                    Text {
                        id: errText
                        x: 42; y: 12
                        width: parent.width - 42 - 40
                        text: Devices.scanError
                        color: Theme.text
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        y: 10
                        text: "✕"
                        color: Theme.textSecondary
                        font.pixelSize: 16
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -8
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Devices.clearScanError()
                        }
                    }
                }

                Repeater {
                    // Rowers right after the trainers (heart rate sensors serve
                    // both); a connected rower without a trainer comes first.
                    // Controllers (classified Zwift Clicks) are always listed;
                    // "Show all devices" adds the unclassified ones.
                    // A static model (sections keyed by kind): a model rebuilt on every
                    // connection change recreated all sections and closed their menus.
                    // The rower has two slots; only one of them is visible.
                    model: [page.sectionRower, page.sectionTrainer, page.sectionRower,
                            page.sectionHr, page.sectionController, page.sectionOther]
                    Rectangle {
                        id: section
                        readonly property bool rowerFirst: Devices.rowerConnected && Devices.trainerState !== 2
                        visible: index === 0 ? rowerFirst
                               : index === 2 ? !rowerFirst
                               : modelData.kind === "other" ? Devices.showAllDevices : true
                        readonly property string kindName: modelData.kind
                        readonly property bool open: modelData.kind === "trainer" ? page.trainersOpen
                                                   : modelData.kind === "rower" ? page.rowersOpen
                                                   : modelData.kind === "other" ? page.othersOpen
                                                   : modelData.kind === "controller" ? page.controllersOpen : page.hrOpen
                        readonly property var listModel: modelData.kind === "trainer" ? Devices.trainers
                                                       : modelData.kind === "rower" ? Devices.rowers
                                                       : modelData.kind === "other" ? Devices.otherDevices
                                                       : modelData.kind === "controller" ? Devices.controllers : Devices.heartRateSensors
                        width: devCol.width
                        height: 50 + (open ? listCol.height + 8 : 0)
                        radius: 10
                        color: Theme.cardStrong
                        border.color: Theme.cardBorder
                        clip: true
                        Component.onCompleted: if (kindName === "controller") page.controllerSection = section

                        Row {
                            x: 14
                            y: 12
                            spacing: 14
                            Icon {
                                name: modelData.icon
                                size: 28
                                color: modelData.kind === "hr" ? Theme.danger
                                     : modelData.kind === "rower" ? Theme.rowingColor
                                     : modelData.kind === "controller" ? Theme.orange
                                     : modelData.kind === "other" ? Theme.textSecondary : Theme.accentLight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.title
                                color: Theme.text
                                font.pixelSize: 18
                                font.weight: Font.Bold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Icon {
                            id: chevron
                            anchors.right: parent.right
                            anchors.rightMargin: 18
                            y: 14
                            name: section.open ? "chevronUp" : "chevronDown"
                            size: 22
                        }
                        // connected device of this kind, visible even when collapsed
                        Rectangle {
                            readonly property string connectedName: section.kindName === "trainer"
                                                                     ? (Devices.trainerState === 2 ? Devices.trainerName : "")
                                                                     : section.kindName === "rower"
                                                                       ? (Devices.rowerState === 2 ? Devices.rowerName : "")
                                                                       : section.kindName === "controller"
                                                                         ? (Devices.controllerConnected
                                                                            ? (Devices.controllerCount > 1 ? Devices.controllerCount + " Zwift Click pods" : Devices.controllerName)
                                                                            : "")
                                                                         : section.kindName === "other" ? ""
                                                                         : (Devices.heartRateState === 2 ? Devices.heartRateName : "")
                            visible: connectedName.length > 0
                            anchors.right: chevron.left
                            anchors.rightMargin: 12
                            y: 11
                            height: 28
                            width: Math.min(220, badgeRow.implicitWidth + 22)
                            radius: 14
                            color: Qt.rgba(0.2, 0.85, 0.4, 0.12)
                            border.color: Qt.rgba(0.25, 0.85, 0.45, 0.6)
                            clip: true
                            Row {
                                id: badgeRow
                                x: 11
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6
                                Rectangle { width: 8; height: 8; radius: 4; color: Theme.success; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    text: parent.parent.connectedName
                                    color: Theme.text
                                    font.pixelSize: 13
                                    width: Math.min(implicitWidth, 180)
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        MouseArea {
                            width: parent.width
                            height: 50
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.kind === "trainer")
                                    page.trainersOpen = !page.trainersOpen
                                else if (modelData.kind === "rower")
                                    page.rowersOpen = !page.rowersOpen
                                else if (modelData.kind === "other")
                                    page.othersOpen = !page.othersOpen
                                else if (modelData.kind === "controller")
                                    page.controllersOpen = !page.controllersOpen
                                else
                                    page.hrOpen = !page.hrOpen
                            }
                        }

                        Column {
                            id: listCol
                            y: 50
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: section.listModel
                                Rectangle {
                                    x: 0
                                    width: listCol.width
                                    readonly property bool rowerLive: section.kindName === "rower" && linkState === 2
                                    height: rowerLive ? 160 : 92
                                    radius: 10
                                    color: linkState === 2 ? Theme.navSelected : "transparent"
                                    border.width: linkState === 2 ? 1.5 : 0
                                    border.color: Theme.accent

                                    Rectangle {
                                        x: 12
                                        y: 10
                                        width: 86; height: 72; radius: 8
                                        color: Theme.field
                                        Icon {
                                            anchors.centerIn: parent
                                            name: section.kindName === "hr" ? "heartPulse" : section.kindName === "rower" ? "rowing"
                                                : section.kindName === "controller" ? "controller"
                                                : section.kindName === "other" ? "bluetooth" : "trainer"
                                            size: 44
                                            strokeWidth: 1.6
                                            color: section.kindName === "hr" ? "#ff6b8a" : section.kindName === "rower" ? Theme.rowingColor
                                                 : section.kindName === "controller" ? Theme.orange
                                                 : section.kindName === "other" ? Theme.textSecondary : Theme.text
                                        }
                                        Text {
                                            visible: simulated
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.bottomMargin: 2
                                            text: "VIRTUAL"
                                            color: Theme.textMuted
                                            font.pixelSize: 9
                                            font.letterSpacing: 1
                                        }
                                    }
                                    Column {
                                        x: 114
                                        y: 46 - height / 2
                                        spacing: 4
                                        Text {
                                            text: name
                                            color: Theme.text
                                            font.pixelSize: 18
                                            font.weight: Font.Bold
                                            width: 170
                                            elide: Text.ElideRight
                                        }
                                        StatusDot {
                                            visible: section.kindName !== "other"
                                            linkState: model.linkState; fontSize: 15
                                        }
                                        Text {
                                            visible: section.kindName === "other"
                                            text: Devices.signalText(rssi)
                                            color: Theme.textSecondary
                                            font.pixelSize: 14
                                        }
                                        Row {
                                            spacing: 8
                                            Rectangle { width: 12; height: 12; radius: 6; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                                            Text {
                                                text: protocol
                                                color: Theme.textSecondary
                                                font.pixelSize: 14
                                                width: section.kindName === "hr" ? 230 : section.kindName === "rower" ? 270
                                                     : section.kindName === "controller" ? (linkState === 2 ? 150 : 230)
                                                     : section.kindName === "other" ? 210 : 150
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    // live values
                                    Row {
                                        visible: linkState === 2 && section.kindName !== "rower" && section.kindName !== "controller"
                                        x: section.kindName === "hr" ? 360 : 286
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 14
                                        // static model; the live values are bindings inside
                                        // the delegate (no delegate rebuild per sensor update)
                                        Repeater {
                                            model: section.kindName === "hr" ? ["hr"] : ["power", "cadence"]
                                            Column {
                                                spacing: 2
                                                Text {
                                                    text: modelData === "hr" ? "Heart Rate"
                                                          : modelData === "power" ? (Sensors.powerSmoothing ? "Power " + Sensors.powerSmoothingSeconds + "s" : "Power")
                                                          : "Cadence"
                                                    color: Theme.textSecondary
                                                    font.pixelSize: 13
                                                }
                                                Text {
                                                    text: modelData === "hr" ? Sensors.heartRate + " BPM"
                                                          : modelData === "power" ? Sensors.displayPower + " W"
                                                          : Sensors.cadence + " RPM"
                                                    color: Theme.text
                                                    font.pixelSize: 17
                                                    font.weight: Font.Bold
                                                }
                                            }
                                        }
                                    }
                                    // live controller values: battery + button test
                                    Row {
                                        visible: linkState === 2 && section.kindName === "controller"
                                        x: 286
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 14
                                        Column {
                                            spacing: 2
                                            Text { text: "Battery"; color: Theme.textSecondary; font.pixelSize: 13 }
                                            Text {
                                                text: Devices.controllerBattery >= 0 ? Devices.controllerBattery + " %" : "–"
                                                color: Devices.controllerBattery >= 0 && Devices.controllerBattery <= 15 ? Theme.danger : Theme.text
                                                font.pixelSize: 17
                                                font.weight: Font.Bold
                                            }
                                        }
                                        Column {
                                            spacing: 2
                                            Text { text: "Buttons"; color: Theme.textSecondary; font.pixelSize: 13 }
                                            Row {
                                                spacing: 5
                                                Repeater {
                                                    model: ["−", "+"]
                                                    Rectangle {
                                                        readonly property bool down: index === 0 ? page.clickMinus : page.clickPlus
                                                        width: 24; height: 22; radius: 6
                                                        color: down ? (index === 0 ? Theme.accent : Theme.orange) : Theme.field
                                                        border.color: down ? "transparent" : Theme.fieldBorder
                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: modelData
                                                            color: parent.down ? "white" : Theme.text
                                                            font.pixelSize: 15
                                                            font.weight: Font.Bold
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    // live rower values (second line)
                                    Row {
                                        visible: parent.rowerLive
                                        x: 12
                                        y: 92
                                        spacing: 8
                                        Repeater {
                                            // static captions; values bound in the delegate
                                            model: ["Power", "Stroke rate", "Pace /500m", "Distance"]
                                            Rectangle {
                                                width: (listCol.width - 24 - 3 * 8) / 4
                                                height: 56
                                                radius: 8
                                                color: Theme.field
                                                Column {
                                                    x: 10
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    spacing: 2
                                                    Text { text: modelData; color: Theme.textSecondary; font.pixelSize: 12 }
                                                    Text {
                                                        text: index === 0 ? Devices.rowerWatts + " W"
                                                              : index === 1 ? (Devices.rowerStrokeRateEstimated ? "≈" : "") + Devices.rowerStrokesPerMinute + " SPM"
                                                              : index === 2 ? page.paceText(Devices.rowerPace500)
                                                              : Theme.thousands(Devices.rowerDistance) + " m"
                                                        color: Theme.text
                                                        font.pixelSize: 17
                                                        font.weight: Font.Bold
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    Row {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 12
                                        y: 46 - height / 2
                                        spacing: 10
                                        AppButton {
                                            visible: section.kindName !== "other"
                                            width: 112
                                            height: 44
                                            fontSize: 16
                                            bold: false
                                            kind: linkState === 0 ? "primary" : "secondary"
                                            text: linkState === 0 ? "Connect" : (linkState === 1 ? "Cancel" : "Disconnect")
                                            onClicked: linkState === 0 ? Devices.connectDevice(deviceId) : Devices.disconnectDevice(deviceId)
                                        }
                                        // fallback when a PM5 is not recognised from its advertisement
                                        AppButton {
                                            visible: section.kindName === "other"
                                            width: 170
                                            height: 44
                                            fontSize: 15
                                            bold: false
                                            kind: "secondary"
                                            iconName: "rowing"
                                            iconSize: 18
                                            iconSpacing: 8
                                            horizontalPadding: 12
                                            text: "Connect as rower"
                                            onClicked: Devices.connectAsRower(deviceId)
                                        }
                                        // A PM5 (or other FTMS rowing machine) that was listed
                                        // as trainer: connect it with the rower driver instead.
                                        IconButton {
                                            visible: section.kindName === "trainer" && !simulated && linkState === 0
                                            width: 44; height: 44
                                            iconName: "rowing"
                                            tooltip: "Connect as rower"
                                            onClicked: Devices.connectAsRower(deviceId)
                                        }
                                        IconButton {
                                            width: 44; height: 44
                                            iconName: "settings"
                                            tooltip: "Device info"
                                            onClicked: {
                                                infoDialog.info = { name: name, id: deviceId, protocol: protocol, rssi: rssi, state: linkState, simulated: simulated }
                                                infoDialog.open()
                                            }
                                        }
                                    }
                                }
                            }
                            Text {
                                visible: section.listModel.count === 0
                                x: 16
                                height: 40
                                verticalAlignment: Text.AlignVCenter
                                text: Devices.scanning ? "Searching…"
                                      : section.kindName === "controller" ? "No Zwift Click found – press a button on it, then Scan"
                                      : "No devices found – press Scan for Devices"
                                elide: Text.ElideRight
                                width: listCol.width - 32
                                color: Theme.textSecondary
                                font.pixelSize: 15
                            }
                            Row {
                                visible: section.kindName === "rower" || section.kindName === "other" || section.kindName === "controller"
                                         || (section.kindName === "trainer" && section.listModel.count > (Devices.simulatorEnabled ? 1 : 0))
                                x: 16
                                height: 34
                                spacing: 8
                                Icon { name: "info"; size: 18; color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    width: listCol.width - 60
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: section.kindName === "controller"
                                          ? "Press any button on the Zwift Click to wake it  •  Click v2: pair both pods"
                                          : section.kindName === "other"
                                          ? "Unrecognised Bluetooth LE devices – is your PM5 here? Use Connect as rower."
                                          : section.kindName === "trainer"
                                          ? "A rowing machine (Concept2 PM5) listed here? Use the rowing button to connect it as rower."
                                          : "On the PM5: More Options → Turn Wireless ON → Connect Device"
                                    color: Theme.textSecondary
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }
                            }
                            // virtual gears: the bike used with the Zwift Click in
                            // Free Ride and Video Ride (VirtualGears, "Gears")
                            Loader {
                                active: section.kindName === "controller"
                                visible: active
                                width: listCol.width
                                sourceComponent: virtualGearsCard
                            }
                        }
                    }
                }
            }
        }
    }

    // Virtual shifting = the Zwift Click (no switch, no gear setup): Zwift's
    // 24 gears while a Click is connected, sent with the Zwift protocol when
    // the connected trainer has the Zwift trainer service, else as an FTMS
    // gradient; without a Click the rider shifts the real gears.
    Component {
        id: virtualGearsCard
        Rectangle {
            id: vg
            width: parent ? parent.width : 0
            height: vgCol.implicitHeight + 24
            radius: 10
            color: Theme.navSelected
            border.color: Gears.enabled ? Theme.orange : Theme.cardBorder
            border.width: Gears.enabled ? 1.5 : 1

            Connections {
                target: Gears
                function onShifted(direction) { gearPulse.restart() }
            }

            Column {
                id: vgCol
                x: 14; y: 12
                width: parent.width - 28
                spacing: 6

                Item {
                    width: parent.width
                    height: 30
                    Row {
                        spacing: 10
                        anchors.verticalCenter: parent.verticalCenter
                        Icon { name: "cog"; size: 24; color: Gears.enabled ? Theme.orange : Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            objectName: "shiftingStatus"
                            text: Gears.shiftingStatus
                            color: Theme.text
                            font.pixelSize: 18
                            font.weight: Font.Bold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                Text {
                    objectName: "shiftingDetail"
                    width: parent.width
                    text: Gears.shiftingDetail
                    color: Gears.shiftingProblem ? Theme.warningText : Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }

                // current gear + on-screen shift buttons (only with a Zwift Click)
                Item {
                    visible: Gears.enabled
                    width: parent.width
                    height: visible ? 52 : 0
                    IconButton {
                        id: vgEasier
                        objectName: "devicesShiftEasier"
                        width: 44; height: 44
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "minus"
                        tooltip: "Shift easier"
                        enabled: Gears.canShiftDown
                        onClicked: Gears.shiftDown()
                    }
                    Column {
                        id: vgGear
                        x: vgEasier.width + 10
                        width: 190
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            id: gearText
                            objectName: "devicesGearText"
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Gears.gearText
                            color: Theme.text
                            font.pixelSize: 22
                            font.weight: Font.Black
                            SequentialAnimation {
                                id: gearPulse
                                NumberAnimation { target: gearText; property: "scale"; to: 1.12; duration: 80 }
                                NumberAnimation { target: gearText; property: "scale"; to: 1; duration: 220; easing.type: Easing.OutCubic }
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Zwift's 24 virtual gears"
                            color: Theme.textSecondary
                            font.pixelSize: 13
                        }
                    }
                    IconButton {
                        objectName: "devicesShiftHarder"
                        x: vgGear.x + vgGear.width + 10
                        width: 44; height: 44
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "plus"
                        tooltip: "Shift harder"
                        enabled: Gears.canShiftUp
                        onClicked: Gears.shiftUp()
                    }
                }
                Text {
                    visible: !Gears.enabled
                    width: parent.width
                    text: "Connect a Zwift Click for virtual shifting with Zwift's 24 gears – nothing to set up. "
                          + "Trainers with the Zwift trainer service (e.g. Zwift Hub, Jetblack Victory) then get the Zwift protocol automatically."
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                // FTMS gradient only: the real gear on the trainer matters
                Text {
                    visible: Gears.referenceNeeded && Gears.referencePrompt === ""
                    width: parent.width
                    text: "Leave your bike in " + Gears.referenceLabel + " on the trainer."
                          + (Gears.detectedRatio > 0 && !Gears.detectedLooksVirtual
                             ? "  Detected: " + (Gears.detectedLabel !== "" ? Gears.detectedLabel + " " : "")
                               + "(" + Gears.detectedRatio.toFixed(2) + ")" : "")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    objectName: "referencePrompt"
                    visible: Gears.referencePrompt !== ""
                    width: parent.width
                    height: visible ? promptText.implicitHeight + 54 : 0
                    radius: 8
                    color: Qt.rgba(1, 0.65, 0.1, 0.10)
                    border.color: Theme.warningText
                    Text {
                        id: promptText
                        x: 12; y: 8
                        width: parent.width - 24
                        text: Gears.referencePrompt
                        color: Theme.warningText
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                    Row {
                        x: 12
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        spacing: 10
                        AppButton {
                            height: 32
                            fontSize: 13
                            bold: false
                            horizontalPadding: 12
                            text: "Use detected gear" + (Gears.detectedLabel !== "" ? " (" + Gears.detectedLabel + ")" : "")
                            onClicked: Gears.useDetectedGear()
                        }
                        AppButton {
                            height: 32
                            fontSize: 13
                            bold: false
                            horizontalPadding: 12
                            kind: "secondary"
                            text: "Keep " + Gears.referenceLabel
                            onClicked: Gears.dismissReferencePrompt()
                        }
                    }
                }

                // the road model of the rider: what the gears weigh
                Item {
                    width: parent.width
                    height: 22
                    Text {
                        width: parent.width - weightLink.width - 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: Gears.physicsSummary
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Text {
                        id: weightLink
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Edit weight"
                        color: Theme.accentText
                        font.pixelSize: 14
                        font.underline: weightArea.containsMouse
                        MouseArea {
                            id: weightArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.openProfile()
                        }
                    }
                }
            }
        }
    }

    // --- diagnostics -------------------------------------------------------------
    Card {
        id: diagCard
        x: availableCard.x
        y: availableCard.y + availableCard.height + Theme.spacing
        width: availableCard.width
        height: parent.height - y - Theme.pageMargin
        Row {
            x: 18; y: 20
            spacing: 12
            Icon { name: "bluetooth"; size: 30; color: Theme.accentLight; strokeWidth: 2.2 }
            Text { text: "Bluetooth Diagnostics"; color: Theme.text; font.pixelSize: 22; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
        }
        AppButton {
            id: refreshAdapterButton
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: 16
            height: 44
            width: 130
            kind: "secondary"
            iconName: "refresh"
            text: "Refresh"
            fontSize: 16
            bold: false
            onClicked: Devices.refreshAdapter()
        }
        AppButton {
            anchors.right: refreshAdapterButton.left
            anchors.rightMargin: 10
            y: 16
            height: 44
            width: 130
            kind: "secondary"
            iconName: "info"
            text: "Details…"
            fontSize: 16
            bold: false
            onClicked: diagDialog.show()
        }
        Column {
            x: 18
            y: 64
            width: parent.width - 36
            Repeater {
                model: [
                    { k: "Adapter", v: Devices.adapterName },
                    { k: "Connection mode", v: "Bluetooth Low Energy" },
                    { k: "Signal", v: Devices.trainerState === 2 ? (Devices.trainerRssi >= -75 ? "Stable" : "Weak") : "–", good: Devices.trainerRssi >= -75 },
                    { k: "Last scan", v: Devices.lastScanText },
                    { k: "Trainer control", v: Devices.trainerControlStatus.length > 0 ? Devices.trainerControlStatus : "–",
                      problem: Devices.trainerControlProblem },
                    { k: "Rower data", v: Devices.rowerConnected
                                          ? Devices.rowerDataSource + "  •  " + Devices.rowerStrokesPerMinute + " SPM"
                                            + (Devices.rowerStrokeRateEstimated ? " (from stroke timing)" : "")
                                          : "–" }
                ]
                Item {
                    width: parent.width
                    // six rows share the space below the header
                    height: Math.max(24, Math.min(36, (diagCard.height - 64 - 8) / 6))
                    Rectangle { width: parent.width; height: 1; color: Theme.cardBorder }
                    Text { x: 6; anchors.verticalCenter: parent.verticalCenter; text: modelData.k; color: Theme.text; font.pixelSize: 17 }
                    Text {
                        x: 175
                        width: parent.width - x - 6
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.v
                        color: modelData.problem ? Theme.warningText
                               : modelData.k === "Signal" && modelData.v === "Stable" ? Theme.success : Theme.textSecondary
                        font.pixelSize: 17
                    }
                }
            }
        }
    }

    // --- selected trainer ------------------------------------------------------------
    Card {
        id: trainerCard
        x: availableCard.x + availableCard.width + Theme.spacing + 6
        y: 264
        width: parent.width - x - Theme.pageMargin
        height: 240
        title: "Selected Trainer"
        titleSize: 22
        // the connected rower is shown here too (its details are in Available Devices)
        headerRight: [
            Item {
                visible: Devices.rowerName.length > 0
                width: rowerPill.width
                height: 30
                Rectangle {
                    id: rowerPill
                    height: 30
                    width: Math.min(330, rowerPillRow.implicitWidth + 24)
                    radius: 15
                    color: Theme.cardStrong
                    border.color: Devices.rowerState === 2 ? Theme.rowingColor : Theme.cardBorder
                    clip: true
                    Row {
                        id: rowerPillRow
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7
                        Icon { name: "rowing"; size: 18; color: Theme.rowingColor; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: "Rower: " + Devices.rowerName + "  •  "
                                  + (Devices.rowerState === 2 ? "Connected" : Devices.rowerState === 1 ? "Connecting…" : "Disconnected")
                            color: Theme.text
                            font.pixelSize: 14
                            width: Math.min(implicitWidth, 280)
                            elide: Text.ElideMiddle
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        ]

        Item {
            anchors.fill: parent
            visible: Devices.trainerName.length > 0

            Rectangle {
                x: 18; y: 58
                width: 162; height: 116; radius: 10
                color: Theme.field
                Icon { anchors.centerIn: parent; name: "trainer"; size: 82; strokeWidth: 1.3 }
            }
            Column {
                x: 200; y: 64
                spacing: 8
                Text { text: Devices.trainerName; color: Theme.text; font.pixelSize: 24; font.weight: Font.Bold }
                StatusDot {
                    linkState: Devices.trainerState
                    text: Devices.trainerState === 2 ? "Connected" : Devices.trainerState === 1 ? "Connecting…" : "Disconnected"
                    fontSize: 19
                }
                Row {
                    spacing: 18
                    Row {
                        spacing: 6
                        Icon { name: "bluetooth"; size: 18; color: Theme.accentLight; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: Devices.trainerProtocol; color: Theme.text; font.pixelSize: 16 }
                    }
                    Row {
                        spacing: 6
                        visible: Devices.trainerState === 2
                        Icon { name: "signal"; size: 20; color: Theme.success; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: Devices.signalText(Devices.trainerRssi); color: Theme.text; font.pixelSize: 16 }
                    }
                }
                // what the trainer ACTUALLY does (from its answers), or the problem
                Text {
                    objectName: "trainerControlStatus"
                    visible: Devices.trainerState === 2
                    width: trainerCard.width - 200 - 18
                    // the shifting status line (Zwift Click: Zwift protocol / FTMS
                    // gradient; else the bike's gears), then what the trainer does
                    text: Gears.shiftingStatus
                          + (Devices.trainerControlStatus.length > 0 ? "  •  " + Devices.trainerControlStatus : "")
                          + (Devices.trainerInclinationNote.length > 0 ? "  •  " + Devices.trainerInclinationNote : "")
                    color: Devices.trainerControlProblem || Gears.shiftingProblem ? Theme.warningText : Theme.textSecondary
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    ToolTip.visible: statusHover.containsMouse
                    ToolTip.text: text
                    MouseArea { id: statusHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                }
            }
            Rectangle {
                x: 12
                y: 184
                width: parent.width - 24
                height: 46
                radius: 10
                color: Theme.cardStrong
                border.color: Theme.cardBorder
                Text {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Capabilities"
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Repeater {
                        model: Devices.trainerCapabilities
                        Rectangle {
                            height: 34
                            width: capText.implicitWidth + 40
                            radius: 17
                            color: Qt.rgba(0.2, 0.85, 0.4, 0.12)
                            border.color: Qt.rgba(0.25, 0.85, 0.45, 0.6)
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                Icon {
                                    name: modelData === "Power" ? "bolt" : modelData === "Cadence" ? "bike"
                                        : modelData === "ERG Control" ? "target" : modelData === "Heart Rate" ? "heart" : "trendUp"
                                    size: 17
                                    color: Theme.success
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text { id: capText; text: modelData; color: Theme.text; font.pixelSize: 13 }
                            }
                        }
                    }
                }
            }
        }
        Text {
            visible: Devices.trainerName.length === 0
            anchors.centerIn: parent
            text: "No trainer connected"
            color: Theme.textSecondary
            font.pixelSize: 18
        }
    }

    // --- trainer control test ----------------------------------------------------------
    Card {
        id: testCard
        x: trainerCard.x
        y: trainerCard.y + trainerCard.height + Theme.spacing
        width: trainerCard.width
        height: 176
        title: "Trainer Control Test"
        titleSize: 22

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: 22
            spacing: 8
            Icon { name: "info"; size: 20; color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Only available when trainer supports control"; color: Theme.textSecondary; font.pixelSize: 14 }
        }
        Text { x: 18; y: 58; text: "ERG Target"; color: Theme.text; font.pixelSize: 17 }
        Rectangle {
            x: 18; y: 88
            width: 236; height: 64; radius: 10
            color: Theme.field
            border.color: Theme.fieldBorder
            IconButton {
                x: 8; anchors.verticalCenter: parent.verticalCenter
                width: 48; height: 48
                iconName: "minus"
                onClicked: {
                    page.ergTarget = Math.max(25, page.ergTarget - 10)
                    if (Devices.ergTestActive) Devices.startErgTest(page.ergTarget)
                }
            }
            Text {
                anchors.centerIn: parent
                text: page.ergTarget + " W"
                color: Theme.text
                font.pixelSize: 28
                font.weight: Font.Bold
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 48; height: 48
                iconName: "plus"
                onClicked: {
                    page.ergTarget = Math.min(1500, page.ergTarget + 10)
                    if (Devices.ergTestActive) Devices.startErgTest(page.ergTarget)
                }
            }
        }
        AppButton {
            x: 272; y: 62
            width: (parent.width - 272 - 18 - 12) * 0.55
            height: 52
            iconName: "play"
            text: Devices.ergTestActive ? "Testing… " + Sensors.power + " W" : "Test ERG"
            enabled: Devices.trainerSupportsControl && !Training.active
            onClicked: Devices.startErgTest(page.ergTarget)
        }
        AppButton {
            x: 272 + (parent.width - 272 - 18 - 12) * 0.55 + 12; y: 62
            width: (parent.width - 272 - 18 - 12) * 0.45
            height: 52
            kind: "secondary"
            iconName: "stop"
            text: "Stop Test"
            bold: false
            enabled: Devices.ergTestActive
            onClicked: Devices.stopErgTest()
        }
        Text {
            x: 272; y: 122
            width: parent.width - 272 - 18
            // while the test runs: "ERG test running – N W" (it stops by itself
            // after 5 min, when this page is left or a ride starts)
            text: Devices.ergTestActive
                  ? Devices.ergTestStatus
                  : "Sends a temporary ERG command to test trainer control. Stops by itself after 5 min, when you leave this page or a ride starts."
            color: Devices.ergTestActive ? Theme.warning : Theme.textSecondary
            font.pixelSize: 14
            font.weight: Devices.ergTestActive ? Font.Bold : Font.Normal
            wrapMode: Text.WordWrap
        }
    }

    // --- preferences ----------------------------------------------------------------
    Card {
        id: prefCard
        x: trainerCard.x
        y: testCard.y + testCard.height + Theme.spacing
        width: (trainerCard.width - Theme.spacing) * 0.47
        height: parent.height - y - Theme.pageMargin
        title: "Connection Preferences"
        titleSize: 19

        Column {
            x: 16
            y: 44
            width: parent.width - 32
            spacing: 2
            Repeater {
                model: [
                    { t: "Auto-connect known devices", s: "Automatically connect to previously paired devices", p: "autoConnect" },
                    { t: "Reconnect when signal is lost", s: "Try to reconnect automatically", p: "autoReconnect" },
                    { t: "Allow multiple sensors", s: "Connect compatible BLE sensors simultaneously", p: "allowMultiple" },
                    { t: "Show virtual devices", s: "Simulated trainer, HR sensor and rower for testing", p: "simulatorEnabled" }
                ]
                Item {
                    width: parent.width
                    height: 37
                    Column {
                        width: parent.width - 60
                        Text { text: modelData.t; color: Theme.text; font.pixelSize: 15 }
                        Text { text: modelData.s; color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
                    }
                    ToggleSwitch {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 46; height: 26
                        onColor: Theme.accent
                        checked: Devices[modelData.p]
                        onToggled: (on) => Devices[modelData.p] = on
                    }
                }
            }
        }
        Item {
            x: 16
            width: parent.width - 32
            height: 44
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text { text: "Bluetooth Adapter"; color: Theme.text; font.pixelSize: 15 }
                Text { text: "Adapter used for scanning"; color: Theme.textSecondary; font.pixelSize: 11 }
            }
            AppComboBox {
                anchors.right: parent.right
                width: 128
                height: 42
                fontSize: 15
                model: Devices.adapters
                currentIndex: Devices.selectedAdapter
                onActivated: (index) => Devices.selectedAdapter = index
            }
        }
    }

    // --- connection log -------------------------------------------------------------------
    Card {
        x: prefCard.x + prefCard.width + Theme.spacing
        y: prefCard.y
        width: trainerCard.x + trainerCard.width - x
        height: prefCard.height
        title: "Connection Log"
        titleSize: 19
        headerRight: [
            Text {
                text: "Clear"
                color: Theme.accentLight
                font.pixelSize: 16
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Devices.clearLog()
                }
            }
        ]
        ListView {
            x: 16
            y: 50
            width: parent.width - 28
            height: parent.height - 60
            clip: true
            model: Devices.log
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Row {
                spacing: 12
                height: 23
                Rectangle {
                    width: 11; height: 11; radius: 5.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: modelData.ok ? Theme.success : Qt.rgba(0.6, 0.65, 0.95, 1)
                }
                Text { text: modelData.time; color: Theme.textSecondary; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: modelData.text
                    color: Theme.text
                    font.pixelSize: 13
                    width: ListView.view ? ListView.view.width - 110 : 200
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // --- Bluetooth diagnostics report ---------------------------------------------------
    AppDialog {
        id: diagDialog
        title: "Bluetooth diagnostics"
        panelWidth: 960
        panelHeight: 760
        function show() {
            reportArea.text = Devices.diagnosticsReport()
            copiedHint.visible = false
            open()
        }
        Text {
            x: 28; y: 72
            width: diagDialog.panelWidth - 56
            text: "Everything the app saw during Bluetooth scans. Put the PM5 on its Connect screen, press Scan, wait 20 s, then Refresh and Copy – paste it into a bug report."
            color: Theme.textSecondary
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
        Rectangle {
            x: 28; y: 118
            width: diagDialog.panelWidth - 56
            height: diagDialog.panelHeight - 118 - 92 - 48
            radius: 10
            color: Theme.field
            border.color: Theme.fieldBorder
            ScrollView {
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                TextArea {
                    id: reportArea
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    color: Theme.text
                    selectionColor: Theme.accent
                    font.family: "monospace"
                    font.pixelSize: 13
                    background: null
                }
            }
        }
        // the automatic diagnostic log (Settings > Diagnostic log)
        Item {
            objectName: "diagLogRow"
            x: 28
            y: diagDialog.panelHeight - 92 - 44
            width: diagDialog.panelWidth - 56
            height: 40
            Text {
                id: diagLogLabel
                text: "Log file:"
                color: Theme.textSecondary
                font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            TextEdit {
                anchors.left: diagLogLabel.right
                anchors.leftMargin: 8
                anchors.right: diagLogOpen.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                readOnly: true
                selectByMouse: true
                clip: true
                text: Logs.enabled && Logs.currentFile.length > 0 ? Logs.currentFile
                                                                  : "off – turn on Settings › Diagnostic log"
                color: Theme.text
                selectionColor: Theme.accent
                font.family: "monospace"
                font.pixelSize: 13
            }
            AppButton {
                id: diagLogOpen
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                kind: "secondary"
                iconName: "folder"
                text: "Open folder"
                fontSize: 14
                bold: false
                onClicked: { Logs.ensureFolder(); Qt.openUrlExternally(Logs.folderUrl) }
            }
        }
        Row {
            x: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            spacing: 12
            AppButton {
                kind: "secondary"
                iconName: "refresh"
                text: "Refresh"
                fontSize: 16
                bold: false
                onClicked: { reportArea.text = Devices.diagnosticsReport(); copiedHint.visible = false }
            }
            AppButton {
                kind: "secondary"
                iconName: "search"
                text: Devices.scanning ? "Scanning…" : "Scan"
                fontSize: 16
                bold: false
                onClicked: if (!Devices.scanning) Devices.startScan()
            }
            AppButton {
                objectName: "gradientTestButton"
                kind: "secondary"
                iconName: "trendUp"
                text: "Gradient test…"
                fontSize: 16
                bold: false
                onClicked: gradientDialog.open()
            }
            AppButton {
                objectName: "advancedShiftingButton"
                kind: "secondary"
                iconName: "cog"
                text: "Advanced…"
                fontSize: 16
                bold: false
                onClicked: advancedDialog.open()
            }
            Text {
                id: copiedHint
                visible: false
                text: "Copied to clipboard"
                color: Theme.success
                font.pixelSize: 15
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            spacing: 12
            AppButton {
                text: "Copy"
                iconName: "copy"
                onClicked: { Devices.copyToClipboard(reportArea.text); copiedHint.visible = true }
            }
            AppButton {
                kind: "secondary"
                text: "Close"
                onClicked: diagDialog.close()
            }
        }
    }

    // --- advanced virtual shifting overrides (not part of the normal UI) -----------------
    AppDialog {
        id: advancedDialog
        objectName: "advancedShiftingDialog"
        title: "Advanced: virtual shifting"
        panelWidth: 760
        panelHeight: 520
        Column {
            x: 28; y: 72
            width: advancedDialog.panelWidth - 56
            spacing: 12
            Text {
                width: parent.width
                text: "Normally nothing needs to be set: with a Zwift Click connected the app uses Zwift's 24 virtual gears, "
                      + "with the Zwift protocol on trainers that offer it and an FTMS gradient otherwise. "
                      + "These overrides are for troubleshooting only. Now: " + Gears.shiftingStatus + "."
                color: Theme.textSecondary
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Item {
                width: parent.width
                height: 44
                Text {
                    text: "Zwift trainer protocol"
                    color: Theme.text
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
                SegmentedControl {
                    objectName: "hubOverrideSelector"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 40
                    fontSize: 14
                    segmentPadding: 12
                    options: [
                        { value: "auto", label: "Automatic (default)" },
                        { value: "off", label: "Off – FTMS gradient only" }
                    ]
                    value: Gears.hubOverride
                    onActivated: (v) => Gears.hubOverride = v
                }
            }
            Item {
                width: parent.width
                height: 44
                Text {
                    text: "FTMS virtual shifting"
                    color: Theme.text
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
                SegmentedControl {
                    objectName: "gearMethodSelector"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 40
                    fontSize: 14
                    segmentPadding: 12
                    options: [
                        { value: "gradient", label: "Gradient (default)" },
                        { value: "auto", label: "Auto (experimental)" },
                        { value: "erg", label: "ERG (experimental)" }
                    ]
                    value: Gears.shiftMethod
                    onActivated: (v) => Gears.shiftMethod = v
                }
            }
            Text {
                width: parent.width
                text: "Gradient is the simulation (gear-adjusted grade). Auto / ERG are experimental, used only on the "
                      + "FTMS path and reset to Gradient when the app restarts."
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            AppButton {
                kind: "secondary"
                iconName: "cog"
                text: "Bike: wheel, weight, air drag…"
                fontSize: 15
                bold: false
                onClicked: { advancedDialog.close(); page.openGearSetup() }
            }
        }
        AppButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            kind: "secondary"
            text: "Close"
            onClicked: advancedDialog.close()
        }
    }

    // Screenshot aid (WATT_SCREENSHOT_MENU=diagnostics / gradienttest, see Main.qml):
    // "" closes both.
    function openScreenshotDialog(name) {
        if (name === "diagnostics")
            diagDialog.show()
        else if (diagDialog.visible)
            diagDialog.close()
        if (name === "advanced")
            advancedDialog.open()
        else if (advancedDialog.visible)
            advancedDialog.close()
        if (name === "gradienttest")
            gradientDialog.open()
        else if (gradientDialog.visible && !Devices.gradientTest.running)
            gradientDialog.close()
    }

    // --- gradient test (simulation only) ---------------------------------------------------
    AppDialog {
        id: gradientDialog
        objectName: "gradientTestDialog"
        readonly property var test: Devices.gradientTest
        title: "Gradient test"
        panelWidth: 760
        panelHeight: 600
        closable: !test.running
        Text {
            x: 28; y: 72
            width: gradientDialog.panelWidth - 56
            text: "Finds out whether your trainer really simulates descents. Pedal steadily at about 90 rpm in any real gear "
                  + "and keep it there: the app sends the gradients +2 %, 0 %, −2 % and −5 % (FTMS simulation, 20 s each) "
                  + "and measures power, speed and cadence. Virtual gears are not used during the test; afterwards "
                  + "the previous command is restored. Everything goes to the diagnostic log."
            color: Theme.textSecondary
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
        Text {
            x: 28; y: 168
            width: gradientDialog.panelWidth - 56
            text: gradientDialog.test.running
                  ? gradientDialog.test.status + "  •  " + gradientDialog.test.secondsLeft + " s left  •  "
                    + Sensors.power + " W  " + Sensors.cadence + " rpm  " + Sensors.speedKmh.toFixed(1) + " km/h"
                  : (gradientDialog.test.status.length > 0 ? gradientDialog.test.status : "Not started")
            color: Theme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        Text {
            x: 28; y: 196
            width: gradientDialog.panelWidth - 56
            visible: Devices.trainerControlStatus.length > 0
            text: "Trainer: " + Devices.trainerControlStatus
            color: Devices.trainerControlProblem ? Theme.warningText : Theme.textSecondary
            font.pixelSize: 14
            elide: Text.ElideRight
        }
        // result table
        Column {
            objectName: "gradientTestTable"
            x: 28; y: 230
            width: gradientDialog.panelWidth - 56
            Repeater {
                model: [{ h: true }].concat(gradientDialog.test.results)
                Item {
                    width: parent.width
                    height: 34
                    Rectangle { width: parent.width; height: 1; color: Theme.cardBorder; anchors.bottom: parent.bottom }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 6
                        Repeater {
                            model: modelData.h
                                   ? ["Grade", "Power", "Speed", "Cadence", "Force", "Trainer"]
                                   : [(modelData.grade > 0 ? "+" : modelData.grade < 0 ? "−" : "") + Math.abs(modelData.grade) + " %",
                                      modelData.done || modelData.samples > 0 ? modelData.power + " W" : "–",
                                      modelData.done || modelData.samples > 0 ? modelData.speedKmh.toFixed(1) + " km/h" : "–",
                                      modelData.done || modelData.samples > 0 ? modelData.cadence + " rpm" : "–",
                                      modelData.done && modelData.force > 0 ? modelData.force.toFixed(1) + " N" : "–",
                                      !modelData.done ? "" : modelData.accepted ? "accepted" : "not confirmed"]
                            Text {
                                width: index === 5 ? 150 : 106
                                text: modelData
                                color: index === 5 && text === "not confirmed" ? Theme.warningText : Theme.text
                                font.pixelSize: 15
                                font.weight: parent.parent.modelHeader ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                    readonly property bool modelHeader: modelData.h === true
                }
            }
        }
        Text {
            objectName: "gradientTestVerdict"
            x: 28; y: 230 + 5 * 34 + 14
            width: gradientDialog.panelWidth - 56
            visible: gradientDialog.test.verdict.length > 0
            text: "Result: " + gradientDialog.test.verdict
            color: Theme.text
            font.pixelSize: 15
            wrapMode: Text.WordWrap
        }
        Row {
            x: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            spacing: 12
            AppButton {
                objectName: "gradientTestStart"
                iconName: "play"
                text: "Start"
                enabled: !gradientDialog.test.running && Devices.trainerState === 2 && !Training.active
                onClicked: Devices.startGradientTest()
            }
            AppButton {
                kind: "secondary"
                iconName: "stop"
                text: "Stop"
                bold: false
                enabled: gradientDialog.test.running
                onClicked: Devices.stopGradientTest()
            }
        }
        AppButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            kind: "secondary"
            text: "Close"
            enabled: !gradientDialog.test.running
            onClicked: gradientDialog.close()
        }
    }

    // --- device info -------------------------------------------------------------------
    AppDialog {
        id: infoDialog
        property var info: ({})
        title: info.name || ""
        panelWidth: 480
        panelHeight: 330
        Column {
            x: 28; y: 80
            spacing: 10
            Repeater {
                model: [
                    ["Identifier", infoDialog.info.id || ""],
                    ["Protocol", infoDialog.info.protocol || ""],
                    ["Signal", Devices.signalText(infoDialog.info.rssi || 0)],
                    ["Status", infoDialog.info.state === 2 ? "Connected" : infoDialog.info.state === 1 ? "Connecting" : "Not connected"],
                    ["Type", infoDialog.info.simulated ? "Virtual device (simulator)" : "Bluetooth Low Energy"]
                ]
                Row {
                    Text { width: 130; text: modelData[0]; color: Theme.textSecondary; font.pixelSize: 16 }
                    Text { width: 290; text: modelData[1]; color: Theme.text; font.pixelSize: 16; elide: Text.ElideMiddle }
                }
            }
        }
        AppButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            text: "Close"
            onClicked: infoDialog.close()
        }
    }
}
