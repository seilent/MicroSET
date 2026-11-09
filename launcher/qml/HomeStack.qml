import QtQuick
import MicroSet

Item {
    id: stack

    property Item home: null
    property Item allApps: null
    property string page: "home"

    readonly property Item currentItem: stack.page === "allapps" ? stack.allApps : stack.home
    readonly property int depth: stack.page === "allapps" ? 2 : 1
    readonly property bool inputLocked: false
}
