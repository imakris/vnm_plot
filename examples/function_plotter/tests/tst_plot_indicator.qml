import QtQuick
import QtTest
import "../../../qml/VnmPlot" as VnmPlot

TestCase {
    name: "PlotIndicator"
    when: windowShown
    width: 200
    height: 100
    visible: true

    QtObject {
        id: plotWidget
        property real vbar_width_qml: 0
        property real reserved_height: 0
        property real t_min: 0
        property real t_max: 1
        property real v_min: 0
        property real v_max: 1
    }

    VnmPlot.PlotIndicator {
        id: indicator
        anchors.fill: parent
        plot_widget: plotWidget
    }

    QtObject {
        id: fixedWidthContext
        function measureText(text) { return { width: text.length } }
    }

    function test_cumulative_marker_note_visibility() {
        compare(indicator.cumulative_marker_note([{ series_label: "f(x1)" }]), "")
        compare(
            indicator.cumulative_marker_note([{ series_label: "f(x1)", stacked_marker: true }]),
            "Markers show cumulative stack positions")
    }

    function test_cumulative_marker_note_wraps_within_value_width() {
        var lines = indicator.wrap_text(
            fixedWidthContext, "Markers show cumulative stack positions", 12)
        verify(lines.length > 1)
        for (var i = 0; i < lines.length; ++i) {
            verify(fixedWidthContext.measureText(lines[i]).width <= 12)
        }
        compare(lines.length * 18, 72)
    }
}
