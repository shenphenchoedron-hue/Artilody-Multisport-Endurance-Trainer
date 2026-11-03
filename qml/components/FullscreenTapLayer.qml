import QtQuick

// Double-click on a ride picture toggles full screen. Placed right above the
// picture and under its overlays, so clicks on those never count.
Item {
    id: layer
    signal toggle()
    anchors.fill: parent
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: layer.toggle()
    }
}
