import QtQuick
import Artilody.Backend

// "Distance line" of the Video Ride page: the elevation profile of the
// selected Real Life Video route across the full width.
//   * filled area tinted by the gradient (green descents … red climbs),
//     the ridden part (start -> position) drawn strong, the rest faint
//   * position marker with a "4.3 / 12.0 km" bubble
//   * small ticks for the route's information texts, km labels below
//   * interactive (before the start): hover shows distance + elevation,
//     a click / drag release chooses the start point (startChosen)
Item {
    id: prof

    property var profile: Rlv.profile
    property real total: Rlv.totalDistanceM
    property real minEle: Rlv.minElevationM
    property real maxEle: Rlv.maxElevationM
    property real position: Rlv.distanceMeters
    property real startDistance: Rlv.startDistanceM
    property real elevation: Rlv.elevationM
    property var informations: Rlv.informations
    property int currentInformation: Rlv.currentInformationIndex
    property bool interactive: false
    property bool showPositionLabel: true
    readonly property real hoverDistance: mouse.containsMouse || mouse.pressed ? distanceAt(mouse.mouseX) : -1
    signal startChosen(real meters)

    readonly property bool hasData: profile !== undefined && profile !== null && profile.length > 1 && total > 0
    readonly property real span: Math.max(40, maxEle - minEle)
    readonly property real lo: minEle - span * 0.12
    readonly property real hi: maxEle + span * 0.10
    readonly property real labelH: 20
    readonly property real plotH: Math.max(10, height - labelH)
    readonly property real kmStep: {
        var km = total / 1000
        var steps = [0.5, 1, 2, 5, 10, 20, 50, 100]
        for (var i = 0; i < steps.length; ++i)
            if (km / steps[i] <= 12)
                return steps[i]
        return 200
    }

    function xOf(d) { return total > 0 ? Math.max(0, Math.min(width, d / total * width)) : 0 }
    function yOf(e) { return plotH - 2 - (e - lo) / Math.max(1, hi - lo) * (plotH - 8) }
    function distanceAt(x) { return Math.max(0, Math.min(total, x / Math.max(1, width) * total)) }
    function eleAt(d) {
        var p = profile
        if (!hasData)
            return 0
        var n = p.length
        var i = Math.max(0, Math.min(n - 2, Math.floor(d / total * (n - 1))))
        while (i > 0 && p[i].d > d) --i
        while (i < n - 2 && p[i + 1].d < d) ++i
        var a = p[i], b = p[i + 1]
        var t = b.d > a.d ? Math.max(0, Math.min(1, (d - a.d) / (b.d - a.d))) : 0
        return a.ele + (b.ele - a.ele) * t
    }
    function km(d, digits) { return (d / 1000).toFixed(digits) }
    function gradeColor(g, alpha) {
        if (g <= -2) return Qt.rgba(0.24, 0.86, 0.36, alpha)      // descent
        if (g < 2) return Qt.rgba(0.30, 0.64, 1.0, alpha)         // flat
        if (g < 5) return Qt.rgba(1.0, 0.76, 0.10, alpha)         // climb
        if (g < 8) return Qt.rgba(1.0, 0.54, 0.15, alpha)         // steep
        return Qt.rgba(0.96, 0.32, 0.23, alpha)                   // very steep
    }
    function paintProfile(ctx, w, alpha, lineColor) {
        ctx.reset()
        if (!hasData)
            return
        var p = profile
        var n = p.length
        var bottom = plotH
        // filled area tinted by the gradient: consecutive samples of the same
        // gradient class form one polygon (no seams between slices); the
        // gradient is smoothed over ~±8 samples so GPS noise does not flicker
        function cls(i) {
            var a = Math.max(0, i - 8), b = Math.min(n - 1, i + 8)
            var dd = p[b].d - p[a].d
            var g = dd > 0 ? (p[b].ele - p[a].ele) / dd * 100 : 0
            return g <= -2 ? -2 : g < 2 ? 0 : g < 5 ? 3 : g < 8 ? 6 : 9
        }
        var i0 = 0
        while (i0 < n - 1) {
            var c = cls(i0 + 1)
            var i1 = i0 + 1
            while (i1 < n - 1 && cls(i1 + 1) === c)
                ++i1
            ctx.fillStyle = gradeColor(c, alpha)
            ctx.beginPath()
            ctx.moveTo(p[i0].d / total * w, bottom)
            for (var j = i0; j <= i1; ++j)
                ctx.lineTo(p[j].d / total * w, yOf(p[j].ele))
            ctx.lineTo(p[i1].d / total * w, bottom)
            ctx.closePath()
            ctx.fill()
            i0 = i1
        }
        ctx.strokeStyle = lineColor
        ctx.lineWidth = 2
        ctx.lineJoin = "round"
        ctx.beginPath()
        for (var k = 0; k < n; ++k) {
            var x = p[k].d / total * w, y = yOf(p[k].ele)
            if (k === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
        }
        ctx.stroke()
    }

    // baseline grid (min / max elevation)
    Rectangle { visible: prof.hasData; x: 0; y: prof.plotH; width: prof.width; height: 1; color: Theme.faintLine }

    // full route, faint
    Canvas {
        id: baseCanvas
        width: prof.width
        height: prof.plotH
        antialiasing: true
        onPaint: prof.paintProfile(getContext("2d"), width, Theme.dark ? 0.55 : 0.45, Theme.softLine)
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
    // ridden part (start -> position), strong
    Item {
        id: riddenClip
        visible: prof.hasData && prof.position > prof.startDistance
        x: prof.xOf(prof.startDistance)
        width: Math.max(0, prof.xOf(prof.position) - x)
        height: prof.plotH
        clip: true
        Canvas {
            id: riddenCanvas
            x: -riddenClip.x
            width: prof.width
            height: prof.plotH
            antialiasing: true
            onPaint: prof.paintProfile(getContext("2d"), width, 0.92, Theme.strongLine)
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }
    }
    function repaint() {
        baseCanvas.requestPaint()
        riddenCanvas.requestPaint()
    }
    onProfileChanged: repaint()
    onTotalChanged: repaint()
    onLoChanged: repaint()
    onHiChanged: repaint()
    Connections {
        target: Theme
        function onDarkChanged() { prof.repaint() }
    }

    // information ticks
    Repeater {
        model: prof.hasData ? prof.informations : []
        Rectangle {
            readonly property bool current: index === prof.currentInformation
            x: prof.xOf(modelData.distance) - width / 2
            y: prof.plotH - height
            width: current ? 4 : 2
            height: current ? 14 : 9
            radius: 1
            color: current ? Theme.accentText : Theme.softLine
        }
    }

    // start point (when not at 0)
    Item {
        visible: prof.hasData && prof.startDistance > 1
        x: prof.xOf(prof.startDistance)
        height: prof.plotH
        Rectangle { width: 2; height: parent.height; color: Theme.successText; opacity: 0.85 }
        Icon {
            x: 3
            y: 0
            name: "flag"
            size: 18
            color: Theme.successText
        }
    }

    // km labels
    Repeater {
        model: prof.hasData ? Math.floor(prof.total / 1000 / prof.kmStep) + 1 : 0
        Text {
            readonly property real d: index * prof.kmStep * 1000
            visible: index === 0 || prof.total - d > prof.total * 0.05
            x: Math.max(0, Math.min(prof.width - width, prof.xOf(d) - width / 2))
            y: prof.plotH + 3
            text: prof.kmStep < 1 ? (d / 1000).toFixed(1) : Math.round(d / 1000)
            color: Theme.textMuted
            font.pixelSize: 13
        }
    }
    Text {
        visible: prof.hasData
        anchors.right: parent.right
        y: prof.plotH + 3
        text: prof.km(prof.total, 1) + " km"
        color: Theme.textSecondary
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    // hover / drag preview
    Item {
        id: hover
        visible: prof.hasData && prof.hoverDistance >= 0
        x: prof.xOf(prof.hoverDistance)
        height: prof.plotH
        Rectangle {
            x: -1
            width: 2
            height: parent.height
            color: prof.interactive ? Theme.successText : Theme.softLine
        }
        Rectangle {
            id: hoverBubble
            x: Math.max(-hover.x, Math.min(prof.width - hover.x - width, -width / 2))
            y: 2
            width: hoverText.implicitWidth + 18
            height: 26
            radius: 13
            color: Theme.menuBackground
            border.color: prof.interactive ? Theme.successText : Theme.menuBorder
            Text {
                id: hoverText
                anchors.centerIn: parent
                text: (prof.interactive ? "Start at " : "") + prof.km(prof.hoverDistance, 2) + " km  •  "
                      + Math.round(prof.eleAt(prof.hoverDistance)) + " m"
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }
    }

    // current position
    Item {
        id: marker
        visible: prof.hasData
        x: prof.xOf(prof.position)
        height: prof.plotH
        Rectangle {
            x: -1
            width: 2
            height: parent.height
            color: Theme.strongLine
        }
        Rectangle {
            x: -width / 2
            y: prof.yOf(prof.elevation) - height / 2
            width: 14
            height: 14
            radius: 7
            color: Theme.accent
            border.width: 2
            border.color: "white"
        }
        Rectangle {
            visible: prof.showPositionLabel && !hover.visible
            x: Math.max(-marker.x, Math.min(prof.width - marker.x - width, -width / 2))
            y: 2
            width: posText.implicitWidth + 18
            height: 26
            radius: 13
            color: Theme.accent
            Text {
                id: posText
                anchors.centerIn: parent
                text: prof.km(prof.position, 1) + " / " + prof.km(prof.total, 1) + " km"
                color: "white"
                font.pixelSize: 13
                font.weight: Font.Bold
            }
        }
    }

    MouseArea {
        id: mouse
        width: prof.width
        height: prof.plotH
        enabled: prof.hasData
        hoverEnabled: true
        cursorShape: prof.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onReleased: (ev) => {
            if (prof.interactive && containsMouse)
                prof.startChosen(prof.distanceAt(ev.x))
        }
    }
}
