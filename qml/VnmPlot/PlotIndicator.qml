import QtQuick

Item {
    id: root

    required property var plotWidget

    readonly property real usableWidth: width - plotWidget.vbar_width_qml
    readonly property real usableHeight: height - plotWidget.reserved_height

    readonly property var samples: internal.indicatorSamples

    function updateMousePosition(x, y) {
        internal.hasMouseInPlot = true
        internal.mouseX = x
        internal.mouseY = y
        refreshIndicator()
    }

    QtObject {
        id: internal
        property bool hasMouseInPlot: false
        property var indicatorSamples: []
        property real mouseX: -1
        property real mouseY: -1
    }

    Connections {
        target: plotWidget
        function onV_limits_changed() { refreshIndicator() }
        function onT_limits_changed() { refreshIndicator() }
    }

    function decimalsForSpan(span, n) {
        if (!isFinite(span) || span === 0) {
            return n
        }
        var absSpan = Math.abs(span)
        var raw = -Math.log10(absSpan)
        var i = Math.max(0, Math.ceil(raw))
        if (!isFinite(i) || i > 20) i = 20
        return i + n - 1
    }

    function refreshIndicator() {
        var inMainPlot = internal.hasMouseInPlot
            && internal.mouseX >= 0 && internal.mouseX <= usableWidth
            && internal.mouseY >= 0 && internal.mouseY < usableHeight

        if (!inMainPlot) {
            internal.indicatorSamples = []
            canvas.requestPaint()
            return
        }

        var tmin = plotWidget.t_min
        var tmax = plotWidget.t_max
        var tspan = tmax - tmin
        if (tspan <= 0 || usableWidth <= 0 || usableHeight <= 0) {
            internal.indicatorSamples = []
            canvas.requestPaint()
            return
        }

        var xVal = tmin + (internal.mouseX / usableWidth) * tspan
        internal.indicatorSamples = plotWidget.get_indicator_samples(xVal, usableWidth, usableHeight)
        canvas.requestPaint()
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.bottomMargin: plotWidget.reserved_height
        anchors.rightMargin: plotWidget.vbar_width_qml
        renderTarget: Canvas.FramebufferObject
        renderStrategy: Canvas.Threaded
        z: 100

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.lineWidth = 1

            if (!internal.hasMouseInPlot || internal.indicatorSamples.length === 0) {
                return
            }

            var samples = internal.indicatorSamples
            if (samples.length === 0) {
                return
            }

            var xLine = samples[0].px
            var xVal = samples[0].x

            ctx.strokeStyle = "#ffffff"
            ctx.beginPath()
            ctx.moveTo(xLine, 0)
            ctx.lineTo(xLine, height)
            ctx.stroke()
            ctx.closePath()

            var tspan = plotWidget.t_max - plotWidget.t_min
            var vspan = plotWidget.v_max - plotWidget.v_min
            var dry = Math.max(3, root.decimalsForSpan(vspan, 3))
            var drx = Math.max(3, root.decimalsForSpan(tspan, 3))
            var x_txt = xVal.toFixed(drx)
            var PI = Math.PI
            var bulletChar = "\u2022"

            var lines = []
            var maxValueWidth = ctx.measureText(x_txt).width

            for (var i = 0; i < samples.length; ++i) {
                var s = samples[i]
                var vtxt = s.y.toFixed(dry)
                var w = ctx.measureText(vtxt).width
                if (w > maxValueWidth) maxValueWidth = w

                lines.push({
                    value: vtxt,
                    color: s.color,
                    px: s.px,
                    py: s.py
                })
            }

            var showBullet = (lines.length > 1)
            var lineHeight = 18
            var boxPaddingX = 8
            var boxPaddingY = 6
            var bulletWidth = showBullet ? ctx.measureText(bulletChar).width + 4 : 0
            var textWidth = Math.max(ctx.measureText(x_txt).width, bulletWidth + maxValueWidth)

            var x0 = (xLine > width / 2) ? xLine - 10 - textWidth - boxPaddingX * 2 : xLine + 10
            var x1 = x0 + textWidth + boxPaddingX * 2
            var y0 = 10
            var y1 = y0 + boxPaddingY * 2 + lineHeight * (lines.length + 1)

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
            ctx.fillText(x_txt, x0 + boxPaddingX, y0 + boxPaddingY + lineHeight)
            ctx.fill()
            ctx.closePath()

            for (var li = 0; li < lines.length; ++li) {
                var ly = y0 + boxPaddingY + lineHeight * (li + 2)
                var line = lines[li]

                if (showBullet) {
                    ctx.fillStyle = line.color
                    ctx.beginPath()
                    ctx.fillText(bulletChar, x0 + boxPaddingX, ly)
                    ctx.fill()
                    ctx.closePath()
                }

                ctx.fillStyle = "#000000"
                ctx.beginPath()
                ctx.fillText(line.value, x0 + boxPaddingX + bulletWidth, ly)
                ctx.fill()
                ctx.closePath()
            }

            for (var di = 0; di < lines.length; ++di) {
                var sx = lines[di].px
                var sy = lines[di].py
                ctx.strokeStyle = "#ffffffff"
                ctx.fillStyle = lines[di].color
                ctx.beginPath()
                ctx.arc(sx, sy, 4, 0, 2 * PI, false)
                ctx.fill()
                ctx.stroke()
                ctx.closePath()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton

        onEntered: {
            internal.hasMouseInPlot = true
        }

        onExited: {
            internal.hasMouseInPlot = false
            internal.mouseX = -1
            internal.mouseY = -1
            canvas.requestPaint()
        }

        onPositionChanged: function(mouse) {
            internal.mouseX = mouse.x
            internal.mouseY = mouse.y
            root.refreshIndicator()
        }
    }
}
