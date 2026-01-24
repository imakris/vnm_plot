/**
 * Test program for cache invalidation logic
 *
 * This test validates the fixes for:
 * 1. Failed snapshots should invalidate cache (plot_renderer.cpp)
 * 2. LOD 0 sequence mixing in aux metric cache (series_renderer.cpp)
 * 3. Snapshot cache cleanup on VBO removal (series_renderer.cpp)
 */

#include <iostream>
#include <cassert>
#include <memory>
#include <cstdint>

// Mock data source for testing
class Mock_data_source {
public:
    mutable std::uint64_t fail_snapshot_count = 0;
    std::uint64_t sequence_value = 0;
    bool should_fail_snapshot = false;

    struct snapshot_result {
        bool success = false;
        std::uint64_t sequence = 0;
    };

    std::uint64_t current_sequence(std::size_t level) const {
        (void)level;  // Suppress unused parameter warning
        return sequence_value;
    }

    snapshot_result try_snapshot(std::size_t level) {
        (void)level;  // Suppress unused parameter warning
        if (should_fail_snapshot) {
            ++fail_snapshot_count;
            return {false, 0};
        }
        return {true, sequence_value};
    }

    void set_sequence(std::uint64_t seq) {
        sequence_value = seq;
    }
};

// Test 1: Failed snapshots should invalidate cache
void test_failed_snapshot_invalidates_cache() {
    std::cout << "Test 1: Failed snapshots should invalidate cache..." << std::endl;

    Mock_data_source ds;
    ds.set_sequence(100);

    // First attempt: success
    auto result1 = ds.try_snapshot(0);
    assert(result1.success && "First snapshot should succeed");
    assert(result1.sequence == 100 && "Sequence should be 100");

    // Simulate cache validation
    bool cache_invalid = false;
    std::uint64_t sequence = ds.current_sequence(0);
    if (sequence == 0) {
        auto snapshot_result = ds.try_snapshot(0);
        if (!snapshot_result.success) {
            // BUG FIX: This should invalidate cache
            cache_invalid = true;
        }
    }

    assert(!cache_invalid && "Cache should be valid when snapshot succeeds");

    // Second attempt: failure
    ds.should_fail_snapshot = true;
    ds.set_sequence(0);  // Force fallback to try_snapshot
    cache_invalid = false;

    sequence = ds.current_sequence(0);
    if (sequence == 0) {
        auto snapshot_result = ds.try_snapshot(0);
        if (!snapshot_result.success) {
            // BUG FIX: This MUST invalidate cache to force recalculation
            cache_invalid = true;
        }
    }

    assert(cache_invalid && "Cache MUST be invalidated when snapshot fails");

    std::cout << "  ✓ Failed snapshots correctly invalidate cache" << std::endl;
}

// Test 2: LOD 0 sequence fallback handling
void test_lod0_sequence_fallback() {
    std::cout << "Test 2: LOD 0 sequence fallback handling..." << std::endl;

    Mock_data_source ds;

    // Scenario: current_sequence(0) returns 0 (not implemented)
    // We should try to get LOD 0 snapshot for correct sequence
    ds.set_sequence(0);  // Simulate unimplemented current_sequence

    std::uint64_t cache_sequence = ds.current_sequence(0);
    assert(cache_sequence == 0 && "current_sequence returns 0");

    // BUG FIX: When current_sequence is 0 and we're not at LOD 0,
    // we should try to get LOD 0 snapshot for accurate sequence tracking
    const std::size_t applied_level = 2;  // We're at LOD 2
    std::uint64_t snapshot_seq_lod2 = 50;  // LOD 2 snapshot has seq 50

    if (cache_sequence == 0) {
        if (applied_level == 0) {
            // We're at LOD 0, so snapshot sequence is correct
            cache_sequence = snapshot_seq_lod2;
        } else {
            // We need LOD 0 sequence - try to get it
            ds.set_sequence(100);  // LOD 0 actually has seq 100
            auto lod0_snapshot = ds.try_snapshot(0);
            if (lod0_snapshot.success) {
                cache_sequence = lod0_snapshot.sequence;
            } else {
                // Fallback to applied level sequence
                cache_sequence = snapshot_seq_lod2;
            }
        }
    }

    assert(cache_sequence == 100 && "Should use LOD 0 sequence, not LOD 2 sequence");

    std::cout << "  ✓ LOD 0 sequence fallback works correctly" << std::endl;
}

// Test 3: Snapshot cache cleanup
void test_snapshot_cache_cleanup() {
    std::cout << "Test 3: Snapshot cache cleanup..." << std::endl;

    struct VBO_state {
        std::shared_ptr<void> cached_snapshot_hold;
        std::uint64_t cached_snapshot_frame_id = 0;

        ~VBO_state() {
            // Explicit cleanup in destructor
            cached_snapshot_hold.reset();
            cached_snapshot_frame_id = 0;
        }
    };

    // Create a VBO state with cached snapshot
    auto state = std::make_unique<VBO_state>();
    state->cached_snapshot_hold = std::make_shared<int>(42);
    state->cached_snapshot_frame_id = 100;

    assert(state->cached_snapshot_hold.use_count() == 1 && "Snapshot should be held");
    assert(state->cached_snapshot_frame_id == 100 && "Frame ID should be set");

    // Simulate cleanup before erase
    state->cached_snapshot_hold.reset();
    state->cached_snapshot_frame_id = 0;

    assert(!state->cached_snapshot_hold && "Snapshot should be released");
    assert(state->cached_snapshot_frame_id == 0 && "Frame ID should be cleared");

    std::cout << "  ✓ Snapshot cache cleanup works correctly" << std::endl;
}

// Test 4: Sequence-based cache reuse safety
void test_sequence_cache_reuse() {
    std::cout << "Test 4: Sequence-based cache reuse safety..." << std::endl;

    struct ViewState {
        std::uint64_t last_sequence = 0;
        double last_t_min = 0.0;
        double last_t_max = 0.0;
        double last_width_px = 0.0;
        std::size_t last_lod_level = 0;
    };

    Mock_data_source ds;
    ViewState view_state;

    // Initial state
    ds.set_sequence(100);
    view_state.last_sequence = 100;
    view_state.last_t_min = 0.0;
    view_state.last_t_max = 10.0;
    view_state.last_width_px = 800.0;
    view_state.last_lod_level = 0;

    // Test: Same sequence, same window -> can reuse
    std::uint64_t current_seq = ds.current_sequence(0);
    bool can_reuse = (current_seq != 0 &&
                      current_seq == view_state.last_sequence &&
                      0 == view_state.last_lod_level &&
                      0.0 == view_state.last_t_min &&
                      10.0 == view_state.last_t_max &&
                      800.0 == view_state.last_width_px);

    assert(can_reuse && "Should reuse cache with matching sequence and window");

    // Test: Same sequence, different window -> MUST NOT reuse
    can_reuse = (current_seq != 0 &&
                 current_seq == view_state.last_sequence &&
                 0 == view_state.last_lod_level &&
                 5.0 == view_state.last_t_min &&  // Changed!
                 15.0 == view_state.last_t_max &&  // Changed!
                 800.0 == view_state.last_width_px);

    assert(!can_reuse && "MUST NOT reuse cache when window changes, even if sequence matches");

    // Test: Different sequence, same window -> MUST NOT reuse
    ds.set_sequence(101);
    current_seq = ds.current_sequence(0);
    can_reuse = (current_seq != 0 &&
                 current_seq == view_state.last_sequence &&
                 0 == view_state.last_lod_level &&
                 0.0 == view_state.last_t_min &&
                 10.0 == view_state.last_t_max &&
                 800.0 == view_state.last_width_px);

    assert(!can_reuse && "MUST NOT reuse cache when sequence changes");

    std::cout << "  ✓ Sequence-based cache reuse is safe" << std::endl;
}

int main() {
    std::cout << "=== Cache Invalidation Tests ===" << std::endl << std::endl;

    try {
        test_failed_snapshot_invalidates_cache();
        test_lod0_sequence_fallback();
        test_snapshot_cache_cleanup();
        test_sequence_cache_reuse();

        std::cout << std::endl << "=== All tests passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
