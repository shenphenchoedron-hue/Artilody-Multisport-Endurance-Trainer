import QtQuick

// Watt plan of a rowing workout: interval blocks (height = target watts,
// colour = intensity zone), the rowed power on top and, below, a course line
// on which the boat moves with elapsed / total time. The bow tip marks the
// elapsed time on the same axis as the blocks (start post = 0:00, finish
// post = total); the hull trails into the left padding.
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

    readonly property real boatLength: 42
    readonly property real padL: Math.max(54, boatLength + 12)
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
    // elapsed time shown by the boat and cursor: glides between the 250 ms
    // metric updates, snaps back on a reset or a new plan
    readonly property real elapsedTarget: Math.max(0, Math.min(totalSec, elapsed))
    property real shownElapsed: 0
    onElapsedTargetChanged: {
        glide.stop()
        if (elapsedTarget < shownElapsed || elapsedTarget - shownElapsed > Math.max(2, totalSec * 0.2))
            shownElapsed = elapsedTarget
        else if (elapsedTarget !== shownElapsed) {
            glide.from = shownElapsed
            glide.to = elapsedTarget
            glide.start()
        }
    }
    onTotalSecChanged: { glide.stop(); shownElapsed = elapsedTarget }
    Component.onCompleted: shownElapsed = elapsedTarget
    NumberAnimation { id: glide; target: chart; property: "shownElapsed"; duration: 280; easing.type: Easing.Linear }

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

    // QML color -> CSS string for the Canvas (keeps the alpha channel)
    function css(c) {
        return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
    }

    onIntervalsChanged: canvas.requestPaint()
    onPowerSeriesChanged: canvas.requestPaint()
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
                    ctx.strokeStyle = chart.css(Theme.strongLine)
                    ctx.lineWidth = 2.5
                    ctx.stroke()
                    ctx.lineWidth = 1
                }
            }
            ctx.globalAlpha = 1
            // rowed power
            var n = chart.powerSeries.length
            if (n > 1) {
                ctx.strokeStyle = chart.css(Theme.text)
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
            ctx.strokeStyle = chart.css(Theme.softLine)
            ctx.setLineDash([7, 6])
            ctx.lineWidth = 2
            ctx.beginPath(); ctx.moveTo(L, ly); ctx.lineTo(L + W, ly); ctx.stroke()
            ctx.setLineDash([])
            // start / finish posts
            var post = chart.css(Theme.strongLine)
            var dark = Theme.dark ? "#111a40" : "#ffffff"
            ctx.fillStyle = post
            ctx.fillRect(L - 1, ly - 14, 2.5, 28)
            ctx.fillRect(L + W - 1, ly - 14, 2.5, 28)
            var s = 5
            for (var r = 0; r < 3; ++r)
                for (var cc = 0; cc < 4; ++cc) {
                    ctx.fillStyle = (r + cc) % 2 === 0 ? post : dark
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
        visible: chart.shownElapsed > 0
        x: chart.px(chart.shownElapsed) - 1
        y: chart.blocksTop
        width: 2
        height: chart.laneY - chart.blocksTop
        color: Theme.strongLine
    }

    // the boat on the course line
    Rectangle {
        x: chart.padL - boat.tipOffset
        y: chart.laneY - 7
        width: Math.max(0, boat.x + boat.tipOffset * 0.3 - x)
        height: 14
        radius: 7
        visible: chart.shownElapsed > 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: Qt.rgba(0.3, 0.64, 1, 0.6) }
        }
    }
    RowingBoat {
        id: boat
        boatLength: chart.boatLength
        x: chart.px(chart.shownElapsed) - tipOffset
        y: chart.laneY - height / 2
        strokeCount: chart.strokeCount
        strokeRate: chart.strokeRate
    }
}
