import QtQuick

// Watt plan of a rowing workout: interval blocks (height = target watts,
// colour = intensity zone), the rowed power on top and, below, a course line
// on which the boat moves with elapsed / total time.
Item {
    id: chart
    property var intervals: []          // {name,startSec,durationSec,watts,endWatts,percent,ramp}
    property int totalSec: 0
    property real elapsed: 0
    property int currentIndex: -1
    property var powerSeries: []
    property int seriesStep: 1
    property int strokeCount: 0
    property int strokeRate: 24

    readonly property real padL: 54
    readonly property real padR: 54
    readonly property real blocksTop: 6
    readonly property real blocksBottom: height - 74
    readonly property real blocksH: Math.max(10, blocksBottom - blocksTop)
    readonly property real plotW: Math.max(10, width - padL - padR)
    readonly property real laneY: height - 24

    readonly property real wattsMax: {
        var m = 100
        for (var i = 0; i < intervals.length; ++i)
            m = Math.max(m, intervals[i].watts, intervals[i].endWatts)
        for (var j = 0; j < powerSeries.length; ++j)
            m = Math.max(m, Number(powerSeries[j]))
        return Math.ceil(m * 1.12 / 50) * 50
    }
    function px(sec) { return padL + plotW * Math.max(0, Math.min(1, sec / Math.max(1, totalSec))) }
    function py(w) { return blocksTop + blocksH * (1 - Math.max(0, Math.min(1, w / wattsMax))) }
    function zoneFor(percent) {
        return percent <= 55 ? 0 : percent <= 75 ? 1 : percent <= 90 ? 2 : percent <= 105 ? 3 : percent <= 120 ? 4 : 5
    }
    function clockText(s) {
        s = Math.round(s)
        var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (ss < 10 ? "0" : "") + ss
    }

    onIntervalsChanged: canvas.requestPaint()
    onPowerSeriesChanged: canvas.requestPaint()
    onCurrentIndexChanged: canvas.requestPaint()
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
            var L = chart.padL, W = chart.plotW, T = chart.blocksTop, B = chart.blocksBottom
            var grid = Theme.dark ? "rgba(255,255,255,0.14)" : "rgba(25,40,100,0.14)"
            var label = Theme.dark ? "rgba(255,255,255,0.8)" : "rgba(22,33,79,0.8)"
            ctx.font = "13px sans-serif"
            // watt grid
            var step = chart.wattsMax > 400 ? 100 : 50
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (var w = 0; w <= chart.wattsMax; w += step) {
                var y = chart.py(w)
                ctx.strokeStyle = grid
                ctx.setLineDash([4, 4])
                ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + W, y); ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = label
                ctx.fillText(w + "", L - 8, y)
            }
            // blocks
            for (var i = 0; i < chart.intervals.length; ++i) {
                var iv = chart.intervals[i]
                var x0 = chart.px(iv.startSec) + 1, x1 = chart.px(iv.startSec + iv.durationSec) - 1
                var c = Theme.zoneColor(chart.zoneFor(iv.percent))
                var done = (iv.startSec + iv.durationSec) <= chart.elapsed
                ctx.globalAlpha = i === chart.currentIndex ? 1 : (done ? 0.45 : 0.8)
                ctx.fillStyle = c
                ctx.beginPath()
                ctx.moveTo(x0, B)
                ctx.lineTo(x0, chart.py(iv.watts))
                ctx.lineTo(x1, chart.py(iv.endWatts))
                ctx.lineTo(x1, B)
                ctx.closePath()
                ctx.fill()
                if (i === chart.currentIndex) {
                    ctx.globalAlpha = 1
                    ctx.strokeStyle = "white"
                    ctx.lineWidth = 2.5
                    ctx.stroke()
                    ctx.lineWidth = 1
                }
            }
            ctx.globalAlpha = 1
            // rowed power
            var n = chart.powerSeries.length
            if (n > 1) {
                ctx.strokeStyle = "white"
                ctx.lineWidth = 2
                ctx.beginPath()
                for (var k = 0; k < n; ++k) {
                    var xx = chart.px((k + 0.5) * chart.seriesStep), yy = chart.py(Number(chart.powerSeries[k]))
                    if (k === 0) ctx.moveTo(xx, yy); else ctx.lineTo(xx, yy)
                }
                ctx.stroke()
                ctx.lineWidth = 1
            }
            // time labels
            var opts = [30, 60, 120, 300, 600, 900, 1200, 1800, 3600]
            var ts = 3600
            for (var o = 0; o < opts.length; ++o)
                if (chart.totalSec / opts[o] <= 8) { ts = opts[o]; break }
            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            ctx.fillStyle = label
            for (var t = 0; t <= chart.totalSec; t += ts)
                ctx.fillText(chart.clockText(t), chart.px(t), B + 6)
            // course line
            var ly = chart.laneY
            ctx.strokeStyle = "rgba(255,255,255,0.45)"
            ctx.setLineDash([7, 6])
            ctx.lineWidth = 2
            ctx.beginPath(); ctx.moveTo(L, ly); ctx.lineTo(L + W, ly); ctx.stroke()
            ctx.setLineDash([])
            // start / finish posts
            ctx.fillStyle = "white"
            ctx.fillRect(L - 1, ly - 14, 2.5, 28)
            ctx.fillRect(L + W - 1, ly - 14, 2.5, 28)
            var s = 5
            for (var r = 0; r < 3; ++r)
                for (var cc = 0; cc < 4; ++cc) {
                    ctx.fillStyle = (r + cc) % 2 === 0 ? "white" : "#111a40"
                    ctx.fillRect(L + W + 2 + cc * s, ly - 14 + r * s, s, s)
                }
        }
    }

    // block labels
    Repeater {
        model: chart.intervals
        Column {
            readonly property real bw: chart.px(modelData.startSec + modelData.durationSec) - chart.px(modelData.startSec)
            readonly property real blockTop: chart.py(Math.max(modelData.watts, modelData.endWatts))
            visible: bw > 46
            x: chart.px(modelData.startSec) + bw / 2 - width / 2
            // inside the block when it is tall enough, otherwise above it
            readonly property bool inside: chart.blocksBottom - blockTop >= height + 6
            y: inside ? blockTop + 3 : Math.max(chart.blocksTop, blockTop - height - 2)
            spacing: 0
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: modelData.ramp ? modelData.watts + "–" + modelData.endWatts : modelData.watts + " W"
                color: parent.inside ? Theme.textOnColor : Theme.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: chart.clockText(modelData.durationSec)
                color: parent.inside ? Theme.textOnColor : Theme.textSecondary
                font.pixelSize: 12
            }
        }
    }

    // elapsed cursor across blocks
    Rectangle {
        visible: chart.elapsed > 0
        x: chart.px(chart.elapsed) - 1
        y: chart.blocksTop
        width: 2
        height: chart.laneY - chart.blocksTop
        color: "white"
        opacity: 0.8
    }

    // the boat on the course line
    Rectangle {
        x: chart.padL
        y: chart.laneY - 7
        width: Math.max(0, boat.x + boat.width * 0.3 - chart.padL)
        height: 14
        radius: 7
        visible: chart.elapsed > 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: Qt.rgba(0.3, 0.64, 1, 0.6) }
        }
    }
    RowingBoat {
        id: boat
        boatLength: 54
        x: Math.max(chart.padL - 4, chart.px(chart.elapsed) - boatLength)
        y: chart.laneY - height / 2
        strokeCount: chart.strokeCount
        strokeRate: chart.strokeRate
    }
}
