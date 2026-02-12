import QtQuick

Item {
    id: root

    required property var plot_widget

    property var selected_sample: null
    property bool show_selected_x: true
    property bool show_selected_y: true
    property color selected_color: "#638ceb"
    property bool show_vertical_line: true
    property bool show_horizontal_line: false
    property bool link_indicator: false
    property string x_value_label: "x"
    property string y_value_label: "y"

    readonly property var time_axis: plot_widget ? plot_widget.timeAxis : null

    readonly property real usable_width: width - plot_widget.vbar_width_qml
    readonly property real usable_height: height - plot_widget.reserved_height

    readonly property var samples: internal.indicator_samples

    function update_mouse_position(x, y) {
        internal.has_mouse_in_plot = true
        internal.mouse_x = x
        internal.mouse_y = y
        internal.in_main_plot_at_move =
            x >= 0 && x <= usable_width &&
            y >= 0 && y < usable_height
        refresh_indicator()
    }

    function set_mouse_in_plot(in_plot) {
        var was_in_plot = internal.has_mouse_in_plot
        internal.has_mouse_in_plot = in_plot
        if (!in_plot) {
            internal.mouse_x = -1
            internal.mouse_y = -1
            internal.in_main_plot_at_move = false
        }
        if (was_in_plot && !in_plot && root.link_indicator && root.time_axis) {
            root.time_axis.set_indicator_state(plot_widget, false, 0.0, 0.0)
        }
        canvas.requestPaint()
    }

    QtObject {
        id: internal
        property bool has_mouse_in_plot: false
        property var indicator_samples: []
        property real mouse_x: -1
        property real mouse_y: -1
        property bool indicator_active: false
        property bool in_main_plot_at_move: false
        property var last_samples: []
        property real last_samples_time_ms: 0.0
    }

    Connections {
        target: plot_widget
        function onV_limits_changed() { refresh_indicator() }
        function onT_limits_changed() { refresh_indicator() }
    }

    Connections {
        target: root.link_indicator ? root.time_axis : null
        function onIndicator_state_changed() { refresh_indicator() }
    }

    onSelected_sampleChanged: canvas.requestPaint()
    onShow_selected_xChanged: canvas.requestPaint()
    onShow_selected_yChanged: canvas.requestPaint()
    onSelected_colorChanged: canvas.requestPaint()
    onShow_vertical_lineChanged: canvas.requestPaint()
    onShow_horizontal_lineChanged: canvas.requestPaint()

    function decimals_for_span(span, n) {
        if (!isFinite(span) || span === 0) {
            return n
        }
        var abs_span = Math.abs(span)
        var raw = -Math.log10(abs_span)
        var i = Math.max(0, Math.ceil(raw))
        if (!isFinite(i) || i > 20) i = 20
        return i + n - 1
    }

    function format_timestamp(x_val, tspan) {
        try {
            var formatted = plot_widget.format_timestamp_precise(x_val)
            if (formatted)
                return formatted
        } catch (e) {}
        var drx = Math.max(3, root.decimals_for_span(tspan, 3))
        return x_val.toFixed(drx)
    }

    function label_prefix(label, fallback) {
        var txt = (label === undefined || label === null) ? "" : ("" + label).trim()
        if (txt.length === 0) {
            txt = fallback
        }
        txt = txt.replace(/\s*:\s*$/, "")
        return txt + ": "
    }

    function labeled_x_value(value_text) {
        return root.label_prefix(root.x_value_label, "x") + value_text
    }

    function labeled_series_value(series_label, value_text) {
        var txt = (series_label === undefined || series_label === null) ? "" : ("" + series_label).trim()
        var label = (txt.length > 0) ? txt : root.y_value_label
        return root.label_prefix(label, "y") + value_text
    }

    function refresh_indicator() {
        var in_main_plot = internal.has_mouse_in_plot
            && internal.in_main_plot_at_move
            && internal.mouse_y >= 0 && internal.mouse_y < usable_height

        var tmin = plot_widget.t_min
        var tmax = plot_widget.t_max
        var tspan = tmax - tmin
        if (tspan <= 0 || usable_width <= 0 || usable_height <= 0) {
            internal.indicator_samples = []
            internal.indicator_active = false
            canvas.requestPaint()
            return
        }

        var local_t = 0.0
        var local_px = -1.0
        var local_x_norm = 0.0
        if (in_main_plot) {
            local_px = Math.max(0.0, Math.min(internal.mouse_x, usable_width))
            local_x_norm = local_px / usable_width
            local_t = tmin + local_x_norm * tspan
        }

        if (!in_main_plot && internal.has_mouse_in_plot && root.link_indicator && root.time_axis) {
            if (root.time_axis.indicator_active &&
                root.time_axis.indicator_owned_by(plot_widget)) {
                root.time_axis.set_indicator_state(plot_widget, false, 0.0, 0.0)
            }
        }

        var allow_shared = !(internal.has_mouse_in_plot && !in_main_plot)
        var use_shared = root.link_indicator &&
            root.time_axis &&
            root.time_axis.indicator_active &&
            root.time_axis.indicator_x_norm_valid &&
            allow_shared
        var target_t = in_main_plot ? local_t : (use_shared ? root.time_axis.indicator_t : null)
        var shared_px = -1.0
        if (!in_main_plot && use_shared) {
            var shared_norm = Math.max(0.0, Math.min(root.time_axis.indicator_x_norm, 1.0))
            shared_px = shared_norm * usable_width
        }
        if (target_t === null || target_t === undefined) {
            internal.indicator_samples = []
            internal.indicator_active = false
            canvas.requestPaint()
            return
        }

        var now_ms = Date.now()
        var next_samples = plot_widget.get_indicator_samples(
            target_t, usable_width, usable_height, in_main_plot ? local_px : shared_px)

        if (in_main_plot && root.link_indicator && root.time_axis) {
            var publish_t = local_t
            if (next_samples.length > 0) {
                var resolved_t = next_samples[0].x
                if (resolved_t !== undefined && resolved_t !== null && isFinite(resolved_t)) {
                    publish_t = resolved_t
                }
            }
            if (publish_t !== undefined && publish_t !== null && isFinite(publish_t)) {
                root.time_axis.set_indicator_state(plot_widget, true, publish_t, local_x_norm)
            }
        }

        if (next_samples.length > 0) {
            internal.indicator_samples = next_samples
            internal.indicator_active = true
            internal.last_samples = next_samples
            internal.last_samples_time_ms = now_ms
        } else {
            var grace_ms = 120
            var can_reuse = internal.last_samples.length > 0
                && (now_ms - internal.last_samples_time_ms) <= grace_ms
            if (can_reuse) {
                internal.indicator_samples = internal.last_samples
                internal.indicator_active = true
            } else {
                internal.indicator_samples = []
                internal.indicator_active = false
            }
        }
        canvas.requestPaint()
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.bottomMargin: plot_widget.reserved_height
        anchors.rightMargin: plot_widget.vbar_width_qml
        renderTarget: Canvas.FramebufferObject
        renderStrategy: Canvas.Threaded
        z: 100

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.lineWidth = 1

            var tspan = plot_widget.t_max - plot_widget.t_min
            var vspan = plot_widget.v_max - plot_widget.v_min
            var dry = Math.max(3, root.decimals_for_span(vspan, 3))
            var pi = Math.PI
            var bullet_char = "\u2022"

            if (internal.indicator_active && internal.indicator_samples.length > 0) {
                var samples = internal.indicator_samples
                var x_line = samples[0].px
                var x_val = samples[0].x
                var y_line = samples[0].py

                ctx.strokeStyle = "#ffffff"
                if (root.show_vertical_line) {
                    ctx.beginPath()
                    ctx.moveTo(x_line, 0)
                    ctx.lineTo(x_line, height)
                    ctx.stroke()
                    ctx.closePath()
                }
                if (root.show_horizontal_line && samples.length === 1) {
                    ctx.beginPath()
                    ctx.moveTo(0, y_line)
                    ctx.lineTo(width, y_line)
                    ctx.stroke()
                    ctx.closePath()
                }

                var x_txt = root.format_timestamp(x_val, tspan)
                var x_axis_txt = root.labeled_x_value(x_txt)
                var lines = []
                var max_value_width = ctx.measureText(x_axis_txt).width

                for (var i = 0; i < samples.length; ++i) {
                    var s = samples[i]
                    var vtxt = s.y.toFixed(dry)
                    var value_label = root.labeled_series_value(s.series_label, vtxt)
                    var w = ctx.measureText(value_label).width
                    if (w > max_value_width) max_value_width = w

                    lines.push({
                        value: value_label,
                        color: s.color,
                        px: s.px,
                        py: s.py
                    })
                }

                var show_bullet = (lines.length > 1)
                var line_height = 18
                var box_padding_x = 8
                var box_padding_y = 6
                var bullet_width = show_bullet ? ctx.measureText(bullet_char).width + 4 : 0
                var text_width = Math.max(ctx.measureText(x_axis_txt).width, bullet_width + max_value_width)
                var box_width = text_width + box_padding_x * 2

                var x0 = (x_line > width / 2) ? x_line - 10 - box_width : x_line + 10
                var x1 = x0 + box_width
                var y0 = 10
                var y1 = y0 + box_padding_y * 2 + line_height * (lines.length + 1)

                ctx.strokeStyle = "#ffffff"
                ctx.fillStyle = "#ccdadada"
                ctx.beginPath()
                ctx.moveTo(x0, y0)
                ctx.lineTo(x1, y0)
                ctx.lineTo(x1, y1)
                ctx.lineTo(x0, y1)
                ctx.lineTo(x0, y0)
                ctx.fill()
                ctx.closePath()

                ctx.fillStyle = "#000000"
                ctx.beginPath()
                ctx.fillText(x_axis_txt, x0 + box_padding_x, y0 + box_padding_y + line_height)
                ctx.fill()
                ctx.closePath()

                for (var li = 0; li < lines.length; ++li) {
                    var ly = y0 + box_padding_y + line_height * (li + 2)
                    var line = lines[li]

                    if (show_bullet) {
                        ctx.fillStyle = line.color
                        ctx.beginPath()
                        ctx.fillText(bullet_char, x0 + box_padding_x, ly)
                        ctx.fill()
                        ctx.closePath()
                    }

                    ctx.fillStyle = "#000000"
                    ctx.beginPath()
                    ctx.fillText(line.value, x0 + box_padding_x + bullet_width, ly)
                    ctx.fill()
                    ctx.closePath()
                }

                for (var di = 0; di < lines.length; ++di) {
                    var sx = lines[di].px
                    var sy = lines[di].py
                    ctx.strokeStyle = "#ffffffff"
                    ctx.fillStyle = lines[di].color
                    ctx.beginPath()
                    ctx.arc(sx, sy, 4, 0, 2 * pi, false)
                    ctx.fill()
                    ctx.stroke()
                    ctx.closePath()
                }
            }

            if (!internal.indicator_active && root.selected_sample) {
                var ts_sel = root.selected_sample.x
                var v_sel = root.selected_sample.y

                if (ts_sel !== undefined && v_sel !== undefined) {
                    var x_sel = 0
                    var y_sel = 0
                    if (tspan > 0 && vspan > 0) {
                        x_sel = (ts_sel - plot_widget.t_min) / tspan * width
                        y_sel = (1.0 - (v_sel - plot_widget.v_min) / vspan) * height
                    }
                    else {
                        x_sel = root.selected_sample.px
                        y_sel = root.selected_sample.py
                    }

                    if (x_sel === undefined || y_sel === undefined) {
                        return
                    }

                    x_sel = Math.max(0, Math.min(x_sel, width - 1))
                    y_sel = Math.max(0, Math.min(y_sel, height - 1))

                    var x_txt_sel = root.format_timestamp(ts_sel, tspan)
                    var y_txt_sel = v_sel.toFixed(dry)
                    var y_labeled_sel = root.labeled_series_value(root.selected_sample.series_label, y_txt_sel)

                    var txt_sel = ""
                    var dirty_sel = false
                    if (root.show_selected_x) {
                        txt_sel = root.labeled_x_value(x_txt_sel)
                        dirty_sel = true
                    }
                    if (root.show_selected_y) {
                        txt_sel += (dirty_sel ? ", " : "") + y_labeled_sel
                    }

                    var text_width_sel = ctx.measureText(txt_sel).width
                    ctx.strokeStyle = "#ffffff"
                    ctx.fillStyle = "#ccdadada"
                    ctx.beginPath()

                    var box_pad_sel_x = 5
                    var box_width_sel = text_width_sel + box_pad_sel_x * 2
                    var x0s = (x_sel > width / 2) ? x_sel - 10 - box_width_sel : x_sel + 10
                    var x1s = x0s + box_width_sel
                    var y0s = 10
                    var y1s = 30

                    ctx.moveTo(x0s, y0s)
                    ctx.lineTo(x1s, y0s)
                    ctx.lineTo(x1s, y1s)
                    ctx.lineTo(x0s, y1s)
                    ctx.lineTo(x0s, y0s)
                    ctx.fill()
                    ctx.closePath()

                    ctx.fillStyle = "#000000"
                    ctx.fillText(txt_sel, x0s + box_pad_sel_x, y0s + 15)

                    ctx.strokeStyle = "#ffffffff"
                    ctx.fillStyle = root.selected_color
                    ctx.beginPath()
                    ctx.arc(x_sel, y_sel, 4, 0, 2 * pi, false)
                    ctx.fill()
                    ctx.stroke()
                    ctx.closePath()
                }
            }
        }
    }
}
