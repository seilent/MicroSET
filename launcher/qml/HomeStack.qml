import QtQuick
import MicroSet

FocusList {
    id: stack

    property Item currentItem: null
    readonly property int depth: 1
    interactive: false
    visible: false
}
