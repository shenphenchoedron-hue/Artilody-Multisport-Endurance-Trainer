import QtQuick

// Round avatar: profile picture if available, otherwise initials.
Item {
    id: avatar
    property url source
    property string initials: "?"
    property real size: 44
    property color ringColor: Qt.rgba(1, 1, 1, 0.25)

    width: size
    height: size

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: "#ffb340" }
            GradientStop { position: 1; color: "#ff4f8b" }
        }
        visible: image.status !== Image.Ready
        Text {
            anchors.centerIn: parent
            text: avatar.initials
            color: "white"
            font.pixelSize: avatar.size * 0.38
            font.weight: Font.Bold
        }
    }

    Image {
        id: image
        anchors.fill: parent
        source: avatar.source
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: avatar.size * 2
        sourceSize.height: avatar.size * 2
        visible: false
        asynchronous: true
    }

    // circular mask without extra modules: draw the image through a Canvas
    Canvas {
        id: masked
        anchors.fill: parent
        visible: image.status === Image.Ready
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, width / 2, 0, Math.PI * 2)
            ctx.closePath()
            ctx.clip()
            var iw = image.sourceSize.width > 0 ? image.implicitWidth : width
            var ih = image.sourceSize.height > 0 ? image.implicitHeight : height
            var s = Math.max(width / iw, height / ih)
            ctx.drawImage(image, (width - iw * s) / 2, (height - ih * s) / 2, iw * s, ih * s)
        }
        Connections {
            target: image
            function onStatusChanged() { masked.requestPaint() }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.color: avatar.ringColor
        border.width: 2
    }
}
