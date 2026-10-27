import QtQuick
import QtQuick.Controls
import Artilody.Backend

// Watt Training "Music Player" card
Card {
    id: card
    signal openLibrary()

    Row {
        x: 18
        y: 16
        spacing: 12
        Icon {
            name: "music"
            size: 28
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: "Music Player"
            color: Theme.text
            font.pixelSize: 19
            font.weight: Font.Bold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    IconButton {
        anchors.right: parent.right
        anchors.rightMargin: 12
        y: 12
        flat: true
        iconName: "playlist"
        iconSize: 22
        tooltip: "Playlists & library"
        onClicked: card.openLibrary()
    }

    // cover
    Rectangle {
        id: cover
        x: 16
        y: 56
        width: 92
        height: 92
        radius: 6
        clip: true
        gradient: Gradient {
            GradientStop { position: 0; color: "#ff4f8b" }
            GradientStop { position: 1; color: "#5b3df5" }
        }
        Icon {
            anchors.centerIn: parent
            visible: coverImage.status !== Image.Ready
            name: "music"
            size: 44
            color: "white"
        }
        Image {
            id: coverImage
            anchors.fill: parent
            source: Music.coverSource
            fillMode: Image.PreserveAspectCrop
            cache: false
            visible: status === Image.Ready && Music.coverSource !== ""
        }
    }

    Text {
        x: 122
        y: 58
        width: parent.width - 122 - 48
        text: Music.title
        color: Theme.text
        font.pixelSize: 18
        font.weight: Font.Bold
        elide: Text.ElideRight
    }
    Text {
        x: 122
        y: 82
        width: parent.width - 122 - 20
        text: Music.artist
        color: Theme.textSecondary
        font.pixelSize: 14
        elide: Text.ElideRight
    }
    Icon {
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: 58
        name: Music.favorite ? "heart" : "heartOutline"
        color: Music.favorite ? Theme.heartRate : Theme.text
        size: 24
        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            cursorShape: Qt.PointingHandCursor
            onClicked: Music.toggleFavorite()
        }
    }

    // transport
    Row {
        x: 122
        y: 102
        spacing: 22
        IconButton {
            flat: true
            iconName: "skipBack"
            iconSize: 24
            anchors.verticalCenter: parent.verticalCenter
            onClicked: Music.previous()
        }
        Rectangle {
            width: 52
            height: 52
            radius: 26
            color: Theme.heartRate
            anchors.verticalCenter: parent.verticalCenter
            Icon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: Music.playing ? 0 : 2
                name: Music.playing ? "pause" : "play"
                color: "white"
                size: 24
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: Music.togglePlay()
            }
        }
        IconButton {
            flat: true
            iconName: "skipForward"
            iconSize: 24
            anchors.verticalCenter: parent.verticalCenter
            onClicked: Music.next()
        }
    }

    // progress
    AppSlider {
        id: progress
        x: 16
        y: 160
        width: parent.width - 32 - 52
        from: 0
        to: Math.max(1, Music.duration)
        value: pressed ? value : Music.position
        fillColor: Theme.danger
        onMoved: Music.seek(value)
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: progress.verticalCenter
        text: Music.durationText
        color: Theme.text
        font.pixelSize: 13
    }
    Text {
        x: 16
        y: 182
        text: Music.positionText
        color: Theme.text
        font.pixelSize: 13
    }

    // volume
    Icon {
        x: 16
        y: 204
        name: Music.muted || Music.volume === 0 ? "mute" : "volume"
        size: 22
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Music.muted = !Music.muted
        }
    }
    AppSlider {
        id: volume
        x: 50
        y: 203
        width: parent.width - 50 - 66
        from: 0
        to: 1
        value: Music.volume
        fillColor: Theme.text
        trackHeight: 4
        onMoved: Music.volume = value
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: volume.verticalCenter
        text: Math.round(Music.volume * 100) + "%"
        color: Theme.text
        font.pixelSize: 13
    }

    // up next
    Text {
        x: 16
        y: 234
        text: Music.upNext.length > 0 ? "Up Next" : (Music.hasTrack ? "" : "No music yet – open the library to add songs")
        color: Theme.text
        font.pixelSize: 15
        font.weight: Font.DemiBold
        width: parent.width - 32
        wrapMode: Text.WordWrap
    }
    // Bounded to the space left in the card: it never draws past the frame and
    // scrolls if the rows do not fit (small cards).
    ListView {
        id: upNextList
        x: 16
        y: 258
        width: parent.width - 32
        height: Math.max(0, parent.height - y - 12)
        clip: true
        spacing: 3
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        model: Music.upNext
        ScrollBar.vertical: ScrollBar {
            id: upNextBar
            policy: upNextList.contentHeight > upNextList.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }
        delegate: Item {
            width: upNextList.width - (upNextBar.visible ? upNextBar.width + 4 : 0)
            height: 34
            Text {
                text: index + 1
                color: Theme.text
                font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                x: 22
                width: 30; height: 30; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.zoneColor((modelData.index * 2 + 1) % 6)
                opacity: 0.85
                Icon { anchors.centerIn: parent; name: "music"; size: 16; color: "white" }
            }
            Column {
                x: 62
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 62 - 80
                Text {
                    text: modelData.title
                    color: Theme.text
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    width: parent.width
                }
                Text {
                    text: modelData.artist
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 36
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.durationText
                color: Theme.text
                font.pixelSize: 13
            }
            Icon {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                name: "dots"
                size: 22
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Music.playIndex(modelData.index)
                }
            }
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 30
                z: -1
                cursorShape: Qt.PointingHandCursor
                onDoubleClicked: Music.playIndex(modelData.index)
            }
        }
    }
}
