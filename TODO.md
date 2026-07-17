# TODO

## Make horizontal time labels distinguishable

### Problem

`default_format_timestamp()` currently discards the sub-second part of a
timestamp and formats every tick below one minute as `HH:MM:SS`. When the
horizontal tick interval is less than one second, distinct ticks therefore
receive identical labels. For example, ticks at 0.0, 0.2, 0.4, 0.6, and 0.8
seconds are all displayed as `00:00:00`.

This exposes two separate issues:

1. The default timestamp formatter does not provide the step-appropriate
   precision promised by its contract.
2. The layout assumes that distinct tick coordinates always produce distinct
   formatted text. Custom formatters can violate that assumption even after
   the default formatter is corrected.

### Required behavior

- Choose the default timestamp precision from `step_ns`. Sub-second tick
  intervals must include enough fractional-second precision to distinguish
  adjacent labels.
- After all horizontal labels have been formatted with the final accepted tick
  step, suppress consecutive labels whose rendered text is identical.
- Retain the first label encountered in the configured axis direction. This is
  the leftmost label for a left-to-right axis and the rightmost label for a
  right-to-left axis.
- Compare the final rendered strings, not the underlying numeric coordinates.
- Do not change the grid inside the plot body. Grid density and positions must
  remain independent of label suppression.
- When a duplicate label is suppressed, also suppress the corresponding grid
  line extension through the horizontal label pane.
- Do not suppress nonconsecutive equal strings. This rule relies on progressive
  axis formatting, for which equal labels form one consecutive run.

### Acceptance examples

- A 0-to-2-second view with 0.2-second ticks displays distinguishable
  fractional-second labels rather than repeated `00:00:00` and `00:00:01`
  strings.
- If a custom formatter maps several consecutive ticks to the same text, only
  one copy is visible. A left-to-right axis retains the leftmost copy; a
  right-to-left axis retains the rightmost copy.
- Suppressed labels have no grid extension in the horizontal label pane, while
  all corresponding grid lines remain visible in the plot body.
- Labels that already differ are unaffected.
