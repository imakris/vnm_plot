# Report: Potential Alignment UB in Hold-Forward Sample Synthesis

Date: 2026-02-12  
Repo: `fetched_libs/vnm_plot`

## Summary
The current hold-forward implementation can invoke undefined behavior when writing the synthetic sample because it may reinterpret an unaligned byte buffer as `Sample*` and assign through that pointer.

## Affected Code Paths

1. Destination buffer allocation in renderer:
- `src/core/series_renderer.cpp:656`
- `src/core/series_renderer.cpp:658`

```cpp
auto& hold_sample = view_state.hold_sample_buffer;
hold_sample.resize(snapshot.stride);
access.clone_with_timestamp(hold_sample.data(), source_sample, result.hold_t);
```

2. Typed-to-erased adapter dereferencing destination as typed pointer:
- `include/vnm_plot/core/access_policy.h:120`
- `include/vnm_plot/core/access_policy.h:122`

```cpp
policy.clone_with_timestamp = [fn = clone_with_timestamp](void* dst_sample, const void* src_sample, double timestamp) {
    fn(
        *static_cast<Sample*>(dst_sample),
        *static_cast<const Sample*>(src_sample),
        timestamp);
};
```

## Root Cause
`std::vector<unsigned char>` only guarantees alignment suitable for `unsigned char`. The destination pointer returned by `hold_sample_buffer.data()` is therefore not guaranteed to satisfy `alignof(Sample)`. Reinterpreting and dereferencing that pointer as `Sample*` is UB if alignment is insufficient.

The risk is not theoretical for all targets:
- Over-aligned sample types (`alignas(...)`) can exceed default allocations.
- Some architectures trap on misaligned access.
- Even on tolerant architectures, optimizer assumptions around alignment can cause incorrect codegen.

## Impact
Potential outcomes include:
- Crash (misaligned load/store trap on strict-alignment platforms).
- Silent data corruption in synthesized hold-forward sample bytes.
- Non-deterministic behavior under optimization.

The path is executed only when all hold-forward conditions are met, but once enabled it can run often (every redraw where hold sample upload is needed).

## Suggested Solution

### Preferred Fix (minimal API churn)
Keep the existing erased callback signature, but avoid typed writes into the destination pointer in the typed-to-erased adapter:

1. In `Data_access_policy_typed<Sample>::erase()`:
- Build the cloned sample into a properly aligned local `Sample tmp`.
- Copy bytes from `tmp` into `dst_sample` via `std::memcpy`.

2. Keep renderer-side destination as raw bytes; this is safe once the adapter no longer dereferences `dst_sample` as `Sample*`.

Pseudo-shape:

```cpp
Sample tmp{};
fn(tmp, *static_cast<const Sample*>(src_sample), timestamp);
std::memcpy(dst_sample, &tmp, sizeof(Sample));
```

This removes the alignment hazard on destination writes without changing public APIs.

### Alternative (stronger API-level guarantee)
Redesign `clone_with_timestamp` to be byte-oriented by contract (e.g. return-by-value buffer or callback taking output span), so implementers never assume typed destination alignment.

This is cleaner semantically but requires wider API updates.

## Additional Guardrails (optional)
- Document in `Data_access_policy::clone_with_timestamp` that source pointers must reference valid sample memory and destination must have enough bytes.
- Add test coverage using an over-aligned sample type to ensure no typed write is performed through potentially unaligned storage.

## Recommendation
Implement the preferred fix immediately. It is low-risk, localized, and removes a real UB class from a hot render path.