pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Dialog {
    id: page

    property var rows: ["suspend", "reboot", "power off"]

    signal picked(string verb)
    signal dismissed()

    title: "power"
    items: page.rows

    onAccepted: page.choose()
    onRejected: page.dismissed()

    function choose(): void {
        if (page.focusIndex < 0 || page.focusIndex >= page.rows.length)
            return
        const verb = page.rows[page.focusIndex]
        page.close()
        page.picked(verb)
    }
}
