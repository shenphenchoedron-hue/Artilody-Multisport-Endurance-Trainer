import QtQuick

// Live chart of the Treadmill Run: belt speed (filled), the workout target
// (dashed) and the heart rate (pink, own scale on the right).
Item {
    id: chart
    property var speed: []
    property var target: []
    property var heartRate: []
    property int step: 5
    property real antSpeed: 12

    readonly property real vMax: {
        var m = antSpeed * 1.2
        for (var i = 0; i < speed.length; ++i) m = Math.max(m, speed[i] + 1)
        for (var j = 0; j < target.length; ++j) m = Math.max(m, target[j] + 1)
        return Math.ceil(m / 2) * 2
    }
    readonly property real hrMin: 60
    readonly property real hrMax: 200
    readonly property real leftPad: 34
    readonly property real rightPad: 34
    readonly property real plotW: Math.max(10, width - leftPad - rightPad)
    readonly property real plotH: Math.max(10, height - 22)
    readonly property int count: Math.max(speed.length, 1)

    function xOf(i) { return leftPad + (count <= 1 ? 0 : i / (count - 1) * plotW) }
    function yV(v) { return 4 + plotH - 4 - Math.max(0, Math.min(1, v / vMax)) * (plotH - 8) }
    function yHr(h) { return 4 + plotH - 4 - Math.max(0, Math.min(1, (h - hrMin) / (hrMax - hrMin))) * (plotH - 8) }

    onSpeedChanged: canvas.requestPaint()
    onTargetChanged: canvas.requestPaint()
    onHeartRateChanged: canvas.requestPaint()
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
            ctx.font = "11px sans-serif"
            // grid + speed axis
            ctx.strokeStyle = Theme.grid
            ctx.fillStyle = Theme.axisText
            ctx.lineWidth = 1
            var stepV = chart.vMax > 16 ? 4 : 2
            for (var v = 0; v <= chart.vMax + 0.01; v += stepV) {
                var y = chart.yV(v)
                ctx.globalAlpha = 0.5
                ctx.beginPath(); ctx.moveTo(chart.leftPad, y); ctx.lineTo(chart.leftPad + chart.plotW, y); ctx.stroke()
                ctx.globalAlpha = 1
                ctx.fillText(v.toFixed(0), 4, y + 4)
            }
            ctx.fillStyle = Theme.heartRate
            for (var h = 80; h <= 200; h += 40)
                ctx.fillText("" + h, chart.leftPad + chart.plotW + 6, chart.yHr(h) + 4)
            var n = chart.speed.length
            if (n < 2)
                return
            // speed area
            ctx.beginPath()
            ctx.moveTo(chart.xOf(0), chart.yV(0))
            for (var i = 0; i < n; ++i)
                ctx.lineTo(chart.xOf(i), chart.yV(chart.speed[i]))
            ctx.lineTo(chart.xOf(n - 1), chart.yV(0))
            ctx.closePath()
            ctx.fillStyle = Qt.rgba(1.0, 0.54, 0.15, 0.25)
            ctx.fill()
            ctx.beginPath()
            for (var k = 0; k < n; ++k) {
                if (k === 0) ctx.moveTo(chart.xOf(k), chart.yV(chart.speed[k]))
                else ctx.lineTo(chart.xOf(k), chart.yV(chart.speed[k]))
            }
            ctx.strokeStyle = Theme.runningColor
            ctx.lineWidth = 2
            ctx.stroke()
            // target
            var anyTarget = false
            for (var t = 0; t < chart.target.length; ++t) if (chart.target[t] > 0) { anyTarget = true; break }
            if (anyTarget) {
                ctx.setLineDash([5, 4])
                ctx.strokeStyle = Theme.strongLine
                ctx.lineWidth = 1.5
                ctx.beginPath()
                var started = false
                for (var m = 0; m < chart.target.length && m < n; ++m) {
                    var tv = chart.target[m]
                    if (tv <= 0) { started = false; continue }
                    if (!started) { ctx.moveTo(chart.xOf(m), chart.yV(tv)); started = true }
                    else ctx.lineTo(chart.xOf(m), chart.yV(tv))
                }
                ctx.stroke()
                ctx.setLineDash([])
            }
            // heart rate
            ctx.beginPath()
            var on = false
            for (var q = 0; q < chart.heartRate.length && q < n; ++q) {
                var hv = chart.heartRate[q]
                if (hv <= 0) { on = false; continue }
                if (!on) { ctx.moveTo(chart.xOf(q), chart.yHr(hv)); on = true }
                else ctx.lineTo(chart.xOf(q), chart.yHr(hv))
            }
            ctx.strokeStyle = Theme.heartRate
            ctx.lineWidth = 1.6
            ctx.stroke()
        }
    }
    Text {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: chart.leftPad
        text: "km/h"
        color: Theme.axisText
        font.pixelSize: 11
    }
    Text {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        text: "bpm"
        color: Theme.heartRate
        font.pixelSize: 11
    }
}
