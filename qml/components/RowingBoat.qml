import QtQuick

// Stylised single scull seen from above (bow pointing right) with a rower
// and two oars. Every change of strokeCount plays one oar stroke
// (drive + recovery) timed by the stroke rate.
Item {
    id: boat
    property color hullColor: "white"
    property color deckColor: "#16214f"
    property color oarColor: "white"
    property color bladeColor: Theme.cyan
    property int strokeCount: 0
    property int strokeRate: 24          // strokes per minute (animation timing)
    property bool animated: true
    property real boatLength: 116
    // -1 = finish (blades toward the stern), +1 = catch (blades toward the bow)
    property real sweep: 0.15

    readonly property real hullHeight: boatLength * 0.15
    readonly property real oarLength: boatLength * 0.46
    readonly property real maxAngle: 38

    implicitWidth: boatLength
    implicitHeight: oarLength * 2 + 8
    width: implicitWidth
    height: implicitHeight

    onStrokeCountChanged: {
        if (!animated)
            return
        stroke.stop()
        stroke.start()
    }

    SequentialAnimation {
        id: stroke
        readonly property int period: Math.max(1200, Math.min(4000, 60000 / Math.max(10, boat.strokeRate)))
        NumberAnimation { target: boat; property: "sweep"; to: 1; duration: stroke.period * 0.12; easing.type: Easing.OutQuad }
        NumberAnimation { target: boat; property: "sweep"; to: -1; duration: stroke.period * 0.33; easing.type: Easing.InOutQuad }
        NumberAnimation { target: boat; property: "sweep"; to: 0.6; duration: stroke.period * 0.45; easing.type: Easing.InOutSine }
    }

    // --- oars (drawn below the hull) -------------------------------------------
    Repeater {
        model: [-1, 1]   // -1: top (port), 1: bottom (starboard)
        Item {
            id: oar
            readonly property int side: modelData
            x: boat.width * 0.47
            y: boat.height / 2 + side * boat.hullHeight * 0.42
            rotation: -side * boat.sweep * boat.maxAngle
            // shaft from the rigger outwards (plus a short handle inboard)
            Rectangle {
                width: 2.6
                height: boat.oarLength + boat.hullHeight * 0.4
                x: -width / 2
                y: oar.side < 0 ? -boat.oarLength : -boat.hullHeight * 0.4
                radius: 1.3
                color: boat.oarColor
                antialiasing: true
            }
            // blade
            Rectangle {
                width: 7
                height: boat.oarLength * 0.26
                radius: 3
                x: -width / 2
                y: oar.side < 0 ? -boat.oarLength - 2 : boat.oarLength - height + 2
                color: boat.bladeColor
                antialiasing: true
            }
        }
    }

    // --- hull ---------------------------------------------------------------
    Canvas {
        id: hull
        width: boat.boatLength
        height: boat.hullHeight
        anchors.verticalCenter: parent.verticalCenter
        antialiasing: true
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height, cy = h / 2
            ctx.beginPath()
            ctx.moveTo(0, cy)
            ctx.quadraticCurveTo(w * 0.22, 0, w * 0.5, 0)
            ctx.quadraticCurveTo(w * 0.82, 0, w, cy)
            ctx.quadraticCurveTo(w * 0.82, h, w * 0.5, h)
            ctx.quadraticCurveTo(w * 0.22, h, 0, cy)
            ctx.closePath()
            ctx.fillStyle = boat.hullColor
            ctx.fill()
            // cockpit
            ctx.beginPath()
            var cx0 = w * 0.36, cx1 = w * 0.58
            ctx.moveTo(cx0, cy)
            ctx.quadraticCurveTo(cx0, h * 0.2, (cx0 + cx1) / 2, h * 0.2)
            ctx.quadraticCurveTo(cx1, h * 0.2, cx1, cy)
            ctx.quadraticCurveTo(cx1, h * 0.8, (cx0 + cx1) / 2, h * 0.8)
            ctx.quadraticCurveTo(cx0, h * 0.8, cx0, cy)
            ctx.closePath()
            ctx.fillStyle = boat.deckColor
            ctx.fill()
        }
        onWidthChanged: requestPaint()
        Connections {
            target: boat
            function onHullColorChanged() { hull.requestPaint() }
            function onDeckColorChanged() { hull.requestPaint() }
        }
    }

    // rower: shoulders + head, sliding with the stroke
    Item {
        x: boat.width * 0.47 - boat.sweep * boat.boatLength * 0.035
        y: boat.height / 2
        Rectangle {
            width: boat.hullHeight * 0.55
            height: boat.hullHeight * 1.05
            radius: width / 2
            x: -width * 0.9
            y: -height / 2
            color: boat.oarColor
            border.color: boat.deckColor
            border.width: 1
            antialiasing: true
        }
        Rectangle {
            width: boat.hullHeight * 0.5
            height: width
            radius: width / 2
            x: -width * 0.25
            y: -height / 2
            color: boat.bladeColor
            border.color: boat.deckColor
            border.width: 1
            antialiasing: true
        }
    }
}
