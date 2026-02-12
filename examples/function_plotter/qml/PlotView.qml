import QtQuick
import QtQuick.Controls
import QtQuick.Window
import VnmPlot 1.0 as VnmPlotLib

Item {
    id: control

    property alias plot_widget: libPlotView.plot_widget
    property var time_axis: null

    property real preview_height: 0
    property real preview_height_target: 0
    property real preview_height_min: 0
    property real preview_height_max: 0

    property real screen_scaling_trigger: Screen.pixelDensity
    onScreen_scaling_triggerChanged: {
        libPlotView.plot_widget.update_dpi_scaling_factor()
    }

    NumberAnimation {
        id: preview_height_anim
        target: control
        property: "preview_height"
        easing.type: Easing.Linear
        running: false
    }

    Connections {
        target: libPlotView.plot_widget

        function onPreview_height_target_changed(target) {
            if (preview_height_min === 0 && preview_height_max === 0 && preview_height === 0) {
                preview_height = target
                preview_height_target = target
                preview_height_min = target
                preview_height_max = target
                libPlotView.plot_widget.preview_height = preview_height
                return
            }

            preview_height_target = target

            if (preview_height_min === 0 || target < preview_height_min)
                preview_height_min = target
            if (target > preview_height_max)
                preview_height_max = target

            var range = Math.max(1, preview_height_max - preview_height_min)
            var dist = Math.abs(preview_height - target)
            var frac = dist / range
            var dur = 250 * frac

            if (dur <= 0.5) {
                preview_height = target
                libPlotView.plot_widget.preview_height = preview_height
                preview_height_anim.stop()
                return
            }

            preview_height_anim.stop()
            preview_height_anim.from = preview_height
            preview_height_anim.to = target
            preview_height_anim.duration = dur
            preview_height_anim.start()
        }
    }

    onPreview_heightChanged: {
        libPlotView.plot_widget.preview_height = preview_height
    }

    VnmPlotLib.PlotView {
        id: libPlotView
        anchors.fill: parent
        dark_mode: true
        time_axis: control.time_axis
    }

    Component.onCompleted: {
        preview_height = libPlotView.plot_widget.preview_height
        preview_height_target = libPlotView.plot_widget.preview_height_target
        if (preview_height_target > 0) {
            preview_height_min = preview_height_target
            preview_height_max = preview_height_target
        }
    }
}
