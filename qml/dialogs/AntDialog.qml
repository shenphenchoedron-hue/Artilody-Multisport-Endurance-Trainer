import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

// Anaerobic threshold (AnT) of the runner: current value and zones, the test
// history, the test reminder interval, and setting the AnT by hand.
AppDialog {
    id: dlg
    signal startTest()
    signal message(string text)
    property string errorText: ""

    title: "Anaerobic Threshold"
    panelWidth: 860
    panelHeight: 640

    function openDialog() {
        speedField.text = RunLibrary.antSpeedKmh.toFixed(1)
        hrField.text = RunLibrary.antHeartRate > 0 ? "" + RunLibrary.antHeartRate : ""
        errorText = ""
        open()
    }

    RowLayout {
        x: 28
        y: 80
        width: dlg.panelWidth - 56
        height: dlg.panelHeight - 80 - 28
        spacing: 22

        // ---- left: current AnT, zones, set by hand ----
        ColumnLayout {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 104
                radius: 12
                color: Theme.cardStrong
                border.color: Theme.cardBorder
                Row {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 16
                    Icon { name: "run"; size: 44; color: Theme.runningColor; anchors.verticalCenter: parent.verticalCenter }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: RunLibrary.antSpeedKmh.toFixed(1) + " km/h  •  " + RunLibrary.antPace + " /km"
                            color: Theme.text
                            font.pixelSize: 26
                            font.weight: Font.Black
                        }
                        Text {
                            text: (RunLibrary.antHeartRate > 0 ? "AnT heart rate " + RunLibrary.antHeartRate + " bpm" : "AnT heart rate unknown")
                            color: Theme.textSecondary
                            font.pixelSize: 15
                        }
                        Text {
                            width: 290
                            text: RunLibrary.testStatusText
                            color: RunLibrary.testDue ? Theme.warningText : Theme.textSecondary
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Text { text: "Running zones (% of AnT speed)"; color: Theme.text; font.pixelSize: 16; font.weight: Font.Bold }
            Column {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: RunLibrary.zones
                    Rectangle {
                        width: parent.width
                        height: 30
                        radius: 7
                        color: Theme.cardStrong
                        Rectangle { width: 6; height: parent.height - 10; radius: 3; x: 8; anchors.verticalCenter: parent.verticalCenter; color: Theme.zoneColor(modelData.zone) }
                        Text {
                            x: 24
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Z" + (modelData.zone + 1) + "  " + modelData.name
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: (modelData.zone === 0 ? "< " + modelData.toPercent + " %   < " + modelData.to.toFixed(1)
                                   : modelData.zone === 5 ? "> " + modelData.fromPercent + " %   > " + modelData.from.toFixed(1)
                                   : modelData.fromPercent + "–" + modelData.toPercent + " %   " + modelData.from.toFixed(1) + "–" + modelData.to.toFixed(1))
                                  + " km/h"
                            color: Theme.textSecondary
                            font.pixelSize: 13
                        }
                    }
                }
            }

            Text { text: "Set by hand"; color: Theme.text; font.pixelSize: 16; font.weight: Font.Bold }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppTextField {
                    id: speedField
                    Layout.preferredWidth: 120
                    implicitHeight: 44
                    font.pixelSize: 16
                    placeholderText: "km/h"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                Text { text: "km/h"; color: Theme.textSecondary; font.pixelSize: 15 }
                AppTextField {
                    id: hrField
                    Layout.preferredWidth: 90
                    implicitHeight: 44
                    font.pixelSize: 16
                    placeholderText: "bpm"
                    inputMethodHints: Qt.ImhDigitsOnly
                }
                Text { text: "bpm"; color: Theme.textSecondary; font.pixelSize: 15 }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: "Save"
                    height: 44
                    fontSize: 15
                    horizontalPadding: 14
                    onClicked: {
                        var v = parseFloat(speedField.text.replace(",", "."))
                        var h = hrField.text.trim().length > 0 ? parseInt(hrField.text, 10) : 0
                        if (isNaN(v) || isNaN(h) || !RunLibrary.setManualAnt(v, h)) {
                            dlg.errorText = RunLibrary.lastError.length > 0 ? RunLibrary.lastError : "Enter a speed in km/h"
                        } else {
                            dlg.errorText = ""
                            dlg.message("AnT set to " + v.toFixed(1) + " km/h")
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: dlg.errorText.length > 0
                text: dlg.errorText
                color: Theme.dangerText
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillHeight: true }
        }

        // ---- right: test interval + history ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Text { text: "AnT test"; color: Theme.text; font.pixelSize: 16; font.weight: Font.Bold }
            Text {
                Layout.fillWidth: true
                text: "30 minutes as fast as you can hold an even pace (after a 10-minute warm-up). The mean speed and heart "
                      + "rate of the last 20 minutes become your AnT. Test regularly to keep your zones up to date."
                color: Theme.textSecondary
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Remind me every"; color: Theme.text; font.pixelSize: 15 }
                IconButton { width: 36; height: 36; iconName: "minus"; iconSize: 16; onClicked: RunLibrary.testIntervalWeeks = RunLibrary.testIntervalWeeks - 1 }
                Text { text: RunLibrary.testIntervalWeeks + " weeks"; color: Theme.text; font.pixelSize: 16; font.weight: Font.Bold }
                IconButton { width: 36; height: 36; iconName: "plus"; iconSize: 16; onClicked: RunLibrary.testIntervalWeeks = RunLibrary.testIntervalWeeks + 1 }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: "Run AnT test"
                    iconName: "flag"
                    height: 44
                    fontSize: 15
                    iconSize: 18
                    horizontalPadding: 14
                    enabled: !Running.active
                    onClicked: {
                        dlg.close()
                        dlg.startTest()
                    }
                }
            }

            Text { text: "History"; color: Theme.text; font.pixelSize: 16; font.weight: Font.Bold }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: Theme.cardStrong
                border.color: Theme.cardBorder
                clip: true
                ListView {
                    id: historyList
                    anchors.fill: parent
                    anchors.margins: 6
                    model: RunLibrary.antHistory
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        width: historyList.width
                        height: 46
                        radius: 8
                        color: index === 0 ? Theme.navSelected : "transparent"
                        Icon {
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            name: modelData.manual ? "edit" : "flag"
                            size: 18
                            color: modelData.manual ? Theme.textSecondary : Theme.runningColor
                        }
                        Column {
                            x: 38
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: modelData.speed.toFixed(1) + " km/h  ·  " + modelData.pace + " /km"
                                      + (modelData.heartRate > 0 ? "  ·  " + modelData.heartRate + " bpm" : "")
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: modelData.dateText + "  ·  " + (modelData.manual ? "set by hand" : "AnT test, incline " + modelData.incline.toFixed(1) + " %")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                        }
                        IconButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            flat: true
                            iconName: "trash"
                            iconSize: 16
                            tooltip: "Delete this entry"
                            onClicked: RunLibrary.deleteAntTest(modelData.id)
                        }
                    }
                    Text {
                        visible: historyList.count === 0
                        anchors.centerIn: parent
                        text: "No AnT tests yet"
                        color: Theme.textSecondary
                        font.pixelSize: 15
                    }
                }
            }
        }
    }
}
