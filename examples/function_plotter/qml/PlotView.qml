import QtQuick
import QtQuick.Controls
import QtQuick.Window
import VnmPlot 1.0

Item {
    id: control

    property alias plot_widget: vnm_plot
    property bool has_mouse: false

    // Preview height animation state (DIP).
    property real preview_height: 0
    property real preview_height_target: 0
    property real preview_height_min: 0
    property real preview_height_max: 0

    // Smooth wheel-zoom state
    property real zoom_vel_t: 0.0
    property real zoom_vel_v: 0.0
    property real zoom_friction: 0.75
    property real zoom_impulse_per_step: 1.0
    property real zoom_max_vel: 5.0

    // Total zoom per wheel notch, used to derive base_k
    property real zoom_per_notch: 1.05
    property real base_k: Math.pow(zoom_per_notch,
                                   (1 - zoom_friction) / zoom_impulse_per_step)

    // Last pivot position used for zooming (normalized 0..1)
    property real last_px: 0.5
    property real last_py: 0.5

    readonly property real vbar_width_effective: vnm_plot.vbar_width_qml
    readonly property real usable_width: width - vbar_width_effective
    readonly property real usable_height: height - vnm_plot.reserved_height

    readonly property real t_available_span: Math.max(1e-9, vnm_plot.t_available_max - vnm_plot.t_available_min)
    readonly property real t_stop_min: (vnm_plot.t_min - vnm_plot.t_available_min) / t_available_span
    readonly property real t_stop_max: 1.0 - (vnm_plot.t_available_max - vnm_plot.t_max) / t_available_span

    property real screen_scaling_trigger: Screen.pixelDensity
    onScreen_scaling_triggerChanged: {
        vnm_plot.update_dpi_scaling_factor()
    }

    function apply_zoom_step() {
        const eps = 1e-3
        var active = false

        if (Math.abs(zoom_vel_t) > eps) {
            var factorT = Math.pow(base_k, zoom_vel_t)
            if (factorT < 1.0 && !vnm_plot.can_zoom_in()) {
                zoom_vel_t = 0
            }
            else {
                vnm_plot.adjust_t_from_pivot_and_scale(last_px, factorT)
                zoom_vel_t *= zoom_friction
                active = true
            }
        }

        if (Math.abs(zoom_vel_v) > eps) {
            var factorV = Math.pow(base_k, zoom_vel_v)
            vnm_plot.adjust_v_from_pivot_and_scale(last_py, factorV)
            zoom_vel_v *= zoom_friction
            active = true
        }

        if (!active) {
            zoom_timer.running = false
        }
    }

    Timer {
        id: zoom_timer
        interval: 16
        repeat: true
        running: false
        onTriggered: control.apply_zoom_step()
    }

    NumberAnimation {
        id: preview_height_anim
        target: control
        property: "preview_height"
        easing.type: Easing.Linear
        running: false
    }

    Connections {
        target: vnm_plot

        function onPreview_height_target_changed(target) {
            if (preview_height_min === 0 && preview_height_max === 0 && preview_height === 0) {
                preview_height = target
                preview_height_target = target
                preview_height_min = target
                preview_height_max = target
                vnm_plot.preview_height = preview_height
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
                vnm_plot.preview_height = preview_height
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
        vnm_plot.preview_height = preview_height
    }

    PlotWidget {
        id: vnm_plot
        anchors.fill: parent
        dark_mode: true
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true

        property bool dragging: false
        property real dragging_start: -1
        property real dragging_last_y: 0
        property bool dragging_preview: false
        property real dragging_preview_start: 0

        onEntered: { control.has_mouse = true }
        onExited: { control.has_mouse = false }

        onPressed: function(mouse) {
            if (mouse.button !== Qt.LeftButton) {
                return
            }

            if (mouse.y < control.usable_height) {
                dragging = true
                dragging_start = mouse.x
                dragging_last_y = mouse.y
            }
            else
            if (preview_height > 0 && mouse.y > height - preview_height) {
                if (mouse.x < width * t_stop_min || mouse.x > width * t_stop_max) {
                    vnm_plot.adjust_t_from_mouse_pos_on_preview(width, mouse.x)
                }
                dragging_preview = true
                dragging_preview_start = mouse.x
            }
        }

        onReleased: function(mouse) {
            dragging = false
            dragging_preview = false
            dragging_start = -1
            dragging_preview_start = 0
        }

        onPositionChanged: function(mouse) {
            indicator.updateMousePosition(mouse.x, mouse.y)

            if (dragging) {
                const mods = mouse.modifiers
                const ctrlHeld = (mods & Qt.ControlModifier)
                const altHeld = (mods & Qt.AltModifier)

                // Vertical (value) panning: Ctrl or Alt
                if (ctrlHeld || altHeld) {
                    const dy = mouse.y - dragging_last_y
                    dragging_last_y = mouse.y
                    vnm_plot.adjust_v_from_mouse_diff(control.usable_height, dy)
                }

                // Horizontal (time) panning: always, unless Alt is held
                // (Ctrl allows both axes, Alt restricts to vertical only)
                if (!altHeld) {
                    vnm_plot.adjust_t_from_mouse_diff(control.usable_width, mouse.x - dragging_start)
                    dragging_start = mouse.x
                }
            }
            else
            if (dragging_preview) {
                vnm_plot.adjust_t_from_mouse_diff_on_preview(width, mouse.x - dragging_preview_start)
                dragging_preview_start = mouse.x
            }
        }

        function wheel_signed_delta(ev) {
            var dy = ev.angleDelta.y
            if (!dy && ev.pixelDelta) dy = ev.pixelDelta.y
            if (!dy) dy = ev.angleDelta.x
            if (!dy && ev.pixelDelta) dy = ev.pixelDelta.x
            return dy || 0
        }

        onWheel: function (wheel) {
            if (preview_height > 0 && wheel.y > height - (preview_height - 1)) {
                return
            }

            const mods = wheel.modifiers
            const dy = wheel_signed_delta(wheel)
            if (!dy)
                return

            const steps = dy / 120.0
            const impulse = -steps * control.zoom_impulse_per_step
            const zoom_time = (mods & Qt.ControlModifier) || !(mods & Qt.AltModifier)
            // Block zoom IN (impulse < 0) when at minimum range, but allow zoom OUT
            if (zoom_time && impulse < 0 && !vnm_plot.can_zoom_in())
                return

            const wx = (wheel.x > control.usable_width) ? control.usable_width : wheel.x
            const px = control.usable_width > 0 ? (wx / control.usable_width) : 0.5
            const py = control.usable_height > 0 ? (wheel.y / control.usable_height) : 0.5

            control.last_px = px
            control.last_py = py

            if (mods & Qt.ControlModifier) {
                control.zoom_vel_t += impulse
                control.zoom_vel_v += impulse
            }
            else
            if (mods & Qt.AltModifier) {
                control.zoom_vel_v += impulse
            }
            else {
                control.zoom_vel_t += impulse
            }

            control.zoom_vel_t = Math.max(-control.zoom_max_vel, Math.min(control.zoom_max_vel, control.zoom_vel_t))
            control.zoom_vel_v = Math.max(-control.zoom_max_vel, Math.min(control.zoom_max_vel, control.zoom_vel_v))

            control.apply_zoom_step()
            if (!zoom_timer.running)
                zoom_timer.running = true

            wheel.accepted = true
        }
    }

    PlotIndicator {
        id: indicator
        anchors.fill: parent
        plotWidget: vnm_plot
    }

    Component.onCompleted: {
        preview_height = vnm_plot.preview_height
        preview_height_target = vnm_plot.preview_height_target
        if (preview_height_target > 0) {
            preview_height_min = preview_height_target
            preview_height_max = preview_height_target
        }
    }
}
