import QtQuick

// Pill toggle (green ERG switch / blue preference switches in the mockups).
// Controlled component: a click only emits toggled(!checked) and never assigns
// `checked` itself, so a binding such as `checked: Gears.enabled` survives and
// the switch always shows the backend state (also when the backend refuses).
Rectangle {
    id: toggle
    property bool checked: false
    property color onColor: Theme.success
    property bool enabled: true
    property string accessibleName
    signal toggled(bool checked)

    function requestToggle() {
        if (toggle.enabled)
            toggle.toggled(!toggle.checked)
    }

    implicitWidth: 56
    implicitHeight: 32
    radius: height / 2
    opacity: enabled ? 1 : 0.45
    color: checked ? onColor : Theme.fieldBorder
    Behavior on color { ColorAnimation { duration: 140 } }

    Accessible.role: Accessible.CheckBox
    Accessible.name: accessibleName
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onToggleAction: requestToggle()
    Accessible.onPressAction: requestToggle()

    Rectangle {
        width: parent.height - 6
        height: width
        radius: width / 2
        y: 3
        x: toggle.checked ? toggle.width - width - 3 : 3
        color: "white"
        Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        anchors.fill: parent
        // a slightly larger hit area than the drawn pill (small switches)
        anchors.margins: -4
        cursorShape: toggle.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: toggle.requestToggle()
    }
}
