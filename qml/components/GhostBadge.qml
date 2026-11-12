import QtQuick

// On the 3D picture of the GPX Ride / Treadmill Run: you against the ghost
// (RouteGhost) or the pacer (RoutePacer, pacer: true) - metres and seconds
// ahead (green) or behind (red). The pacer's (adjustable) also has − / + for
// its W/kg during the ride (full screen too).
//
//   GhostBadge { ghost: Gpx.ghost; active: Gpx.active || finished }
Rectangle {
    id: badge
    property var ghost: null
    property bool active: false                      // riding / paused / finished
    property bool pacer: false                       // the pacer (orange): ghost = the RoutePacer
    property bool adjustable: false                  // pacer: − / + W/kg
    property string namePrefix: ""
    readonly property bool showWkg: pacer && adjustable && hasGhost
    readonly property bool hasGhost: !!ghost && ghost.available
    readonly property real gap: hasGhost ? ghost.gapM : 0
    readonly property bool ahead: gap >= 0
    readonly property string who: pacer ? "pacer" : "ghost"
    readonly property color iconTone: pacer ? "#ff9e40" : "#c8d6ff"
    readonly property color tone: !active || Math.abs(gap) < 1 ? "#c8d6ff" : ahead ? "#2fd35a" : "#ff8a7a"

    function metres(m) {
        const a = Math.abs(m)
        return a >= 1000 ? (a / 1000).toFixed(2) + " km" : Math.round(a) + " m"
    }
    function seconds(s) {
        const a = Math.round(Math.abs(s))
        return a >= 60 ? Theme.clock(a) : a + " s"
    }

    visible: hasGhost
    width: row.implicitWidth + (showWkg ? 12 : 28)
    height: showWkg ? Theme.touch(44) : 38
    radius: height / 2
    color: Qt.rgba(0, 0, 0, 0.55)
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.7)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 9
        Icon {
            name: badge.pacer ? "bolt" : "ghost"
            size: 20
            color: badge.iconTone
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: {
                if (!badge.hasGhost)
                    return ""
                if (!badge.active)
                    return badge.pacer ? badge.ghost.ghostLabel : "Ghost  " + badge.ghost.ghostLabel
                if (Math.abs(badge.gap) < 1)
                    return "Level with the " + badge.who
                var t = (badge.pacer ? "Pacer  " : "") + badge.metres(badge.gap) + (badge.ahead ? " ahead" : " behind")
                if (badge.ghost.hasGapSec && Math.abs(badge.ghost.gapSec) >= 1)
                    t += "  ·  " + (badge.ghost.gapSec >= 0 ? "+" : "−") + badge.seconds(badge.ghost.gapSec)
                if (badge.ghost.ghostDone)
                    t += badge.pacer ? "  ·  pacer finished" : "  ·  ghost done"
                return t
            }
            color: "white"
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        // the pacer's W/kg: − value +
        Rectangle {
            visible: badge.showWkg
            width: 1
            height: badge.height - 16
            color: Qt.rgba(1, 1, 1, 0.25)
            anchors.verticalCenter: parent.verticalCenter
        }
        IconButton {
            visible: badge.showWkg
            objectName: badge.namePrefix + "PacerBadgeSlower"
            width: badge.height - 6
            height: width
            anchors.verticalCenter: parent.verticalCenter
            iconName: "minus"
            tooltip: "Pacer 0.1 W/kg slower"
            enabled: badge.showWkg && badge.ghost.wkg > badge.ghost.minWkg
            onClicked: badge.ghost.wkg = badge.ghost.wkg - badge.ghost.stepWkg
        }
        Text {
            visible: badge.showWkg
            objectName: badge.namePrefix + "PacerBadgeWkg"
            anchors.verticalCenter: parent.verticalCenter
            text: badge.showWkg ? badge.ghost.wkg.toFixed(1) + " W/kg" : ""
            color: badge.iconTone
            font.pixelSize: 15
            font.weight: Font.Bold
        }
        IconButton {
            visible: badge.showWkg
            objectName: badge.namePrefix + "PacerBadgeFaster"
            width: badge.height - 6
            height: width
            anchors.verticalCenter: parent.verticalCenter
            iconName: "plus"
            tooltip: "Pacer 0.1 W/kg faster"
            enabled: badge.showWkg && badge.ghost.wkg < badge.ghost.maxWkg
            onClicked: badge.ghost.wkg = badge.ghost.wkg + badge.ghost.stepWkg
        }
    }
}
