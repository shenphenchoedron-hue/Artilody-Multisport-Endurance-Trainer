import QtQuick
import QtQuick.Layouts
import Artilody.Backend

// The result of the AnT test (Treadmill Run): shown when the 30-minute test
// block ends (Running.antTestCompleted). The runner may keep running the
// cool-down meanwhile. "Use as new AnT" saves it (Running.acceptAntResult()).
AppDialog {
    id: dlg
    property var result: ({})
    readonly property bool valid: !!result && result.valid === true
    readonly property real diff: valid ? (result.diff || 0) : 0
    property string errorText: ""
    signal message(string text)

    title: "AnT Test Complete"
    panelWidth: 680
    panelHeight: 500

    function openWith(r) {
        result = r || {}
        errorText = ""
        open()
    }
    function speedText(v) { return (v || 0).toFixed(1) + " km/h" }

    Text {
        x: 28
        y: 76
        width: dlg.panelWidth - 56
        text: dlg.valid ? (dlg.result.message || "") + " – keep running the cool-down, the run continues."
                        : (dlg.result.message || "The test could not be evaluated.")
        color: dlg.valid ? Theme.textSecondary : Theme.dangerText
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }

    Column {
        visible: dlg.valid
        anchors.horizontalCenter: parent.horizontalCenter
        y: 124
        spacing: 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Anaerobic threshold speed"
            color: Theme.textSecondary
            font.pixelSize: 16
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Text {
                id: bigValue
                text: (dlg.result.speed || 0).toFixed(1)
                color: Theme.text
                font.pixelSize: 76
                font.weight: Font.Black
            }
            Text {
                anchors.baseline: bigValue.baseline
                text: "km/h"
                color: Theme.textSecondary
                font.pixelSize: 28
                font.weight: Font.Bold
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: (dlg.result.pace || "–") + " min/km" + (dlg.result.heartRate > 0 ? "   •   " + dlg.result.heartRate + " bpm" : "")
                  + "   •   incline " + (dlg.result.incline || 0).toFixed(1) + " %"
            color: Theme.textSecondary
            font.pixelSize: 17
        }
    }

    RowLayout {
        visible: dlg.valid
        x: 24
        y: 286
        width: dlg.panelWidth - 48
        height: 76
        spacing: 10
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true
            iconName: "run"; iconColor: Theme.runningColor
            caption: "Current AnT"; value: dlg.speedText(dlg.result.previousSpeed); valueSize: 20
        }
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true
            iconName: "trendUp"; iconColor: dlg.diff >= 0 ? Theme.success : Theme.danger
            caption: "Change"; value: (dlg.diff >= 0 ? "+" : "") + dlg.diff.toFixed(1) + " km/h"; valueSize: 20
        }
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true
            iconName: "heart"; iconColor: Theme.heartRate
            caption: "AnT heart rate"; value: dlg.result.heartRate > 0 ? dlg.result.heartRate + " bpm" : "no sensor"; valueSize: 20
        }
    }

    Text {
        x: 28
        y: 376
        width: dlg.panelWidth - 56
        text: dlg.errorText.length > 0 ? dlg.errorText
              : dlg.result.accepted ? "Saved – your running zones and workout targets use the new AnT."
              : "The new AnT sets your running zones and the targets of all % AnT workouts."
        color: dlg.errorText.length > 0 ? Theme.dangerText : Theme.textMuted
        font.pixelSize: 15
        wrapMode: Text.WordWrap
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 26
        spacing: 12
        AppButton {
            text: dlg.valid && !dlg.result.accepted ? "Keep current" : "Close"
            kind: "secondary"
            onClicked: {
                if (!dlg.result.accepted)
                    Running.dismissAntResult()
                dlg.close()
            }
        }
        Item { Layout.fillWidth: true }
        AppButton {
            visible: dlg.valid && !dlg.result.accepted
            text: "Use as new AnT"
            iconName: "check"
            onClicked: {
                if (Running.acceptAntResult()) {
                    dlg.close()
                    dlg.message("AnT updated to " + (dlg.result.speed || 0).toFixed(1) + " km/h – zones and workouts adjusted")
                } else {
                    dlg.errorText = RunLibrary.lastError.length > 0 ? RunLibrary.lastError : "Could not save the AnT"
                }
            }
        }
    }
}
