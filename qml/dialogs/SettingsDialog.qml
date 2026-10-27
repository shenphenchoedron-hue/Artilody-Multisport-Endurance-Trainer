import QtQuick
import QtQuick.Layouts
import Artilody.Backend

AppDialog {
    id: dlg
    title: "Settings"
    panelWidth: 560
    panelHeight: 556

    Column {
        x: 28
        y: 84
        width: dlg.panelWidth - 56
        spacing: 18

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

    AppButton {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        text: "Done"
        onClicked: dlg.close()
    }
}
