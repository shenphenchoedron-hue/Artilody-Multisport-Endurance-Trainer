import QtQuick
import QtQuick.Controls

Slider {
    id: slider
    property color fillColor: Theme.danger
    property real trackHeight: 5

    implicitHeight: 24
    leftPadding: 0
    rightPadding: 0

    background: Rectangle {
        x: slider.leftPadding
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.availableWidth
        height: slider.trackHeight
        radius: height / 2
        color: Theme.fieldBorder
        Rectangle {
            width: slider.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: slider.fillColor
        }
    }

    handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: 20
        height: 20
        radius: 10
        color: "white"
        border.width: 4
        border.color: slider.fillColor
    }
}
