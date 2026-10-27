import QtQuick
import Artilody.Backend

// Compact workout block chart (Video Ride page: the workout ridden during the
// video). Blocks are scaled by % FTP (× the intensity offset) and coloured by
// the rider's power zone; ramps are drawn as slopes. Finished blocks are
// faded, the current one is outlined and a cursor marks the elapsed time.
//   intervals: [{startSec, durationSec, watts, endWatts, percent, ramp, name}]
//              (watts at the rider's FTP without the intensity offset)
Item {
    id: strip
    property var intervals: []
    property int totalSec: 0
    property real elapsed: 0
    property int currentIndex: -1
    property int ftp: Profiles.ftp
    property int intensity: 0                  // % offset (Rlv.intensityPercent)
    property var zoneBounds: Profiles.zoneBounds
    property bool finished: false
    property real fontSize: 12

    readonly property real factor: (100 + intensity) / 100
    readonly property real padL: 44
    readonly property real padR: 8
    readonly property real padT: 4
    readonly property real padB: 18
    readonly property real plotW: Math.max(10, width - padL - padR)
    readonly property real plotH: Math.max(10, height - padT - padB)
    readonly property real fracMax: {
        var m = 1.2
        for (var i = 0; i < intervals.length; ++i)
            m = Math.max(m, fracOf(intervals[i].watts) * factor * 1.08, fracOf(intervals[i].endWatts) * factor * 1.08)
        return m
    }

    function fracOf(w) { return ftp > 0 ? w / ftp : 0 }
    function px(sec) { return padL + plotW * Math.max(0, Math.min(1, sec / Math.max(1, totalSec))) }
    function py(frac) { return padT + plotH * (1 - Math.max(0, Math.min(1, frac / fracMax))) }
    function zoneFor(frac) {
        for (var i = 0; i < zoneBounds.length; ++i)
            if (frac < zoneBounds[i])
                return i
        return 5
    }
    function css(c) {
        return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
    }
    function clockText(s) {
        s = Math.round(s)
        var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (ss < 10 ? "0" : "") + ss
    }

    // the cursor glides between the 1 s workout ticks
    readonly property real elapsedTarget: Math.max(0, Math.min(totalSec, elapsed))
    property real shownElapsed: 0
    onElapsedTargetChanged: {
        glide.stop()
        if (elapsedTarget < shownElapsed || elapsedTarget - shownElapsed > 3)
            shownElapsed = elapsedTarget
        else if (elapsedTarget !== shownElapsed) {
            glide.from = shownElapsed
            glide.to = elapsedTarget
            glide.start()
        }
    }
    Component.onCompleted: shownElapsed = elapsedTarget
    NumberAnimation { id: glide; target: strip; property: "shownElapsed"; duration: 950; easing.type: Easing.Linear }

    onIntervalsChanged: canvas.requestPaint()
    onTotalSecChanged: canvas.requestPaint()
    onCurrentIndexChanged: canvas.requestPaint()
    onFactorChanged: canvas.requestPaint()
    onFtpChanged: canvas.requestPaint()
    onZoneBoundsChanged: canvas.requestPaint()
    onFinishedChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onVisibleChanged: if (visible) canvas.requestPaint()
    Connections {
        target: Theme
        function onDarkChanged() { canvas.requestPaint() }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true
        renderStrategy: Canvas.Cooperative
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var L = strip.padL, W = strip.plotW, B = strip.padT + strip.plotH
            ctx.font = strip.fontSize + "px sans-serif"
            // % FTP grid (50 % steps) with the FTP line stronger
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (var f = 0.5; f < strip.fracMax; f += 0.5) {
                var y = strip.py(f)
                ctx.strokeStyle = strip.css(Math.abs(f - 1) < 0.01 ? Theme.softLine : Theme.faintLine)
                ctx.setLineDash([4, 4])
                ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + W, y); ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = strip.css(Theme.axisText)
                ctx.fillText(Math.abs(f - 1) < 0.01 ? "FTP" : Math.round(f * 100) + "%", L - 6, y)
            }
            // blocks
            for (var i = 0; i < strip.intervals.length; ++i) {
                var iv = strip.intervals[i]
                var f0 = strip.fracOf(iv.watts) * strip.factor
                var f1 = strip.fracOf(iv.ramp ? iv.endWatts : iv.watts) * strip.factor
                var x0 = strip.px(iv.startSec) + 0.5, x1 = Math.max(x0 + 1, strip.px(iv.startSec + iv.durationSec) - 0.5)
                var done = strip.finished || (iv.startSec + iv.durationSec) <= strip.elapsed
                var current = i === strip.currentIndex && !strip.finished
                ctx.globalAlpha = current ? 1 : (done ? 0.38 : 0.82)
                ctx.beginPath()
                ctx.moveTo(x0, B)
                ctx.lineTo(x0, strip.py(f0))
                ctx.lineTo(x1, strip.py(f1))
                ctx.lineTo(x1, B)
                ctx.closePath()
                if (iv.ramp) {
                    var g = ctx.createLinearGradient(x0, 0, x1, 0)
                    g.addColorStop(0, Theme.zoneColor(strip.zoneFor(f0)))
                    g.addColorStop(1, Theme.zoneColor(strip.zoneFor(f1)))
                    ctx.fillStyle = g
                } else {
                    ctx.fillStyle = Theme.zoneColor(strip.zoneFor((f0 + f1) / 2))
                }
                ctx.fill()
                if (current) {
                    ctx.strokeStyle = strip.css(Theme.strongLine)
                    ctx.lineWidth = 2
                    ctx.stroke()
                    ctx.lineWidth = 1
                }
            }
            ctx.globalAlpha = 1
            // base line + time labels
            ctx.strokeStyle = strip.css(Theme.faintLine)
            ctx.beginPath(); ctx.moveTo(L, B + 0.5); ctx.lineTo(L + W, B + 0.5); ctx.stroke()
            var opts = [60, 120, 300, 600, 900, 1200, 1800, 3600]
            var ts = 3600
            for (var o = 0; o < opts.length; ++o)
                if (strip.totalSec / opts[o] <= Math.max(3, W / 90)) { ts = opts[o]; break }
            ctx.fillStyle = strip.css(Theme.axisText)
            ctx.textBaseline = "top"
            for (var t = 0; t <= strip.totalSec; t += ts) {
                ctx.textAlign = t === 0 ? "left" : (strip.totalSec - t < ts / 2 ? "right" : "center")
                ctx.fillText(strip.clockText(t), strip.px(t), B + 3)
            }
        }
    }

    // block labels (wide blocks only)
    Repeater {
        model: strip.intervals
        Text {
            readonly property real bw: strip.px(modelData.startSec + modelData.durationSec) - strip.px(modelData.startSec)
            // inside the block: below its lower edge (ramps)
            readonly property real topY: strip.py((modelData.ramp ? Math.min(strip.fracOf(modelData.watts), strip.fracOf(modelData.endWatts))
                                                                   : strip.fracOf(modelData.watts)) * strip.factor)
            visible: bw > implicitWidth + 8 && strip.padT + strip.plotH - topY > implicitHeight + 4
            x: strip.px(modelData.startSec) + (bw - implicitWidth) / 2
            y: topY + 2
            text: modelData.ramp ? Math.round(modelData.watts * strip.factor) + "–" + Math.round(modelData.endWatts * strip.factor)
                                 : Math.round(modelData.watts * strip.factor) + " W"
            color: Theme.textOnColor
            font.pixelSize: strip.fontSize
            font.weight: Font.Bold
        }
    }

    // elapsed cursor
    Rectangle {
        visible: strip.totalSec > 0 && (strip.shownElapsed > 0 || strip.currentIndex >= 0)
        x: strip.px(strip.shownElapsed) - 1
        y: strip.padT
        width: 2
        height: strip.plotH
        color: Theme.strongLine
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -3
            width: 8; height: 8; radius: 4
            color: Theme.strongLine
        }
    }
}
