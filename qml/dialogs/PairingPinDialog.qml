import QtQuick
import QtQuick.Layouts
import Artilody.Backend

// Bluetooth pairing with a PIN (Linux/BlueZ): a device asks for the number on
// its display (e.g. KETTLER Track treadmill in APP mode). Typed on a keyboard
// or on the number pad (touch screens without a keyboard). Opened and closed
// by Main.qml following Devices.pairingPinRequested.
AppDialog {
    id: dlg
    title: "Bluetooth PIN"
    panelWidth: 520
    panelHeight: 640
    closable: false

    property string pin: ""
    property bool invalid: false
    readonly property int maxDigits: Devices.pairingMaxDigits
    readonly property bool complete: maxDigits === 6 ? pin.length === 6 : pin.length > 0

    function reset() {
        pin = ""
        invalid = false
    }
    function addDigit(d) {
        if (pin.length < maxDigits) {
            pin += d
            invalid = false
        }
    }
    function backspace() {
        pin = pin.slice(0, -1)
        invalid = false
    }
    function send() {
        if (!complete)
            return
        if (Devices.submitPairingPin(pin))
            dlg.close()
        else
            invalid = true
    }
    function cancel() {
        Devices.cancelPairing()
        dlg.close()
    }

    // AppDialog.open() focuses the dialog root after showing it: take the
    // focus back for the key handler once open() has finished
    onVisibleChanged: if (visible) { reset(); Qt.callLater(() => keyCatcher.forceActiveFocus()) }

    // hardware keyboard: digits, Backspace, Enter, Escape
    Item {
        id: keyCatcher
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => {
            if (event.text.length === 1 && event.text >= "0" && event.text <= "9") {
                dlg.addDigit(event.text)
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                dlg.backspace()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                dlg.send()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                dlg.cancel()
                event.accepted = true
            }
        }
    }

    Text {
        x: 28
        y: 76
        width: dlg.panelWidth - 56
        // the name comes from the device's advertisement: escaped, never markup
        text: "<b>" + Devices.pairingDeviceName.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
              + "</b> wants to pair. Type the "
              + (dlg.maxDigits === 6 ? "6-digit " : "") + "PIN shown on its display (within 30 seconds)."
        textFormat: Text.StyledText
        color: Theme.textSecondary
        font.pixelSize: 17
        wrapMode: Text.WordWrap
    }

    // the PIN: one box per digit (passkey) or a plain field (legacy PIN code)
    Row {
        id: boxes
        visible: dlg.maxDigits === 6
        y: 150
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 10
        Repeater {
            model: 6
            Rectangle {
                width: 52
                height: 62
                radius: 10
                color: Theme.field
                border.width: index === dlg.pin.length ? 2 : 1
                border.color: dlg.invalid ? Theme.danger
                              : index === dlg.pin.length ? Theme.accentLight : Theme.fieldBorder
                Text {
                    anchors.centerIn: parent
                    text: index < dlg.pin.length ? dlg.pin.charAt(index) : ""
                    color: Theme.text
                    font.pixelSize: 30
                    font.weight: Font.Bold
                }
            }
        }
    }
    Rectangle {
        visible: dlg.maxDigits !== 6
        y: 150
        width: 360
        height: 62
        anchors.horizontalCenter: parent.horizontalCenter
        radius: 10
        color: Theme.field
        border.width: 1
        border.color: dlg.invalid ? Theme.danger : Theme.fieldBorder
        Text {
            anchors.centerIn: parent
            text: dlg.pin
            color: Theme.text
            font.pixelSize: 30
            font.weight: Font.Bold
        }
    }

    Text {
        y: 220
        anchors.horizontalCenter: parent.horizontalCenter
        visible: dlg.invalid
        text: "The PIN was not accepted – check the number and try again."
        color: Theme.dangerText
        font.pixelSize: 15
    }

    // number pad (touch screens without a keyboard)
    Grid {
        id: pad
        y: 250
        anchors.horizontalCenter: parent.horizontalCenter
        columns: 3
        spacing: 10
        Repeater {
            model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "⌫", "0", "C"]
            AppButton {
                width: 110
                height: 58
                text: modelData
                kind: "secondary"
                fontSize: 24
                enabled: modelData === "⌫" || modelData === "C" ? dlg.pin.length > 0
                                                                : dlg.pin.length < dlg.maxDigits
                onClicked: {
                    if (modelData === "⌫")
                        dlg.backspace()
                    else if (modelData === "C")
                        dlg.reset()
                    else
                        dlg.addDigit(modelData)
                    keyCatcher.forceActiveFocus()
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
            text: "Cancel"
            kind: "secondary"
            Layout.preferredWidth: 120
            onClicked: dlg.cancel()
        }
        Item { Layout.fillWidth: true }
        AppButton {
            text: "Pair"
            enabled: dlg.complete
            Layout.preferredWidth: 140
            onClicked: dlg.send()
        }
    }
}
