// Allocator-aware exclusive ownership: handle size versus allocation size.
//
// own::allocated_unique_owner keeps its cleanup state in the handle (pointer,
// storage, allocator context, deallocate and dispose: 5 words), so the
// allocator receives exactly sizeof(T). The prototype here moves that state
// into a header at the front of the allocation and keeps 2 words in the handle
// (payload pointer and header), at the cost of a larger allocation. This file
// measures that tradeoff only; the library header is not changed.
//
// Baselines in the same binary: std::unique_ptr with the 5-word erased deleter
// upstream's unique benchmarks use (twice, as an A/A control) and plain
// std::unique_ptr<T> (1 word, no allocator support) as the floor.
#include <own/ownership.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
using deallocate_fn = void (*)(void*, void*, std::size_t, std::size_t) noexcept;

template<class T> inline void escape(const T& value) noexcept {
    asm volatile("" : : "g"(std::addressof(value)) : "memory");
}

struct resource {
    std::uint64_t id;
    std::uint64_t words[3];
    explicit resource(std::uint64_t i) noexcept : id(i), words{i, i + 1, i + 2} {}
};

// ---- Prototype: cleanup header inside the allocation, 2-word handle. ----
namespace proto {
struct header {
    void* context;
    deallocate_fn deallocate;
    void (*dispose)(header*) noexcept;
};
template<class T>
struct block {
    header head;
    alignas(T) unsigned char storage[sizeof(T)];
};
template<class T>
void dispose(header* head) noexcept {
    auto* whole = reinterpret_cast<block<T>*>(head);
    std::launder(reinterpret_cast<T*>(whole->storage))->~T();
    auto context = head->context;
    auto deallocate = head->deallocate;
    deallocate(context, whole, sizeof(block<T>), alignof(block<T>));
}

template<class T>
class owner {
public:
    owner() noexcept = default;
    owner(T* pointer, header* head) noexcept : pointer_(pointer), head_(head) {}
    owner(owner&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)), head_(other.head_) {}
    owner& operator=(owner&& other) noexcept {
        owner(std::move(other)).swap(*this);
        return *this;
    }
    ~owner() { if (pointer_) head_->dispose(head_); }
    void swap(owner& other) noexcept {
        std::swap(pointer_, other.pointer_);
        std::swap(head_, other.head_);
    }
    T* operator->() const noexcept { return pointer_; }
    own::local_view<T> borrow() const& noexcept = delete; // not needed for the measurement
    explicit operator bool() const noexcept { return pointer_ != nullptr; }

private:
    // pointer_ is the ownership indicator; head_ is read only when it is set.
    T* pointer_ = nullptr;
    header* head_ = nullptr;
};

template<class T, class... Args>
owner<T> allocate(own::allocator_ref allocator, Args&&... args) {
    void* storage = allocator.allocate(allocator.context, sizeof(block<T>), alignof(block<T>));
    if (!storage) throw std::bad_alloc();
    auto* whole = static_cast<block<T>*>(storage);
    try {
        auto* value = ::new (static_cast<void*>(whole->storage)) T(std::forward<Args>(args)...);
        ::new (static_cast<void*>(&whole->head)) header{allocator.context, allocator.deallocate, &dispose<T>};
        return {value, &whole->head};
    } catch (...) {
        allocator.deallocate(allocator.context, storage, sizeof(block<T>), alignof(block<T>));
        throw;
    }
}
} // namespace proto

// ---- Upstream's erased-deleter std baseline (5 words). ----
struct erased_delete {
    void* storage = nullptr;
    void* context = nullptr;
    deallocate_fn deallocate = nullptr;
    void (*dispose)(void*, void*, deallocate_fn) noexcept = nullptr;
    template<class T> void operator()(T*) const noexcept { dispose(storage, context, deallocate); }
};
template<class T>
void erased_dispose(void* storage, void* context, deallocate_fn deallocate) noexcept {
    static_cast<T*>(storage)->~T();
    deallocate(context, storage, sizeof(T), alignof(T));
}
template<class T, class... Args>
std::unique_ptr<T, erased_delete> make_erased(own::allocator_ref allocator, Args&&... args) {
    void* storage = allocator.allocate(allocator.context, sizeof(T), alignof(T));
    if (!storage) throw std::bad_alloc();
    try {
        auto* value = ::new (storage) T(std::forward<Args>(args)...);
        return {value, {storage, allocator.context, allocator.deallocate, &erased_dispose<T>}};
    } catch (...) {
        allocator.deallocate(allocator.context, storage, sizeof(T), alignof(T));
        throw;
    }
}

struct own40 {
    using owner = own::allocated_unique_owner<resource>;
    static owner make(std::uint64_t id) { return own::allocate_unique<resource>({}, id); }
};
struct proto16 {
    using owner = proto::owner<resource>;
    static owner make(std::uint64_t id) { return proto::allocate<resource>({}, id); }
};
struct std_erased40 {
    using owner = std::unique_ptr<resource, erased_delete>;
    static owner make(std::uint64_t id) { return make_erased<resource>({}, id); }
};
struct std_default8 {
    using owner = std::unique_ptr<resource>;
    static owner make(std::uint64_t id) { return std::make_unique<resource>(id); }
};

static_assert(sizeof(own40::owner) == 5 * sizeof(void*));
static_assert(sizeof(proto16::owner) == 2 * sizeof(void*));
static_assert(sizeof(std_erased40::owner) == 5 * sizeof(void*));

double elapsed_ns(clock_type::time_point start) {
    return std::chrono::duration<double, std::nano>(clock_type::now() - start).count();
}

template<class Impl>
double create_destroy(std::size_t n) {
    std::uint64_t sum = 0;
    auto start = clock_type::now();
    for (std::size_t i = 0; i < n; ++i) {
        auto owner = Impl::make(i);
        escape(owner);
        sum += owner->id;
    }
    double ns = elapsed_ns(start);
    if (sum != n * (n - 1) / 2) throw std::runtime_error("create_destroy checksum");
    return ns / static_cast<double>(n);
}

// Moves one owner through 64 slots; every slot is observable so no move can
// be elided. ns per move.
template<class Impl>
double move_chain(std::size_t rounds) {
    std::vector<typename Impl::owner> slots(64);
    slots[0] = Impl::make(7);
    std::uint64_t sum = 0;
    auto start = clock_type::now();
    for (std::size_t r = 0; r < rounds; ++r) {
        for (std::size_t i = 1; i < slots.size(); ++i) {
            slots[i] = std::move(slots[i - 1]);
            escape(slots[i]);
        }
        slots[0] = std::move(slots.back());
        sum += slots[0]->id;
    }
    double ns = elapsed_ns(start);
    if (sum != 7 * rounds) throw std::runtime_error("move_chain checksum");
    return ns / static_cast<double>(rounds * slots.size());
}

// Moves 4,096 owners between two reserved vectors and back. ns per element move.
template<class Impl>
double vector_transfer(std::size_t rounds) {
    constexpr std::size_t count = 4096;
    std::vector<typename Impl::owner> a, b;
    a.reserve(count);
    b.reserve(count);
    for (std::size_t i = 0; i < count; ++i) a.push_back(Impl::make(i));
    std::uint64_t sum = 0;
    auto start = clock_type::now();
    for (std::size_t r = 0; r < rounds; ++r) {
        for (auto& owner : a) b.push_back(std::move(owner));
        a.clear();
        for (auto& owner : b) a.push_back(std::move(owner));
        b.clear();
        sum += a[r % count]->id;
    }
    double ns = elapsed_ns(start);
    std::uint64_t expected = 0;
    for (std::size_t r = 0; r < rounds; ++r) expected += r % count;
    if (sum != expected) throw std::runtime_error("vector_transfer checksum");
    return ns / static_cast<double>(rounds * count * 2);
}

// Reads every payload through a vector of 4,096 owners: the handle stride
// decides how many handle bytes the scan touches. ns per read.
template<class Impl>
double owner_scan(std::size_t passes) {
    constexpr std::size_t count = 4096;
    std::vector<typename Impl::owner> owners;
    owners.reserve(count);
    for (std::size_t i = 0; i < count; ++i) owners.push_back(Impl::make(i));
    std::uint64_t sum = 0;
    auto start = clock_type::now();
    for (std::size_t p = 0; p < passes; ++p) {
        for (auto& owner : owners) sum += owner->id;
        escape(sum);
    }
    double ns = elapsed_ns(start);
    if (sum != passes * (count * (count - 1) / 2)) throw std::runtime_error("owner_scan checksum");
    return ns / static_cast<double>(passes * count);
}

struct sample_case {
    std::string name, implementation;
    std::function<double()> run;
    std::vector<double> samples;
};

std::size_t arg(int argc, char** argv, const char* name, std::size_t fallback) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return std::strtoull(argv[i + 1], nullptr, 10);
    return fallback;
}
std::string arg_text(int argc, char** argv, const char* name, const char* fallback) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return fallback;
}
double quantile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(v.size())));
    return v[std::max<std::size_t>(rank, 1) - 1];
}
} // namespace

int main(int argc, char** argv) try {
    const std::size_t samples = arg(argc, argv, "--samples", 15);
    const std::size_t warmups = arg(argc, argv, "--warmups", 2);
    const std::size_t scale = std::max<std::size_t>(1, arg(argc, argv, "--scale", 1));
    const std::string output = arg_text(argc, argv, "--output", "");

    std::vector<sample_case> cases;
    auto add_impl = [&](const char* name, auto impl) {
        using Impl = decltype(impl);
        cases.push_back({"create_read_destroy", name, [=] { return create_destroy<Impl>(400000 / scale); }, {}});
        cases.push_back({"move_chain", name, [=] { return move_chain<Impl>(20000 / scale); }, {}});
        cases.push_back({"vector_transfer_4096", name, [=] { return vector_transfer<Impl>(100 / scale + 1); }, {}});
        cases.push_back({"owner_scan_4096", name, [=] { return owner_scan<Impl>(400 / scale + 1); }, {}});
    };
    add_impl("own_allocated_40", own40{});
    add_impl("proto_header_16", proto16{});
    add_impl("std_erased_40", std_erased40{});
    add_impl("std_erased_40_b", std_erased40{});
    add_impl("std_default_8", std_default8{});

    std::mt19937_64 rng(20261007);
    std::vector<std::size_t> order(cases.size());
    for (std::size_t round = 0; round < warmups + samples; ++round) {
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::shuffle(order.begin(), order.end(), rng);
        for (auto index : order) {
            double ns = cases[index].run();
            if (round >= warmups) cases[index].samples.push_back(ns);
        }
    }

    std::printf("SIZE handle own_allocated_40=%zu proto_header_16=%zu std_erased_40=%zu std_default_8=%zu\n",
                sizeof(own40::owner), sizeof(proto16::owner), sizeof(std_erased40::owner), sizeof(std_default8::owner));
    std::printf("SIZE allocation own_allocated_40=%zu proto_header_16=%zu (payload %zu)\n", sizeof(resource),
                sizeof(proto::block<resource>), sizeof(resource));
    std::ofstream raw, summary;
    if (!output.empty()) {
        raw.open(output + "/raw.csv");
        summary.open(output + "/summary.csv");
        raw << "case,implementation,sample,ns\n";
        summary << "case,implementation,samples,median_ns,min_ns,p90_ns,cv\n";
    }
    std::printf("%-22s %-17s %10s %10s %10s %7s\n", "case", "impl", "median_ns", "min_ns", "p90_ns", "cv");
    for (auto& c : cases) {
        double mean = 0;
        for (double v : c.samples) mean += v;
        mean /= static_cast<double>(c.samples.size());
        double var = 0;
        for (double v : c.samples) var += (v - mean) * (v - mean);
        double cv = c.samples.size() > 1 ? std::sqrt(var / static_cast<double>(c.samples.size() - 1)) / mean : 0;
        double med = quantile(c.samples, .5), mn = quantile(c.samples, 0), p90 = quantile(c.samples, .9);
        std::printf("%-22s %-17s %10.3f %10.3f %10.3f %7.3f\n", c.name.c_str(), c.implementation.c_str(), med, mn,
                    p90, cv);
        std::printf("RESULT %s/%s %.3f ns\n", c.name.c_str(), c.implementation.c_str(), med);
        if (raw.is_open()) {
            for (std::size_t s = 0; s < c.samples.size(); ++s)
                raw << c.name << ',' << c.implementation << ',' << s << ',' << c.samples[s] << '\n';
            summary << c.name << ',' << c.implementation << ',' << c.samples.size() << ',' << med << ',' << mn
                    << ',' << p90 << ',' << cv << '\n';
        }
    }
    std::printf("samples=%zu warmups=%zu scale=%zu\n", samples, warmups, scale);
    std::printf("verification: every sample matched its checksum\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "allocated_unique_layout: %s\n", error.what());
    return 1;
}
