import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Artilody.Backend

// "User Profile" dialog from the mockup, with an expandable zones panel.
AppDialog {
    id: dlg
    title: creating ? "New Profile" : "User Profile"
    property bool creating: false
    property bool showZones: false
    property url avatar
    property var bounds: Profiles.defaultZoneBounds

    panelWidth: showZones ? 1010 : 528
    // grows when an error message is shown so it never overlaps the fields or buttons
    panelHeight: Math.max(586, fields.y + fields.height + (errorText.text.length > 0 ? errorText.height + 16 : 0) + 96)

    function openFor(newProfile) {
        creating = newProfile
        showZones = false
        nameField.text = newProfile ? "" : Profiles.name
        weight.value = newProfile ? 75 : Profiles.weight
        heightSpin.lastValue = heightSpin.value = newProfile ? 0 : Profiles.heightCm
        ftp.value = Profiles.ftp
        rowingFtp.value = Profiles.rowingFtp
        maxHr.value = Profiles.maxHr
        restHr.value = Profiles.restHr
        cadence.value = Profiles.preferredCadence
        mode.currentIndex = Profiles.defaultErg ? 0 : 1
        avatar = newProfile ? "" : Profiles.avatarUrl
        var b = []
        var z = Profiles.powerZones
        for (var i = 0; i < 5; ++i)
            b.push(z[i].highPercent)
        bounds = b
        errorText.text = ""
        open()
        if (newProfile)
            nameField.forceActiveFocus()
    }

    function values() {
        var zb = []
        for (var i = 0; i < 5; ++i)
            zb.push(bounds[i] / 100)
        return {
            name: nameField.text,
            weight: weight.value,
            heightCm: heightSpin.value,
            ftp: ftp.value,
            rowingFtp: rowingFtp.value,
            maxHr: maxHr.value,
            restHr: restHr.value,
            preferredCadence: cadence.value,
            defaultErg: mode.currentIndex === 0,
            avatarUrl: avatar,
            zoneBounds: zb
        }
    }

    FileDialog {
        id: avatarPicker
        title: "Choose profile picture"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp *.webp)"]
        onAccepted: dlg.avatar = selectedFile
    }

    // avatar
    Item {
        x: 28
        y: 82
        width: 112
        height: 112
        Avatar {
            anchors.fill: parent
            size: 112
            source: dlg.avatar
            initials: nameField.text.length > 0 ? nameField.text.charAt(0).toUpperCase() : "?"
            ringColor: Qt.rgba(1, 1, 1, 0.35)
        }
        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: -6
            anchors.bottomMargin: -2
            width: 40; height: 40; radius: 20
            color: Theme.accent
            border.width: 3
            border.color: Theme.dark ? "#25378a" : "white"
            Icon { anchors.centerIn: parent; name: "camera"; size: 20; color: "white" }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: avatarPicker.open()
            }
        }
    }

    // fields
    GridLayout {
        id: fields
        x: 168
        y: 76
        width: 332
        columns: 2
        columnSpacing: 12
        rowSpacing: 12

        Text { text: "Name"; color: Theme.text; font.pixelSize: 16; Layout.preferredWidth: 116 }
        AppTextField {
            id: nameField
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            font.pixelSize: 17
            placeholderText: "Your name"
        }

        Text { text: "Weight"; color: Theme.text; font.pixelSize: 16 }
        UnitSpinBox { id: weight; Layout.fillWidth: true; unit: "kg"; from: 30; to: 200; stepSize: 0.5; decimals: 1 }

        Text { text: "Height"; color: Theme.text; font.pixelSize: 16; Layout.alignment: Qt.AlignTop; Layout.topMargin: 12 }
        Column {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: 4
            UnitSpinBox {
                id: heightSpin
                // 0 = not set; the arrows jump between 0 and 100..230 cm
                property real lastValue: 0
                width: parent.width
                unit: "cm"
                from: 0; to: 230; stepSize: 1
                onValueChanged: {
                    if (value > 0 && value < 100)
                        value = lastValue >= 100 ? 0 : 170
                    lastValue = value
                }
            }
            Text {
                width: parent.width
                text: "Optional (0 = not set) · sizes your air drag"
                color: Theme.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        Text { text: "FTP"; color: Theme.text; font.pixelSize: 16; Layout.alignment: Qt.AlignTop; Layout.topMargin: 12 }
        Column {
            Layout.fillWidth: true
            // keep the hint's implicit width from widening the grid column
            Layout.preferredWidth: 1
            spacing: 4
            UnitSpinBox { id: ftp; width: parent.width; unit: "W"; from: 50; to: 600; stepSize: 5 }
            Text {
                width: parent.width
                text: (weight.value > 0 ? (ftp.value / weight.value).toFixed(2) : "0") + " W/kg · used for zones"
                color: Theme.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        Text { text: "Rowing FTP"; color: Theme.text; font.pixelSize: 16; Layout.alignment: Qt.AlignTop; Layout.topMargin: 12 }
        Column {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: 4
            UnitSpinBox { id: rowingFtp; width: parent.width; unit: "W"; from: 50; to: 600; stepSize: 5 }
            Text {
                width: parent.width
                text: "Used for rowing workouts"
                color: Theme.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        Text { text: "Max Heart Rate"; color: Theme.text; font.pixelSize: 16 }
        UnitSpinBox { id: maxHr; Layout.fillWidth: true; unit: "BPM"; from: 100; to: 230 }

        Text { text: "Resting Heart Rate"; color: Theme.text; font.pixelSize: 15 }
        UnitSpinBox { id: restHr; Layout.fillWidth: true; unit: "BPM"; from: 30; to: 110 }

        Text { text: "Preferred Cadence"; color: Theme.text; font.pixelSize: 15 }
        UnitSpinBox { id: cadence; Layout.fillWidth: true; unit: "RPM"; from: 50; to: 130 }

        Text { text: "Default Mode"; color: Theme.text; font.pixelSize: 16 }
        AppComboBox {
            id: mode
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            model: ["ERG", "Free Ride"]
        }
    }

    Text {
        x: 28
        y: 216
        width: 120
        text: dlg.showZones ? "Hide zones ‹" : "Training zones ›"
        color: Theme.accentLight
        font.pixelSize: 15
        wrapMode: Text.WordWrap
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: dlg.showZones = !dlg.showZones
        }
    }

    Text {
        id: errorText
        x: 28
        y: fields.y + fields.height + 10
        width: 472
        color: Theme.danger
        font.pixelSize: 14
        wrapMode: Text.WordWrap
    }

    // zones panel
    Item {
        visible: dlg.showZones
        x: 528
        y: 76
        width: 454
        height: 420

        Rectangle { x: -14; width: 1; height: parent.height; color: Theme.cardBorder }

        Text { text: "Power zones"; color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold }
        Text {
            anchors.right: parent.right
            text: "Reset"
            color: Theme.accentLight
            font.pixelSize: 14
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: dlg.bounds = Profiles.defaultZoneBounds.slice()
            }
        }
        Column {
            y: 32
            width: parent.width
            spacing: 6
            Repeater {
                model: 6
                Item {
                    width: parent.width
                    height: 32
                    readonly property int low: index === 0 ? 0 : dlg.bounds[index - 1]
                    readonly property int high: index < 5 ? dlg.bounds[index] : 0
                    Rectangle {
                        width: 14; height: 14; radius: 7
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.zoneColor(index)
                    }
                    Text {
                        x: 24
                        anchors.verticalCenter: parent.verticalCenter
                        text: ["Z1 Recovery", "Z2 Endurance", "Z3 Tempo", "Z4 Sweet Spot", "Z5 Threshold", "Z6 VO2 Max"][index]
                        color: Theme.text
                        font.pixelSize: 14
                    }
                    Text {
                        x: 150
                        anchors.verticalCenter: parent.verticalCenter
                        text: index < 5 ? low + " – " : "> " + low + " %"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                    }
                    SpinBox {
                        visible: index < 5
                        x: 200
                        width: 124
                        height: 30
                        anchors.verticalCenter: parent.verticalCenter
                        from: 20
                        to: 200
                        value: parent.high
                        editable: true
                        font.pixelSize: 13
                        onValueModified: {
                            var b = dlg.bounds.slice()
                            b[index] = value
                            dlg.bounds = b
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: index < 5 ? Math.round(low / 100 * ftp.value) + "–" + (Math.round(high / 100 * ftp.value) - 1) + " W"
                                        : "> " + Math.round(low / 100 * ftp.value) + " W"
                        color: Theme.text
                        font.pixelSize: 14
                    }
                }
            }
        }

        Text {
            y: 268
            text: "Heart-rate zones (Karvonen)"
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.Bold
        }
        Column {
            y: 298
            width: parent.width
            spacing: 4
            Repeater {
                model: Profiles.heartRateZonesFor(maxHr.value, restHr.value)
                Item {
                    width: parent.width
                    height: 22
                    Rectangle {
                        width: 14; height: 14; radius: 7
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.hrZoneColors[modelData.zone]
                    }
                    Text {
                        x: 24
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: Theme.text
                        font.pixelSize: 14
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.low + " – " + modelData.high + " BPM"
                        color: Theme.text
                        font.pixelSize: 14
                    }
                }
            }
        }
    }

    // buttons
    AppButton {
        x: 28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        width: 128
        height: 52
        kind: "secondary"
        text: "Cancel"
        bold: false
        onClicked: dlg.close()
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 26
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        spacing: 12
        AppButton {
            visible: !dlg.creating && Profiles.profiles.length > 1 && !Training.active && !Rowing.active
            height: 52
            kind: "secondary"
            iconName: "trash"
            text: ""
            width: 56
            onClicked: deleteConfirm.open()
        }
        AppButton {
            width: 166
            height: 52
            text: dlg.creating ? "Create Profile" : "Save Profile"
            onClicked: {
                var ok = dlg.creating ? Profiles.createProfileWith(dlg.values()) >= 0
                                      : Profiles.saveProfile(dlg.values())
                if (ok)
                    dlg.close()
                else
                    errorText.text = Profiles.lastError
            }
        }
    }

    ConfirmDialog {
        id: deleteConfirm
        title: "Delete profile?"
        message: "\"" + Profiles.name + "\" and all of its rides will be deleted."
        acceptText: "Delete"
        acceptKind: "danger"
        onAccepted: {
            Profiles.deleteProfile(Profiles.profileId)
            dlg.close()
        }
    }
}
