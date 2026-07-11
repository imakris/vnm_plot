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

    function test_cumulative_marker_note_visibility() {
        compare(indicator.cumulative_marker_note([{ series_label: "f(x1)" }]), "")
        compare(
            indicator.cumulative_marker_note([{ series_label: "f(x1)", stacked_marker: true }]),
            "Markers show cumulative stack positions")
    }
}
