import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

// Bike gearing for virtual shifting (Gears, Devices page): chainrings, cassette, wheel
// size, shifting mode and the road model of the bike. Validation and the
// preview come from Gears (validateGearSetup / previewGears).
AppDialog {
    id: dlg
    title: "Gear Setup"
    panelWidth: 1010
    panelHeight: advancedOpen ? 800 : 750

    property string ringsText: ""
    property string cogsText: ""
    property bool seq: true
    property bool advancedOpen: false
    property bool cdaAuto: true
    property string saveError: ""

    readonly property var rings: Gears.parseTeeth(ringsText)
    readonly property var cogs: Gears.parseTeeth(cogsText)
    readonly property int circumference: Math.round(circSpin.value)
    readonly property string error: {
        if (ringsText.trim().length > 0 && rings.length === 0)
            return "Chainrings: enter whole numbers separated by commas, e.g. 50, 34"
        if (cogsText.trim().length > 0 && cogs.length === 0)
            return "Cassette: enter whole numbers separated by commas, e.g. 11, 12, 13, 14"
        return Gears.validateGearSetup(rings, cogs, circumference)
    }
    readonly property var preview: error.length === 0 ? Gears.previewGears(rings, cogs, circumference, seq, 90) : []
    readonly property real maxSpeed: preview.length > 0 ? Number(preview[preview.length - 1].speedKmh) : 1

    function listText(values, descending) {
        var a = []
        for (var i = 0; i < values.length; ++i)
            a.push(Number(values[i]))
        a.sort(function(x, y) { return descending ? y - x : x - y })
        return a.join(", ")
    }
    function sameSet(a, b) {
        if (a.length !== b.length)
            return false
        var x = [], y = []
        for (var i = 0; i < a.length; ++i) { x.push(Number(a[i])); y.push(Number(b[i])) }
        x.sort(function(p, q) { return p - q })
        y.sort(function(p, q) { return p - q })
        for (var j = 0; j < x.length; ++j)
            if (x[j] !== y[j])
                return false
        return true
    }
    function withCustom(presets) {
        var a = []
        for (var i = 0; i < presets.length; ++i)
            a.push(presets[i])
        a.push({ name: "Custom", values: [], mm: 0 })
        return a
    }
    function presetIndex(model, values) {
        for (var i = 0; i < model.length - 1; ++i)
            if (sameSet(model[i].values, values))
                return i
        return model.length - 1
    }
    function wheelIndex(model, mm) {
        for (var i = 0; i < model.length - 1; ++i)
            if (model[i].mm === mm)
                return i
        return model.length - 1
    }
    function load(s) {
        ringsText = listText(s.chainrings || [], true)
        cogsText = listText(s.cogs || [], false)
        circSpin.value = s.circumferenceMm || 2105
        seq = s.sequential !== false
        weightSpin.value = s.bikeWeightKg !== undefined ? s.bikeWeightKg : 9
        cdaSpin.value = s.cda !== undefined ? s.cda : 0.32
        cdaAuto = s.cdaAuto !== false
        crrSpin.value = s.crr !== undefined ? s.crr : 0.004
        saveError = ""
        Qt.callLater(function() { ringField.cursorPosition = 0; cogField.cursorPosition = 0 })
    }
    function openDialog() {
        load(Gears.gearSetup)
        advancedOpen = false
        open()
    }
    function save() {
        if (error.length > 0)
            return
        if (!Gears.setGearSetup(rings, cogs, circumference, seq)) {
            saveError = Gears.lastError
            return
        }
        if (!Gears.setBikePhysics(weightSpin.value, cdaSpin.value, crrSpin.value, cdaAuto)) {
            // the gears above are already saved: say so, only the bike values failed
            saveError = "Gears saved (" + Gears.gearCount + " gears), but the bike values were not: "
                        + (Gears.lastError.length > 0 ? Gears.lastError : "invalid value")
            return
        }
        close()
        var w = ApplicationWindow.window
        if (w && typeof w.showToast === "function")
            w.showToast("Gear setup saved – " + Gears.gearCount + " gears")
    }

    Text {
        x: 28
        y: 72
        width: dlg.panelWidth - 56
        text: "Enter your bike's gearing. Virtual gears work like your real ones: a harder gear means more "
              + "speed and more power at the same cadence. Used in Free Ride and Video Ride, saved per profile."
        color: Theme.textSecondary
        font.pixelSize: 15
        wrapMode: Text.WordWrap
    }

    // ================= left: fields =================
    Column {
        id: fields
        x: 28
        y: 122
        width: 480
        spacing: 8

        component FieldLabel: Text {
            color: Theme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }

        FieldLabel { text: "Chainrings" }
        Row {
            spacing: 10
            AppComboBox {
                id: ringCombo
                width: 236
                fontSize: 15
                textRole: "name"
                model: dlg.withCustom(Gears.chainringPresets)
                currentIndex: dlg.presetIndex(model, dlg.rings)
                onActivated: (index) => {
                    if (index < model.length - 1)
                        dlg.ringsText = dlg.listText(model[index].values, true)
                    else
                        ringField.forceActiveFocus()
                    currentIndex = Qt.binding(function() { return dlg.presetIndex(ringCombo.model, dlg.rings) })
                }
            }
            AppTextField {
                id: ringField
                width: fields.width - 246
                font.pixelSize: 16
                placeholderText: "e.g. 50, 34 or 40"
                text: dlg.ringsText
                onTextEdited: dlg.ringsText = text
            }
        }

        FieldLabel { text: "Cassette"; topPadding: 4 }
        Row {
            spacing: 10
            AppComboBox {
                id: cogCombo
                width: 236
                fontSize: 15
                textRole: "name"
                model: dlg.withCustom(Gears.cassettePresets)
                currentIndex: dlg.presetIndex(model, dlg.cogs)
                onActivated: (index) => {
                    if (index < model.length - 1)
                        dlg.cogsText = dlg.listText(model[index].values, false)
                    else
                        cogField.forceActiveFocus()
                    currentIndex = Qt.binding(function() { return dlg.presetIndex(cogCombo.model, dlg.cogs) })
                }
            }
            AppTextField {
                id: cogField
                width: fields.width - 246
                font.pixelSize: 16
                placeholderText: "e.g. 11, 12, 13, …, 34"
                text: dlg.cogsText
                onTextEdited: dlg.cogsText = text
            }
        }

        FieldLabel { text: "Wheel size"; topPadding: 4 }
        Row {
            spacing: 10
            AppComboBox {
                id: wheelCombo
                width: 236
                fontSize: 15
                textRole: "name"
                model: dlg.withCustom(Gears.wheelPresets)
                currentIndex: dlg.wheelIndex(model, dlg.circumference)
                onActivated: (index) => {
                    if (index < model.length - 1)
                        circSpin.value = model[index].mm
                    currentIndex = Qt.binding(function() { return dlg.wheelIndex(wheelCombo.model, dlg.circumference) })
                }
            }
            UnitSpinBox {
                id: circSpin
                width: fields.width - 246
                height: 50
                from: 1000
                to: 3000
                stepSize: 1
                unit: "mm"
            }
        }

        FieldLabel { text: "Shifting"; topPadding: 4 }
        SegmentedControl {
            height: 46
            fontSize: 15
            options: [
                { value: "seq", label: "Sequential (virtual gears)" },
                { value: "der", label: "Front + rear (like your bike)" }
            ]
            value: dlg.seq ? "seq" : "der"
            onActivated: (v) => dlg.seq = v === "seq"
        }
        Text {
            width: fields.width
            text: dlg.seq
                  ? "One list of gears from easiest to hardest – one button shifts up, one down (ideal for the Zwift Click). "
                    + "Duplicate ratios are skipped."
                  : "Front and rear shift separately like a real derailleur bike: + / − shift the cassette, "
                    + "the ring buttons (Shift+↑ / ↓) change the chainring."
            color: Theme.textSecondary
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        // advanced (collapsible)
        Item {
            width: fields.width
            height: 34
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Icon {
                    name: dlg.advancedOpen ? "chevronDown" : "chevronRight"
                    size: 18
                    color: Theme.accentText
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Advanced: bike weight, air drag (CdA), Crr"
                    color: Theme.accentText
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: dlg.advancedOpen = !dlg.advancedOpen
            }
        }
        Grid {
            visible: dlg.advancedOpen
            columns: 3
            columnSpacing: 10
            rowSpacing: 4
            Repeater {
                model: ["Bike weight", "CdA (air drag)", "Crr (rolling)"]
                Text { text: modelData; color: Theme.textSecondary; font.pixelSize: 13; width: (fields.width - 20) / 3 }
            }
            UnitSpinBox {
                id: weightSpin
                width: (fields.width - 20) / 3
                from: 0; to: 60; stepSize: 0.5; decimals: 1
                unit: "kg"
            }
            Item {
                width: (fields.width - 20) / 3
                height: 46
                UnitSpinBox {
                    id: cdaSpin
                    anchors.fill: parent
                    visible: !dlg.cdaAuto
                    from: 0.1; to: 1.0; stepSize: 0.01; decimals: 2
                    unit: "m²"
                }
                // auto: the estimate for the profile's weight (and height)
                Rectangle {
                    anchors.fill: parent
                    visible: dlg.cdaAuto
                    radius: 9
                    color: Theme.field
                    border.width: 1
                    border.color: Theme.fieldBorder
                    opacity: 0.85
                    Text {
                        x: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: Number(Gears.autoCda).toFixed(2)
                        color: Theme.text
                        font.pixelSize: 18
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: "m² auto"
                        color: Theme.textSecondary
                        font.pixelSize: 15
                    }
                }
            }
            UnitSpinBox {
                id: crrSpin
                width: (fields.width - 20) / 3
                from: 0.001; to: 0.05; stepSize: 0.001; decimals: 3
                unit: ""
            }
        }
        Item {
            visible: dlg.advancedOpen
            width: fields.width
            height: 32
            ToggleSwitch {
                id: cdaAutoSwitch
                width: 46; height: 26
                anchors.verticalCenter: parent.verticalCenter
                onColor: Theme.orange
                checked: dlg.cdaAuto
                onToggled: (on) => {
                    if (!on && dlg.cdaAuto)
                        cdaSpin.value = Math.round(Gears.autoCda * 100) / 100   // start from the estimate
                    dlg.cdaAuto = on
                }
            }
            Text {
                x: cdaAutoSwitch.width + 10
                width: parent.width - x
                anchors.verticalCenter: parent.verticalCenter
                text: "CdA auto (from weight" + (Profiles.heightCm > 0 ? " and height)" : ")")
                      + " · rider + bike " + (Gears.riderWeightKg + weightSpin.value).toFixed(0) + " kg"
                color: Theme.text
                font.pixelSize: 14
                elide: Text.ElideRight
            }
        }
        Text {
            visible: dlg.advancedOpen
            width: fields.width
            text: "Mass = profile weight (" + Number(Gears.riderWeightKg).toFixed(1) + " kg) + bike. "
                  + "Auto CdA = 0.32 m² × (weight / 75 kg)^⅔ (hoods), so lighter riders need fewer watts. "
                  + "Own value: hoods ≈ 0.32, drops ≈ 0.28 m²."
            color: Theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // validation error
        Rectangle {
            readonly property string msg: dlg.error.length > 0 ? dlg.error : dlg.saveError
            visible: msg.length > 0
            width: fields.width
            height: errText.implicitHeight + 20
            radius: 10
            color: Qt.rgba(0.95, 0.3, 0.35, 0.12)
            border.color: Theme.danger
            Icon { x: 12; y: 10; name: "info"; size: 18; color: Theme.danger }
            Text {
                id: errText
                x: 40; y: 10
                width: parent.width - 52
                text: parent.msg
                color: Theme.text
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
        }
    }

    // ================= right: preview =================
    Rectangle {
        id: previewBox
        x: 534
        y: 122
        width: dlg.panelWidth - x - 28
        height: dlg.panelHeight - y - 100
        radius: 12
        color: Theme.cardStrong
        border.color: Theme.cardBorder

        Text {
            x: 16; y: 14
            text: "Preview"
            color: Theme.text
            font.pixelSize: 17
            font.weight: Font.Bold
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 16
            y: 16
            text: dlg.preview.length > 0 ? dlg.preview.length + " gears  •  speed at 90 rpm" : ""
            color: Theme.textSecondary
            font.pixelSize: 14
        }
        Row {
            id: previewHead
            x: 16; y: 46
            width: parent.width - 32
            Text { width: 44; text: "#"; color: Theme.textMuted; font.pixelSize: 13 }
            Text { width: 84; text: "Gear"; color: Theme.textMuted; font.pixelSize: 13 }
            Text { width: 62; text: "Ratio"; color: Theme.textMuted; font.pixelSize: 13; horizontalAlignment: Text.AlignRight }
            Item { width: 14; height: 1 }
            Text { width: previewHead.width - 44 - 84 - 62 - 14 - 64; text: "Speed"; color: Theme.textMuted; font.pixelSize: 13 }
            Text { width: 64; text: "Flat"; color: Theme.textMuted; font.pixelSize: 13; horizontalAlignment: Text.AlignRight }
        }
        ListView {
            id: previewList
            x: 8
            y: 68
            width: parent.width - 16
            height: parent.height - y - 8
            clip: true
            model: dlg.preview
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                readonly property bool current: modelData.label === Gears.gearLabel
                width: previewList.width - 10
                height: 27
                radius: 6
                color: current ? Theme.navSelected : (index % 2 === 1 ? Theme.hover : "transparent")
                Row {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Text { width: 44; text: index + 1; color: Theme.textSecondary; font.pixelSize: 14 }
                    Text {
                        width: 84
                        text: modelData.label
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: parent.parent.current ? Font.Bold : Font.DemiBold
                    }
                    Text {
                        width: 62
                        text: Number(modelData.ratio).toFixed(2)
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignRight
                    }
                    Item { width: 14; height: 1 }
                    Item {
                        width: previewHead.width - 44 - 84 - 62 - 14 - 64
                        height: 18
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            height: 8
                            radius: 4
                            width: Math.max(4, (parent.width - 78) * Number(modelData.speedKmh) / Math.max(1, dlg.maxSpeed))
                            color: Theme.speed
                            opacity: 0.8
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: Number(modelData.speedKmh).toFixed(1) + " km/h"
                            color: Theme.text
                            font.pixelSize: 14
                        }
                    }
                    Text {
                        width: 64
                        text: modelData.watts + " W"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }
        Text {
            visible: dlg.preview.length === 0
            anchors.centerIn: parent
            width: parent.width - 60
            horizontalAlignment: Text.AlignHCenter
            text: "Fix the setup to see the gears"
            color: Theme.textSecondary
            font.pixelSize: 15
            wrapMode: Text.WordWrap
        }
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 26
        spacing: 12
        AppButton {
            kind: "secondary"
            iconName: "refresh"
            iconSize: 18
            fontSize: 16
            bold: false
            text: "Reset to Defaults"
            onClicked: dlg.load(Gears.defaultGearSetup())
        }
        Item { Layout.fillWidth: true }
        AppButton {
            text: "Cancel"
            kind: "secondary"
            Layout.preferredWidth: 120
            onClicked: dlg.close()
        }
        AppButton {
            text: "Save"
            iconName: "check"
            Layout.preferredWidth: 140
            enabled: dlg.error.length === 0
            onClicked: dlg.save()
        }
    }
}
