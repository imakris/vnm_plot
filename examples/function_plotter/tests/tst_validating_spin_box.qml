import QtQuick
import QtTest
import "../qml"

TestCase {
    name: "ValidatingSpinBox"
    when: windowShown
    width: 200
    height: 100
    visible: true

    ValidatingSpinBox {
        id: spinBox
        from: 10
        to: 100
        value: 20
        width: 90
        height: 30
    }

    SignalSpy {
        id: valueModifiedSpy
        target: spinBox
        signalName: "valueModified"
    }

    function test_repeated_steps_follow_edited_value() {
        spinBox.value = 20

        spinBox.contentItem.forceActiveFocus()
        verify(spinBox.contentItem.activeFocus)
        keyClick(Qt.Key_A, Qt.ControlModifier)
        keyClick(Qt.Key_4)
        keyClick(Qt.Key_2)
        compare(spinBox.contentItem.text, "42")
        keyClick(Qt.Key_Return)
        compare(spinBox.value, 42)
        valueModifiedSpy.clear()

        mouseClick(spinBox, spinBox.width - spinBox.indicatorWidth / 2, spinBox.height / 4)
        compare(spinBox.value, 43)
        compare(spinBox.contentItem.text, "43")
        mouseClick(spinBox, spinBox.width - spinBox.indicatorWidth / 2, spinBox.height / 4)
        compare(spinBox.value, 44)
        compare(spinBox.contentItem.text, "44")

        mouseClick(spinBox, spinBox.width - spinBox.indicatorWidth / 2, spinBox.height * 3 / 4)
        mouseClick(spinBox, spinBox.width - spinBox.indicatorWidth / 2, spinBox.height * 3 / 4)
        compare(spinBox.value, 42)
        compare(spinBox.contentItem.text, "42")
        compare(valueModifiedSpy.count, 4)
    }
}
