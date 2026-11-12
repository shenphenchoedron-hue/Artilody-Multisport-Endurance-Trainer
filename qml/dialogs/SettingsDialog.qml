import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Artilody.Backend

AppDialog {
    id: dlg
    title: "Settings"
    panelWidth: 640
    panelHeight: 820

    // (scrolls when the settings outgrow the panel)
    Flickable {
        id: scroller
        x: 28
        y: 84
        width: dlg.panelWidth - 56
        height: dlg.panelHeight - y - 92   // above the Done button
        contentHeight: settingsColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: scroller.contentHeight > scroller.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

    Column {
        id: settingsColumn
        width: scroller.width - 14
        spacing: 16

        component SettingRow: Item {
            property string title
            property string sub
            property alias checked: sw.checked
            signal toggled(bool on)
            width: parent ? parent.width : 0
            height: 46
            Column {
                Text { text: title; color: Theme.text; font.pixelSize: 17 }
                Text { text: sub; color: Theme.textSecondary; font.pixelSize: 13 }
            }
            ToggleSwitch {
                id: sw
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                onColor: Theme.accent
                accessibleName: title
                onToggled: (on) => parent.toggled(on)
            }
        }

        SettingRow {
            title: "Dark theme"
            sub: "Use the dark blue design"
            checked: Theme.dark
            onToggled: (on) => Theme.dark = on
        }
        SettingRow {
            title: "Show virtual devices"
            sub: "Simulated trainer + HR sensor (no hardware needed)"
            checked: Devices.simulatorEnabled
            onToggled: (on) => Devices.simulatorEnabled = on
        }
        SettingRow {
            title: "Auto-connect known devices"
            sub: "Connect automatically on start-up"
            checked: Devices.autoConnect
            onToggled: (on) => Devices.autoConnect = on
        }

        // Display smoothing of the cycling power (SensorHub.displayPower).
        // Recording, averages/Effort Power/Training Load, ERG and the FTP test use the raw power.
        Item {
            width: parent.width
            height: 46
            Column {
                Text { text: "Smooth power"; color: Theme.text; font.pixelSize: 17 }
                Text {
                    text: "Show cycling watts as a rolling average"
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }
            AppComboBox {
                id: smoothCombo
                anchors.right: smoothSwitch.left
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 104
                height: 40
                fontSize: 15
                enabled: Sensors.powerSmoothing
                opacity: enabled ? 1 : 0.5
                readonly property var seconds: [1, 3, 5, 10]
                model: ["1 s", "3 s", "5 s", "10 s"]
                currentIndex: Math.max(0, seconds.indexOf(Sensors.powerSmoothingSeconds))
                onActivated: (index) => Sensors.powerSmoothingSeconds = seconds[index]
            }
            ToggleSwitch {
                id: smoothSwitch
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                onColor: Theme.accent
                checked: Sensors.powerSmoothing
                accessibleName: "Smooth power"
                // ToggleSwitch never assigns checked itself: the binding keeps
                // following the setting (the power tiles toggle it too)
                onToggled: (on) => Sensors.powerSmoothing = on
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.cardBorder }

        // The network server (ServerSettings, docs/server-plan.md): off = this
        // station works on its own, as always.
        SettingRow {
            objectName: "serverSwitch"
            title: "Server"
            sub: Server.enabled ? "Use a server on the local network" : "Off – this station works on its own"
            checked: Server.enabled
            onToggled: (on) => Server.enabled = on
        }
        Column {
            visible: Server.enabled
            width: parent.width
            spacing: 6
            AppTextField {
                id: serverAddress
                objectName: "serverAddress"
                width: parent.width
                implicitHeight: 46
                font.pixelSize: 16
                placeholderText: "Server address, e.g. artilody-server.local"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                text: Server.address
                onEditingFinished: Server.address = text
            }
            // the address, then the connection (ServerStatus: looked for every
            // ServerStatus.intervalSeconds). Plain text: the server's name and
            // version come from the network.
            Item {
                width: parent.width
                height: Math.max(statusText.implicitHeight, checkButton.visible ? checkButton.height : 0)
                Rectangle {
                    id: statusDot
                    visible: Server.active
                    width: 12
                    height: 12
                    radius: 6
                    y: 3
                    color: ServerStatus.connected ? (ServerStatus.message.length > 0 ? "#f0b429" : Theme.success)
                           : ServerStatus.state === "checking" ? Theme.textMuted : Theme.danger
                }
                Text {
                    id: statusText
                    objectName: "serverStatus"
                    x: statusDot.visible ? 20 : 0
                    width: parent.width - x - (checkButton.visible ? checkButton.width + 12 : 0)
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    font.pixelSize: 13
                    color: (Server.address.length > 0 && !Server.addressValid)
                           || ServerStatus.state === "unreachable" || ServerStatus.state === "invalid"
                           ? Theme.dangerText : ServerStatus.connected ? Theme.text : Theme.textSecondary
                    text: Server.address.length === 0 ? "Enter the server's name or IP address (optionally :port)."
                          : !Server.addressValid ? "Not a valid address – use a name or IP address, optionally :port."
                          : ServerStatus.state === "checking" ? "Looking for the server at " + Server.url + " …"
                          : ServerStatus.connected
                            ? "Connected to " + ServerStatus.serverName + " (server " + ServerStatus.serverVersion + ")"
                              + (ServerStatus.message.length > 0 ? " – " + ServerStatus.message : "")
                          : ServerStatus.state === "invalid"
                            ? Server.url + ": " + ServerStatus.message
                          : "Not connected – " + Server.url + ": " + ServerStatus.message
                            + ". The station keeps working on its own and tries again every "
                            + ServerStatus.intervalSeconds + " s."
                            + (isNaN(ServerStatus.lastContact) ? ""
                               : " Last contact " + Qt.formatTime(ServerStatus.lastContact, "hh:mm") + ".")
                }
                AppButton {
                    id: checkButton
                    objectName: "serverCheckButton"
                    visible: Server.active
                    anchors.right: parent.right
                    kind: "secondary"
                    text: "Check now"
                    fontSize: 14
                    bold: false
                    horizontalPadding: 14
                    height: 34
                    enabled: ServerStatus.state !== "checking"
                    onClicked: ServerStatus.checkNow()
                }
            }
            // the profiles kept with the server (ProfileSync)
            Text {
                objectName: "profileSyncStatus"
                visible: Server.active
                width: parent.width
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                font.pixelSize: 13
                color: ProfileSync.state === "error" ? Theme.dangerText : Theme.textSecondary
                text: "Profiles: "
                      + (ProfileSync.state === "syncing" ? "updating …"
                         : ProfileSync.state === "error" ? ProfileSync.message
                         : ProfileSync.waiting > 0
                           ? ProfileSync.waiting + (ProfileSync.waiting === 1 ? " change waits" : " changes wait")
                             + " to be sent to the server"
                         : ServerStatus.connected ? "up to date with the server"
                         : "kept on this station until the server is back")
            }
            // the rides kept with the server (RideSyncStatus)
            Text {
                objectName: "rideSyncStatus"
                visible: Server.active
                width: parent.width
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                font.pixelSize: 13
                color: RideSyncStatus.state === "error" ? Theme.dangerText : Theme.textSecondary
                text: "Training: "
                      + (RideSyncStatus.state === "syncing" ? "updating …"
                         : RideSyncStatus.state === "error" ? RideSyncStatus.message
                         : RideSyncStatus.waiting > 0
                           ? RideSyncStatus.waiting + (RideSyncStatus.waiting === 1 ? " session waits" : " sessions wait")
                             + " to be sent to the server"
                         : ServerStatus.connected ? "up to date with the server (analysis: " + Server.url + "training.php)"
                         : "kept on this station until the server is back")
            }
            // the server's media library (MediaLibrary)
            Text {
                objectName: "mediaSyncStatus"
                visible: Server.active
                width: parent.width
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                font.pixelSize: 13
                color: MediaLibrary.state === "error" ? Theme.dangerText : Theme.textSecondary
                text: {
                    const items = MediaLibrary.items
                    const routes = items.filter((i) => i.kind === "gpx").length
                    const videos = items.filter((i) => i.kind === "video").length
                    const videosHere = items.filter((i) => i.kind === "video" && i.state === "local").length
                    return "Media: "
                        + (MediaLibrary.transferring
                           ? (MediaLibrary.transferKind === "upload" ? "sending " : MediaLibrary.transferKind === "checking" ? "checking "
                              : "downloading ") + MediaLibrary.transferName + " – " + Math.round(MediaLibrary.transferProgress * 100) + " %"
                           : MediaLibrary.state === "error" ? MediaLibrary.message
                           : routes + (routes === 1 ? " GPX route, " : " GPX routes, ") + videos
                             + (videos === 1 ? " video" : " videos") + " on the server (" + videosHere + " here)."
                             + " GPX Ride / Video Ride › library menu › Server Library.")
                }
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                font.pixelSize: 12
                color: Theme.textMuted
                text: "Station ID: " + Server.stationId
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.cardBorder }

        // Automatic diagnostic log (DiagLog): one file per app session in the
        // logs folder; fetch it with scp or copy it to a USB stick.
        SettingRow {
            objectName: "diagLogSwitch"
            title: "Diagnostic log"
            sub: Logs.available ? "Record trainer commands, responses and gears to a file (" + Logs.summary + ")"
                                : "Disabled by WATT_LOG=0"
            checked: Logs.enabled && Logs.available
            onToggled: (on) => Logs.enabled = on
        }
        Column {
            width: parent.width
            spacing: 4
            Text { text: "Logs folder (select to copy):"; color: Theme.textSecondary; font.pixelSize: 13 }
            TextEdit {
                objectName: "diagLogFolder"
                width: parent.width
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                text: Logs.directory
                color: Theme.text
                selectionColor: Theme.accent
                font.family: "monospace"
                font.pixelSize: 13
            }
            Text { text: "Fetch from a Raspberry Pi (run on the Mac):"; color: Theme.textSecondary; font.pixelSize: 13 }
            TextEdit {
                width: parent.width
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                text: Logs.scpCommand
                color: Theme.text
                selectionColor: Theme.accent
                font.family: "monospace"
                font.pixelSize: 13
            }
        }
        Row {
            spacing: 12
            AppButton {
                objectName: "openLogsButton"
                kind: "secondary"
                iconName: "folder"
                text: "Open logs folder"
                fontSize: 15
                bold: false
                onClicked: { Logs.ensureFolder(); Qt.openUrlExternally(Logs.folderUrl) }
            }
            AppButton {
                objectName: "exportLogsButton"
                kind: "secondary"
                iconName: "download"
                text: "Export logs…"
                fontSize: 15
                bold: false
                onClicked: exportLogsDialog.open()
            }
        }
        Text {
            id: exportResult
            width: parent.width
            visible: text.length > 0
            color: Theme.textSecondary
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        Rectangle { width: parent.width; height: 1; color: Theme.cardBorder }

        Column {
            spacing: 6
            width: parent.width
            Text { text: "Data"; color: Theme.text; font.pixelSize: 17; font.weight: Font.Bold }
            Text {
                text: "All data is stored locally (offline) in SQLite:"
                color: Theme.textSecondary
                font.pixelSize: 13
            }
            Text {
                text: databasePath
                color: Theme.text
                font.pixelSize: 13
                width: parent.width
                elide: Text.ElideMiddle
            }
        }
        Text {
            text: "Artilody Multisport Endurance Trainer " + appVersion
            color: Theme.textMuted
            font.pixelSize: 13
        }
    }
    }

    // "Export logs…": copies the log files into a chosen folder (e.g. a USB stick)
    FolderDialog {
        id: exportLogsDialog
        title: "Copy the diagnostic logs to…"
        onAccepted: {
            exportResult.text = Logs.exportTo(selectedFolder)
            if (ApplicationWindow.window && ApplicationWindow.window.showToast)
                ApplicationWindow.window.showToast(exportResult.text)
        }
    }

    onVisibleChanged: if (visible) { Logs.refresh(); exportResult.text = "" }

    AppButton {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        text: "Done"
        onClicked: dlg.close()
    }
}
