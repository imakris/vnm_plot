#pragma once

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace vnm::plot::detail {

template <typename Labels, typename Tracker, typename KeyFunc, typename DrawFunc>
bool update_and_draw_faded_labels(
    const Labels&                            labels,
    Tracker&                                 tracker,
    float                                    fade_duration_ms,
    bool                                     animate_changes,
    std::chrono::steady_clock::time_point    now,
    KeyFunc&&                                key_fn,
    DrawFunc&&                               draw_fn)
{
    using key_t    = typename Tracker::key_type;
    using states_t = std::remove_reference_t<decltype(tracker.states)>;
    using state_t  = typename states_t::mapped_type;

    if (!tracker.initialized) {
        tracker.states.clear();
        for (const auto& label : labels) {
            state_t state;
            state.alpha     = 1.0f;
            state.direction = 0;
            state.text      = label.text;
            tracker.states.emplace(key_fn(label), std::move(state));
        }
        tracker.last_update = now;
        tracker.initialized = true;
        for (const auto& entry : tracker.states) {
            draw_fn(entry.first, entry.second.text, entry.second.alpha);
        }
        return false;
    }

    const float dt_ms = std::chrono::duration_cast<
        std::chrono::duration<float, std::milli>>(now - tracker.last_update).count();
    tracker.last_update = now;
    const float step = fade_duration_ms > 0.0f
        ? std::clamp(dt_ms / fade_duration_ms, 0.0f, 1.0f)
        : 1.0f;

    constexpr float k_alpha_eps = 1e-6f;
    for (auto it = tracker.states.begin(); it != tracker.states.end(); ) {
        auto& state = it->second;
        if (state.direction != 0 && step > 0.0f) {
            state.alpha += step * static_cast<float>(state.direction);
        }
        state.alpha = std::clamp(state.alpha, 0.0f, 1.0f);

        if (state.direction > 0 && state.alpha >= 1.0f - k_alpha_eps) {
            state.alpha     = 1.0f;
            state.direction = 0;
        }
        if (state.direction < 0 && state.alpha <= k_alpha_eps) {
            it = tracker.states.erase(it);
            continue;
        }
        if (state.text_mix < 1.0f && step > 0.0f) {
            state.text_mix = std::min(state.text_mix + step, 1.0f);
            if (state.text_mix >= 1.0f - k_alpha_eps) {
                state.text_mix = 1.0f;
                state.previous_text.clear();
            }
        }
        ++it;
    }

    std::unordered_set<key_t> current_values;
    current_values.reserve(labels.size());
    for (const auto& label : labels) {
        const key_t key = key_fn(label);
        current_values.insert(key);

        auto it = tracker.states.find(key);
        if (it == tracker.states.end()) {
            state_t state;
            state.alpha     = animate_changes ? 0.0f : 1.0f;
            state.direction = animate_changes ? 1 : 0;
            state.text      = label.text;
            tracker.states.emplace(key, std::move(state));
            continue;
        }

        auto& state = it->second;
        if (state.text != label.text) {
            if (animate_changes) {
                if (state.text_mix < 1.0f && state.previous_text == label.text) {
                    std::swap(state.text, state.previous_text);
                    state.text_mix = 1.0f - state.text_mix;
                }
                else {
                    state.previous_text = std::move(state.text);
                    state.text          = label.text;
                    state.text_mix      = 0.0f;
                }
            }
            else {
                state.text          = label.text;
                state.previous_text.clear();
                state.text_mix      = 1.0f;
            }
        }
        if (state.direction < 0) {
            if (animate_changes) {
                state.direction = 1;
            }
            else {
                state.alpha     = 1.0f;
                state.direction = 0;
            }
        }
    }

    for (auto it = tracker.states.begin(); it != tracker.states.end(); ) {
        auto& state = it->second;
        if (current_values.find(it->first) == current_values.end() && state.direction >= 0) {
            if (animate_changes) {
                state.direction = -1;
            }
            else {
                it = tracker.states.erase(it);
                continue;
            }
        }
        ++it;
    }

    bool any_active = false;
    for (const auto& entry : tracker.states) {
        const auto& state = entry.second;
        if (state.alpha > 0.0f) {
            const float previous_alpha = state.alpha * (1.0f - state.text_mix);
            if (previous_alpha > 0.0f) {
                draw_fn(entry.first, state.previous_text, previous_alpha);
            }
            const float current_alpha = state.alpha * state.text_mix;
            if (current_alpha > 0.0f) {
                draw_fn(entry.first, state.text, current_alpha);
            }
        }
        any_active |= state.direction != 0 || state.text_mix < 1.0f;
    }
    return any_active;
}

} // namespace vnm::plot::detail
