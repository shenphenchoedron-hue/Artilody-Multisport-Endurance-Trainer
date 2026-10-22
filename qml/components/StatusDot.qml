import QtQuick

// Coloured dot + text ("● Connected")
Row {
    property int linkState: 0          // 0 disconnected, 1 connecting, 2 connected
    property string text: linkState === 2 ? "Connected" : linkState === 1 ? "Connecting…" : "Available"
    property real fontSize: 15
    property color textColor: linkState === 2 ? Theme.success : Theme.textSecondary
    spacing: 8

    Rectangle {
        width: 12; height: 12; radius: 6
        anchors.verticalCenter: parent.verticalCenter
        color: parent.linkState === 2 ? Theme.success : parent.linkState === 1 ? Theme.warning : Theme.textMuted
        SequentialAnimation on opacity {
            running: parent.linkState === 1
            loops: Animation.Infinite
            alwaysRunToEnd: true
            NumberAnimation { to: 0.3; duration: 500 }
            NumberAnimation { to: 1; duration: 500 }
        }
    }
    Text {
        text: parent.text
        color: parent.textColor
        font.pixelSize: parent.fontSize
        anchors.verticalCenter: parent.verticalCenter
    }
}
