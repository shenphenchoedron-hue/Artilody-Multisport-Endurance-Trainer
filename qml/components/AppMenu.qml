import QtQuick
import QtQuick.Controls

// Themed context / drop-down menu. Use with AppMenuItem and AppMenuSeparator.
// Sizes itself to the widest item (at least minWidth, at most the window width),
// stays 8 px inside the window and fades in. Keyboard: arrows, Enter, Esc.
Menu {
    id: menu

    property real minWidth: 220
    // widest item; reading implicitWidth makes this follow text changes
    readonly property real widestItem: {
        var w = 0
        for (var i = 0; i < count; ++i) {
            var it = itemAt(i)
            if (it && it.implicitWidth !== undefined)
                w = Math.max(w, it.implicitWidth)
        }
        return w
    }
    readonly property real windowWidth: parent && parent.Window.window ? parent.Window.window.width : 100000

    margins: 8
    padding: 6
    font.pixelSize: Theme.menuFontSize
    implicitWidth: Math.min(Math.max(minWidth, widestItem + leftPadding + rightPadding), windowWidth - 2 * margins)

    delegate: AppMenuItem {}   // for Menu { title } sub-menus / Actions

    contentItem: ListView {
        implicitHeight: contentHeight
        model: menu.contentModel
        interactive: Window.window ? contentHeight + menu.topPadding + menu.bottomPadding > Window.window.height : false
        clip: true
        currentIndex: menu.currentIndex
        keyNavigationEnabled: false   // Menu handles the keys itself
        ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: Item {
        implicitWidth: menu.minWidth
        implicitHeight: 40
        // soft drop shadow from layered translucent rectangles (no effects module needed)
        Repeater {
            model: 4
            Rectangle {
                x: -index
                y: 2 + index * 0.5 - index
                width: parent.width + 2 * index
                height: parent.height + 2 * index
                radius: 12 + index
                color: Qt.rgba(0, 0.02, 0.12, Theme.dark ? 0.16 - index * 0.03 : 0.07 - index * 0.012)
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: Theme.menuBackground
            border.width: 1
            border.color: Theme.menuBorder
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 120; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90; easing.type: Easing.InCubic }
    }
}
