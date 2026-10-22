import QtQuick
import QtQuick.Layouts
import Artilody.Backend

AppDialog {
    id: dlg
    title: Training.workoutName
    panelWidth: 820
    panelHeight: 600

    Text {
        x: 28
        y: 74
        width: dlg.panelWidth - 56
        text: Training.summary
        color: Theme.textSecondary
        font.pixelSize: 17
    }
    Text {
        x: 28
        y: 106
        width: dlg.panelWidth - 56
        text: Training.description.length > 0 ? Training.description : "Ride at your own pace. Power, cadence and heart rate are still recorded."
        color: Theme.text
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }

    RowLayout {
        x: 24
        y: 170
        width: dlg.panelWidth - 48
        height: 78
        spacing: 10
        StatTile { Layout.fillWidth: true; Layout.fillHeight: true; iconName: "timer"; caption: "Duration"; value: Training.freeRide ? "Open" : Theme.clock(Training.totalSec) }
        StatTile { Layout.fillWidth: true; Layout.fillHeight: true; iconName: "dashboard"; iconColor: Theme.accentLight; caption: "Blocks"; value: Training.blockCount }
        StatTile { Layout.fillWidth: true; Layout.fillHeight: true; iconName: "bolt"; iconColor: Theme.warning; caption: "Avg power (ride)"; value: Training.avgPower + " W" }
        StatTile { Layout.fillWidth: true; Layout.fillHeight: true; iconName: "flame"; iconColor: Theme.orange; caption: "Energy"; value: Math.round(Training.kilojoules) + " kJ" }
    }

    Text {
        x: 28
        y: 268
        text: "Intervals"
        color: Theme.text
        font.pixelSize: 18
        font.weight: Font.Bold
    }
    ListView {
        x: 24
        y: 300
        width: dlg.panelWidth - 48
        height: dlg.panelHeight - 300 - 24
        clip: true
        model: Training.timeline
        spacing: 4
        delegate: Rectangle {
            width: ListView.view.width
            height: 40
            radius: 8
            color: index === Training.currentIndex && Training.active ? Theme.navSelected : Theme.cardStrong
            Rectangle {
                x: 10
                width: 8; height: 24; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.zoneColor(modelData.zone)
            }
            Text { x: 32; anchors.verticalCenter: parent.verticalCenter; text: (index + 1) + ".  " + modelData.name; color: Theme.text; font.pixelSize: 15 }
            Text { x: 330; anchors.verticalCenter: parent.verticalCenter; text: "starts " + Theme.clock(modelData.start); color: Theme.textSecondary; font.pixelSize: 14 }
            Text { x: 480; anchors.verticalCenter: parent.verticalCenter; text: modelData.durationText; color: Theme.text; font.pixelSize: 15 }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.wattsText
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
        }
    }
}
