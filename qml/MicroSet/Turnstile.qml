import QtQuick

Item {
    id: root

    property real angle: 0
    property real hinge: 0
    property int axis: Qt.YAxis
    readonly property real perspective: Motion.perspective(root.width)

    function projectedW(x: real, y: real): real {
        const r = root.angle * Math.PI / 180
        const d = root.perspective > 0 ? root.perspective : 1
        if (root.axis === Qt.XAxis)
            return 1 - (y - root.hinge) * Math.sin(r) / d
        return 1 + (x - root.hinge) * Math.sin(r) / d
    }

    function matrix() {
        const r = root.angle * Math.PI / 180
        const c = Math.cos(r)
        const sn = Math.sin(r)
        const d = root.perspective > 0 ? root.perspective : 1
        if (root.axis === Qt.XAxis) {
            const toHinge = Qt.matrix4x4(1, 0, 0, 0, 0, 1, 0, root.hinge, 0, 0, 1, 0, 0, 0, 0, 1)
            const rot = Qt.matrix4x4(1, 0, 0, 0, 0, c, -sn, 0, 0, sn, c, 0, 0, -sn / d, -c / d, 1)
            const fromHinge = Qt.matrix4x4(1, 0, 0, 0, 0, 1, 0, -root.hinge, 0, 0, 1, 0, 0, 0, 0, 1)
            return toHinge.times(rot).times(fromHinge)
        }
        const toHingeX = Qt.matrix4x4(1, 0, 0, root.hinge, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        const rotY = Qt.matrix4x4(c, 0, sn, 0, 0, 1, 0, 0, -sn, 0, c, 0, sn / d, 0, -c / d, 1)
        const fromHingeX = Qt.matrix4x4(1, 0, 0, -root.hinge, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        return toHingeX.times(rotY).times(fromHingeX)
    }

    readonly property matrix4x4 currentMatrix: root.matrix()

    transform: Matrix4x4 {
        matrix: root.currentMatrix
    }
}
