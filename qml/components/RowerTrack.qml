import QtQuick

// Top view of a rowing course: distance ticks, a finish flag and one lane
// ("YOU") or two lanes (YOU + translucent GHOST, see GhostRaceTrack.qml).
// Boats move from the start to the finish line by metres rowed: the bow tip
// sits exactly on the start line at 0 m and on the finish line at the target.
Item {
    id: track
    property real totalMeters: 2000
    property real youMeters: 0
    property int strokeCount: 0
    property int strokeRate: 24
    property bool showGhost: false
    property bool ghostAvailable: true
    property real ghostMeters: 0
    property bool ghostRowing: false          // animate the ghost's oars
    property real gapMeters: 0                // > 0: you are ahead
    property string emptyGhostText: "No record yet – this row becomes your ghost"
    property string finishLabel: Theme.thousands(totalMeters) + " m"

    readonly property real laneTop: 64
    readonly property real laneBottom: height - 10
    readonly property int laneCount: showGhost ? 2 : 1
    readonly property real laneH: (laneBottom - laneTop) / laneCount
    readonly property real boatLength: Math.max(46, Math.min(96, laneH * 0.82))
    readonly property real pillRight: 12 + 58
    // start line: the whole boat waits behind it, right of the name pill
    readonly property real x0: pillRight + 10 + boatLength
    readonly property real x1: width - 64

    function progressFor(m) {
        return Math.max(0, Math.min(1, m / Math.max(1, totalMeters)))
    }
    function xFor(m) {
        return x0 + (x1 - x0) * progressFor(m)
    }

    // --- smoothed positions ------------------------------------------------------
    // Metrics arrive every 250 ms; the shown distances glide linearly between
    // updates and snap (no backwards slide) on a reset or a new set-up.
    readonly property int glideMs: 280
    readonly property real youTarget: Math.max(0, Math.min(totalMeters, youMeters))
    readonly property real ghostTarget: Math.max(0, Math.min(totalMeters, ghostMeters))
    property real youShown: 0
    property real ghostShown: 0
    readonly property real youTipX: xFor(youShown)
    readonly property real ghostTipX: xFor(ghostShown)

    function glide(anim, prop, value) {
        anim.stop()
        var cur = track[prop]
        if (value < cur || value - cur > totalMeters * 0.2)
            track[prop] = value
        else if (value !== cur) {
            anim.from = cur
            anim.to = value
            anim.start()
        }
    }
    function snap() {
        youGlide.stop()
        ghostGlide.stop()
        youShown = youTarget
        ghostShown = ghostTarget
    }
    onYouTargetChanged: glide(youGlide, "youShown", youTarget)
    onGhostTargetChanged: glide(ghostGlide, "ghostShown", ghostTarget)
    onTotalMetersChanged: snap()
    Component.onCompleted: snap()
    NumberAnimation { id: youGlide; target: track; property: "youShown"; duration: track.glideMs; easing.type: Easing.Linear }
    NumberAnimation { id: ghostGlide; target: track; property: "ghostShown"; duration: track.glideMs; easing.type: Easing.Linear }
    // QML color -> CSS string for the Canvas (keeps the alpha channel)
    function css(c) {
        return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
    }
    Connections {
        target: Theme
        function onDarkChanged() {
            water.requestPaint()
            flag.requestPaint()
            separator.requestPaint()
        }
    }
    function laneCenter(i) {
        return laneTop + laneH * (i + 0.5)
    }
    readonly property real tickStep: {
        var t = totalMeters
        var opts = [100, 250, 500, 1000, 2000, 2500, 5000]
        for (var i = 0; i < opts.length; ++i)
            if (t / opts[i] <= 6)
                return opts[i]
        return 10000
    }
    readonly property var ticks: {
        var out = []
        for (var m = 0; m < totalMeters - tickStep * 0.35; m += tickStep)
            out.push(m)
        return out
    }

    // water
    Rectangle {
        anchors.fill: parent
        radius: 12
        color: Theme.dark ? Qt.rgba(0.05, 0.12, 0.42, 0.55) : Qt.rgba(0.3, 0.55, 0.95, 0.12)
        border.color: Theme.cardBorder
        Canvas {
            id: water
            anchors.fill: parent
            anchors.margins: 4
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = Theme.dark ? "rgba(120,170,255,0.10)" : "rgba(40,90,200,0.10)"
                ctx.lineWidth = 1.2
                for (var y = 70; y < height; y += 17) {
                    ctx.beginPath()
                    var off = (y * 7) % 40
                    for (var x = 0; x <= width; x += 8) {
                        var yy = y + Math.sin((x + off) / 26) * 2.2
                        if (x === 0) ctx.moveTo(x, yy); else ctx.lineTo(x, yy)
                    }
                    ctx.stroke()
                }
            }
        }
    }

    // distance ticks
    Repeater {
        model: track.ticks
        Item {
            x: track.xFor(modelData)
            Text {
                text: Theme.thousands(modelData) + " m"
                color: Theme.text
                font.pixelSize: 14
                x: -width / 2
                y: 14
            }
            Rectangle { x: -4; y: 40; width: 8; height: 8; radius: 4; color: Theme.textSecondary }
            Column {
                x: -0.5
                y: 52
                spacing: 5
                Repeater {
                    model: Math.max(0, Math.floor((track.height - 62) / 11))
                    Rectangle { width: 1; height: 6; color: Theme.faintLine }
                }
            }
        }
    }

    // start line
    Column {
        x: track.x0 - 1
        y: 40
        spacing: 4
        Repeater {
            model: Math.max(0, Math.floor((track.height - 50) / 12))
            Rectangle { width: 2.5; height: 8; color: Theme.strongLine }
        }
    }

    // finish line + flag
    Item {
        x: track.x1
        Text {
            text: track.finishLabel
            color: Theme.text
            font.pixelSize: 14
            font.weight: Font.Bold
            x: -width - 14
            y: 14
        }
        Canvas {
            id: flag
            x: 6; y: 8; width: 28; height: 26
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var pole = track.css(Theme.strongLine)
                var other = Theme.dark ? "#111a40" : "#ffffff"
                ctx.fillStyle = pole
                ctx.fillRect(0, 0, 2.5, height)
                var s = 5
                for (var r = 0; r < 4; ++r)
                    for (var c = 0; c < 5; ++c) {
                        ctx.fillStyle = (r + c) % 2 === 0 ? pole : other
                        ctx.fillRect(2.5 + c * s, 1 + r * s, s, s)
                    }
            }
        }
        Column {
            x: -1
            y: 40
            spacing: 4
            Repeater {
                model: Math.max(0, Math.floor((track.height - 50) / 12))
                Rectangle { width: 2.5; height: 8; color: Theme.strongLine }
            }
        }
    }

    // lane separator
    Canvas {
        id: separator
        visible: track.showGhost
        x: track.x0
        y: track.laneTop + track.laneH - 1
        width: track.x1 - track.x0
        height: 2
        onWidthChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = track.css(Theme.softLine)
            ctx.setLineDash([6, 5])
            ctx.beginPath(); ctx.moveTo(0, 1); ctx.lineTo(width, 1); ctx.stroke()
        }
    }

    // --- lanes -----------------------------------------------------------------
    Repeater {
        model: track.laneCount
        Item {
            id: lane
            readonly property bool ghost: index === 1
            readonly property color tint: ghost ? Theme.ghost : Theme.accentLight
            readonly property bool hasBoat: !ghost || track.ghostAvailable
            readonly property real tipX: ghost ? track.ghostTipX : track.youTipX
            readonly property real boatX: tipX - boat.tipOffset
            y: track.laneCenter(index)
            width: track.width

            // name pill
            Rectangle {
                x: 12
                y: -height / 2
                width: 58
                height: 28
                radius: 9
                color: lane.ghost ? Theme.ghost : Theme.cyan
                Text {
                    anchors.centerIn: parent
                    text: lane.ghost ? "GHOST" : "YOU"
                    color: Theme.textOnColor
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
            }

            // wake / trail
            Rectangle {
                visible: lane.hasBoat && lane.tipX > track.x0 + 1
                x: track.x0 - boat.tipOffset
                y: -8
                width: Math.max(0, lane.boatX + boat.tipOffset * 0.3 - x)
                height: 16
                radius: 8
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: Qt.rgba(lane.tint.r, lane.tint.g, lane.tint.b, lane.ghost ? 0.45 : 0.6) }
                }
            }

            RowingBoat {
                id: boat
                visible: lane.hasBoat
                x: lane.boatX
                y: -height / 2
                boatLength: track.boatLength
                opacity: lane.ghost ? 0.78 : 1
                hullColor: lane.ghost ? Theme.ghost : (Theme.dark ? "white" : "#1f3d9a")
                deckColor: lane.ghost ? "#5a1050" : (Theme.dark ? "#16214f" : "#dfe6f8")
                oarColor: lane.ghost ? (Theme.dark ? "#ffb3ea" : "#a8237f") : (Theme.dark ? "white" : "#16214f")
                bladeColor: lane.ghost ? Theme.ghost : Theme.cyan
                strokeRate: track.strokeRate
                strokeCount: lane.ghost ? ghostStrokes.count : track.strokeCount
            }

            Text {
                visible: lane.hasBoat
                x: Math.min(lane.tipX + 10, track.width - width - 8)
                y: -height / 2
                text: Theme.thousands(Math.floor(lane.ghost ? track.ghostTarget : track.youTarget)) + " m"
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.Bold
                style: Text.Outline
                styleColor: Theme.dark ? Qt.rgba(0.05, 0.08, 0.3, 0.6) : Qt.rgba(1, 1, 1, 0.8)
            }

            Text {
                visible: !lane.hasBoat
                x: track.pillRight + 14
                y: -height / 2
                text: track.emptyGhostText
                color: Theme.textSecondary
                font.pixelSize: 16
                font.italic: true
            }
        }
    }

    // ghost oar timing (presentation only)
    Timer {
        id: ghostStrokes
        property int count: 0
        interval: 60000 / Math.max(16, track.strokeRate > 0 ? track.strokeRate : 24)
        repeat: true
        running: track.showGhost && track.ghostRowing && track.ghostAvailable
        onTriggered: ++count
    }

    // gap marker between the boats
    Rectangle {
        visible: track.showGhost && track.ghostAvailable && (track.youShown > 0 || track.ghostShown > 0)
        readonly property real cx: Math.max(track.youTipX, track.ghostTipX)
        x: Math.min(track.width - width - 6, cx + 8)
        y: track.laneTop + track.laneH - height / 2
        width: gapText.implicitWidth + 20
        height: 28
        radius: 8
        color: track.gapMeters >= 0 ? Theme.success : Theme.danger
        Text {
            id: gapText
            anchors.centerIn: parent
            text: (track.gapMeters >= 0 ? "+" : "−") + Theme.thousands(Math.floor(Math.abs(track.gapMeters))) + " m"
            color: Theme.textOnColor
            font.pixelSize: 14
            font.weight: Font.Bold
        }
    }
}
