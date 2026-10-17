pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: groups

    property bool overlay: false
    property bool hideArmed: false

    signal hideRequested()

    title: "settings"
    modelRows: groups.model === null ? [] : groups.model.groups(groups.model.revision)

    function press(name: string) {
        if (name === "a")
            groups.pressed = true
        if (name === "b")
            groups.hideArmed = groups.overlay && !groups.editing
    }

    function release(name: string) {
        if (name === "a")
            groups.pressed = false
        if (name !== "b")
            return
        const armed = groups.hideArmed
        groups.hideArmed = false
        if (armed && groups.overlay && !groups.editing)
            groups.hideRequested()
    }
}
