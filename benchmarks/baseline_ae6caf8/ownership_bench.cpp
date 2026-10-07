// Reproducible ownership cost and engine-shaped workflow benchmarks.
// This is a CPU simulation. It does not integrate with a renderer or GPU API.
#include <own/ownership.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#if __has_include(<sys/single_threaded.h>)
#include <sys/single_threaded.h>
#define BENCH_HAS_GLIBC_THREAD_FLAG 1
#endif

namespace bench {
using clock = std::chrono::steady_clock;
std::uint64_t observable_checksum = 0;

template<class T> inline void escape(const T& value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(std::addressof(value)) : "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}
inline void check(bool value, const char* what) {
    if (!value) throw std::runtime_error(what);
}

struct asset {
    std::uint64_t id;
    std::uint64_t version;
    std::array<std::uint64_t, 8> words;
    std::atomic<std::size_t>* deaths;
    asset(std::uint64_t i = 1, std::uint64_t v = 1,
          std::atomic<std::size_t>* d = nullptr) : id(i), version(v), deaths(d) {
        for (std::size_t j = 0; j < words.size(); ++j) words[j] = i * 13 + v * 7 + j;
    }
    ~asset() { if (deaths) deaths->fetch_add(1, std::memory_order_relaxed); }
};

struct std_ops {
    template<class T> using shared = std::shared_ptr<T>;
    template<class T> using local = std::shared_ptr<T>;
    template<class T> using weak = std::weak_ptr<T>;
    static auto make(std::uint64_t i, std::uint64_t v = 1,
                     std::atomic<std::size_t>* d = nullptr) {
        return std::make_shared<asset>(i, v, d);
    }
    static auto localize(const shared<asset>& p) { return p; }
    static auto share(const local<asset>& p) { return p; }
};
struct own_ops {
    template<class T> using shared = own::shared_owner<T>;
    template<class T> using local = own::local_owner<T>;
    template<class T> using weak = own::weak_owner<T>;
    static auto make(std::uint64_t i, std::uint64_t v = 1,
                     std::atomic<std::size_t>* d = nullptr) {
        return own::make_shared<asset>(i, v, d);
    }
    static auto localize(const shared<asset>& p) { return p.localize(); }
    static auto share(const local<asset>& p) { return p.share(); }
};

struct sample_case {
    std::string name, implementation, unit;
    std::size_t operations;
    std::function<std::uint64_t()> run;
    std::vector<double> samples;
};

// The caller retains its owner throughout, so final payload destruction is not
// mixed into the copy/drop microbenchmark. Escaping the copy prevents elision.
template<class Pointer>
std::uint64_t copy_drop(const Pointer& root, std::size_t iterations) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < iterations; ++i) {
        Pointer copy = root;
        escape(copy);
        sum += copy->words[i & 7];
    }
    return sum;
}

template<class Ops>
class scene {
    using Shared = typename Ops::template shared<asset>;
    using Local = typename Ops::template local<asset>;
    static constexpr std::size_t resources = 64;
    static constexpr std::size_t objects = 4096;
    std::vector<Shared> registry_;
    std::vector<Local> groups_;
    std::vector<Local> objects_;
    std::vector<Shared> leases_;
public:
    scene() {
        registry_.reserve(resources); groups_.reserve(resources);
        objects_.reserve(objects); leases_.reserve(resources);
        for (std::size_t i = 0; i < resources; ++i) {
            registry_.push_back(Ops::make(i));
            groups_.push_back(Ops::localize(registry_.back()));
        }
        attach();
    }
    std::uint64_t attach() {
        objects_.clear();
        for (std::size_t i = 0; i < objects; ++i)
            objects_.push_back(groups_[(i * 17) % resources]);
        escape(objects_);
        return objects_.size();
    }
    std::uint64_t frame() {
        // Both implementations deduplicate to ONE global lease per resource.
        // Existing scene objects are borrowed during submission, not recopied.
        std::array<bool, resources> seen{};
        leases_.clear();
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            const auto& object = objects_[i];
            const auto id = object->id;
            if (!seen[id]) {
                seen[id] = true;
                leases_.push_back(Ops::share(object));
            }
            sum += object->words[i & 7];
        }
        escape(leases_);
        leases_.clear();
        return sum;
    }
};

// A persistent queue and worker. Only global shared owners cross its boundary.
// Worker-local groups are created, copied and destroyed on that worker only.
template<class Ops>
class async_queue {
    using Shared = typename Ops::template shared<asset>;
    struct job { Shared value; std::size_t copies; };
    std::mutex mutex_;
    std::condition_variable available_, finished_;
    std::deque<job> jobs_;
    bool stopping_ = false;
    std::size_t submitted_ = 0, completed_ = 0;
    std::uint64_t sum_ = 0;
    std::thread worker_;
    void work() {
        for (;;) {
            job item;
            {
                std::unique_lock lock(mutex_);
                available_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
                if (stopping_ && jobs_.empty()) return;
                item = std::move(jobs_.front());
                jobs_.pop_front();
            }
            std::uint64_t value;
            {
                auto local = Ops::localize(item.value);
                value = copy_drop(local, item.copies);
            }
            item.value.reset();
            {
                std::lock_guard lock(mutex_);
                sum_ += value;
                ++completed_;
            }
            finished_.notify_one();
        }
    }
public:
    async_queue() : worker_([this] { work(); }) {}
    ~async_queue() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        available_.notify_one();
        worker_.join();
    }
    std::uint64_t run(const Shared& value, std::size_t count, std::size_t copies) {
        std::unique_lock lock(mutex_);
        auto before = sum_;
        for (std::size_t i = 0; i < count; ++i) jobs_.push_back({value, copies});
        submitted_ += count;
        available_.notify_one();
        finished_.wait(lock, [&] { return completed_ == submitted_; });
        return sum_ - before;
    }
};

class copy_workers {
    std::barrier<> begin_, end_;
    std::vector<std::thread> workers_;
    std::function<void(std::size_t)> function_;
    bool stop_ = false;
public:
    explicit copy_workers(std::size_t count) : begin_(count + 1), end_(count + 1) {
        for (std::size_t i = 0; i < count; ++i) workers_.emplace_back([this, i] {
            for (;;) {
                begin_.arrive_and_wait();
                if (stop_) return;
                function_(i);
                end_.arrive_and_wait();
            }
        });
    }
    ~copy_workers() {
        stop_ = true;
        begin_.arrive_and_wait();
        for (auto& worker : workers_) worker.join();
    }
    template<class F> void run(F&& function) {
        function_ = std::forward<F>(function);
        begin_.arrive_and_wait();
        end_.arrive_and_wait();
    }
};

struct allocations {
    std::size_t calls = 0, frees = 0, bytes = 0, live_bytes = 0, peak_bytes = 0;
    void* allocate(std::size_t size, std::size_t alignment) {
        auto* result = ::operator new(size, std::align_val_t(alignment));
        ++calls; bytes += size; live_bytes += size;
        peak_bytes = std::max(peak_bytes, live_bytes);
        return result;
    }
    void deallocate(void* ptr, std::size_t size, std::size_t alignment) noexcept {
        ++frees; live_bytes -= size;
        ::operator delete(ptr, std::align_val_t(alignment));
    }
    own::allocator_ref ref() {
        return {this,
            [](void* c, std::size_t n, std::size_t a) -> void* {
                return static_cast<allocations*>(c)->allocate(n, a);
            },
            [](void* c, void* p, std::size_t n, std::size_t a) noexcept {
                static_cast<allocations*>(c)->deallocate(p, n, a);
            }};
    }
};
// Stateless allocator keeps the std control block's allocator storage empty,
// like std::make_shared's default allocator. Used only in this serial report.
thread_local allocations* active_allocations = nullptr;
template<class T> struct counting_allocator {
    using value_type = T;
    explicit counting_allocator(allocations* c) { active_allocations = c; }
    template<class U> counting_allocator(const counting_allocator<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(active_allocations->allocate(n * sizeof(T), alignof(T))); }
    void deallocate(T* p, std::size_t n) noexcept { active_allocations->deallocate(p, n * sizeof(T), alignof(T)); }
    template<class U> bool operator==(const counting_allocator<U>&) const { return true; }
};

void allocation_report(const std::filesystem::path& output) {
    std::ofstream csv(output / "allocations.csv");
    csv << "scenario,implementation,allocation_calls,deallocation_calls,total_requested_bytes,peak_requested_bytes,live_bytes_after_release\n";
    auto record = [&](const char* scenario, const char* impl, auto action) {
        allocations counts;
        action(counts);
        check(counts.calls == counts.frees && counts.live_bytes == 0, "allocation leak in benchmark");
        csv << scenario << ',' << impl << ',' << counts.calls << ',' << counts.frees << ','
            << counts.bytes << ',' << counts.peak_bytes << ',' << counts.live_bytes << '\n';
    };
    record("create_shared", "std_shared", [](auto& c) {
        auto value = std::allocate_shared<asset>(counting_allocator<asset>(&c), 1); escape(value);
    });
    record("create_shared", "own_shared", [](auto& c) {
        auto value = own::allocate_shared<asset>(c.ref(), 1); escape(value);
    });
    record("create_local", "std_shared", [](auto& c) {
        auto value = std::allocate_shared<asset>(counting_allocator<asset>(&c), 1); escape(value);
    });
    record("create_local", "own_local", [](auto& c) {
        auto value = own::allocate_local<asset>(c.ref(), 1); escape(value);
    });
    record("one_group_4096_attachments", "std_shared", [](auto& c) {
        auto root = std::allocate_shared<asset>(counting_allocator<asset>(&c), 1);
        auto group = root;
        std::vector<std::shared_ptr<asset>> values(4096, group); escape(values);
    });
    record("one_group_4096_attachments", "own_local", [](auto& c) {
        auto root = own::allocate_shared<asset>(c.ref(), 1);
        auto group = root.localize(c.ref());
        std::vector<own::local_owner<asset>> values(4096, group); escape(values);
    });
    record("4096_independent_localizations", "std_shared", [](auto& c) {
        auto root = std::allocate_shared<asset>(counting_allocator<asset>(&c), 1);
        std::vector<std::shared_ptr<asset>> values;
        values.reserve(4096);
        for (std::size_t i = 0; i < 4096; ++i) values.push_back(root);
        escape(values);
    });
    record("4096_independent_localizations", "own_local", [](auto& c) {
        auto root = own::allocate_shared<asset>(c.ref(), 1);
        std::vector<own::local_owner<asset>> values;
        values.reserve(4096);
        for (std::size_t i = 0; i < 4096; ++i) values.push_back(root.localize(c.ref()));
        escape(values);
    });
    std::ofstream sizes(output / "sizes.csv");
    sizes << "type,sizeof_bytes\nasset," << sizeof(asset)
          << "\nstd_shared_ptr," << sizeof(std::shared_ptr<asset>)
          << "\nstd_weak_ptr," << sizeof(std::weak_ptr<asset>)
          << "\nown_shared_owner," << sizeof(own::shared_owner<asset>)
          << "\nown_local_owner," << sizeof(own::local_owner<asset>)
          << "\nown_weak_owner," << sizeof(own::weak_owner<asset>) << '\n';
}

// Fixed-capacity, externally synchronized deferred destruction simulation.
// The callback is noexcept, does not allocate, and must never overflow.
// A real engine must provide a reliable queue and real completion fences.
template<class Task> class fence_queue {
    struct entry { std::uint64_t fence = 0; std::optional<Task> task; };
    std::array<entry, 256> entries_{};
    std::mutex mutex_;
    std::uint64_t completed_ = 0;
public:
    void retire(Task task) noexcept {
        std::lock_guard lock(mutex_);
        for (auto& entry : entries_) if (!entry.task) {
            entry.fence = completed_ + 3;
            entry.task.emplace(std::move(task));
            return;
        }
        std::terminate();
    }
    void advance(std::uint64_t completed) {
        std::array<std::optional<Task>, 256> ready;
        {
            std::lock_guard lock(mutex_);
            completed_ = completed;
            std::size_t i = 0;
            for (auto& entry : entries_) if (entry.task && entry.fence <= completed_) {
                ready[i++].emplace(std::move(*entry.task));
                entry.task.reset();
            }
        }
        for (auto& task : ready) if (task) task->run();
    }
    ~fence_queue() { advance(UINT64_MAX); }
};
struct std_retirement_task {
    asset* value = nullptr;
    explicit std_retirement_task(asset* p) noexcept : value(p) {}
    std_retirement_task(std_retirement_task&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    std_retirement_task(const std_retirement_task&) = delete;
    void run() noexcept { delete std::exchange(value, nullptr); }
    ~std_retirement_task() { run(); }
};

void lifetime_validation(const std::filesystem::path& output) {
    std::ofstream trace(output / "lifetime_validation.txt");
    {
        std::atomic<std::size_t> deaths{0};
        fence_queue<own::retirement_task> queue;
        own::retirement_hook hook{&queue, [](void* c, own::retirement_task task) noexcept {
            static_cast<fence_queue<own::retirement_task>*>(c)->retire(std::move(task));
        }};
        auto version1 = own::make_shared_with<asset>(hook, 7, 1, &deaths);
        own::weak_owner<asset> cache = version1;
        auto scene = version1.localize();
        auto gpu_lease = scene.share();
        auto version2 = own::make_shared_with<asset>(hook, 7, 2, &deaths);
        version1 = version2; // Registry hot reload; existing scene still owns v1.
        check(scene->version == 1 && version1->version == 2, "own hot reload lost old version");
        scene.reset();
        check(!cache.expired() && deaths.load() == 0, "own GPU lease did not retain old payload");
        std::thread([lease = std::move(gpu_lease)]() mutable { lease.reset(); }).join();
        check(cache.expired() && !cache.lock() && deaths.load() == 0, "own weak/deferred semantics");
        queue.advance(2);
        check(deaths.load() == 0, "own retired before fence");
        queue.advance(3);
        check(deaths.load() == 1, "own failed to retire at fence");
        cache.reset();
        version1.reset(); version2.reset();
        queue.advance(6);
        check(deaths.load() == 2, "own scene/cache release leaked payload");
        trace << "own: old hot-reload version retained by scene and global frame lease; weak cache expired at last strong; destructor waited for simulated fence; both versions destroyed exactly once: PASS\n";
    }
    {
        std::atomic<std::size_t> deaths{0};
        fence_queue<std_retirement_task> queue;
        auto make = [&](std::uint64_t v) {
            return std::shared_ptr<asset>(new asset(7, v, &deaths), [&queue](asset* p) noexcept {
                queue.retire(std_retirement_task(p));
            });
        };
        auto version1 = make(1);
        std::weak_ptr<asset> cache = version1;
        auto scene = version1;
        auto gpu_lease = scene;
        auto version2 = make(2);
        version1 = version2;
        check(scene->version == 1 && version1->version == 2, "std hot reload lost old version");
        scene.reset();
        check(!cache.expired() && deaths.load() == 0, "std GPU lease did not retain old payload");
        std::thread([lease = std::move(gpu_lease)]() mutable { lease.reset(); }).join();
        check(cache.expired() && !cache.lock() && deaths.load() == 0, "std weak/deferred semantics");
        queue.advance(2); check(deaths.load() == 0, "std retired before fence");
        queue.advance(3); check(deaths.load() == 1, "std failed to retire at fence");
        cache.reset(); version1.reset(); version2.reset();
        queue.advance(6); check(deaths.load() == 2, "std scene/cache release leaked payload");
        trace << "std: equivalent custom-deleter lifetime sequence: PASS\n";
    }
    trace << "Fences are monotonically advanced integers; no GPU, renderer, graphics driver, or actual completion event is measured. Deferred std payloads use a separate allocation because make_shared cannot accept a custom deleter. This validation is untimed.\n";
}

// All lifetime checks remain enabled in the optimized benchmark. This cycle
// includes creation, local group setup, scene attach/unload, hot reload, and
// weak-cache expiration. The async queue and deferred GPU simulation are tested
// separately so their scheduler/retirement costs are not mislabeled as RC cost.
template<class Ops>
std::uint64_t resource_lifecycle() {
    using Shared = typename Ops::template shared<asset>;
    using Local = typename Ops::template local<asset>;
    using Weak = typename Ops::template weak<asset>;
    std::atomic<std::size_t> deaths{0};
    std::vector<Shared> registry;
    std::vector<Local> groups, objects;
    std::vector<Weak> cache;
    registry.reserve(64); groups.reserve(64); cache.reserve(64);
    objects.reserve(4096);
    for (std::size_t i = 0; i < 64; ++i) {
        registry.push_back(Ops::make(i, 1, &deaths));
        groups.push_back(Ops::localize(registry.back()));
        cache.push_back(Weak(registry.back()));
    }
    for (std::size_t i = 0; i < 4096; ++i) objects.push_back(groups[(i * 17) % 64]);
    groups.clear();
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < 64; ++i) registry[i] = Ops::make(i, 2, &deaths);
    for (std::size_t i = 0; i < objects.size(); ++i) {
        check(objects[i]->version == 1, "hot reload invalidated a scene attachment");
        sum += objects[i]->words[i & 7];
    }
    check(deaths.load() == 0, "old versions died while scene was attached");
    objects.clear();
    for (const auto& cached : cache) {
        check(cached.expired(), "old weak cache did not expire on scene release");
        auto failed = cached.lock();
        check(!failed, "expired cache resurrected old asset");
        escape(failed);
    }
    check(deaths.load() == 64, "scene release did not destroy old versions");
    cache.clear();
    registry.clear();
    check(deaths.load() == 128, "registry/cache release leaked assets");
    return sum;
}

struct settings { std::size_t samples = 101, iterations = 100000, warmups = 5; std::filesystem::path output = "benchmarks/results/latest"; };
settings parse(int argc, char** argv) {
    settings options;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (i + 1 == argc) throw std::runtime_error("missing option value");
        std::string value = argv[++i];
        if (key == "--samples") options.samples = std::stoull(value);
        else if (key == "--iterations") options.iterations = std::stoull(value);
        else if (key == "--warmups") options.warmups = std::stoull(value);
        else if (key == "--output") options.output = value;
        else throw std::runtime_error("unknown option: " + key);
    }
    check(options.samples >= 3 && options.iterations >= 1000, "samples >= 3 and iterations >= 1000 required");
    return options;
}
} // namespace bench

int main(int argc, char** argv) try {
    using namespace bench;
    auto options = parse(argc, argv);
    std::filesystem::create_directories(options.output);
    // Required fairness guard: glibc/libstdc++ may use non-atomic refcounts until
    // the process has created a thread. Activate threading before any timing.
    std::thread([] {}).join();
#ifdef BENCH_HAS_GLIBC_THREAD_FLAG
    check(__libc_single_threaded == 0, "libstdc++ single-thread refcount path remains active");
#endif
    allocation_report(options.output);
    lifetime_validation(options.output);

    const auto n = options.iterations;
    auto std_root = std::make_shared<asset>(1);
    auto own_root = own::make_shared<asset>(1);
    auto own_local = own_root.localize();
    auto std_local = std_root;
    std::weak_ptr<asset> std_weak = std_root;
    own::weak_owner<asset> own_weak = own_root;
    auto std_dying = std::make_shared<asset>(2);
    auto own_dying = own::make_shared<asset>(2);
    std::weak_ptr<asset> std_expired = std_dying;
    own::weak_owner<asset> own_expired = own_dying;
    std_dying.reset(); own_dying.reset();
    scene<std_ops> std_scene;
    scene<own_ops> own_scene;
    async_queue<std_ops> std_async;
    async_queue<own_ops> own_async;
    copy_workers contenders(2);
    std::array<std::uint64_t, 2> sums{};
    std::vector<sample_case> cases;
    auto add = [&](std::string name, std::string impl, std::string unit,
                   std::size_t count, std::function<std::uint64_t()> fn) {
        cases.push_back({std::move(name), std::move(impl), std::move(unit), count, std::move(fn), {}});
    };
    add("copy_read_drop", "std_shared", "copy", n, [&] { return copy_drop(std_root, n); });
    add("copy_read_drop", "own_shared", "copy", n, [&] { return copy_drop(own_root, n); });
    add("copy_read_drop", "own_local", "copy", n, [&] { return copy_drop(own_local, n); });
    add("borrow_read", "raw_borrow", "read", n, [&] {
        auto* root = std_root.get(); std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto* value = root; escape(value); sum += value->words[i & 7]; }
        return sum;
    });
    add("localize_read_drop", "std_shared", "group", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto local = std_ops::localize(std_root); escape(local); sum += local->words[i & 7]; }
        return sum;
    });
    add("localize_read_drop", "own_local", "group", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto local = own_ops::localize(own_root); escape(local); sum += local->words[i & 7]; }
        return sum;
    });
    for (auto copies : {std::size_t(1), std::size_t(8), std::size_t(64), std::size_t(1024)}) {
        auto groups = std::max<std::size_t>(32, n / copies);
        auto name = "group_setup_then_" + std::to_string(copies) + "_copies";
        add(name, "std_shared", "group", groups, [&, copies, groups] {
            std::uint64_t sum = 0;
            for (std::size_t i = 0; i < groups; ++i) {
                auto local = std_ops::localize(std_root);
                sum += copy_drop(local, copies);
            }
            return sum;
        });
        add(name, "own_local", "group", groups, [&, copies, groups] {
            std::uint64_t sum = 0;
            for (std::size_t i = 0; i < groups; ++i) {
                auto local = own_ops::localize(own_root);
                sum += copy_drop(local, copies);
            }
            return sum;
        });
    }
    add("share_read_drop", "std_shared", "handoff", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto global = std_ops::share(std_local); escape(global); sum += global->words[i & 7]; }
        return sum;
    });
    add("share_read_drop", "own_shared", "handoff", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto global = own_ops::share(own_local); escape(global); sum += global->words[i & 7]; }
        return sum;
    });
    add("create_read_destroy", "std_shared", "asset", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto value = std::make_shared<asset>(i); escape(value); sum += value->words[i & 7]; }
        return sum;
    });
    add("create_read_destroy", "own_shared", "asset", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto value = own::make_shared<asset>(i); escape(value); sum += value->words[i & 7]; }
        return sum;
    });
    add("create_read_destroy", "own_local", "asset", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto value = own::make_local<asset>(i); escape(value); sum += value->words[i & 7]; }
        return sum;
    });
    add("weak_lock_live", "std_shared", "lock", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto value = std_weak.lock(); escape(value); sum += value->words[i & 7]; }
        return sum;
    });
    add("weak_lock_live", "own_shared", "lock", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto value = own_weak.lock(); escape(value); sum += value->words[i & 7]; }
        return sum;
    });
    add("weak_lock_expired", "std_shared", "lock", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto value = std_expired.lock(); escape(value); sum += bool(value); }
        return sum;
    });
    add("weak_lock_expired", "own_shared", "lock", n, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) { auto value = own_expired.lock(); escape(value); sum += bool(value); }
        return sum;
    });
    add("contended_copy_2_workers", "std_shared", "copy", 2 * n, [&] {
        contenders.run([&](std::size_t i) { sums[i] = copy_drop(std_root, n); });
        return sums[0] + sums[1];
    });
    add("contended_copy_2_workers", "own_shared", "copy", 2 * n, [&] {
        contenders.run([&](std::size_t i) { sums[i] = copy_drop(own_root, n); });
        return sums[0] + sums[1];
    });
    auto scenes = std::max<std::size_t>(1, n / 4096);
    auto frames = std::max<std::size_t>(4, n / 4096);
    add("scene_4096_attachments", "std_shared", "attachment", scenes * 4096, [&] {
        std::uint64_t sum = 0; for (std::size_t i = 0; i < scenes; ++i) sum += std_scene.attach(); return sum;
    });
    add("scene_4096_attachments", "own_local", "attachment", scenes * 4096, [&] {
        std::uint64_t sum = 0; for (std::size_t i = 0; i < scenes; ++i) sum += own_scene.attach(); return sum;
    });
    add("frame_4096_draws_64_unique_leases", "std_shared", "frame", frames, [&] {
        std::uint64_t sum = 0; for (std::size_t i = 0; i < frames; ++i) sum += std_scene.frame(); return sum;
    });
    add("frame_4096_draws_64_unique_leases", "own_shared", "frame", frames, [&] {
        std::uint64_t sum = 0; for (std::size_t i = 0; i < frames; ++i) sum += own_scene.frame(); return sum;
    });
    const auto cycles = std::max<std::size_t>(1, n / 10000);
    add("resource_lifecycle_64_assets_4096_objects", "std_shared", "cycle", cycles, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < cycles; ++i) sum += resource_lifecycle<std_ops>();
        return sum;
    });
    add("resource_lifecycle_64_assets_4096_objects", "own_local", "cycle", cycles, [&] {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < cycles; ++i) sum += resource_lifecycle<own_ops>();
        return sum;
    });
    const auto jobs = std::max<std::size_t>(32, n / 1000);
    for (auto copies : {std::size_t(1), std::size_t(64), std::size_t(1024)}) {
        auto name = "async_queue_job_" + std::to_string(copies) + "_local_copies";
        add(name, "std_shared", "job", jobs, [&, copies] { return std_async.run(std_root, jobs, copies); });
        add(name, "own_local", "job", jobs, [&, copies] { return own_async.run(own_root, jobs, copies); });
    }

    // One deterministic shuffled case ordering per round avoids systematically
    // assigning the same thermal/scheduler phase to either implementation.
    std::vector<std::size_t> order(cases.size());
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(0xC0FFEEu);
    for (std::size_t round = 0; round < options.warmups; ++round) {
        std::shuffle(order.begin(), order.end(), rng);
        for (auto index : order) observable_checksum += cases[index].run();
    }
    std::map<std::string, std::uint64_t> expected_checksums;
    std::ofstream raw(options.output / "raw.csv");
    raw << "round,order,case,implementation,unit,operations,elapsed_ns,ns_per_operation,checksum\n";
    raw << std::setprecision(12);
    for (std::size_t round = 0; round < options.samples; ++round) {
        std::shuffle(order.begin(), order.end(), rng);
        for (std::size_t slot = 0; slot < order.size(); ++slot) {
            auto& entry = cases[order[slot]];
            auto start = clock::now();
            auto checksum = entry.run();
            auto end = clock::now();
            escape(checksum);
            auto [expected, first] = expected_checksums.emplace(entry.name, checksum);
            check(first || expected->second == checksum, "payload checksum changed across implementations or samples");
            observable_checksum += checksum;
            auto elapsed = std::chrono::duration<double, std::nano>(end - start).count();
            auto per_op = elapsed / static_cast<double>(entry.operations);
            entry.samples.push_back(per_op);
            raw << round << ',' << slot << ',' << entry.name << ',' << entry.implementation << ','
                << entry.unit << ',' << entry.operations << ',' << elapsed << ',' << per_op << ',' << checksum << '\n';
        }
    }
    std::ofstream summary(options.output / "summary.csv");
    summary << "case,implementation,unit,samples,operations_per_sample,median_ns,p95_ns,p99_ns,min_ns,max_ns\n";
    summary << std::setprecision(12);
    std::cout << "case / implementation: median, p95, p99 ns per unit\n" << std::fixed << std::setprecision(3);
    for (auto& entry : cases) {
        std::sort(entry.samples.begin(), entry.samples.end());
        auto quantile = [&](double p) {
            // Nearest-rank quantiles; 101 samples have only limited tail fidelity.
            auto index = static_cast<std::size_t>(std::ceil(p * entry.samples.size())) - 1;
            return entry.samples[std::min(index, entry.samples.size() - 1)];
        };
        summary << entry.name << ',' << entry.implementation << ',' << entry.unit << ',' << entry.samples.size() << ','
                << entry.operations << ',' << quantile(.5) << ',' << quantile(.95) << ',' << quantile(.99) << ','
                << entry.samples.front() << ',' << entry.samples.back() << '\n';
        std::cout << entry.name << " / " << entry.implementation << ": " << quantile(.5) << ", "
                  << quantile(.95) << ", " << quantile(.99) << '\n';
    }
    std::ofstream runinfo(options.output / "run.txt");
    runinfo << "samples=" << options.samples << "\nwarmups=" << options.warmups << "\niterations=" << n
            << "\nshuffle_seed=12648430\nthread_activation=created_and_joined_before_timing\n"
            << "clock=steady_clock\ntail_definition=nearest_rank_across_batch_normalized_samples\n"
            << "observable_checksum=" << observable_checksum << '\n';
#ifdef BENCH_HAS_GLIBC_THREAD_FLAG
    runinfo << "glibc_single_threaded=" << static_cast<int>(__libc_single_threaded) << '\n';
#endif
    std::cout << "Output: " << options.output << "\nChecksum: " << observable_checksum << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "benchmark failed: " << error.what() << '\n';
    return 1;
}
