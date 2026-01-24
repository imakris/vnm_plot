/**
 * Test program for snapshot caching behavior
 *
 * This test validates:
 * 1. Frame-scoped snapshot cache reuse between main and preview views
 * 2. Cache invalidation on frame ID change
 * 3. Proper snapshot hold lifetime management
 */

#include <iostream>
#include <cassert>
#include <memory>
#include <cstdint>
#include <cstring>

// Mock snapshot structure
struct Mock_snapshot {
    void* data = nullptr;
    std::size_t count = 0;
    std::uint64_t sequence = 0;
    std::shared_ptr<void> hold;

    Mock_snapshot() = default;
    Mock_snapshot(void* d, std::size_t c, std::uint64_t seq, std::shared_ptr<void> h)
        : data(d), count(c), sequence(seq), hold(h) {}

    operator bool() const { return data != nullptr; }
};

// Mock VBO state with snapshot caching
struct Mock_VBO_state {
    std::uint64_t cached_snapshot_frame_id = 0;
    std::size_t cached_snapshot_level = SIZE_MAX;
    Mock_snapshot cached_snapshot;
    std::shared_ptr<void> cached_snapshot_hold;

    void clear_cache() {
        cached_snapshot_frame_id = 0;
        cached_snapshot_level = SIZE_MAX;
        cached_snapshot = {};
        cached_snapshot_hold.reset();
    }
};

// Mock data source
class Mock_data_source {
public:
    std::uint64_t snapshot_call_count = 0;
    std::uint64_t sequence_value = 100;
    bool should_fail = false;

    Mock_snapshot try_snapshot(std::size_t level) {
        (void)level;  // Suppress unused parameter warning
        ++snapshot_call_count;
        if (should_fail) {
            return {};
        }
        auto hold = std::make_shared<int>(42);
        return Mock_snapshot{
            reinterpret_cast<void*>(0x1000),  // Fake data pointer
            1000,
            sequence_value,
            hold
        };
    }
};

// Test 1: Frame-scoped cache reuse
void test_frame_scoped_cache_reuse() {
    std::cout << "Test 1: Frame-scoped cache reuse between views..." << std::endl;

    Mock_data_source ds;
    Mock_VBO_state state;
    const std::uint64_t frame_id = 1;
    const std::size_t level = 0;

    // Simulate main view requesting snapshot
    Mock_snapshot snapshot;
    if (state.cached_snapshot_frame_id == frame_id &&
        state.cached_snapshot_level == level &&
        state.cached_snapshot) {
        // Cache hit
        snapshot = state.cached_snapshot;
    } else {
        // Cache miss - acquire new snapshot
        snapshot = ds.try_snapshot(level);
        if (snapshot) {
            state.cached_snapshot_frame_id = frame_id;
            state.cached_snapshot_level = level;
            state.cached_snapshot = snapshot;
            state.cached_snapshot_hold = snapshot.hold;
        }
    }

    assert(snapshot && "Main view should get snapshot");
    assert(ds.snapshot_call_count == 1 && "Should call try_snapshot once");

    // Simulate preview view requesting same snapshot in same frame
    Mock_snapshot preview_snapshot;
    if (state.cached_snapshot_frame_id == frame_id &&
        state.cached_snapshot_level == level &&
        state.cached_snapshot) {
        // Cache hit - reuse!
        preview_snapshot = state.cached_snapshot;
    } else {
        // Cache miss
        preview_snapshot = ds.try_snapshot(level);
        if (preview_snapshot) {
            state.cached_snapshot_frame_id = frame_id;
            state.cached_snapshot_level = level;
            state.cached_snapshot = preview_snapshot;
            state.cached_snapshot_hold = preview_snapshot.hold;
        }
    }

    assert(preview_snapshot && "Preview view should get snapshot");
    assert(ds.snapshot_call_count == 1 && "Should NOT call try_snapshot again (cache hit)");
    assert(preview_snapshot.data == snapshot.data && "Should reuse same snapshot");
    // Note: hold count will be higher due to copies, but data pointer should match

    std::cout << "  ✓ Frame-scoped cache correctly reuses snapshots" << std::endl;
}

// Test 2: Cache invalidation on frame change
void test_cache_invalidation_on_frame_change() {
    std::cout << "Test 2: Cache invalidation on frame change..." << std::endl;

    Mock_data_source ds;
    Mock_VBO_state state;
    const std::size_t level = 0;

    // Frame 1
    std::uint64_t frame_id = 1;

    auto snapshot1 = ds.try_snapshot(level);
    state.cached_snapshot_frame_id = frame_id;
    state.cached_snapshot_level = level;
    state.cached_snapshot = snapshot1;
    state.cached_snapshot_hold = snapshot1.hold;

    assert(ds.snapshot_call_count == 1 && "First frame should call try_snapshot");

    // Frame 2 - cache should be invalidated
    frame_id = 2;

    // At start of frame, invalidate cache if frame changed
    if (state.cached_snapshot_frame_id != frame_id) {
        state.clear_cache();
    }

    Mock_snapshot snapshot2;
    if (state.cached_snapshot_frame_id == frame_id &&
        state.cached_snapshot_level == level &&
        state.cached_snapshot) {
        // Cache hit
        snapshot2 = state.cached_snapshot;
    } else {
        // Cache miss - must acquire new snapshot
        snapshot2 = ds.try_snapshot(level);
        if (snapshot2) {
            state.cached_snapshot_frame_id = frame_id;
            state.cached_snapshot_level = level;
            state.cached_snapshot = snapshot2;
            state.cached_snapshot_hold = snapshot2.hold;
        }
    }

    assert(snapshot2 && "Frame 2 should get snapshot");
    assert(ds.snapshot_call_count == 2 && "Frame 2 MUST call try_snapshot (cache invalidated)");

    std::cout << "  ✓ Cache correctly invalidated on frame change" << std::endl;
}

// Test 3: Different LOD levels don't share cache
void test_different_lod_levels() {
    std::cout << "Test 3: Different LOD levels have separate caches..." << std::endl;

    Mock_data_source ds;
    Mock_VBO_state state;
    const std::uint64_t frame_id = 1;

    // Request LOD 0
    auto snapshot_lod0 = ds.try_snapshot(0);
    state.cached_snapshot_frame_id = frame_id;
    state.cached_snapshot_level = 0;
    state.cached_snapshot = snapshot_lod0;
    state.cached_snapshot_hold = snapshot_lod0.hold;

    assert(ds.snapshot_call_count == 1 && "LOD 0 should call try_snapshot");

    // Request LOD 2 in same frame
    Mock_snapshot snapshot_lod2;
    if (state.cached_snapshot_frame_id == frame_id &&
        state.cached_snapshot_level == 2 &&  // Different level!
        state.cached_snapshot) {
        // Cache hit
        snapshot_lod2 = state.cached_snapshot;
    } else {
        // Cache miss - different level
        snapshot_lod2 = ds.try_snapshot(2);
        if (snapshot_lod2) {
            state.cached_snapshot_frame_id = frame_id;
            state.cached_snapshot_level = 2;
            state.cached_snapshot = snapshot_lod2;
            state.cached_snapshot_hold = snapshot_lod2.hold;
        }
    }

    assert(snapshot_lod2 && "LOD 2 should get snapshot");
    assert(ds.snapshot_call_count == 2 && "LOD 2 MUST call try_snapshot (different level)");

    std::cout << "  ✓ Different LOD levels correctly maintain separate caches" << std::endl;
}

// Test 4: Snapshot hold lifetime management
void test_snapshot_hold_lifetime() {
    std::cout << "Test 4: Snapshot hold lifetime management..." << std::endl;

    Mock_data_source ds;
    Mock_VBO_state state;

    auto snapshot = ds.try_snapshot(0);
    std::weak_ptr<void> weak_hold = snapshot.hold;

    assert(!weak_hold.expired() && "Hold should be alive");

    // Cache snapshot
    state.cached_snapshot_frame_id = 1;
    state.cached_snapshot_level = 0;
    state.cached_snapshot = snapshot;
    state.cached_snapshot_hold = snapshot.hold;

    // Release local snapshot
    snapshot = {};

    assert(!weak_hold.expired() && "Hold should still be alive (cached)");
    assert(weak_hold.use_count() == 2 && "Two references: cache + cached_snapshot_hold");

    // Clear cache
    state.clear_cache();

    assert(weak_hold.expired() && "Hold should be released after cache clear");

    std::cout << "  ✓ Snapshot hold lifetime correctly managed" << std::endl;
}

// Test 5: Cache behavior on failed snapshots
void test_cache_on_failed_snapshots() {
    std::cout << "Test 5: Cache behavior on failed snapshots..." << std::endl;

    Mock_data_source ds;
    Mock_VBO_state state;
    const std::uint64_t frame_id = 1;
    const std::size_t level = 0;

    // First: successful snapshot
    auto snapshot1 = ds.try_snapshot(level);
    state.cached_snapshot_frame_id = frame_id;
    state.cached_snapshot_level = level;
    state.cached_snapshot = snapshot1;
    state.cached_snapshot_hold = snapshot1.hold;

    assert(snapshot1 && "First snapshot should succeed");
    assert(state.cached_snapshot && "Cache should hold snapshot");

    // Second: failed snapshot (new frame)
    const std::uint64_t frame_id2 = 2;
    ds.should_fail = true;

    // Invalidate cache for new frame
    if (state.cached_snapshot_frame_id != frame_id2) {
        state.clear_cache();
    }

    auto snapshot2 = ds.try_snapshot(level);
    if (snapshot2) {
        state.cached_snapshot_frame_id = frame_id2;
        state.cached_snapshot_level = level;
        state.cached_snapshot = snapshot2;
        state.cached_snapshot_hold = snapshot2.hold;
    }

    assert(!snapshot2 && "Second snapshot should fail");
    assert(!state.cached_snapshot && "Cache should NOT hold failed snapshot");
    assert(state.cached_snapshot_frame_id == 0 && "Frame ID should remain 0 after failure");

    std::cout << "  ✓ Cache correctly handles failed snapshots" << std::endl;
}

int main() {
    std::cout << "=== Snapshot Caching Tests ===" << std::endl << std::endl;

    try {
        test_frame_scoped_cache_reuse();
        test_cache_invalidation_on_frame_change();
        test_different_lod_levels();
        test_snapshot_hold_lifetime();
        test_cache_on_failed_snapshots();

        std::cout << std::endl << "=== All tests passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
