import QtQuick
import QtQuick.Controls
import Artilody.Backend

// Ride pages (Video Ride, Free Ride): what the trainer ACTUALLY does when
// something is wrong (a rejected / unanswered command, control lost, another
// mode than requested: Devices.trainerControlStatus) and the reference gear
// prompt of the virtual gears ("Your trainer reports 44×14 (3.14) but the
// setting is 50×17 (2.94) — use the detected gear?", FTMS gradient virtual
// shifting only) and a failed Zwift protocol handshake ("Zwift protocol
// failed (…) – using FTMS gradient."). Hidden (no space) when there is
// nothing to say.
Rectangle {
    id: notice
    property bool gears: false          // virtual gears in use on the page
    property string namePrefix: ""
    readonly property bool showStatus: Devices.trainerState === 2 && Devices.trainerControlProblem
    readonly property bool showPrompt: gears && Gears.referencePrompt.length > 0
    readonly property bool showShifting: gears && Gears.shiftingProblem
    readonly property bool shown: showStatus || showPrompt || showShifting

    visible: shown
    implicitHeight: shown ? col.implicitHeight + 16 : 0
    height: implicitHeight
    radius: 8
    color: Qt.rgba(1, 0.65, 0.1, 0.10)
    border.color: Theme.warningText
    border.width: 1

    Column {
        id: col
        x: 10; y: 8
        width: parent.width - 20
        spacing: 6
        Text {
            objectName: notice.namePrefix + "TrainerStatusText"
            visible: notice.showStatus
            width: parent.width
            text: Devices.trainerControlStatus
            color: Theme.warningText
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Text {
            objectName: notice.namePrefix + "ShiftingProblemText"
            visible: notice.showShifting
            width: parent.width
            text: Gears.shiftingDetail
            color: Theme.warningText
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Text {
            objectName: notice.namePrefix + "ReferencePromptText"
            visible: notice.showPrompt
            width: parent.width
            text: Gears.referencePrompt
            color: Theme.warningText
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Row {
            visible: notice.showPrompt
            spacing: 8
            AppButton {
                objectName: notice.namePrefix + "UseDetectedGear"
                height: 30
                fontSize: 13
                bold: false
                horizontalPadding: 10
                text: "Use detected gear"
                onClicked: Gears.useDetectedGear()
            }
            AppButton {
                height: 30
                fontSize: 13
                bold: false
                horizontalPadding: 10
                kind: "secondary"
                text: "Keep " + Gears.referenceLabel
                onClicked: Gears.dismissReferencePrompt()
            }
        }
    }
}
