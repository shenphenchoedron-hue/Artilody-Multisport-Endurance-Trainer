import QtQuick

// Power (bars), pace /500 m and heart rate (lines) of the current row over
// distance ("distance") or time ("time"). Pace axis is inverted (fast = up).
Item {
    id: chart
    property var powerSeries: []
    property var paceSeries: []
    property var hrSeries: []
    property var distanceSeries: []
    property int seriesStep: 1
    property string xMode: "distance"
    property real xMax: 2000            // m or s
    property real cursor: -1            // m or s, -1 = none
    property string cursorLabel: ""
    property var targets: []            // plan intervals {startSec,durationSec,watts,endWatts}

    readonly property real padL: 62
    readonly property real padR: 128
    readonly property real padT: 34
    readonly property real padB: 52
    readonly property real plotW: Math.max(10, width - padL - padR)
    readonly property real plotH: Math.max(10, height - padT - padB)
    readonly property int count: powerSeries ? powerSeries.length : 0

    function niceStep(range, ticks) {
        var raw = range / Math.max(1, ticks)
        var mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10))
        var n = raw / mag
        return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10) * mag
    }
    // x of bucket i (its end) in data units
    function bucketX(i) {
        if (xMode === "distance")
            return i < distanceSeries.length ? Number(distanceSeries[i]) : 0
        return (i + 1) * seriesStep
    }
    readonly property real powerMax: {
        var m = 150
        for (var i = 0; i < count; ++i)
            m = Math.max(m, Number(powerSeries[i]) * 1.15)
        for (var j = 0; j < targets.length; ++j)
            m = Math.max(m, Math.max(targets[j].watts, targets[j].endWatts) * 1.15)
        var st = niceStep(m, 6)
        return Math.ceil(m / st) * st
    }
    readonly property var paceRange: {
        var lo = 1e9, hi = 0
        for (var i = 0; i < paceSeries.length; ++i) {
            var p = Number(paceSeries[i])
            if (p <= 0)
                continue
            p = Math.max(60, Math.min(300, p))
            lo = Math.min(lo, p); hi = Math.max(hi, p)
        }
        if (hi === 0) { lo = 90; hi = 180 }
        lo = Math.floor((lo - 5) / 15) * 15
        hi = Math.ceil((hi + 5) / 15) * 15
        if (hi - lo < 90) {
            var mid = (lo + hi) / 2
            lo = Math.max(30, Math.floor((mid - 45) / 15) * 15)
            hi = lo + 90
        }
        return [lo, hi]
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
    function pyPace(p) {
        var r = paceRange
        return padT + plotH * Math.max(0, Math.min(1, (Math.max(60, Math.min(300, p)) - r[0]) / (r[1] - r[0])))
    }
    function pyHr(h) {
        var r = hrRange
        return padT + plotH * (1 - Math.max(0, Math.min(1, (h - r[0]) / (r[1] - r[0]))))
    }
    function clockText(s) {
        s = Math.round(s)
        var m = Math.floor(s / 60), ss = s % 60
        return m + ":" + (ss < 10 ? "0" : "") + ss
    }

    onPowerSeriesChanged: canvas.requestPaint()
    onXMaxChanged: canvas.requestPaint()
    onXModeChanged: canvas.requestPaint()
    onCursorChanged: canvas.requestPaint()
    onTargetsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true
        renderStrategy: Canvas.Cooperative

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var L = chart.padL, T = chart.padT, W = chart.plotW, H = chart.plotH
            var grid = Theme.dark ? "rgba(255,255,255,0.16)" : "rgba(25,40,100,0.14)"
            var label = Theme.dark ? "rgba(255,255,255,0.85)" : "rgba(22,33,79,0.85)"
            ctx.font = "14px sans-serif"
            ctx.lineWidth = 1

            // --- grid + power axis (left) ---------------------------------
            var pst = chart.niceStep(chart.powerMax, 6)
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
            // vertical grid + x labels
            var xst = chart.xMode === "distance" ? chart.niceStep(chart.xMax, 10) : chart.niceStep(chart.xMax, 8)
            if (chart.xMode === "time") {
                var opts = [30, 60, 120, 300, 600, 900, 1200, 1800, 3600]
                xst = opts[opts.length - 1]
                for (var oi = 0; oi < opts.length; ++oi)
                    if (chart.xMax / opts[oi] <= 10) { xst = opts[oi]; break }
            }
            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            for (var xv = 0; xv <= chart.xMax + 0.1; xv += xst) {
                var x = chart.px(xv)
                ctx.strokeStyle = grid
                ctx.setLineDash([4, 4])
                ctx.beginPath(); ctx.moveTo(x, T); ctx.lineTo(x, T + H); ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = label
                ctx.fillText(chart.xMode === "distance" ? Theme.thousands(xv) : chart.clockText(xv), x, T + H + 10)
            }
            // axes
            ctx.strokeStyle = Theme.warning
            ctx.beginPath(); ctx.moveTo(L, T); ctx.lineTo(L, T + H); ctx.lineTo(L + W, T + H); ctx.stroke()

            // --- pace axis (right 1) -----------------------------------------
            var pr = chart.paceRange
            var pStep = (pr[1] - pr[0]) > 120 ? 30 : 15
            var ax1 = L + W + 8
            ctx.strokeStyle = Theme.pace
            ctx.beginPath(); ctx.moveTo(ax1, T); ctx.lineTo(ax1, T + H); ctx.stroke()
            ctx.textAlign = "left"
            ctx.textBaseline = "middle"
            ctx.fillStyle = Theme.pace
            for (var p = pr[0]; p <= pr[1]; p += pStep) {
                var yp = chart.pyPace(p)
                ctx.beginPath(); ctx.moveTo(ax1, yp); ctx.lineTo(ax1 + 5, yp); ctx.stroke()
                ctx.fillText(chart.clockText(p), ax1 + 9, yp)
            }
            // --- heart rate axis (right 2) -----------------------------------
            var hr = chart.hrRange
            var ax2 = L + W + 72
            ctx.strokeStyle = Theme.heartRate
            ctx.fillStyle = Theme.heartRate
            ctx.beginPath(); ctx.moveTo(ax2, T); ctx.lineTo(ax2, T + H); ctx.stroke()
            for (var h = hr[0]; h <= hr[1]; h += 20) {
                var yh = chart.pyHr(h)
                ctx.beginPath(); ctx.moveTo(ax2, yh); ctx.lineTo(ax2 + 5, yh); ctx.stroke()
                ctx.fillText(h, ax2 + 9, yh)
            }

            // --- target power (plan) -----------------------------------------
            if (chart.targets.length > 0 && chart.xMode === "time") {
                ctx.strokeStyle = "rgba(255,255,255,0.75)"
                ctx.lineWidth = 2
                ctx.setLineDash([6, 4])
                ctx.beginPath()
                for (var t = 0; t < chart.targets.length; ++t) {
                    var iv = chart.targets[t]
                    var x0 = chart.px(iv.startSec), x1 = chart.px(iv.startSec + iv.durationSec)
                    if (t === 0) ctx.moveTo(x0, chart.pyPower(iv.watts))
                    else ctx.lineTo(x0, chart.pyPower(iv.watts))
                    ctx.lineTo(x1, chart.pyPower(iv.endWatts))
                }
                ctx.stroke()
                ctx.setLineDash([])
                ctx.lineWidth = 1
            }

            var n = chart.count
            if (n === 0)
                return

            // --- power bars ---------------------------------------------------
            var grad = ctx.createLinearGradient(0, T, 0, T + H)
            grad.addColorStop(0, "#ffe34d")
            grad.addColorStop(1, "rgba(255,210,31,0.35)")
            ctx.fillStyle = grad
            var prevX = chart.px(0)
            // thin out bars when there are many buckets
            var every = Math.max(1, Math.ceil(n / Math.max(1, W / 7)))
            for (var i = every - 1; i < n; i += every) {
                var bx = chart.px(chart.bucketX(i))
                var bw = Math.max(2, (bx - prevX) * 0.62)
                var by = chart.pyPower(Number(chart.powerSeries[i]))
                ctx.fillRect(bx - bw, by, bw, T + H - by)
                prevX = bx
            }

            // --- pace area + line ----------------------------------------------
            var pts = []
            // (the first bucket includes the start-up stroke: skip it)
            for (var k = n > 2 ? 1 : 0; k < n && k < chart.paceSeries.length; ++k) {
                var pv = Number(chart.paceSeries[k])
                if (pv > 0)
                    pts.push([chart.px(chart.bucketX(k)), chart.pyPace(pv)])
            }
            if (pts.length > 1) {
                var area = ctx.createLinearGradient(0, T, 0, T + H)
                area.addColorStop(0, "rgba(56,217,245,0.28)")
                area.addColorStop(1, "rgba(56,217,245,0.02)")
                ctx.fillStyle = area
                ctx.beginPath()
                ctx.moveTo(pts[0][0], T + H)
                for (var a = 0; a < pts.length; ++a) ctx.lineTo(pts[a][0], pts[a][1])
                ctx.lineTo(pts[pts.length - 1][0], T + H)
                ctx.closePath()
                ctx.fill()
                ctx.strokeStyle = Theme.pace
                ctx.lineWidth = 2.5
                ctx.beginPath()
                for (var b = 0; b < pts.length; ++b) {
                    if (b === 0) ctx.moveTo(pts[b][0], pts[b][1]); else ctx.lineTo(pts[b][0], pts[b][1])
                }
                ctx.stroke()
            }

            // --- heart rate line ------------------------------------------------
            ctx.strokeStyle = Theme.heartRate
            ctx.lineWidth = 2.5
            ctx.beginPath()
            var started = false
            var lastHr = null
            for (var j = 0; j < n && j < chart.hrSeries.length; ++j) {
                var hv = Number(chart.hrSeries[j])
                if (hv <= 0) { started = false; continue }
                var hx = chart.px(chart.bucketX(j)), hy = chart.pyHr(hv)
                if (!started) { ctx.moveTo(hx, hy); started = true } else ctx.lineTo(hx, hy)
                lastHr = [hx, hy]
            }
            ctx.stroke()

            // --- cursor ---------------------------------------------------------
            if (chart.cursor >= 0) {
                var cx = chart.px(chart.cursor)
                ctx.strokeStyle = "rgba(255,255,255,0.9)"
                ctx.lineWidth = 1.5
                ctx.setLineDash([5, 4])
                ctx.beginPath(); ctx.moveTo(cx, T); ctx.lineTo(cx, T + H); ctx.stroke()
                ctx.setLineDash([])
                // end dots
                var lastP = Number(chart.powerSeries[n - 1])
                ctx.fillStyle = Theme.warning
                ctx.beginPath(); ctx.arc(cx, chart.pyPower(lastP), 5, 0, Math.PI * 2); ctx.fill()
                if (pts.length > 0) {
                    ctx.fillStyle = Theme.pace
                    ctx.beginPath(); ctx.arc(cx, pts[pts.length - 1][1], 5, 0, Math.PI * 2); ctx.fill()
                }
                if (lastHr) {
                    ctx.fillStyle = Theme.heartRate
                    ctx.beginPath(); ctx.arc(cx, lastHr[1], 5, 0, Math.PI * 2); ctx.fill()
                }
            }
        }
    }

    // axis titles
    Text {
        text: "Power (W)"
        color: Theme.warning
        font.pixelSize: 15
        rotation: -90
        x: 6 - width / 2 + height / 2
        y: chart.padT + chart.plotH / 2 - height / 2
    }
    Text {
        text: chart.xMode === "distance" ? "Distance (m)" : "Time"
        color: Theme.text
        font.pixelSize: 16
        x: chart.padL + chart.plotW / 2 - width / 2
        y: chart.height - height - 2
    }

    // cursor label
    Rectangle {
        visible: chart.cursor >= 0 && chart.cursorLabel.length > 0 && chart.count > 0
        x: Math.max(chart.padL, Math.min(chart.padL + chart.plotW - width, chart.px(chart.cursor) - width / 2))
        y: chart.padT - height - 4
        width: cursorText.implicitWidth + 18
        height: 26
        radius: 8
        color: Theme.dark ? "#16214f" : "#ffffff"
        border.color: Theme.text
        border.width: 1
        Text {
            id: cursorText
            anchors.centerIn: parent
            text: chart.cursorLabel
            color: Theme.text
            font.pixelSize: 14
            font.weight: Font.Bold
        }
    }

    Text {
        visible: chart.count === 0
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: (chart.padL - chart.padR) / 2
        text: "Start rowing to see power, pace and heart rate"
        color: Theme.textSecondary
        font.pixelSize: 17
    }
}
