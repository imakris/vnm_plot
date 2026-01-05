#pragma once
// VNM Plot Library - Thread-Local Storage Registry
// Provides per-thread instances with controlled shutdown.

#include <list>
#include <memory>
#include <mutex>
#include <utility>

namespace vnm::plot::tls {

// Simple helper that provides a per-thread instance of T backed by a registry
// so we can destroy all instances in a defined shutdown phase instead of
// during TLS teardown.
template <typename T>
class Thread_local_registry
{
public:
    Thread_local_registry() = default;
    Thread_local_registry(const Thread_local_registry&) = delete;
    Thread_local_registry& operator=(const Thread_local_registry&) = delete;

    template <typename Factory>
    T& get_or_create(Factory&& factory)
    {
        thread_local T* tls_ptr = nullptr;
        if (!tls_ptr) {
            auto obj = factory();
            auto* raw = obj.get();

            registry_entry_t entry;
            entry.object = std::move(obj);
            entry.tls_ptr = &tls_ptr;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_entries.push_back(std::move(entry));
            }

            tls_ptr = raw;
        }
        return *tls_ptr;
    }

    template <typename Cleanup>
    void shutdown(Cleanup&& cleanup)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& entry : m_entries) {
            if (entry.object) {
                cleanup(*entry.object);
                if (entry.tls_ptr) {
                    *entry.tls_ptr = nullptr;
                }
            }
        }
        m_entries.clear();
    }

    void shutdown()
    {
        shutdown([](T&) {});
    }

private:
    struct registry_entry_t
    {
        std::unique_ptr<T> object;
        T** tls_ptr = nullptr;
    };

    std::mutex m_mutex;
    std::list<registry_entry_t> m_entries;
};

} // namespace vnm::plot::tls
