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

    Column {
        x: 28
        y: 84
        width: dlg.panelWidth - 56
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
        // Recording, averages/NP/TSS, ERG and the FTP test use the raw power.
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
            text: "Artilody Watts Trainer " + appVersion
            color: Theme.textMuted
            font.pixelSize: 13
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
