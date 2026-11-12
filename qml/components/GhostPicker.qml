import QtQuick
import QtQuick.Controls

// The ghost of the GPX Ride / Treadmill Run (RouteGhost): which earlier ride
// of yours on this route to race - none, your best, your latest or one of
// them. Chosen before the start.
//
//   GhostPicker { ghost: Gpx.ghost; locked: Gpx.active; rideWord: "ride" }
Column {
    id: picker
    property var ghost: null
    property bool locked: false
    property string rideWord: "ride"      // "ride" / "run"
    property string namePrefix: ""
    readonly property bool hasGhost: !!ghost
    spacing: 6
    visible: hasGhost

    Item {
        width: parent.width
        height: 26
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Icon { name: "ghost"; size: 18; color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Ghost"
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
        }
    }
    AppComboBox {
        objectName: picker.namePrefix + "GhostChoice"
        width: parent.width
        implicitHeight: Theme.touch(42)
        fontSize: 15
        enabled: picker.hasGhost && !picker.locked && picker.ghost.rideCount > 0
        model: picker.hasGhost ? picker.ghost.choices : []
        textRole: "label"
        currentIndex: picker.hasGhost ? picker.ghost.choiceIndex : 0
        onActivated: (i) => {
            if (picker.hasGhost && i >= 0 && i < picker.ghost.choices.length)
                picker.ghost.choice = picker.ghost.choices[i].value
            currentIndex = Qt.binding(() => picker.hasGhost ? picker.ghost.choiceIndex : 0)
        }
        Accessible.name: "Ghost"
    }
    Text {
        width: parent.width
        text: !picker.hasGhost ? ""
              : picker.ghost.rideCount === 0
                ? "Race an earlier " + picker.rideWord + " of yours: once you have saved a " + picker.rideWord
                  + " on this route from this start, its ghost rides beside you."
              : picker.ghost.available ? "Ghost: " + picker.ghost.ghostLabel
                                         + ". It rides beside you, see-through, in its own lane."
              : "No ghost on this " + picker.rideWord + "."
        color: Theme.textSecondary
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
}
