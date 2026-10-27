import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

// Advanced (Devices > Bluetooth diagnostics > Advanced… > Bike): the bike
// behind the virtual gears - wheel size (virtual speed), bike weight, air
// drag (CdA) and Crr. Virtual shifting always uses Zwift's 24 gears (no
// chainrings / cassette to enter); the right side lists them.
AppDialog {
    id: dlg
    title: "Bike (advanced)"
    panelWidth: 1010
    panelHeight: 640

    property bool advancedOpen: true   // (kept for the screenshot hooks)
    property bool cdaAuto: true
    property string saveError: ""
    readonly property int circumference: Math.round(circSpin.value)
    readonly property string error: circumference < 1000 || circumference > 3000 ? "Wheel circumference: 1000–3000 mm" : ""

    function withCustom(presets) {
        var a = []
        for (var i = 0; i < presets.length; ++i)
            a.push(presets[i])
        a.push({ name: "Custom", values: [], mm: 0 })
        return a
    }
    function wheelIndex(model, mm) {
        for (var i = 0; i < model.length - 1; ++i)
            if (model[i].mm === mm)
                return i
        return model.length - 1
    }
    function load(s) {
        circSpin.value = s.circumferenceMm || 2105
        weightSpin.value = s.bikeWeightKg !== undefined ? s.bikeWeightKg : 9
        cdaSpin.value = s.cda !== undefined ? s.cda : 0.32
        cdaAuto = s.cdaAuto !== false
        crrSpin.value = s.crr !== undefined ? s.crr : 0.004
        saveError = ""
    }
    function openDialog() {
        load(Gears.gearSetup)
        open()
    }
    function save() {
        if (error.length > 0)
            return
        if (!Gears.setWheelCircumference(circumference)
                || !Gears.setBikePhysics(weightSpin.value, cdaSpin.value, crrSpin.value, cdaAuto)) {
            saveError = Gears.lastError.length > 0 ? Gears.lastError : "invalid value"
            return
        }
        close()
        var w = ApplicationWindow.window
        if (w && typeof w.showToast === "function")
            w.showToast("Bike saved")
    }

    Text {
        x: 28
        y: 72
        width: dlg.panelWidth - 56
        text: "Virtual shifting uses Zwift's 24 virtual gears with a Zwift Click – nothing to set up. "
              + "These values only refine the road physics (speed, weight, air drag). Saved per profile."
        color: Theme.textSecondary
        font.pixelSize: 15
        wrapMode: Text.WordWrap
    }

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

        FieldLabel { text: "Bike and air drag"; topPadding: 8 }
        Grid {
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


    // ================= right: Zwift's 24 gears =================
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
            text: "Zwift's 24 virtual gears"
            color: Theme.text
            font.pixelSize: 17
            font.weight: Font.Bold
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 16
            y: 16
            text: "speed at 90 rpm"
            color: Theme.textSecondary
            font.pixelSize: 14
        }
        ListView {
            id: previewList
            x: 8
            y: 46
            width: parent.width - 16
            height: parent.height - y - 8
            clip: true
            model: Gears.gears
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                readonly property bool current: index === Gears.gearIndex
                width: previewList.width - 10
                height: 27
                radius: 6
                color: current ? Theme.navSelected : (index % 2 === 1 ? Theme.hover : "transparent")
                Row {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Text { width: 90; text: "Gear " + (index + 1); color: Theme.text; font.pixelSize: 14; font.weight: parent.parent.current ? Font.Bold : Font.Normal }
                    Text { width: 70; text: Number(modelData.ratio).toFixed(2); color: Theme.textSecondary; font.pixelSize: 14; horizontalAlignment: Text.AlignRight }
                    Item { width: 24; height: 1 }
                    Text {
                        text: (90 / 60 * Number(modelData.ratio) * dlg.circumference / 1000 * 3.6).toFixed(1) + " km/h"
                        color: Theme.text
                        font.pixelSize: 14
                    }
                }
            }
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
