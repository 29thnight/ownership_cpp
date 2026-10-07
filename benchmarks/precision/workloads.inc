// One workload source, compiled against both the frozen rejected prototype and
// the lean header. OWN_BENCH_LEAN is the only version/capability switch.
#include <own/ownership.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef OWN_BENCH_LEAN
#define OWN_BENCH_LEAN 1
#endif

namespace bench {
using clock = std::chrono::steady_clock;
using deallocate_fn = void (*)(void*, void*, std::size_t, std::size_t) noexcept;
#if OWN_BENCH_LEAN
constexpr const char* build_name = "lean";
constexpr const char* own_name = "own_unique_8";
#else
constexpr const char* build_name = "frozen_erased";
constexpr const char* own_name = "own_old_unique_40";
#endif
#ifdef UNIQUE_BENCH_CHECK
// This separate untimed build observes lifetimes, not global allocation calls.
// There is NO global allocation replacement in either benchmark executable.
inline std::atomic<std::size_t> births{0}, deaths{0};
#endif

template<class T> inline void escape(const T& value) noexcept {
    asm volatile("" : : "g"(std::addressof(value)) : "memory");
}
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
constexpr std::uint64_t word(std::uint64_t id, std::size_t index) noexcept {
    return id * 13 + index * 7 + 11;
}
struct resource {
    std::array<std::uint64_t, 8> words;
    explicit resource(std::uint64_t id) noexcept {
        for (std::size_t j = 0; j < words.size(); ++j) words[j] = word(id, j);
#ifdef UNIQUE_BENCH_CHECK
        ++births;
#endif
    }
#ifdef UNIQUE_BENCH_CHECK
    ~resource() { ++deaths; }
#endif
};
static_assert(sizeof(resource) == 64);

// Matched opt-in contract: original storage + allocator state + concrete disposal.
struct erased_delete {
    void* storage = nullptr;
    void* context = nullptr;
    deallocate_fn deallocate = nullptr;
    void (*dispose)(void*, void*, deallocate_fn) noexcept = nullptr;
    template<class T> void operator()(T*) const noexcept { dispose(storage, context, deallocate); }
};
template<class T>
void dispose(void* storage, void* context, deallocate_fn deallocate) noexcept {
    static_cast<T*>(storage)->~T();
    deallocate(context, storage, sizeof(T), alignof(T));
}
template<class T, class... Args>
std::unique_ptr<T, erased_delete> make_erased(own::allocator_ref allocator, Args&&... args) {
    void* storage = allocator.allocate(allocator.context, sizeof(T), alignof(T));
    if (!storage) throw std::bad_alloc();
    try {
        auto* value = ::new (storage) T(std::forward<Args>(args)...);
        return {value, {storage, allocator.context, allocator.deallocate, &dispose<T>}};
    } catch (...) {
        allocator.deallocate(allocator.context, storage, sizeof(T), alignof(T));
        throw;
    }
}
struct std_default {
    using owner = std::unique_ptr<resource>;
    using view = const resource*;
    static owner make(std::uint64_t id) { return std::make_unique<resource>(id); }
    static view borrow(const owner& value) noexcept { return value.get(); }
};
struct std_erased {
    using owner = std::unique_ptr<resource, erased_delete>;
    using view = const resource*;
    static owner make(std::uint64_t id) { return make_erased<resource>({}, id); }
    static view borrow(const owner& value) noexcept { return value.get(); }
};
struct own_unique {
    using owner = own::unique_owner<resource>;
    using view = own::local_view<const resource>;
    static owner make(std::uint64_t id) { return own::make_unique<resource>(id); }
    static view borrow(const owner& value) noexcept { return value.borrow(); }
};
#if OWN_BENCH_LEAN
struct own_allocated {
    using owner = own::allocated_unique_owner<resource>;
    using view = own::local_view<const resource>;
    static owner make(std::uint64_t id) { return own::allocate_unique<resource>({}, id); }
    static view borrow(const owner& value) noexcept { return value.borrow(); }
};
static_assert(sizeof(own_allocated::owner) == 5 * sizeof(void*));
static_assert(sizeof(own_unique::owner) == sizeof(void*));
#else
static_assert(sizeof(own_unique::owner) == 5 * sizeof(void*));
#endif
static_assert(sizeof(std_default::owner) == sizeof(void*));
static_assert(sizeof(std_erased::owner) == 5 * sizeof(void*));
static_assert(sizeof(own_unique::view) == sizeof(void*));

// A consumer boundary makes the actual borrowed array and pointees observable.
// Container/frame loops have NO barrier on each intermediate owner move.
template<class View>
[[gnu::noinline]] std::uint64_t consume(const std::vector<View>& views) {
    escape(views);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < views.size(); ++i) sum += views[i]->words[i & 7];
    return sum;
}
constexpr std::size_t resource_count = 256, draw_count = 4096, transfer_count = 1024;
constexpr std::size_t case_count = 6;
struct counts {
    std::size_t create = 50000, move = 500000, borrow = 1000000;
    std::size_t frames = 64, transfers = 64, jobs = 2048;
};

template<class Ops> class job_queue {
    using owner = typename Ops::owner;
    static constexpr std::size_t capacity = 64;
    std::array<owner, capacity> slots_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t head_ = 0, tail_ = 0, queued_ = 0, completed_ = 0;
    std::uint64_t checksum_ = 0;
    bool stop_ = false;
    std::thread worker_;
    void work() {
        precision::pin(precision::worker_cpu);
        for (;;) {
            std::uint64_t part = 0;
            {
                owner task;
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait(lock, [&] { return stop_ || queued_ != 0; });
                    if (stop_) return;
                    task = std::move(slots_[head_]);
                    head_ = (head_ + 1) % capacity;
                    --queued_;
                }
                changed_.notify_all();
                // Ownership has crossed the synchronized queue BEFORE borrowing.
                // Only this thread owns/accesses task during the borrowed scope.
                auto view = Ops::borrow(task);
                for (std::size_t i = 0; i < 8; ++i) part += view->words[i];
            } // view and owner die before completion is acknowledged
            {
                std::lock_guard lock(mutex_);
                checksum_ += part;
                ++completed_;
            }
            changed_.notify_all();
        }
    }
public:
    job_queue() : worker_([this] { work(); }) {}
    job_queue(const job_queue&) = delete;
    ~job_queue() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        changed_.notify_all();
        worker_.join();
    }
    std::uint64_t run(std::size_t jobs) {
        {
            std::lock_guard lock(mutex_);
            check(queued_ == 0, "queue was not drained");
            completed_ = 0;
            checksum_ = 0;
        }
        for (std::size_t i = 0; i < jobs; ++i) {
            auto task = Ops::make(i);
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] { return queued_ < capacity; });
                slots_[tail_] = std::move(task);
                tail_ = (tail_ + 1) % capacity;
                ++queued_;
            }
            changed_.notify_all();
        }
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return completed_ == jobs; });
        return checksum_;
    }
};

template<class Ops> struct workload {
    using owner = typename Ops::owner;
    using view = typename Ops::view;
    owner root = Ops::make(17);
    std::vector<owner> owners, source, target;
    std::vector<view> draws, transfer_views;
    job_queue<Ops> queue;
    workload() {
        owners.reserve(resource_count); draws.reserve(draw_count);
        source.reserve(transfer_count); target.reserve(transfer_count);
        transfer_views.reserve(transfer_count);
        for (std::size_t i = 0; i < transfer_count; ++i) source.push_back(Ops::make(i));
    }
    std::uint64_t create_read_destroy(std::size_t iterations) {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < iterations; ++i) {
            auto value = Ops::make(i);
            escape(value);
            sum += value->words[i & 7];
        }
        return sum;
    }
    std::uint64_t observable_move_roundtrip(std::size_t iterations) {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < iterations; ++i) {
            auto moved = std::move(root);
            escape(root); escape(moved);
            root = std::move(moved);
            escape(root); escape(moved);
            sum += root->words[i & 7];
        }
        return sum;
    }
    std::uint64_t borrow_read_loop(std::size_t iterations) {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < iterations; ++i) {
            auto view = Ops::borrow(root);
            escape(view);
            sum += view->words[i & 7];
        }
        return sum;
    }
    std::uint64_t frame_draw_batch(std::size_t frames) {
        std::uint64_t sum = 0;
        for (std::size_t b = 0; b < frames; ++b) {
            for (std::size_t i = 0; i < resource_count; ++i)
                owners.push_back(Ops::make(b * resource_count + i));
            // Owners remain alive while 4,096 non-owning draw records are read.
            for (std::size_t i = 0; i < draw_count; ++i)
                draws.push_back(Ops::borrow(owners[(i * 17) % resource_count]));
            sum += consume(draws);
            draws.clear();
            owners.clear();
        }
        return sum;
    }
    std::uint64_t vector_owner_transfer(std::size_t batches) {
        std::uint64_t sum = 0;
        for (std::size_t b = 0; b < batches; ++b) {
            std::move(source.begin(), source.end(), std::back_inserter(target));
            source.clear();
            for (const auto& value : target) transfer_views.push_back(Ops::borrow(value));
            sum += consume(transfer_views);
            transfer_views.clear();
            source.swap(target);
        }
        return sum;
    }
};

// Code-generation evidence only, NEVER interpreted as a nanosecond measurement.
// No intermediate owner state escapes; the optimizer may remove the roundtrips.
template<class Ops>
std::uint64_t unobserved_moves(typename Ops::owner& root, std::size_t iterations) {
    for (std::size_t i = 0; i < iterations; ++i) {
        auto moved = std::move(root);
        root = std::move(moved);
    }
    return root->words[0];
}
extern "C" [[gnu::noinline]] std::uint64_t bench_unobserved_own_moves(own_unique::owner& root, std::size_t n) {
    return unobserved_moves<own_unique>(root, n);
}
extern "C" [[gnu::noinline]] std::uint64_t bench_unobserved_std_moves(std_default::owner& root, std::size_t n) {
    return unobserved_moves<std_default>(root, n);
}

struct sample_case {
    const char* name;
    const char* implementation;
    std::size_t operations, expected_objects, handle_bytes, view_bytes;
    std::uint64_t expected_checksum;
    std::function<std::uint64_t()> run;
};
std::array<std::uint64_t, case_count> expected(counts n) {
    std::array<std::uint64_t, case_count> sums{};
    for (std::size_t i = 0; i < n.create; ++i) sums[0] += word(i, i & 7);
    for (std::size_t i = 0; i < n.move; ++i) sums[1] += word(17, i & 7);
    for (std::size_t i = 0; i < n.borrow; ++i) sums[2] += word(17, i & 7);
    for (std::size_t b = 0; b < n.frames; ++b)
        for (std::size_t i = 0; i < draw_count; ++i)
            sums[3] += word(b * resource_count + (i * 17) % resource_count, i & 7);
    for (std::size_t b = 0; b < n.transfers; ++b)
        for (std::size_t i = 0; i < transfer_count; ++i) sums[4] += word(i, i & 7);
    for (std::size_t i = 0; i < n.jobs; ++i)
        for (std::size_t j = 0; j < 8; ++j) sums[5] += word(i, j);
    return sums;
}
template<class Ops>
void add_cases(std::vector<sample_case>& cases, workload<Ops>& w, const char* impl, counts n) {
    const auto sums = expected(n);
    const auto h = sizeof(typename Ops::owner), v = sizeof(typename Ops::view);
    cases.push_back({"create_read_destroy", impl, n.create, n.create, h, v, sums[0],
                     [&w, n] { return w.create_read_destroy(n.create); }});
    cases.push_back({"observable_move_roundtrip", impl, n.move, 0, h, v, sums[1],
                     [&w, n] { return w.observable_move_roundtrip(n.move); }});
    cases.push_back({"borrow_read_loop", impl, n.borrow, 0, h, v, sums[2],
                     [&w, n] { return w.borrow_read_loop(n.borrow); }});
    cases.push_back({"frame_draw_batch", impl, n.frames, n.frames * resource_count, h, v, sums[3],
                     [&w, n] { return w.frame_draw_batch(n.frames); }});
    cases.push_back({"vector_owner_transfer", impl, n.transfers, 0, h, v, sums[4],
                     [&w, n] { return w.vector_owner_transfer(n.transfers); }});
    cases.push_back({"synchronized_job_handoff", impl, n.jobs, n.jobs, h, v, sums[5],
                     [&w, n] { return w.queue.run(n.jobs); }});
}
} // namespace bench

