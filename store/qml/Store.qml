pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Window {
    id: root

    required property QtObject store
    property QtObject pad: null
    property QtObject accent: null
    property bool shotAccentOff: false
    readonly property int accentHue: root.accent !== null && root.accent.hue >= 0 ? root.accent.hue : Ui.accentHueDefault
    property bool animate: true

    readonly property bool shown: root.store !== null && root.store.shown

    function syncSurface(): void {
        Ui.setSurface(root.width, root.height)
    }

    function openList(): void {
        if (root.store !== null)
            root.store.back()
        stack.clear()
        stack.push(listPage, { store: root.store, keyboard: keyboard })
    }

    function openDetail(id: string): void {
        if (root.store === null)
            return
        root.store.open(id)
        stack.push(detailPage, { store: root.store })
    }

    function askUninstall(name: string): void {
        confirm.title = "uninstall " + name + "?"
        confirm.items = ["uninstall " + name, "keep " + name]
        confirm.open()
        confirm.focusIndex = 1
    }

    function confirmPicked(index: int): void {
        confirm.close()
        if (index === 0 && root.store !== null)
            root.store.uninstall()
    }

    function reset(): void {
        stack.clear()
        confirm.complete()
        confirm.close()
        keyboard.close()
    }

    visible: false
    color: Ui.bg
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "microset-store"

    onWidthChanged: root.syncSurface()
    onHeightChanged: root.syncSurface()

    Component.onCompleted: {
        Ui.accentHueColor = Qt.binding(() => Ui.accentForHue(root.accentHue))
        Ui.accentHueBright = Qt.binding(() => Ui.brightForHue(root.accentHue))
        Ui.accentOff = Qt.binding(() => root.shotAccentOff || (root.accent !== null && root.accent.off === true))
        root.syncSurface()
        root.openList()
    }

    PageStack {
        id: stack

        anchors.fill: parent
        animate: root.animate
    }

    Binding {
        target: stack
        property: "dialogFade"
        value: Math.max(confirm.scrim.opacity, keyboard.progress)
    }

    Binding {
        target: stack
        property: "dialogShown"
        value: confirm.shown
    }

    Component {
        id: listPage

        StoreList {
            onOpened: (id) => root.openDetail(id)
        }
    }

    Component {
        id: detailPage

        StoreDetail {
            onUninstallRequested: (name) => root.askUninstall(name)
        }
    }

    Keyboard {
        id: keyboard

        objectName: "keyboard"
        anchors.fill: parent
        screenOff: !root.shown
        animated: root.animate && root.shown
        toastShown: toast.shown
        onDone: (text) => {
            if (root.store !== null)
                root.store.search = text
        }
        onRefused: (title, reason) => toast.show(title, reason)
    }

    Dialog {
        id: confirm

        objectName: "confirm"
        anchors.fill: parent
        screenOff: !root.shown
        onAccepted: (index) => root.confirmPicked(index)
    }

    Toast {
        id: toast

        objectName: "toast"
        x: 0
        y: 0
        width: root.width
        screenOff: !root.shown
    }

    StoreRouter {
        id: router

        store: root.store
        pad: root.pad
        stack: stack
        modal: confirm.shown ? confirm : keyboard.shown ? keyboard : null
    }

    TouchLayer {
        id: touch

        anchors.fill: parent
        enabled: root.shown
        dialogs: [confirm, keyboard]
        router: router
    }

    Connections {
        target: root.store
        ignoreUnknownSignals: true

        function onShownChanged() {
            if (root.shown)
                root.openList()
            else
                root.reset()
        }

        function onToast(title, message) {
            toast.show(title, message)
        }
    }
}
