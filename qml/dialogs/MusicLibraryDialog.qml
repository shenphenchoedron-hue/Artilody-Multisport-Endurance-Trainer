import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Artilody.Backend

// Playlists + local music library
AppDialog {
    id: dlg
    title: "Music Library"
    panelWidth: 920
    panelHeight: 640

    FileDialog {
        id: filePicker
        title: "Add music files"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Audio (*.mp3 *.m4a *.aac *.flac *.wav *.ogg *.opus *.wma)", "All files (*)"]
        onAccepted: Music.addFiles(selectedFiles)
    }
    FolderDialog {
        id: folderPicker
        title: "Add a music folder"
        onAccepted: Music.addFolder(selectedFolder)
    }

    // playlists
    Rectangle {
        x: 24
        y: 80
        width: 260
        height: dlg.panelHeight - 104
        radius: 12
        color: Theme.cardStrong
        border.color: Theme.cardBorder

        Text { x: 16; y: 14; text: "Playlists"; color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold }

        ListView {
            id: playlists
            x: 8
            y: 48
            width: parent.width - 16
            height: parent.height - 48 - 120
            clip: true
            model: Music.playlists
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                id: playlistsBar
                policy: playlists.contentHeight > playlists.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            delegate: Rectangle {
                width: playlists.width - (playlistsBar.visible ? playlistsBar.width + 2 : 0)
                height: 44
                radius: 8
                readonly property bool current: modelData.id === Music.currentPlaylistId
                color: current ? Theme.navSelected : (plMouse.containsMouse ? Theme.hover : "transparent")
                Row {
                    x: 10
                    width: parent.width - 10 - 44
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Icon {
                        id: plIcon
                        name: modelData.name === "Favorites" ? "heart" : "playlist"
                        size: 20
                        color: modelData.name === "Favorites" ? Theme.heartRate : Theme.text
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.name
                        color: current ? Theme.accentLight : Theme.text
                        font.pixelSize: 15
                        width: parent.width - plIcon.width - parent.spacing
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.count
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                MouseArea {
                    id: plMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Music.selectPlaylist(modelData.id)
                }
            }
        }

        Column {
            x: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            width: parent.width - 24
            spacing: 8
            AppTextField {
                id: newName
                width: parent.width
                height: 42
                font.pixelSize: 15
                placeholderText: "New playlist name"
                onAccepted: create()
                function create() {
                    if (text.trim().length === 0)
                        return
                    Music.createPlaylist(text)
                    text = ""
                }
            }
            Row {
                spacing: 8
                AppButton {
                    width: 150; height: 40; fontSize: 14; iconSize: 16
                    iconName: "plus"; text: "Create"
                    onClicked: newName.create()
                }
                AppButton {
                    width: 78; height: 40; fontSize: 14; iconSize: 16
                    kind: "secondary"; iconName: "trash"; text: ""
                    enabled: Music.playlists.length > 1
                    onClicked: Music.deletePlaylist(Music.currentPlaylistId)
                }
            }
        }
    }

    // tracks
    Item {
        x: 304
        y: 80
        width: dlg.panelWidth - 304 - 24
        height: dlg.panelHeight - 104

        TextField {
            id: plName
            width: parent.width - 330
            height: 44
            text: Music.currentPlaylistName
            color: Theme.text
            font.pixelSize: 22
            font.weight: Font.Bold
            leftPadding: 6
            selectByMouse: true
            background: Rectangle {
                radius: 8
                color: plName.activeFocus ? Theme.field : "transparent"
                border.width: plName.activeFocus ? 1 : 0
                border.color: Theme.accentLight
            }
            onEditingFinished: Music.renamePlaylist(Music.currentPlaylistId, text)
            // long names: show the beginning, not the end, while not editing
            onTextChanged: if (!activeFocus) cursorPosition = 0
            onActiveFocusChanged: if (!activeFocus) cursorPosition = 0
        }
        Row {
            anchors.right: parent.right
            spacing: 10
            AppButton {
                height: 44; fontSize: 15; iconSize: 18; bold: false
                kind: "secondary"; iconName: "music"; text: "Add Files"
                onClicked: filePicker.open()
            }
            AppButton {
                height: 44; fontSize: 15; iconSize: 18; bold: false
                kind: "secondary"; iconName: "folder"; text: "Add Folder"
                onClicked: folderPicker.open()
            }
        }
        Row {
            y: 52
            spacing: 14
            Text {
                text: Music.tracks.count + " songs"
                color: Theme.textSecondary
                font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                IconButton {
                    flat: true; iconName: "shuffle"; iconSize: 18
                    iconColor: Music.shuffle ? Theme.accentLight : Theme.textMuted
                    tooltip: "Shuffle"
                    onClicked: Music.shuffle = !Music.shuffle
                }
                IconButton {
                    flat: true; iconName: "repeat"; iconSize: 18
                    iconColor: Music.repeat ? Theme.accentLight : Theme.textMuted
                    tooltip: "Repeat playlist"
                    onClicked: Music.repeat = !Music.repeat
                }
            }
        }

        ListView {
            id: tracks
            y: 92
            width: parent.width
            height: parent.height - 92
            clip: true
            model: Music.tracks
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                id: tracksBar
                policy: tracks.contentHeight > tracks.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            delegate: Rectangle {
                width: tracks.width - (tracksBar.visible ? tracksBar.width + 4 : 0)
                height: 52
                radius: 8
                color: isCurrent ? Theme.navSelected : (trMouse.containsMouse ? Theme.hover : "transparent")
                MouseArea {
                    id: trMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onDoubleClicked: Music.playIndex(index)
                }
                Text {
                    x: 12
                    width: 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: isCurrent && Music.playing ? "▶" : (index + 1)
                    color: isCurrent ? Theme.accentLight : Theme.textSecondary
                    font.pixelSize: 14
                }
                Column {
                    x: 46
                    width: parent.width - 46 - 200
                    anchors.verticalCenter: parent.verticalCenter
                    Text { text: title; color: isCurrent ? Theme.accentLight : Theme.text; font.pixelSize: 15; elide: Text.ElideRight; width: parent.width }
                    Text { text: artist.length > 0 ? artist : "Unknown artist"; color: Theme.textSecondary; font.pixelSize: 12; elide: Text.ElideRight; width: parent.width }
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 150
                    anchors.verticalCenter: parent.verticalCenter
                    text: durationText
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    IconButton { flat: true; iconName: "play"; iconSize: 16; tooltip: "Play"; onClicked: Music.playIndex(index) }
                    IconButton { flat: true; iconName: "chevronUp"; iconSize: 16; tooltip: "Move up"; enabled: index > 0; onClicked: Music.moveTrack(index, index - 1) }
                    IconButton { flat: true; iconName: "chevronDown"; iconSize: 16; tooltip: "Move down"; enabled: index < tracks.count - 1; onClicked: Music.moveTrack(index, index + 1) }
                    IconButton { flat: true; iconName: "trash"; iconSize: 16; tooltip: "Remove"; onClicked: Music.removeTrack(index) }
                }
            }
            Column {
                visible: tracks.count === 0
                anchors.centerIn: parent
                spacing: 10
                Icon { name: "music"; size: 52; color: Theme.textMuted; anchors.horizontalCenter: parent.horizontalCenter }
                Text {
                    text: "This playlist is empty.\nAdd local music files or a whole folder."
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textSecondary
                    font.pixelSize: 16
                }
            }
        }
    }

    Text {
        visible: Music.errorText.length > 0
        x: 304
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        width: dlg.panelWidth - 304 - 24
        text: Music.errorText
        elide: Text.ElideMiddle
        color: Theme.danger
        font.pixelSize: 13
    }
}
