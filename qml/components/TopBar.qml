import QtQuick
import QtQuick.Controls
import Artilody.Backend

// Theme toggle, settings and profile menu (top-right in every mockup).
Item {
    id: bar
    signal settingsRequested()
    signal editProfileRequested()
    signal newProfileRequested()

    width: row.width
    height: 60

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 18

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: Theme.dark ? "sun" : "moon"
            iconSize: 26
            flat: true
            tooltip: Theme.dark ? "Light mode" : "Dark mode"
            onClicked: Theme.dark = !Theme.dark
        }
        Rectangle {
            width: 2; height: 26; radius: 1
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.textMuted
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "settings"
            iconSize: 26
            flat: true
            tooltip: "Settings"
            onClicked: bar.settingsRequested()
        }

        Item {
            width: profileRow.width + 8
            height: 50
            anchors.verticalCenter: parent.verticalCenter
            Row {
                id: profileRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                Avatar {
                    size: 44
                    source: Profiles.avatarUrl
                    initials: Profiles.initials
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: Profiles.name
                    color: Theme.text
                    font.pixelSize: 18
                    anchors.verticalCenter: parent.verticalCenter
                }
                Icon {
                    name: "chevronDown"
                    size: 20
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: profileMenu.popup(0, height + 4)
            }

            Menu {
                id: profileMenu
                width: 230
                background: Rectangle {
                    color: Theme.dark ? "#22327c" : "#ffffff"
                    radius: 10
                    border.color: Theme.cardBorder
                }

                Instantiator {
                    model: Profiles.profiles
                    delegate: MenuItem {
                        text: modelData.name + "  ·  FTP " + modelData.ftp
                        checkable: true
                        checked: modelData.id === Profiles.profileId
                        enabled: !Training.active   // no profile switching mid-ride
                        onTriggered: Profiles.selectProfile(modelData.id)
                        contentItem: Text {
                            text: parent.text
                            color: Theme.text
                            font.pixelSize: 14
                            leftPadding: 22
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    onObjectAdded: (index, object) => profileMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => profileMenu.removeItem(object)
                }
                MenuSeparator {}
                MenuItem {
                    text: "Edit profile…"
                    onTriggered: bar.editProfileRequested()
                    contentItem: Text { text: parent.text; color: Theme.text; font.pixelSize: 14; leftPadding: 22; verticalAlignment: Text.AlignVCenter }
                }
                MenuItem {
                    text: "New profile…"
                    enabled: !Training.active
                    onTriggered: bar.newProfileRequested()
                    contentItem: Text { text: parent.text; color: Theme.text; font.pixelSize: 14; leftPadding: 22; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
    }
}
