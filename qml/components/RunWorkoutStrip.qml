import QtQuick

// Profile of a running workout: one bar per step, height = speed as % of the
// AnT, coloured by running zone (ramps are slanted). Optional progress
// cursor (elapsed) and highlight of the current step.
//   RunWorkoutStrip { segments: Running.workoutSegments; totalSec: Running.workoutTotalSec; elapsed: Running.elapsedSeconds }
Item {
    id: strip
    property var segments: []
    property real totalSec: 0
    property real elapsed: -1          // < 0: no cursor
    property int currentIndex: -1
    property bool showLabels: false
    readonly property real lo: 0.45    // fraction of AnT at the bottom edge
    readonly property real hi: 1.25    // ... at the top edge

    function yOf(f) {
        var t = Math.max(0, Math.min(1, (f - lo) / (hi - lo)))
        return height - 2 - t * (height - 6)
    }
    function xOf(sec) { return totalSec > 0 ? sec / totalSec * width : 0 }

    onSegmentsChanged: canvas.requestPaint()
    onTotalSecChanged: canvas.requestPaint()
    onCurrentIndexChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    Connections {
        target: Theme
        function onDarkChanged() { canvas.requestPaint() }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var segs = strip.segments || []
            // threshold line (100 % AnT)
            ctx.strokeStyle = Theme.faintLine
            ctx.lineWidth = 1
            ctx.setLineDash([4, 4])
            ctx.beginPath()
            ctx.moveTo(0, strip.yOf(1.0))
            ctx.lineTo(width, strip.yOf(1.0))
            ctx.stroke()
            ctx.setLineDash([])
            for (var i = 0; i < segs.length; ++i) {
                var s = segs[i]
                var x0 = strip.xOf(s.start), x1 = strip.xOf(s.start + s.duration)
                var f0 = (s.percent || 0) / 100, f1 = (s.endPercent || 0) / 100
                ctx.globalAlpha = strip.currentIndex < 0 || strip.currentIndex === i ? 0.95 : 0.55
                ctx.fillStyle = Theme.zoneColor(s.zone)
                ctx.beginPath()
                ctx.moveTo(x0 + 0.5, height)
                ctx.lineTo(x0 + 0.5, strip.yOf(f0))
                ctx.lineTo(Math.max(x0 + 1, x1 - 0.5), strip.yOf(f1))
                ctx.lineTo(Math.max(x0 + 1, x1 - 0.5), height)
                ctx.closePath()
                ctx.fill()
                if (strip.currentIndex === i) {
                    ctx.globalAlpha = 1
                    ctx.strokeStyle = Theme.strongLine
                    ctx.lineWidth = 2
                    ctx.stroke()
                }
            }
            ctx.globalAlpha = 1
        }
    }

    // progress cursor
    Rectangle {
        visible: strip.elapsed >= 0 && strip.totalSec > 0
        x: Math.min(strip.width - 2, strip.xOf(strip.elapsed)) - 1
        width: 3
        height: strip.height
        radius: 1.5
        color: Theme.strongLine
    }
    Rectangle {
        visible: strip.elapsed > 0 && strip.totalSec > 0
        width: Math.max(0, Math.min(strip.width, strip.xOf(strip.elapsed)))
        height: strip.height
        color: Theme.dark ? Qt.rgba(0, 0, 0, 0.28) : Qt.rgba(1, 1, 1, 0.35)
    }
}
