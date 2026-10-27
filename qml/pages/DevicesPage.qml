import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

Item {
    id: page
    signal openGearSetup()
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

    Component {
        id: virtualGearsCard
        Rectangle {
            id: vg
            readonly property var setup: Gears.gearSetup
            readonly property var rings: (setup.chainrings || []).slice().reverse()
            readonly property var cogs: setup.cogs || []
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
                        Icon { name: "cog"; size: 24; color: Theme.orange; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: "Virtual gears"
                            color: Theme.text
                            font.pixelSize: 18
                            font.weight: Font.Bold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Text {
                            text: "Use virtual gears"
                            color: Theme.textSecondary
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        ToggleSwitch {
                            width: 46; height: 26
                            anchors.verticalCenter: parent.verticalCenter
                            onColor: Theme.orange
                            checked: Gears.enabled
                            onToggled: (on) => Gears.enabled = on
                        }
                    }
                }
                Text {
                    width: parent.width
                    text: Gears.summary
                    color: Theme.text
                    font.pixelSize: 15
                    elide: Text.ElideRight
                }
                // the road model of the rider: what the gears weigh
                Item {
                    width: parent.width
                    height: 22
                    Text {
                        id: physicsText
                        width: parent.width - physicsLinks.width - 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: Gears.physicsSummary
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Row {
                        id: physicsLinks
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14
                        Repeater {
                            model: [
                                { label: "Edit weight", action: "profile" },
                                { label: "Bike & air drag", action: "setup" }
                            ]
                            Text {
                                text: modelData.label
                                color: Theme.accentText
                                font.pixelSize: 14
                                font.underline: linkArea.containsMouse
                                MouseArea {
                                    id: linkArea
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.action === "profile")
                                            page.openProfile()
                                        else
                                            page.openGearSetup()
                                    }
                                }
                            }
                        }
                    }
                }

                // current gear (live) + test shifting + setup
                Item {
                    width: parent.width
                    height: 52
                    IconButton {
                        id: vgEasier
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
                        width: 132
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            id: gearText
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Gears.gearLabel.replace("×", " × ")
                            color: Theme.text
                            font.pixelSize: 24
                            font.weight: Font.Black
                            SequentialAnimation {
                                id: gearPulse
                                NumberAnimation { target: gearText; property: "scale"; to: 1.12; duration: 80 }
                                NumberAnimation { target: gearText; property: "scale"; to: 1; duration: 220; easing.type: Easing.OutCubic }
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Gear " + (Gears.gearIndex + 1) + " / " + Gears.gearCount
                            color: Theme.textSecondary
                            font.pixelSize: 13
                        }
                    }
                    IconButton {
                        x: vgGear.x + vgGear.width + 10
                        width: 44; height: 44
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "plus"
                        tooltip: "Shift harder"
                        enabled: Gears.canShiftUp
                        onClicked: Gears.shiftUp()
                    }
                    AppButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 156
                        height: 44
                        kind: "secondary"
                        fontSize: 15
                        iconSize: 18
                        bold: false
                        iconName: "cog"
                        text: "Gear Setup…"
                        onClicked: page.openGearSetup()
                    }
                }

                // the real gear on the trainer (reference for the trainer's own simulation)
                Item {
                    width: parent.width
                    height: 42
                    Text {
                        text: "Bike on the trainer"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        AppComboBox {
                            width: 96; height: 40
                            fontSize: 15
                            model: vg.rings
                            currentIndex: vg.rings.indexOf(Gears.referenceFrontTeeth)
                            onActivated: (index) => Gears.setReferenceGear(vg.rings[index], Gears.referenceRearTeeth)
                        }
                        Text { text: "×"; color: Theme.text; font.pixelSize: 17; anchors.verticalCenter: parent.verticalCenter }
                        AppComboBox {
                            width: 96; height: 40
                            fontSize: 15
                            model: vg.cogs
                            currentIndex: vg.cogs.indexOf(Gears.referenceRearTeeth)
                            onActivated: (index) => Gears.setReferenceGear(Gears.referenceFrontTeeth, vg.cogs[index])
                        }
                    }
                }
                // the real gear measured from the trainer's speed and cadence
                Item {
                    width: parent.width
                    height: 26
                    Text {
                        id: detectedText
                        width: parent.width - (useDetected.visible ? useDetected.width + 12 : 0)
                        anchors.verticalCenter: parent.verticalCenter
                        text: Gears.detectedRatio > 0
                              ? "Detected on the trainer: " + (Gears.detectedLabel !== "" ? Gears.detectedLabel + " " : "")
                                + "(" + Gears.detectedRatio.toFixed(2) + ")"
                                + (Gears.detectedDiffers ? " – used instead of " + Gears.referenceLabel : "")
                              : "Detected on the trainer: pedal steadily for a few seconds (needs speed + cadence from the trainer)"
                        color: Gears.detectedDiffers ? Theme.warningText : Theme.textSecondary
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Text {
                        id: useDetected
                        visible: Gears.detectedDiffers && Gears.detectedLabel !== ""
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Save " + Gears.detectedLabel
                        color: Theme.accentText
                        font.pixelSize: 14
                        font.underline: useArea.containsMouse
                        MouseArea {
                            id: useArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Gears.useDetectedGear()
                        }
                    }
                }
                Text {
                    visible: Gears.heavyRealGear
                    width: parent.width
                    text: "Tip: put the bike in a small real gear, e.g. 34×17, and choose it above. The trainer's "
                          + "flywheel then spins slower, so the easy virtual gears (and descents) get lighter."
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                // how the virtual gear reaches a trainer that simulates gradients
                Item {
                    width: parent.width
                    height: 44
                    Text {
                        text: "Virtual shifting method"
                        color: Theme.textSecondary
                        font.pixelSize: 14
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
                            { value: "auto", label: "Auto (recommended)" },
                            { value: "gradient", label: "Gradient" },
                            { value: "erg", label: "ERG" }
                        ]
                        value: Gears.shiftMethod
                        onActivated: (v) => Gears.shiftMethod = v
                    }
                }
                Text {
                    width: parent.width
                    text: Gears.shiftMethod === "auto"
                          ? "Auto: gradient simulation for real effort, ERG at the gear's watts for light gears and descents – the lightest load your trainer can give."
                          : Gears.shiftMethod === "gradient"
                            ? "Gradient: the trainer always gets a gear-adjusted grade. Light gears can feel heavy if your trainer ignores negative grades."
                            : "ERG: the trainer always holds the watts of your gear. For trainers whose gradient simulation misbehaves."
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: "Shift with Zwift Click (or the on-screen buttons). Leave your bike in "
                          + Gears.referenceLabel + " on the trainer."
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
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
                    { k: "Rower data", v: Devices.rowerConnected
                                          ? Devices.rowerDataSource + "  •  " + Devices.rowerStrokesPerMinute + " SPM"
                                            + (Devices.rowerStrokeRateEstimated ? " (from stroke timing)" : "")
                                          : "–" }
                ]
                Item {
                    width: parent.width
                    // five rows share the space below the header
                    height: Math.max(26, Math.min(36, (diagCard.height - 64 - 8) / 5))
                    Rectangle { width: parent.width; height: 1; color: Theme.cardBorder }
                    Text { x: 6; anchors.verticalCenter: parent.verticalCenter; text: modelData.k; color: Theme.text; font.pixelSize: 17 }
                    Text {
                        x: 175
                        width: parent.width - x - 6
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.v
                        color: modelData.k === "Signal" && modelData.v === "Stable" ? Theme.success : Theme.textSecondary
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
            text: "Sends a temporary ERG command to test trainer control. Your trainer will adjust to the selected power."
            color: Theme.textSecondary
            font.pixelSize: 14
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
            height: diagDialog.panelHeight - 118 - 92
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
