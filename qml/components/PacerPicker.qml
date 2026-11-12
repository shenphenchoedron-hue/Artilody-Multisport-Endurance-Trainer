import QtQuick
import QtQuick.Controls

// The pacer of the GPX Ride (RoutePacer): its own switch - a rider holding a
// set number of watts per kilogram all the way (instead of the ghost: one at
// a time). Switched on / off before the start; its W/kg may change during
// the ride too (it speeds up / slows down from where it is).
//
//   PacerPicker { pacer: Gpx.pacer; locked: Gpx.active; riderKg: Profiles.weight }
Column {
    id: picker
    property var pacer: null
    property bool locked: false
    property real riderKg: 75             // the profile's weight (the pacer's watts)
    property string namePrefix: ""
    readonly property bool hasPacer: !!pacer
    readonly property bool on: hasPacer && pacer.enabled
    readonly property color tone: "#ff9e40"   // its tint in the 3D view
    spacing: 6
    visible: hasPacer

    Item {
        width: parent.width
        height: 34
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Icon { name: "bolt"; size: 18; color: picker.tone; anchors.verticalCenter: parent.verticalCenter }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Pacer"
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
        }
        ToggleSwitch {
            objectName: picker.namePrefix + "PacerSwitch"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            accessibleName: "Pacer"
            onColor: picker.tone
            enabled: picker.hasPacer && !picker.locked
            checked: picker.on
            onToggled: (on) => picker.pacer.enabled = on
        }
    }
    // how many watts per kilogram it holds
    Item {
        visible: picker.on
        width: parent.width
        height: Theme.touch(42)
        IconButton {
            id: wkgDown
            objectName: picker.namePrefix + "PacerSlower"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.touch(42)
            height: width
            iconName: "minus"
            tooltip: "Pacer 0.1 W/kg slower"
            enabled: picker.on && picker.pacer.wkg > picker.pacer.minWkg
            onClicked: picker.pacer.wkg = picker.pacer.wkg - picker.pacer.stepWkg
        }
        Column {
            anchors.centerIn: parent
            Text {
                objectName: picker.namePrefix + "PacerWkg"
                anchors.horizontalCenter: parent.horizontalCenter
                text: picker.on ? picker.pacer.wkg.toFixed(1) + " W/kg" : ""
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.Bold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: picker.on ? "≈ " + Math.round(picker.pacer.wkg * (picker.riderKg > 0 ? picker.riderKg : 75)) + " W" : ""
                color: Theme.textSecondary
                font.pixelSize: 13
            }
        }
        IconButton {
            objectName: picker.namePrefix + "PacerFaster"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: wkgDown.width
            height: width
            iconName: "plus"
            tooltip: "Pacer 0.1 W/kg faster"
            enabled: picker.on && picker.pacer.wkg < picker.pacer.maxWkg
            onClicked: picker.pacer.wkg = picker.pacer.wkg + picker.pacer.stepWkg
        }
    }
    Text {
        width: parent.width
        text: picker.on ? "Rides " + picker.pacer.wkg.toFixed(1) + " W/kg all the way – slower up the climbs, faster down. "
                          + "Orange and see-through, on the other side of the centre line."
                        : "A rider holding a set W/kg on the route – try to keep up."
        color: Theme.textSecondary
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
}
