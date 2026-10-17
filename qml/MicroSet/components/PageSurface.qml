pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    readonly property real topAlpha: Ui.backdropStops[0][1]
    property alias shade: shade

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    Rectangle {
        id: shade

        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: Qt.rgba(Ui.accent.r, Ui.accent.g, Ui.accent.b, root.topAlpha) }
            GradientStop { position: 1; color: Qt.rgba(Ui.accent.r, Ui.accent.g, Ui.accent.b, 0) }
        }
    }
}
