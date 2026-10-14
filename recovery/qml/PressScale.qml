pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool pressed: false
    property bool animated: true

    transformOrigin: Item.Center
    scale: root.pressed ? Motion.pressScale : 1

    Behavior on scale {
        enabled: root.animated

        NumberAnimation {
            duration: Motion.press
            easing.type: Motion.linear
        }
    }
}
