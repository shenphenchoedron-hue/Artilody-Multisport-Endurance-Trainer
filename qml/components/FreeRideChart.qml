import QtQuick

// Power (area), virtual speed and heart rate (lines) of the free ride over
// time. One value per seriesStep seconds (FreeRide.*Series).
Item {
    id: chart
    property var powerSeries: []
    property var speedSeries: []
    property var hrSeries: []
    property int seriesStep: 1
    property real xMax: 600             // seconds
    property real cursor: -1            // seconds, -1 = none
    property int targetWatts: 0         // ERG target line (0 = none)
    property string emptyText: "Start riding to see power, speed and heart rate"

    readonly property real padL: 62
    readonly property real padR: 128
    readonly property real padT: 30
    readonly property real padB: 46
    readonly property real plotW: Math.max(10, width - padL - padR)
    readonly property real plotH: Math.max(10, height - padT - padB)
    readonly property int count: powerSeries ? powerSeries.length : 0

    function niceStep(range, ticks) {
        var raw = range / Math.max(1, ticks)
        var mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10))
        var n = raw / mag
        return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10) * mag
    }
    function bucketX(i) { return (i + 1) * seriesStep }
    readonly property real powerMax: {
        var m = Math.max(200, targetWatts * 1.2)
        for (var i = 0; i < count; ++i)
            m = Math.max(m, Number(powerSeries[i]) * 1.15)
        var st = niceStep(m, 5)
        return Math.ceil(m / st) * st
    }
    readonly property real speedMax: {
        var m = 40
        for (var i = 0; i < speedSeries.length; ++i)
            m = Math.max(m, Number(speedSeries[i]) * 1.15)
        return Math.ceil(m / 10) * 10
    }
    readonly property var hrRange: {
        var lo = 80, hi = 200
        for (var i = 0; i < hrSeries.length; ++i) {
            var h = Number(hrSeries[i])
            if (h <= 0)
                continue
            lo = Math.min(lo, Math.floor((h - 10) / 20) * 20)
            hi = Math.max(hi, Math.ceil((h + 10) / 20) * 20)
        }
        return [lo, hi]
    }

    function px(v) { return padL + plotW * Math.max(0, Math.min(1, v / Math.max(1, xMax))) }
    function pyPower(w) { return padT + plotH * (1 - Math.max(0, Math.min(1, w / powerMax))) }
    function pySpeed(s) { return padT + plotH * (1 - Math.max(0, Math.min(1, s / speedMax))) }
    function pyHr(h) {
        var r = hrRange
        return padT + plotH * (1 - Math.max(0, Math.min(1, (h - r[0]) / (r[1] - r[0]))))
    }
    function clockText(s) {
        s = Math.round(s)
        var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60
        if (h > 0)
            return h + ":" + (m < 10 ? "0" : "") + m + ":" + (ss < 10 ? "0" : "") + ss
        return m + ":" + (ss < 10 ? "0" : "") + ss
    }
    function css(c) {
        return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
    }

    onPowerSeriesChanged: canvas.requestPaint()
    onXMaxChanged: canvas.requestPaint()
    onCursorChanged: canvas.requestPaint()
    onTargetWattsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

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
            var L = chart.padL, T = chart.padT, W = chart.plotW, H = chart.plotH
            var grid = chart.css(Theme.grid)
            var label = Theme.dark ? "rgba(255,255,255,0.85)" : "rgba(22,33,79,0.85)"
            ctx.font = "14px sans-serif"
            ctx.lineWidth = 1

            // grid + power axis (left)
            var pst = chart.niceStep(chart.powerMax, 5)
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (var w = 0; w <= chart.powerMax + 0.1; w += pst) {
                var y = chart.pyPower(w)
                ctx.strokeStyle = grid
                ctx.setLineDash([4, 4])
                ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + W, y); ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = label
                ctx.fillText(Math.round(w), L - 10, y)
            }
            // time grid
            var opts = [30, 60, 120, 300, 600, 900, 1200, 1800, 3600, 7200]
            var xst = opts[opts.length - 1]
            for (var oi = 0; oi < opts.length; ++oi)
                if (chart.xMax / opts[oi] <= 10) { xst = opts[oi]; break }
            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            for (var xv = 0; xv <= chart.xMax + 0.1; xv += xst) {
                var x = chart.px(xv)
                ctx.strokeStyle = grid
                ctx.setLineDash([4, 4])
                ctx.beginPath(); ctx.moveTo(x, T); ctx.lineTo(x, T + H); ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = label
                ctx.fillText(chart.clockText(xv), x, T + H + 10)
            }
            ctx.strokeStyle = Theme.warning
            ctx.beginPath(); ctx.moveTo(L, T); ctx.lineTo(L, T + H); ctx.lineTo(L + W, T + H); ctx.stroke()

            // speed axis (right 1)
            var ax1 = L + W + 8
            var sst = chart.speedMax > 60 ? 20 : 10
            // (tick labels: darker shades of the series colours on the light theme)
            ctx.strokeStyle = Theme.speed
            ctx.fillStyle = Theme.dark ? Theme.speed : "#06708a"
            ctx.textAlign = "left"
            ctx.textBaseline = "middle"
            ctx.beginPath(); ctx.moveTo(ax1, T); ctx.lineTo(ax1, T + H); ctx.stroke()
            for (var sv = 0; sv <= chart.speedMax; sv += sst) {
                var ys = chart.pySpeed(sv)
                ctx.beginPath(); ctx.moveTo(ax1, ys); ctx.lineTo(ax1 + 5, ys); ctx.stroke()
                ctx.fillText(sv, ax1 + 9, ys)
            }
            // heart rate axis (right 2)
            var hr = chart.hrRange
            var ax2 = L + W + 66
            ctx.strokeStyle = Theme.heartRate
            ctx.fillStyle = Theme.dark ? Theme.heartRate : "#b0127a"
            ctx.beginPath(); ctx.moveTo(ax2, T); ctx.lineTo(ax2, T + H); ctx.stroke()
            for (var h = hr[0]; h <= hr[1]; h += 20) {
                var yh = chart.pyHr(h)
                ctx.beginPath(); ctx.moveTo(ax2, yh); ctx.lineTo(ax2 + 5, yh); ctx.stroke()
                ctx.fillText(h, ax2 + 9, yh)
            }

            // ERG target
            if (chart.targetWatts > 0) {
                var yt = chart.pyPower(chart.targetWatts)
                ctx.strokeStyle = chart.css(Theme.strongLine)
                ctx.lineWidth = 2
                ctx.setLineDash([6, 4])
                ctx.beginPath(); ctx.moveTo(L, yt); ctx.lineTo(L + W, yt); ctx.stroke()
                ctx.setLineDash([])
                ctx.lineWidth = 1
            }

            var n = chart.count
            if (n === 0)
                return

            // power area + line
            var grad = ctx.createLinearGradient(0, T, 0, T + H)
            grad.addColorStop(0, "rgba(255,210,31,0.55)")
            grad.addColorStop(1, "rgba(255,210,31,0.05)")
            ctx.fillStyle = grad
            ctx.beginPath()
            ctx.moveTo(chart.px(0), T + H)
            for (var i = 0; i < n; ++i)
                ctx.lineTo(chart.px(chart.bucketX(i)), chart.pyPower(Number(chart.powerSeries[i])))
            ctx.lineTo(chart.px(chart.bucketX(n - 1)), T + H)
            ctx.closePath()
            ctx.fill()
            ctx.strokeStyle = Theme.dark ? "#ffd21f" : "#e0a800"
            ctx.lineWidth = 2
            ctx.beginPath()
            for (var j = 0; j < n; ++j) {
                var pxv = chart.px(chart.bucketX(j)), pyv = chart.pyPower(Number(chart.powerSeries[j]))
                if (j === 0) ctx.moveTo(pxv, pyv); else ctx.lineTo(pxv, pyv)
            }
            ctx.stroke()

            // speed line
            ctx.strokeStyle = Theme.speed
            ctx.lineWidth = 2.5
            ctx.beginPath()
            var lastS = null
            for (var k = 0; k < n && k < chart.speedSeries.length; ++k) {
                var sx = chart.px(chart.bucketX(k)), sy = chart.pySpeed(Number(chart.speedSeries[k]))
                if (k === 0) ctx.moveTo(sx, sy); else ctx.lineTo(sx, sy)
                lastS = sy
            }
            ctx.stroke()

            // heart rate line
            ctx.strokeStyle = Theme.heartRate
            ctx.beginPath()
            var started = false
            var lastHr = null
            for (var m = 0; m < n && m < chart.hrSeries.length; ++m) {
                var hv = Number(chart.hrSeries[m])
                if (hv <= 0) { started = false; continue }
                var hx = chart.px(chart.bucketX(m)), hy = chart.pyHr(hv)
                if (!started) { ctx.moveTo(hx, hy); started = true } else ctx.lineTo(hx, hy)
                lastHr = hy
            }
            ctx.stroke()

            // cursor
            if (chart.cursor >= 0) {
                var cx = chart.px(chart.cursor)
                ctx.strokeStyle = chart.css(Theme.strongLine)
                ctx.lineWidth = 1.5
                ctx.setLineDash([5, 4])
                ctx.beginPath(); ctx.moveTo(cx, T); ctx.lineTo(cx, T + H); ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = Theme.warning
                ctx.beginPath(); ctx.arc(cx, chart.pyPower(Number(chart.powerSeries[n - 1])), 5, 0, Math.PI * 2); ctx.fill()
                if (lastS !== null) {
                    ctx.fillStyle = Theme.speed
                    ctx.beginPath(); ctx.arc(cx, lastS, 5, 0, Math.PI * 2); ctx.fill()
                }
                if (lastHr !== null) {
                    ctx.fillStyle = Theme.heartRate
                    ctx.beginPath(); ctx.arc(cx, lastHr, 5, 0, Math.PI * 2); ctx.fill()
                }
            }
        }
    }

    Text {
        text: "Power (W)"
        color: Theme.warningText
        font.pixelSize: 15
        rotation: -90
        x: 6 - width / 2 + height / 2
        y: chart.padT + chart.plotH / 2 - height / 2
    }
    Text {
        visible: chart.targetWatts > 0
        x: chart.padL + 8
        y: chart.pyPower(chart.targetWatts) - height - 2
        text: "Target " + chart.targetWatts + " W"
        color: Theme.text
        font.pixelSize: 13
        font.weight: Font.Bold
    }
    Text {
        visible: chart.count === 0
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: (chart.padL - chart.padR) / 2
        text: chart.emptyText
        color: Theme.textSecondary
        font.pixelSize: 17
    }
}
