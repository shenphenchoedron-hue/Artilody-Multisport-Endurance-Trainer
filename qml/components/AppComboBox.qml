import QtQuick
import QtQuick.Controls

ComboBox {
    id: combo
    property string prefix
    property string iconName
    property color accentColor: "transparent"   // filled style (RPE dropdown)
    property real fontSize: 17
    // Filled style is active whenever a non-transparent accent colour is set.
    // (Plain alpha check instead of Qt.color() so it also works with Qt 6.4.)
    readonly property bool filled: accentColor.a > 0

    implicitHeight: 50
    font.pixelSize: fontSize

    contentItem: Row {
        leftPadding: combo.iconName.length > 0 ? 18 : 16
        spacing: 12
        Icon {
            visible: combo.iconName.length > 0
            name: combo.iconName
            size: 24
            color: Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: combo.prefix + combo.displayText
            color: combo.filled ? Theme.textOnColor : Theme.text
            font.pixelSize: combo.fontSize
            font.weight: combo.filled ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            width: combo.width - 70 - (combo.iconName.length > 0 ? 36 : 0)
        }
    }

    indicator: Icon {
        name: "chevronDown"
        size: 20
        color: combo.filled ? Theme.textOnColor : Theme.text
        x: combo.width - width - 16
        anchors.verticalCenter: parent.verticalCenter
    }

    background: Rectangle {
        radius: 10
        color: combo.filled ? combo.accentColor : Theme.field
        border.width: combo.filled ? 0 : 1
        border.color: combo.activeFocus ? Theme.accentLight : Theme.fieldBorder
    }

    delegate: ItemDelegate {
        width: combo.width
        height: 42
        highlighted: combo.highlightedIndex === index
        contentItem: Text {
            // Works for JS arrays, C++ QStringList/QVariantList (of strings or maps) and
            // item models. A QVariantList from C++ is not a JS Array, so look the role up
            // on modelData first, then on the model roles, then ask the ComboBox itself.
            text: {
                if (!combo.textRole)
                    return modelData !== undefined ? modelData : combo.textAt(index)
                var v = (modelData !== null && typeof modelData === "object") ? modelData[combo.textRole] : undefined
                if (v === undefined && model)
                    v = model[combo.textRole]
                return v !== undefined ? v : combo.textAt(index)
            }
            color: Theme.text
            font.pixelSize: 15
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: parent.highlighted ? Theme.navSelected : "transparent"
            radius: 6
        }
    }

    popup: Popup {
        y: combo.height + 4
        width: combo.width
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 360)
        padding: 6
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: 10
            color: Theme.dark ? "#22327c" : "#ffffff"
            border.color: Theme.cardBorder
        }
    }
}
