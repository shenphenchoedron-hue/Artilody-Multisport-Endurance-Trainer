import QtQuick
import QtQuick.Layouts

// Modal month picker. Dates are "yyyy-MM-dd" strings (local calendar days).
//   DatePickerDialog { id: picker; onPicked: (iso) => Planner.addWorkout(iso, id) }
//   picker.openFor("2026-10-23")
AppDialog {
    id: dlg
    property string subtitle
    property string acceptText: "Choose"
    property string selectedIso
    property int viewYear: 2026
    property int viewMonth: 1          // 1..12
    readonly property string todayIso: dlg.isoOf(new Date())
    signal picked(string iso)

    title: "Pick a date"
    panelWidth: 470
    panelHeight: 604

    function pad(n) { return n < 10 ? "0" + n : "" + n }
    function isoOf(d) { return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) }
    function parseIso(s) {
        var p = String(s).split("-")
        if (p.length !== 3)
            return new Date()
        return new Date(parseInt(p[0], 10), parseInt(p[1], 10) - 1, parseInt(p[2], 10))
    }
    function shift(iso, days) {
        var d = parseIso(iso)
        return isoOf(new Date(d.getFullYear(), d.getMonth(), d.getDate() + days))
    }

    function openFor(iso) {
        selectedIso = iso && String(iso).length === 10 ? String(iso) : isoOf(new Date())
        var d = parseIso(selectedIso)
        viewYear = d.getFullYear()
        viewMonth = d.getMonth() + 1
        open()
    }
    function stepMonth(delta) {
        var m = viewMonth - 1 + delta
        viewYear += Math.floor(m / 12)
        viewMonth = ((m % 12) + 12) % 12 + 1
    }
    function show(iso) {
        selectedIso = iso
        var d = parseIso(iso)
        viewYear = d.getFullYear()
        viewMonth = d.getMonth() + 1
    }

    readonly property var cells: {
        var first = new Date(viewYear, viewMonth - 1, 1)
        var offset = (first.getDay() + 6) % 7          // Monday first
        var out = []
        for (var i = 0; i < 42; ++i) {
            var d = new Date(viewYear, viewMonth - 1, 1 - offset + i)
            out.push({ iso: isoOf(d), day: d.getDate(), inMonth: d.getMonth() === viewMonth - 1 })
        }
        return out
    }

    Text {
        x: 28
        y: 72
        width: dlg.panelWidth - 56
        visible: text.length > 0
        text: dlg.subtitle
        color: Theme.textSecondary
        font.pixelSize: 16
        elide: Text.ElideRight
    }

    // month navigation
    Item {
        x: 24
        y: 104
        width: dlg.panelWidth - 48
        height: 44
        IconButton { iconName: "chevronLeft"; width: 44; height: 44; onClicked: dlg.stepMonth(-1) }
        Text {
            anchors.centerIn: parent
            text: Qt.locale("en_US").standaloneMonthName(dlg.viewMonth - 1) + " " + dlg.viewYear
            color: Theme.text
            font.pixelSize: 20
            font.weight: Font.Bold
        }
        IconButton { anchors.right: parent.right; iconName: "chevronRight"; width: 44; height: 44; onClicked: dlg.stepMonth(1) }
    }

    Grid {
        id: dayGrid
        x: 24
        y: 158
        columns: 7
        readonly property real cell: (dlg.panelWidth - 48) / 7
        Repeater {
            model: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
            Text {
                width: dayGrid.cell
                height: 26
                horizontalAlignment: Text.AlignHCenter
                text: modelData
                color: Theme.textMuted
                font.pixelSize: 13
            }
        }
        Repeater {
            model: dlg.cells
            Item {
                width: dayGrid.cell
                height: 44
                readonly property bool isSel: modelData.iso === dlg.selectedIso
                readonly property bool isToday: modelData.iso === dlg.todayIso
                Rectangle {
                    anchors.centerIn: parent
                    width: 40; height: 40; radius: 20
                    color: parent.isSel ? Theme.accent : (dayMouse.containsMouse ? Theme.hover : "transparent")
                    border.width: parent.isToday && !parent.isSel ? 1.5 : 0
                    border.color: Theme.accentLight
                }
                Text {
                    anchors.centerIn: parent
                    text: modelData.day
                    color: parent.isSel ? "white" : (modelData.inMonth ? Theme.text : Theme.textMuted)
                    font.pixelSize: 16
                    font.weight: parent.isSel || parent.isToday ? Font.Bold : Font.Normal
                }
                MouseArea {
                    id: dayMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: dlg.show(modelData.iso)
                    onDoubleClicked: { dlg.show(modelData.iso); dlg.close(); dlg.picked(modelData.iso) }
                }
            }
        }
    }

    // quick picks
    Row {
        x: 24
        y: dayGrid.y + dayGrid.height + 10
        spacing: 8
        Repeater {
            model: [{ t: "Today", d: 0 }, { t: "Tomorrow", d: 1 }, { t: "+1 week", d: 7 }]
            AppButton {
                height: 36
                kind: "secondary"
                bold: false
                fontSize: 14
                horizontalPadding: 14
                text: modelData.t
                onClicked: dlg.show(dlg.shift(dlg.todayIso, modelData.d))
            }
        }
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        spacing: 12
        Text {
            Layout.fillWidth: true
            text: dlg.parseIso(dlg.selectedIso).toLocaleDateString(Qt.locale("en_US"), "dddd, d MMMM yyyy")
            color: Theme.text
            font.pixelSize: 16
            elide: Text.ElideRight
        }
        AppButton {
            kind: "secondary"
            text: "Cancel"
            bold: false
            Layout.preferredHeight: 50
            onClicked: dlg.close()
        }
        AppButton {
            text: dlg.acceptText
            Layout.preferredHeight: 50
            onClicked: {
                var iso = dlg.selectedIso
                dlg.close()
                dlg.picked(iso)
            }
        }
    }
}
