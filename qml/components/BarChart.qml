import QtQuick

// Simple vertical bar chart (Weekly Training Time)
Canvas {
    id: bars
    property var values: []
    property var labels: []
    property string axisTitle: "Hours"
    property real minAxis: 4

    onValuesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    Connections {
        target: Theme
        function onDarkChanged() { bars.requestPaint() }
    }

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        var L = 56, R = 10, T = 10, B = 34
        var PW = width - L - R, PH = height - T - B
        var max = minAxis
        var avg = 0
        for (var i = 0; i < values.length; ++i) {
            max = Math.max(max, values[i])
            avg += values[i]
        }
        avg = values.length ? avg / values.length : 0
        var step = max > 20 ? 5 : (max > 8 ? 2 : 1)
        max = Math.ceil(max / step) * step
        var yOf = function(v) { return T + PH - v / max * PH }

        ctx.font = "14px sans-serif"
        ctx.strokeStyle = Qt.rgba(Theme.grid.r, Theme.grid.g, Theme.grid.b, Theme.grid.a)
        ctx.setLineDash([4, 4])
        ctx.beginPath()
        for (var g = step; g <= max; g += step) {
            ctx.moveTo(L, Math.round(yOf(g)) + 0.5)
            ctx.lineTo(L + PW, Math.round(yOf(g)) + 0.5)
        }
        ctx.stroke()
        ctx.setLineDash([])

        ctx.fillStyle = Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g, Theme.textSecondary.b, Theme.textSecondary.a)
        ctx.textAlign = "right"
        ctx.textBaseline = "middle"
        for (var y = 0; y <= max; y += step)
            ctx.fillText("" + y, L - 10, yOf(y))

        ctx.save()
        ctx.translate(14, T + PH / 2)
        ctx.rotate(-Math.PI / 2)
        ctx.textAlign = "center"
        ctx.fillText(axisTitle, 0, 0)
        ctx.restore()

        var n = Math.max(1, values.length)
        var slot = PW / n
        var bw = slot * 0.72
        ctx.textAlign = "center"
        ctx.textBaseline = "top"
        for (var b = 0; b < values.length; ++b) {
            var x = L + slot * b + (slot - bw) / 2
            ctx.fillStyle = values[b] > avg * 1.25 && values[b] > 0 ? Theme.orange : "#3d86fb"
            if (values[b] > 0)
                ctx.fillRect(x, yOf(values[b]), bw, T + PH - yOf(values[b]))
            ctx.fillStyle = Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g, Theme.textSecondary.b, Theme.textSecondary.a)
            ctx.fillText(labels[b] || "", x + bw / 2, T + PH + 10)
        }
    }
}
