import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

// ERG panel of the free ride page: hold FreeRide.targetWatts as long as you
// like. Steps, direct entry and the ERG switch call the FreeRide singleton.
Item {
    id: erg

    function pulse(direction) {
        var b = direction > 0 ? plus10 : minus10
        if (b)
            b.pulse()
    }

    readonly property int diff: FreeRide.watts - FreeRide.targetWatts
    readonly property bool onTarget: Math.abs(diff) <= Math.max(5, FreeRide.targetWatts * 0.05)

    RowLayout {
        anchors.fill: parent
        spacing: 22

        // --- target -----------------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 270
            Layout.maximumWidth: 270
            Layout.fillHeight: true
            spacing: 6

            Text {
                text: "Target power"
                color: Theme.textSecondary
                font.pixelSize: 16
            }
            Row {
                spacing: 8
                Text {
                    id: targetText
                    text: FreeRide.targetWatts
                    color: FreeRide.ergEnabled ? Theme.warningText : Theme.textMuted
                    font.pixelSize: 88
                    font.weight: Font.Black
                    font.letterSpacing: -2
                }
                Text {
                    text: "W"
                    color: FreeRide.ergEnabled ? Theme.warningText : Theme.textMuted
                    font.pixelSize: 40
                    font.weight: Font.Bold
                    anchors.baseline: targetText.baseline
                }
            }
            Text {
                text: FreeRide.watts > 0
                      ? "Actual " + FreeRide.watts + " W  (" + (erg.diff > 0 ? "+" : erg.diff < 0 ? "−" : "±") + Math.abs(erg.diff) + ")"
                      : "Actual – W"
                color: FreeRide.watts <= 0 ? Theme.textSecondary : (erg.onTarget ? Theme.successText : Theme.warningText)
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            // actual vs target: the band is ±5 %, the marker the current power
            Item {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.preferredHeight: 22
                visible: FreeRide.ergEnabled
                readonly property real scaleMax: Math.max(1, FreeRide.targetWatts * 2)
                Rectangle {
                    id: track
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 8
                    radius: 4
                    color: Theme.fieldBorder
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: track.width * 0.475
                    width: track.width * 0.05
                    height: 14
                    radius: 3
                    color: Qt.rgba(0.24, 0.86, 0.36, 0.55)
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Math.max(0, Math.min(track.width, track.width * FreeRide.watts / parent.scaleMax)) - width / 2
                    width: 6
                    height: 22
                    radius: 3
                    color: erg.onTarget ? Theme.success : Theme.warning
                    Behavior on x { NumberAnimation { duration: 250 } }
                }
            }
            Item { Layout.fillHeight: true }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 58
                radius: 10
                color: Theme.cardStrong
                border.color: FreeRide.ergEnabled ? Qt.rgba(0.24, 0.86, 0.36, 0.6) : Theme.cardBorder
                Column {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    Text { text: "ERG mode"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text {
                        text: FreeRide.ergEnabled ? "Trainer holds the target" : "Off – back to virtual gears"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }
                ToggleSwitch {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    checked: FreeRide.ergEnabled
                    onToggled: (on) => FreeRide.setErgEnabled(on)
                }
            }
        }

        // --- steps --------------------------------------------------------------
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 3
            rowSpacing: 12
            columnSpacing: 12
            Repeater {
                model: [-25, -10, -5, 5, 10, 25]
                ShiftButton {
                    id: stepButton
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 60
                    label: (modelData > 0 ? "+" : "−") + Math.abs(modelData)
                    sublabel: "watts"
                    labelSize: 28
                    tint: modelData > 0 ? Theme.orange : Theme.accent
                    enabled: modelData > 0 ? FreeRide.targetWatts < FreeRide.maxTargetWatts
                                           : FreeRide.targetWatts > FreeRide.minTargetWatts
                    onClicked: FreeRide.adjustTargetWatts(modelData)
                    Component.onCompleted: {
                        if (modelData === 10) erg.plus10 = stepButton
                        else if (modelData === -10) erg.minus10 = stepButton
                    }
                }
            }
        }

        // --- direct entry + hints -------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.maximumWidth: 250
            Layout.fillHeight: true
            spacing: 8

            Text { text: "Exact target"; color: Theme.textSecondary; font.pixelSize: 16 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppTextField {
                    id: entry
                    Layout.fillWidth: true
                    placeholderText: FreeRide.minTargetWatts + "–" + FreeRide.maxTargetWatts + " W"
                    font.pixelSize: 17
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 9999 }
                    onAccepted: setButton.clicked()
                }
                AppButton {
                    id: setButton
                    Layout.preferredHeight: 50
                    text: "Set"
                    fontSize: 16
                    horizontalPadding: 18
                    enabled: entry.text.length > 0
                    onClicked: {
                        var v = parseInt(entry.text)
                        if (isNaN(v))
                            return
                        FreeRide.setTargetWatts(v)
                        if (!FreeRide.ergEnabled)
                            FreeRide.setErgEnabled(true)
                        entry.text = ""
                        entry.focus = false
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "Values are limited to " + FreeRide.minTargetWatts + "–" + FreeRide.maxTargetWatts + " W."
                color: Theme.textMuted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillHeight: true }
            Row {
                spacing: 8
                Icon { name: "controller"; size: 20; color: Theme.accentLight; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    width: 220
                    text: "Zwift Click + / − changes the target by 10 W"
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Row {
                spacing: 8
                Icon { name: "keyboard"; size: 20; color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    width: 220
                    text: "↑ / ↓  ±5 W\nPgUp / PgDn  ±10 W"
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    property Item plus10: null
    property Item minus10: null
}
