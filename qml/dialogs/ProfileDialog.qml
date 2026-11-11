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
    property bool showAvatar3d: false   // the 3D rider panel (figure, clothes' colours)
    property url avatar
    property var bounds: Profiles.defaultZoneBounds
    // the rider in the GPX Ride's 3D view (Profiles.avatar3d's keys) and the
    // runner in the Treadmill Run's (Profiles.runner3d); the panel shows one
    property var avatar3d: ({ model: "classic", jersey: "", shorts: "", helmet: "", shoes: "", hair: "", bike: "", boat: "" })
    property var runner3d: ({ model: "classic", jersey: "", shorts: "", helmet: "", shoes: "", hair: "", bike: "", boat: "" })
    property var rower3d: ({ model: "classic", jersey: "", shorts: "", helmet: "", shoes: "", hair: "", bike: "", boat: "" })
    property string avatarKind: "cyclist"   // "cyclist" | "runner" | "rower"
    readonly property var shownAvatar: avatarKind === "runner" ? runner3d : avatarKind === "rower" ? rower3d : avatar3d

    function setAvatar3d(key, value) {
        var a = Object.assign({}, shownAvatar)
        a[key] = value
        if (avatarKind === "runner")
            runner3d = a
        else if (avatarKind === "rower")
            rower3d = a
        else
            avatar3d = a
    }

    panelWidth: showZones || showAvatar3d ? 1010 : 528
    // grows when an error message is shown so it never overlaps the fields or buttons
    panelHeight: Math.max(586, fields.y + fields.height + (errorText.text.length > 0 ? errorText.height + 16 : 0) + 96)

    function openFor(newProfile) {
        creating = newProfile
        showZones = false
        showAvatar3d = false
        avatarKind = "cyclist"
        avatar3d = newProfile ? { model: "classic", jersey: "", shorts: "", helmet: "", shoes: "", hair: "", bike: "", boat: "" }
                              : Object.assign({}, Profiles.avatar3d)
        runner3d = newProfile ? { model: "classic", jersey: "", shorts: "", helmet: "", shoes: "", hair: "", bike: "", boat: "" }
                              : Object.assign({}, Profiles.runner3d)
        rower3d = newProfile ? { model: "classic", jersey: "", shorts: "", helmet: "", shoes: "", hair: "", bike: "", boat: "" }
                             : Object.assign({}, Profiles.rower3d)
        nameField.text = newProfile ? "" : Profiles.name
        weight.value = newProfile ? 75 : Profiles.weight
        heightSpin.value = newProfile ? 0 : Profiles.heightCm
        ftp.value = Profiles.ftp
        rowingFtp.value = Profiles.rowingFtp
        walkRun.value = newProfile ? 6.3 : RunLibrary.walkRunKmh
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
            zoneBounds: zb,
            avatar3d: avatar3d,
            runner3d: runner3d,
            rower3d: rower3d
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
                // 0 = not set or 100..230 cm. The arrows jump over 1..99;
                // a typed value there is kept and flagged (never changed silently).
                readonly property bool invalid: value > 0 && value < 100
                width: parent.width
                unit: "cm"
                from: 0; to: 230; stepSize: 1
                stepFilter: function(current, next, dir) {
                    if (next > 0 && next < 100)
                        return dir > 0 ? (current < 100 && current > 0 ? 100 : 170) : 0
                    return next
                }
            }
            Text {
                width: parent.width
                text: heightSpin.invalid ? "Enter 100–230 cm, or 0 = not set"
                                         : "Optional (0 = not set) · sizes your air drag"
                color: heightSpin.invalid ? Theme.dangerText : Theme.textSecondary
                font.pixelSize: 12
                font.weight: heightSpin.invalid ? Font.DemiBold : Font.Normal
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

        Text { text: "Walk → Run"; color: Theme.text; font.pixelSize: 16; Layout.alignment: Qt.AlignTop; Layout.topMargin: 12 }
        Column {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: 4
            UnitSpinBox { id: walkRun; width: parent.width; unit: "km/h"; from: 4; to: 10; stepSize: 0.1; decimals: 1 }
            Text {
                width: parent.width
                text: "The speed where you start running – the treadmill runner walks below it"
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
        id: zonesLink
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
            onClicked: { dlg.showZones = !dlg.showZones; dlg.showAvatar3d = false }
        }
    }

    Text {
        x: 28
        anchors.top: zonesLink.bottom   // (below it, also when it wraps)
        anchors.topMargin: 14
        width: 120
        text: dlg.showAvatar3d ? "Hide avatar ‹" : "3D avatar ›"
        color: Theme.accentLight
        font.pixelSize: 15
        wrapMode: Text.WordWrap
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: { dlg.showAvatar3d = !dlg.showAvatar3d; dlg.showZones = false }
        }
    }

    // the 3D avatar panel: which figure rides / runs / rows in the 3D views,
    // the colours of its clothes (a textured avatar only), the bike, the boat
    ColorDialog {
        id: garmentPicker
        property string key: ""
        title: "Choose a colour"
        onAccepted: dlg.setAvatar3d(key, selectedColor.toString())
    }
    Item {
        id: avatarPanel
        visible: dlg.showAvatar3d
        x: 528
        y: 76
        width: 454
        height: 440
        readonly property var own: preview.ownColors   // the figure's own colours (empty: classic)
        // the figure's clothes can be recoloured (a textured avatar; the bike always can)
        readonly property bool recolourable: Object.keys(own).filter(k => k !== "bike" && k !== "boat").length > 0

        Rectangle { x: -14; width: 1; height: parent.height; color: Theme.cardBorder }

        Text {
            height: 30
            verticalAlignment: Text.AlignVCenter
            text: "3D avatar"; color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold
        }
        // which figure: the GPX Ride's cyclist or the Treadmill Run's runner
        SegmentedControl {
            anchors.right: parent.right
            height: 30
            fontSize: 14
            value: dlg.avatarKind
            options: [ { value: "cyclist", label: "Cyclist" }, { value: "runner", label: "Runner" },
                       { value: "rower", label: "Rower" } ]
            onActivated: (v) => dlg.avatarKind = v
        }

        Rectangle {
            y: 38
            width: parent.width
            height: 150
            radius: 10
            color: Theme.dark ? Qt.rgba(1, 1, 1, 0.06) : "#eef2f8"
            AvatarPreview {
                id: preview
                anchors.fill: parent
                anchors.margins: 6
                avatar: dlg.shownAvatar
                kind: dlg.avatarKind
            }
            // drag to turn the rider round
            MouseArea {
                anchors.fill: parent
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property real lastX: 0
                onPressed: (mouse) => lastX = mouse.x
                onPositionChanged: (mouse) => {
                    preview.yaw = preview.yaw + (mouse.x - lastX) * 0.8
                    lastX = mouse.x
                }
            }
            Text {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                text: "Drag to turn"
                color: Theme.textSecondary
                font.pixelSize: 11
            }
        }

        GridLayout {
            y: 196
            width: parent.width
            columns: 2
            columnSpacing: 12
            rowSpacing: 5

            Text { text: "Figure"; color: Theme.text; font.pixelSize: 15; Layout.preferredWidth: 116 }
            AppComboBox {
                id: modelBox
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                textRole: "name"
                // the installed figures - and the saved one when its model
                // file is not there (shown as such, the classic one drawn)
                readonly property var choices: {
                    var list = preview.models.slice()
                    var saved = dlg.shownAvatar.model || "classic"
                    if (!list.some(m => m.id === saved))
                        list.push({ id: saved, name: saved + " (not installed)" })
                    return list
                }
                model: choices
                currentIndex: {
                    for (var i = 0; i < choices.length; ++i)
                        if (choices[i].id === (dlg.shownAvatar.model || "classic"))
                            return i
                    return 0
                }
                onActivated: (index) => dlg.setAvatar3d("model", choices[index].id)
            }

            Repeater {
                model: [ { key: "jersey", name: dlg.avatarKind === "cyclist" ? "Jersey" : "Top" },
                         { key: "shorts", name: "Shorts" }, { key: "helmet", name: "Helmet" },
                         { key: "shoes", name: "Shoes" }, { key: "hair", name: "Hair" },
                         { key: "bike", name: "Bike" }, { key: "boat", name: "Boat" } ]
                delegate: Item {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    visible: (avatarPanel.recolourable || modelData.key === "bike" || modelData.key === "boat")
                             && (modelData.key !== "helmet" || preview.helmet)
                             && (avatarPanel.own[modelData.key] || "").length > 0
                    readonly property string chosen: dlg.shownAvatar[modelData.key] || ""
                    readonly property string shown: chosen.length > 0 ? chosen : (avatarPanel.own[modelData.key] || "#808080")
                    Text {
                        width: 116
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: Theme.text
                        font.pixelSize: 15
                    }
                    Rectangle {
                        x: 128
                        width: 64; height: 28; radius: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: parent.shown
                        border.width: 1
                        border.color: Theme.cardBorder
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                garmentPicker.key = modelData.key
                                garmentPicker.selectedColor = parent.color
                                garmentPicker.open()
                            }
                        }
                    }
                    Text {
                        x: 204
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.chosen.length > 0 ? "Reset" : "Original"
                        color: parent.chosen.length > 0 ? Theme.accentLight : Theme.textSecondary
                        font.pixelSize: 13
                        MouseArea {
                            anchors.fill: parent
                            enabled: parent.parent.chosen.length > 0
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: dlg.setAvatar3d(modelData.key, "")
                        }
                    }
                }
            }
            Text {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: !avatarPanel.recolourable
                text: dlg.avatarKind === "runner"
                      ? "The classic runner's colours are fixed. Choose a male or female runner to pick the colours of the top, shorts, shoes and hair."
                      : dlg.avatarKind === "rower"
                      ? "The classic rower's colours are fixed. Choose a male or female rower to pick the colours of the top, shorts and hair."
                      : "The classic rider's clothes have fixed colours. Choose a male or female cyclist to pick the colours of the jersey, shorts, helmet, shoes and hair."
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
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
                if (ok) {
                    RunLibrary.walkRunKmh = walkRun.value   // (per profile: the current one)
                    dlg.close()
                }
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
