import QtQuick

// Large tinted button of the free ride page (Shift Easier / Shift Harder,
// ERG steps). Repeats while held. Uses Item.enabled, so a disabled button
// gets no input.
Rectangle {
    id: btn
    property string label
    property string sublabel
    property string iconName
    property real iconSize: 40
    property real labelSize: 18
    property color tint: Theme.accent
    property bool repeatOnHold: true
    signal clicked()

    // brief highlight, e.g. when the Zwift Click pressed the same button
    function pulse() { pulseAnim.restart() }

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.description: sublabel
    Accessible.onPressAction: if (enabled) clicked()
    // With keyboard focus, Space / Return press this button: claim them before
    // the window-wide Space shortcut (pause / resume) gets the key.
    Keys.onShortcutOverride: (event) => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            event.accepted = true
    }
    Keys.onSpacePressed: (event) => { if (enabled) clicked(); event.accepted = true }
    Keys.onReturnPressed: (event) => { if (enabled) clicked(); event.accepted = true }
    Keys.onEnterPressed: (event) => { if (enabled) clicked(); event.accepted = true }

    radius: 14
    opacity: enabled ? 1 : 0.4
    color: Qt.rgba(tint.r, tint.g, tint.b, mouse.pressed ? 0.42 : (mouse.containsMouse ? 0.26 : 0.15))
    border.width: activeFocus ? 2.5 : 1.5
    border.color: activeFocus ? Theme.accentLight : Qt.rgba(tint.r, tint.g, tint.b, 0.75)

    Rectangle {
        id: glow
        anchors.fill: parent
        radius: parent.radius
        color: btn.tint
        opacity: 0
    }

    Column {
        anchors.centerIn: parent
        spacing: 4
        Icon {
            visible: btn.iconName.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            name: btn.iconName
            size: btn.iconSize
            strokeWidth: 3
            color: Theme.text
        }
        Text {
            visible: btn.label.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: btn.label
            color: Theme.text
            font.pixelSize: btn.labelSize
            font.weight: Font.Bold
        }
        Text {
            visible: btn.sublabel.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: btn.sublabel
            color: Theme.textSecondary
            font.pixelSize: 13
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
        onPressAndHold: if (btn.repeatOnHold) repeat.start()
        onReleased: repeat.stop()
        onCanceled: repeat.stop()
        Timer {
            id: repeat
            interval: 260
            repeat: true
            onTriggered: btn.clicked()
        }
    }

    SequentialAnimation {
        id: pulseAnim
        NumberAnimation { target: glow; property: "opacity"; to: 0.45; duration: 70 }
        NumberAnimation { target: glow; property: "opacity"; to: 0; duration: 380; easing.type: Easing.OutCubic }
    }
}
