import QtQuick
import QtQuick.Layouts
import Artilody.Backend

// Shows the FTP estimated from a ramp test and lets the rider apply it.
// The values come from C++ (Training.ftpTestCompleted / History.selectedRide);
// the comparison texts from Profiles.ftpComparison().
AppDialog {
    id: dlg
    property int estimatedFtp: 0
    property int previousFtp: 0
    property int best1min: 0
    property string errorText: ""
    readonly property var cmp: Profiles.ftpComparison(estimatedFtp, previousFtp)
    signal message(string text)

    title: "FTP Test Complete"
    panelWidth: 660
    panelHeight: 470

    function openWith(estimated, previous, best) {
        estimatedFtp = estimated
        previousFtp = previous
        best1min = best
        errorText = ""
        open()
    }

    Text {
        x: 28
        y: 76
        width: dlg.panelWidth - 56
        text: "Based on 75% of your best 1-minute power (" + dlg.best1min + " W)."
        color: Theme.textSecondary
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 116
        spacing: 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Estimated FTP"
            color: Theme.textSecondary
            font.pixelSize: 16
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Text {
                id: bigValue
                text: dlg.estimatedFtp
                color: Theme.text
                font.pixelSize: 76
                font.weight: Font.Black
            }
            Text {
                anchors.baseline: bigValue.baseline
                text: "W"
                color: Theme.textSecondary
                font.pixelSize: 28
                font.weight: Font.Bold
            }
        }
    }

    RowLayout {
        x: 24
        y: 248
        width: dlg.panelWidth - 48
        height: 76
        spacing: 10
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true
            iconName: "bolt"; iconColor: Theme.warning
            caption: "Current FTP"; value: dlg.previousFtp + " W"; valueSize: 20
        }
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true
            iconName: "trendUp"; iconColor: (dlg.cmp.diff || 0) >= 0 ? Theme.success : Theme.danger
            caption: "Change"; value: (dlg.cmp.diffText || "") + "  " + (dlg.cmp.percentText || ""); valueSize: 20
        }
        StatTile {
            Layout.fillWidth: true; Layout.fillHeight: true
            iconName: "user"; iconColor: Theme.accentLight
            caption: "New W/kg"; value: dlg.cmp.wattsPerKgText || ""; valueSize: 20
        }
    }

    Text {
        x: 28
        y: 340
        width: dlg.panelWidth - 56
        text: dlg.errorText.length > 0 ? dlg.errorText
                                       : "Updating your FTP adjusts your power zones and all workout targets."
        color: dlg.errorText.length > 0 ? Theme.danger : Theme.textMuted
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
            text: "Keep current"
            kind: "secondary"
            onClicked: dlg.close()
        }
        Item { Layout.fillWidth: true }
        AppButton {
            text: "Update FTP"
            iconName: "check"
            onClicked: {
                if (Profiles.saveProfile({ ftp: dlg.estimatedFtp })) {
                    dlg.close()
                    dlg.message("FTP updated to " + dlg.estimatedFtp + " W – zones and workouts adjusted")
                } else {
                    dlg.errorText = Profiles.lastError
                }
            }
        }
    }
}
