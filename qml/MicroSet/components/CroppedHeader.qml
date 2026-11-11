pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string text: ""
    property string form: "page"
    readonly property bool hub: root.form === "hub"
    property real penX: root.hub ? Ui.hubEdge : Ui.pageHeaderX
    property real alpha: root.hub ? Ui.hubHeader.alpha : Ui.pageHeader.alpha

    readonly property real textX: label.x
    readonly property real textBaseline: label.y + label.baselineOffset

    width: Ui.windowW
    height: Ui.px(root.hub ? Ui.headerBand : Ui.pivotTop)
    clip: true

    Text {
        id: label

        readonly property string touchKind: "header"

        x: Ui.px(root.penX)
        y: Ui.px(root.hub ? Ui.headerXHeightBottom : Ui.pageHeaderBaseline) - label.baselineOffset
        text: root.text
        textFormat: Text.PlainText
        color: Ui.white(root.alpha)
        font: Ui.font(root.hub ? Ui.hubHeader : Ui.pageHeader)
    }
}
