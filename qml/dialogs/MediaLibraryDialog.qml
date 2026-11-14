import QtQuick
import QtQuick.Controls
import Artilody.Backend

// The server's media library (MediaLibrary / MediaSync, docs/server-plan.md
// step 4) for one kind: the GPX routes (fetched by themselves), the videos
// (fetched when asked: Download), the workouts (added to the library by
// themselves) or the training plans (Import… onto a start date). Shown from
// the last visit while the server is off. Opened from the GPX / Video Ride's
// library menu, and - instead of a file dialog when the app uses the server -
// from Workouts › Import and the Calendar's Import plan.
AppDialog {
    id: dlg
    property string kind: "gpx"   // "gpx" / "video" / "workout" / "plan"
    readonly property bool video: kind === "video"
    readonly property bool plan: kind === "plan"
    readonly property bool workout: kind === "workout"
    readonly property var shown: MediaLibrary.items.filter((i) => i.kind === dlg.kind)
    title: video ? "Server videos" : plan ? "Server training plans" : workout ? "Server workouts" : "Server GPX routes"
    panelWidth: 720
    panelHeight: 640

    function openFor(k) {
        kind = k
        open()
    }

    Column {
        id: top
        x: 28
        y: 84
        width: dlg.panelWidth - 56
        spacing: 10

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            font.pixelSize: 14
            color: !Server.active || !MediaLibrary.serverThere || MediaLibrary.state === "error" ? Theme.dangerText : Theme.textSecondary
            text: !Server.active ? "The server is off in Settings › Server."
                  : !MediaLibrary.serverThere ? "The server is not reachable – this is the list from the last visit."
                  : MediaLibrary.state === "error" ? MediaLibrary.message
                  : MediaLibrary.state === "syncing" ? "Updating the list …"
                  : dlg.video ? "Videos are large: they are fetched when you press Download (an interrupted download goes on where it stopped)."
                  : dlg.workout ? "The server's workouts are added to your workout library by themselves. Add to Workouts adds one again (e.g. after you deleted it) – one already there is not added twice."
                  : dlg.plan ? "Import… puts a plan in your calendar from a start date you choose."
                  : "GPX routes are fetched by themselves. A route of the same name that was edited here is never overwritten – Download fetches the server's as a copy."
        }

        // the transfer under way
        Rectangle {
            visible: MediaLibrary.transferring
            width: parent.width
            height: visible ? 74 : 0
            radius: 10
            color: Theme.cardStrong
            border.color: Theme.cardBorder
            Column {
                x: 14
                y: 10
                width: parent.width - 28 - cancelButton.width - 12
                spacing: 6
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    text: (MediaLibrary.transferKind === "upload" ? "Sending " : MediaLibrary.transferKind === "checking" ? "Checking "
                           : "Downloading ") + MediaLibrary.transferName + " – " + Math.round(MediaLibrary.transferProgress * 100) + " %"
                          + (MediaLibrary.queued > 0 ? "  (+" + MediaLibrary.queued + " waiting)" : "")
                }
                Rectangle {
                    width: parent.width
                    height: 8
                    radius: 4
                    color: Theme.dark ? "#1b2a6b" : "#dde3f5"
                    Rectangle {
                        width: parent.width * MediaLibrary.transferProgress
                        height: parent.height
                        radius: 4
                        color: Theme.success
                    }
                }
            }
            AppButton {
                id: cancelButton
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                kind: "secondary"
                text: "Cancel"
                fontSize: 14
                bold: false
                horizontalPadding: 14
                height: 36
                onClicked: MediaLibrary.cancel()
            }
        }
    }

    ListView {
        id: list
        objectName: "mediaList"
        x: 28
        y: top.y + top.height + 12
        width: dlg.panelWidth - 56
        height: dlg.panelHeight - y - 92
        clip: true
        spacing: 6
        model: dlg.shown
        ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

        Text {
            visible: list.count === 0
            width: list.width
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: 15
            text: dlg.video ? "No videos on the server yet. Upload them in the server's web interface (Media), or send one from here: the library menu › Send to Server."
                  : dlg.workout ? "No workouts on the server yet. Upload them in the server's web interface (Media), or send one from here: Workouts › Export › Send selected workout to the server."
                  : dlg.plan ? "No training plans on the server yet. Upload them in the server's web interface (Media), or send one from here: Calendar › Plans › Export weeks as a plan… › Send to Server."
                  : "No GPX routes on the server yet. Upload them in the server's web interface (Media), or send one from here: the library menu › Send to Server."
        }

        delegate: Rectangle {
            required property var modelData
            width: list.width - 14
            height: 60
            radius: 10
            color: Theme.cardStrong
            border.color: Theme.cardBorder

            Column {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 28 - action.width - 12
                spacing: 2
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    text: modelData.name
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    color: modelData.state === "conflict" ? Theme.dangerText : Theme.textSecondary
                    font.pixelSize: 13
                    text: modelData.sizeText
                          + (modelData.uploadedBy.length > 0 ? " · from " + modelData.uploadedBy : "")
                          + (modelData.state === "conflict" ? " · a route of this name is here (edited) – kept" : "")
                }
            }
            Item {
                id: action
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(stateText.implicitWidth, actionButton.visible ? actionButton.width : 0)
                height: 36
                Text {
                    id: stateText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !actionButton.visible
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: modelData.state === "local" ? Theme.successText : Theme.textSecondary
                    text: modelData.state === "local" ? (dlg.workout ? "In your library" : "On this station")
                          : modelData.state === "queued" ? "Waiting …"
                          : modelData.state === "downloading" ? Math.round(MediaLibrary.transferProgress * 100) + " %"
                          : ""
                }
                AppButton {
                    id: actionButton
                    anchors.right: parent.right
                    // (a plan: Import… - here already or fetched first; a workout:
                    // Add to Workouts - also again, e.g. once deleted in the library)
                    visible: dlg.plan || dlg.workout ? (modelData.state === "local" || modelData.state === "available")
                                                     : modelData.state === "available" || modelData.state === "conflict"
                    enabled: MediaLibrary.serverThere || ((dlg.plan || dlg.workout) && modelData.state === "local")
                    kind: modelData.state === "conflict" || (dlg.workout && modelData.state === "local") ? "secondary" : "primary"
                    text: dlg.plan ? "Import…" : dlg.workout ? "Add to Workouts"
                          : modelData.state === "conflict" ? "Download copy" : "Download"
                    fontSize: 14
                    bold: false
                    horizontalPadding: 14
                    height: 36
                    onClicked: {
                        if (dlg.plan) {
                            dlg.close()
                            MediaLibrary.importPlan(modelData.uuid)
                        } else if (dlg.workout) {
                            MediaLibrary.addWorkout(modelData.uuid)
                        } else {
                            MediaLibrary.download(modelData.uuid)
                        }
                    }
                }
            }
        }
    }

    AppButton {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        text: "Done"
        onClicked: dlg.close()
    }
}
