import QtQuick
import QtQuick.Controls
import Artilody.Backend

// Why the page's Start is disabled - another session is in progress on
// another page - with the fix right here: go there, end and save it, or
// discard it (a second click confirms). blocker = Sessions.<page>Blocker
// ({} = nothing blocks: the card is hidden and takes no space).
Rectangle {
    id: notice
    property var blocker: ({})
    property string startWhat: "a ride"        // "Finish it to start <startWhat>."
    property string namePrefix: ""             // objectNames <prefix>NoticeText / Go / End / Discard
    readonly property bool shown: !!blocker && ("" + (blocker.kind || "")).length > 0
    property bool confirmDiscard: false

    visible: shown
    implicitHeight: shown ? col.implicitHeight + 22 : 0
    height: implicitHeight
    radius: 10
    color: Theme.cardStrong
    border.color: Theme.warning
    border.width: 1

    onBlockerChanged: confirmDiscard = false
    Timer {
        id: confirmTimer
        interval: 5000
        onTriggered: notice.confirmDiscard = false
    }
    function toast(text) {
        var w = ApplicationWindow.window
        if (w && typeof w.showToast === "function")
            w.showToast(text)
    }
    function goThere() {
        var w = ApplicationWindow.window
        if (w && typeof w.showPage === "function")
            w.showPage(blocker.page)
    }
    function end(save) {
        var what = blocker.what
        var id = Sessions.endSession(blocker.kind, save)
        if (!save)
            toast(what.charAt(0).toUpperCase() + what.slice(1) + " discarded")
        else
            toast(id >= 0 ? what.charAt(0).toUpperCase() + what.slice(1) + " saved – see History"
                          : what.charAt(0).toUpperCase() + what.slice(1) + " ended (too short to save)")
    }

    Column {
        id: col
        x: 12
        y: 11
        width: parent.width - 24
        spacing: 9

        Row {
            width: parent.width
            spacing: 8
            Icon {
                name: "info"
                size: 18
                color: Theme.warningText
            }
            Text {
                objectName: notice.namePrefix + "NoticeText"
                width: parent.width - 26
                text: notice.shown ? notice.blocker.text + " Finish it to start " + notice.startWhat + "." : ""
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }
        Flow {
            width: parent.width
            spacing: 8
            AppButton {
                objectName: notice.namePrefix + "NoticeGo"
                height: 36
                kind: "secondary"
                fontSize: 14
                iconSize: 16
                iconSpacing: 7
                horizontalPadding: 12
                bold: false
                iconName: "chevronRight"
                text: notice.shown ? "Go to " + notice.blocker.pageTitle : ""
                onClicked: notice.goThere()
            }
            AppButton {
                objectName: notice.namePrefix + "NoticeEnd"
                height: 36
                kind: "primary"
                fontSize: 14
                iconSize: 16
                iconSpacing: 7
                horizontalPadding: 12
                iconName: "stop"
                text: "End & Save"
                onClicked: notice.end(true)
            }
            AppButton {
                objectName: notice.namePrefix + "NoticeDiscard"
                height: 36
                kind: notice.confirmDiscard ? "danger" : "secondary"
                fontSize: 14
                iconSize: 16
                iconSpacing: 7
                horizontalPadding: 12
                bold: notice.confirmDiscard
                iconName: "trash"
                text: notice.confirmDiscard ? "Really discard?" : "Discard"
                onClicked: {
                    if (!notice.confirmDiscard) {
                        notice.confirmDiscard = true
                        confirmTimer.restart()
                    } else {
                        notice.confirmDiscard = false
                        notice.end(false)
                    }
                }
            }
        }
    }
}
