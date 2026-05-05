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

    function commit_editor_text() {
        if (contentItem.acceptableInput) {
            value = valueFromText(contentItem.text, locale)
            return true
        }
        contentItem.text = textFromValue(value, locale)
        return false
    }

    function step_by(delta) {
        if (!commit_editor_text()) {
            return
        }
        value = Math.max(from, Math.min(to, value + delta))
        valueModified()
    }

    Keys.onUpPressed: function(event) {
        step_by(stepSize)
        event.accepted = true
    }

    Keys.onDownPressed: function(event) {
        step_by(-stepSize)
        event.accepted = true
    }

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

        Keys.onUpPressed: function(event) {
            root.step_by(root.stepSize)
            event.accepted = true
        }

        Keys.onDownPressed: function(event) {
            root.step_by(-root.stepSize)
            event.accepted = true
        }
    }

    up.indicator: Rectangle {
        x: parent.width - width
        width: root.indicatorWidth
        height: parent.height / 2
        color: upArea.pressed ? root.buttonPressed : upArea.containsMouse ? root.buttonHover : root.surfaceBright
        border.color: root.surfaceLight

        Text {
            anchors.centerIn: parent
            text: "+"
            color: root.textColor
            font.pixelSize: root.indicatorFontPixelSize
        }

        MouseArea {
            id: upArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.step_by(root.stepSize)
        }
    }

    down.indicator: Rectangle {
        x: parent.width - width
        y: parent.height / 2
        width: root.indicatorWidth
        height: parent.height / 2
        color: downArea.pressed ? root.buttonPressed : downArea.containsMouse ? root.buttonHover : root.surfaceBright
        border.color: root.surfaceLight

        Text {
            anchors.centerIn: parent
            text: "-"
            color: root.textColor
            font.pixelSize: root.indicatorFontPixelSize
        }

        MouseArea {
            id: downArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.step_by(-root.stepSize)
        }
    }

    background: Rectangle {
        color: root.surfaceBright
        border.color: root.surfaceLight
        border.width: 1
        radius: 2
    }
}
