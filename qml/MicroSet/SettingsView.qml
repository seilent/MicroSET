pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: view

    property QtObject model: null
    property bool animated: true
    property bool overlay: false
    readonly property int accentHue: view.model !== null && view.model.accentHue >= 0
        ? Ui.hueOnGrid(view.model.accentHue) : Ui.accentHueDefault
    property alias stack: stack
    property alias toast: toast

    signal hideRequested()

    function disarm(): void {
        if (stack.depth > 0)
            stack.pages[0].content.hideArmed = false
    }

    function open(group: string): void {
        stack.push(settingsPage, { model: view.model, group: group, animated: view.animated })
    }

    Component.onCompleted: {
        Ui.accent = Qt.binding(() => Ui.accentForHue(view.accentHue))
        Ui.accentBright = Qt.binding(() => Ui.brightForHue(view.accentHue))
        stack.push(groupsPage, { model: view.model, animated: view.animated, overlay: view.overlay })
    }

    PageStack {
        id: stack

        anchors.fill: parent
        animate: view.animated
    }

    Toast {
        id: toast

        x: 0
        y: 0
        width: view.width
    }

    Component {
        id: groupsPage

        GroupsPage {
            onOpened: (group) => view.open(group)
            onHideRequested: view.hideRequested()
        }
    }

    Component {
        id: settingsPage

        SettingsPage {
            onOpened: (group) => view.open(group)
        }
    }

    Connections {
        target: view.model
        ignoreUnknownSignals: true

        function onRefused(title, error) {
            toast.show(title, error)
        }
    }
}
