import QtQuick
import QtQuick.Controls.Basic

SpinBox {
    id: root

    property color textColor: "#e6e6e6"
    property color surfaceBright: "#2c2c2c"
    property color surfaceLight: "#353535"
    property color buttonHover: "#505050"
    property color buttonPressed: "#434343"
    property int indicatorWidth: 20
    property int indicatorFontPixelSize: 10

    editable: true

    contentItem: TextInput {
        text: root.textFromValue(root.value, root.locale)
        font: root.font
        color: root.textColor
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !root.editable
        validator: root.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
    }

    up.indicator: Rectangle {
        x: parent.width - width
        width: root.indicatorWidth
        height: parent.height / 2
        color: root.up.pressed ? root.buttonPressed : root.up.hovered ? root.buttonHover : root.surfaceBright
        border.color: root.surfaceLight

        Text {
            anchors.centerIn: parent
            text: "+"
            color: root.textColor
            font.pixelSize: root.indicatorFontPixelSize
        }
    }

    down.indicator: Rectangle {
        x: parent.width - width
        y: parent.height / 2
        width: root.indicatorWidth
        height: parent.height / 2
        color: root.down.pressed ? root.buttonPressed : root.down.hovered ? root.buttonHover : root.surfaceBright
        border.color: root.surfaceLight

        Text {
            anchors.centerIn: parent
            text: "-"
            color: root.textColor
            font.pixelSize: root.indicatorFontPixelSize
        }
    }

    background: Rectangle {
        color: root.surfaceBright
        border.color: root.surfaceLight
        border.width: 1
        radius: 2
    }
}
