import QtQuick
import QtQuick.Layouts
import Artilody.Backend

AppDialog {
    id: dlg
    title: "Settings"
    panelWidth: 560
    panelHeight: 470

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
