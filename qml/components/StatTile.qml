import QtQuick

// Small statistic tile (icon + caption + big value)
Rectangle {
    id: tile
    property string iconName
    property color iconColor: Theme.text
    property real iconSize: 26
    property string caption
    property string value
    property real valueSize: 24
    property bool large: false     // History top tiles

    radius: 12
    color: large ? Theme.card : Theme.cardStrong
    border.width: 1
    border.color: Theme.cardBorder

    Row {
        anchors.verticalCenter: parent.verticalCenter
        x: tile.large ? 26 : 16
        spacing: tile.large ? 26 : 12
        Icon {
            name: tile.iconName
            color: tile.iconColor
            size: tile.large ? 54 : tile.iconSize
            strokeWidth: tile.large ? 2.4 : 2
            anchors.verticalCenter: tile.large ? parent.verticalCenter : undefined
            y: tile.large ? 0 : 4
        }
        Column {
            spacing: tile.large ? 2 : 6
            Text {
                text: tile.caption
                color: Theme.textSecondary
                font.pixelSize: tile.large ? 17 : 15
            }
            Text {
                text: tile.value
                color: Theme.text
                font.pixelSize: tile.large ? 36 : tile.valueSize
                font.weight: Font.Bold
            }
        }
    }
}
