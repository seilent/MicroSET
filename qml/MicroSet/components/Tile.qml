pragma ComponentBehavior: Bound
import QtQuick

PressScale {
    id: root

    property real size: Ui.tileS
    property url source: ""
    property string title: ""
    property string subtitle: ""
    property bool focused: false
    property bool captioned: true

    readonly property real borderWidth: Ui.px(Ui.focusBorder)
    readonly property real captionAlpha: root.focused ? Ui.alphaFull : Ui.gridCaption.alpha
    readonly property Item captionItem: caption
    readonly property Item titleItem: titleText
    readonly property Item subtitleItem: subtitleText

    width: Ui.px(root.size)
    height: Ui.px(root.size)

    Rectangle {
        anchors.fill: parent
        color: Ui.tile

        Image {
            anchors.fill: parent
            source: root.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            mipmap: false
            sourceSize: Qt.size(root.width, root.height)
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -root.borderWidth
        color: "transparent"
        border.width: root.borderWidth
        border.color: Ui.textFull
        visible: root.focused
    }

    Item {
        id: caption

        visible: root.captioned
        x: 0
        y: root.height
        width: root.width
        height: Ui.px(Ui.tileCaptionBand)
        clip: true

        Text {
            id: titleText

            anchors.baseline: parent.top
            anchors.baselineOffset: Ui.px(Ui.tileCaptionDrop)
            width: parent.width
            text: root.title
            color: Ui.white(root.captionAlpha)
            font: Ui.font(Ui.gridCaption)

            Behavior on color {
                enabled: root.animated
                ColorAnimation {
                    duration: Motion.focusAlpha
                }
            }
        }

        Text {
            id: subtitleText

            anchors.baseline: parent.top
            anchors.baselineOffset: Ui.px(Ui.tileCaptionDrop + Ui.tileCaptionPitch)
            width: parent.width
            text: root.subtitle
            color: Ui.white(Ui.gridCaption.alpha)
            font: Ui.font(Ui.gridCaption)
        }
    }
}
