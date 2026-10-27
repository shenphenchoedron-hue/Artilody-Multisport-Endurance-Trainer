import QtQuick
import Artilody.Backend

// Watt Training "Devices" card
Card {
    id: card
    signal openDevices()
    title: "Devices"
    // trainer + HR sensor only (a connected rower is counted in Devices.connectedCount too)
    readonly property int cyclingConnected: (Devices.trainerState === 2 ? 1 : 0) + (Devices.heartRateState === 2 ? 1 : 0)

    headerRight: [
        Row {
            spacing: 8
            Rectangle {
                width: 12; height: 12; radius: 6
                anchors.verticalCenter: parent.verticalCenter
                color: card.cyclingConnected === 2 ? Theme.success : card.cyclingConnected === 1 ? Theme.warning : Theme.textMuted
            }
            Text {
                text: card.cyclingConnected === 2 ? "All connected"
                    : card.cyclingConnected === 1 ? "1 of 2 connected" : "Not connected"
                color: Theme.textSecondary
                font.pixelSize: 15
            }
        }
    ]

    Column {
        x: 12
        y: card.contentTop
        width: parent.width - 24
        spacing: 10

        Repeater {
            model: [
                { title: "Smart Trainer", icon: "trainer", name: Devices.trainerName, state: Devices.trainerState,
                  proto: Devices.trainerProtocol.replace("Bluetooth FTMS", "Bluetooth") },
                { title: "Heart Rate Sensor", icon: "heartPulse", name: Devices.heartRateName, state: Devices.heartRateState,
                  proto: Devices.heartRateProtocol.indexOf("Simulated") === 0 ? "Simulated" : (Devices.heartRateName ? "Bluetooth" : "") }
            ]
            Rectangle {
                width: parent.width
                height: 60
                radius: 10
                color: Theme.cardStrong
                border.color: Theme.cardBorder

                Icon {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: modelData.icon
                    size: 34
                    strokeWidth: 1.8
                }
                Column {
                    x: 62
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: modelData.title
                        color: Theme.text
                        font.pixelSize: 16
                    }
                    Text {
                        text: modelData.name ? modelData.name : "Not paired"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        width: 120
                        elide: Text.ElideRight
                    }
                }
                Column {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    StatusDot {
                        anchors.right: parent.right
                        linkState: modelData.state
                        text: modelData.state === 2 ? "Connected" : modelData.state === 1 ? "Connecting" : "Offline"
                        textColor: Theme.text
                        fontSize: 14
                    }
                    Text {
                        anchors.right: parent.right
                        text: modelData.proto
                        color: Theme.textSecondary
                        font.pixelSize: 14
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        z: -1
        cursorShape: Qt.PointingHandCursor
        onClicked: card.openDevices()
    }
}
