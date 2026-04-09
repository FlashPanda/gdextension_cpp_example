

#ifndef GDEXTENSION_CPP_EXAMPLE_BUFFER_CACHE_H
#define GDEXTENSION_CPP_EXAMPLE_BUFFER_CACHE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot
{
    template <typename T>
    class BufferCache
    {
    public:
        struct BufferView {
            const T *data = nullptr;
            size_t size = 0;

            bool empty() const noexcept { return size == 0; }
        };

        const T* lookup_or_add(BufferView buf)
        {
            ++_lookups;

            if (buf.data == nullptr || buf.size == 0)
            {
                return nullptr;
            }

            const size_t hash = hash_buffer(buf.data, buf.size);
            const size_t shard_index = hash & (N_SHARDS - 1);

            // 1) 先读锁查
            {
                std::shared_lock<std::shared_mutex> read_lock(_mutexes[shard_index]);

                auto iter = _shards[shard_index].find(hash);
                if (iter != _shards[shard_index].end()) {
                    for (const auto &entry : iter->second) {
                        if (equals(buf, *entry)) {
                            ++_hits;
                            _redundant_buffer_bytes += buf.size * sizeof(T);
                            return entry->data.get();
                        }
                    }
                }
            }

            // 2) 先在锁外分配，减少写锁持有时间
            auto new_entry = std::make_shared<Entry>();
            new_entry->size = buf.size;
            new_entry->hash = hash;
            new_entry->data = std::unique_ptr<T[]>(new T[buf.size]);
            std::copy(buf.data, buf.data + buf.size, new_entry->data.get());

            // 3) 写锁下二次检查，防止并发重复插入
            {
                std::unique_lock<std::shared_mutex> write_lock(_mutexes[shard_index]);

                auto &bucket = _shards[shard_index][hash];
                for (const auto &entry : bucket) {
                    if (equals(buf, *entry)) {
                        ++_hits;
                        _redundant_buffer_bytes += buf.size * sizeof(T);
                        return entry->data.get();
                    }
                }

                bucket.push_back(new_entry);
                _bytes_used += buf.size * sizeof(T);
                return new_entry->data.get();
            }
        }

        size_t bytes_used() const noexcept {
            return _bytes_used.load(std::memory_order_relaxed);
        }

        size_t lookups() const noexcept {
            return _lookups.load(std::memory_order_relaxed);
        }

        size_t hits() const noexcept {
            return _hits.load(std::memory_order_relaxed);
        }

        size_t redundant_buffer_bytes() const noexcept {
            return _redundant_buffer_bytes.load(std::memory_order_relaxed);
        }

    private:
        struct Entry {
            std::unique_ptr<T[]> data;
            size_t size = 0;
            size_t hash = 0;
        };

        static constexpr size_t LOG_SHARDS = 6;
        static constexpr size_t N_SHARDS = 1ull << LOG_SHARDS;

        static bool equals(BufferView view, const Entry &entry) noexcept {
            return view.size == entry.size &&
                   std::memcmp(view.data, entry.data.get(), view.size * sizeof(T)) == 0;
        }

        static size_t hash_buffer(const T *data, size_t count) noexcept {
            // FNV-1a 64-bit
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
            const size_t byte_count = count * sizeof(T);

            uint64_t hash = 1469598103934665603ull;
            for (size_t i = 0; i < byte_count; ++i) {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= 1099511628211ull;
            }
            return static_cast<size_t>(hash);
        }

    private:
        mutable std::shared_mutex _mutexes[N_SHARDS];
        std::unordered_map<size_t, std::vector<std::shared_ptr<Entry>>> _shards[N_SHARDS];

        std::atomic<size_t> _bytes_used{0};
        std::atomic<size_t> _lookups{0};
        std::atomic<size_t> _hits{0};
        std::atomic<size_t> _redundant_buffer_bytes{0};
    };

// BufferCache Global Declarations
extern BufferCache<int> *int_buffer_cache;
extern BufferCache<Vector2> *point2_buffer_cache;
extern BufferCache<Vector3> *point3_buffer_cache;
extern BufferCache<Vector3> *vector3_buffer_cache;
extern BufferCache<Vector3> *normal3_buffer_cache;

void init_buffer_caches();
}

#endif //GDEXTENSION_CPP_EXAMPLE_BUFFER_CACHE_H