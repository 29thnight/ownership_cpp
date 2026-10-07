// Control-block layout: footprint, creation, dense scans and line sharing.
//
// Claim under test: a smaller shared control block lowers allocation size and
// improves scans over many owned objects, but places the payload on the same
// cache line as the reference counts, so a thread reading the payload while
// other threads copy owners of the same object sees the line move.
//
// Build this file once per header (OWN_INCLUDE) to compare layouts; the
// standard library is the in-binary baseline and runs twice (std_a, std_b) as
// an A/A control for run-to-run noise. Every sample verifies its checksum.
#include <own/ownership.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;

template<class T> inline void escape(const T& value) noexcept {
    asm volatile("" : : "g"(std::addressof(value)) : "memory");
}

struct payload {
    std::uint64_t value;
    explicit payload(std::uint64_t v) noexcept : value(v) {}
};

struct own_ops {
    using owner = own::shared_owner<payload>;
    static owner make(std::uint64_t v) { return own::make_shared<payload>(v); }
};
// Opt-in isolation: a 64-byte-aligned payload starts on its own cache line,
// away from the reference counts, whatever the header size.
struct alignas(64) padded_payload {
    std::uint64_t value;
    explicit padded_payload(std::uint64_t v) noexcept : value(v) {}
};
struct own_padded_ops {
    using owner = own::shared_owner<padded_payload>;
    static owner make(std::uint64_t v) { return own::make_shared<padded_payload>(v); }
};
struct std_ops {
    using owner = std::shared_ptr<payload>;
    static owner make(std::uint64_t v) { return std::make_shared<payload>(v); }
};

// --pin 1: the reader (and every single-threaded case) runs on the first
// allowed CPU and copier c on the (c+1)-th, so no two share a core.
bool pin_threads = false;
void pin_current_thread(std::size_t index) {
    if (!pin_threads) return;
    // Read the process's CPUs once, before any thread narrows its own
    // affinity: threads inherit the creating thread's mask.
    static const std::vector<int> cpus = [] {
        cpu_set_t allowed;
        CPU_ZERO(&allowed);
        if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) throw std::runtime_error("sched_getaffinity");
        std::vector<int> list;
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) if (CPU_ISSET(cpu, &allowed)) list.push_back(cpu);
        return list;
    }();
    cpu_set_t target;
    CPU_ZERO(&target);
    CPU_SET(cpus[index % cpus.size()], &target);
    if (pthread_setaffinity_np(pthread_self(), sizeof(target), &target) != 0)
        throw std::runtime_error("pthread_setaffinity_np");
}

double elapsed_ns(clock_type::time_point start) {
    return std::chrono::duration<double, std::nano>(clock_type::now() - start).count();
}

// Create, read and destroy one object per iteration.
template<class Ops>
double create_destroy(std::size_t iterations) {
    std::uint64_t sum = 0;
    auto start = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        auto owner = Ops::make(i);
        escape(owner);
        sum += owner->value;
    }
    double ns = elapsed_ns(start);
    if (sum != iterations * (iterations - 1) / 2) throw std::runtime_error("create_destroy checksum");
    return ns / static_cast<double>(iterations);
}

// Read every payload of `count` owners created back to back, in a fixed
// shuffled order (no prefetch-friendly stride), `passes` times.
template<class Ops>
double scan(std::size_t count, std::size_t passes) {
    std::vector<typename Ops::owner> owners;
    owners.reserve(count);
    for (std::size_t i = 0; i < count; ++i) owners.push_back(Ops::make(i));
    std::vector<std::uint32_t> order(count);
    for (std::size_t i = 0; i < count; ++i) order[i] = static_cast<std::uint32_t>(i);
    std::shuffle(order.begin(), order.end(), std::mt19937_64(7));
    std::uint64_t sum = 0;
    auto start = clock_type::now();
    for (std::size_t p = 0; p < passes; ++p)
        for (auto index : order) sum += owners[index]->value;
    double ns = elapsed_ns(start);
    if (sum != passes * (count * (count - 1) / 2)) throw std::runtime_error("scan checksum");
    return ns / static_cast<double>(count * passes);
}

// One reader thread reads the payload through its own owner while `copiers`
// threads copy and drop owners of the same object. Timing starts only after
// every copier has published progress, and each copier keeps copying until the
// reader is done, so the whole timed window is contended. Returns ns/read.
template<class Owner>
double reader_with_copiers_on(const Owner& root, std::size_t copiers, std::size_t reads) {
    struct alignas(128) progress { std::atomic<std::size_t> copies{0}; };
    std::atomic<bool> stop{false};
    std::vector<progress> counters(copiers);
    std::vector<std::thread> threads;
    for (std::size_t c = 0; c < copiers; ++c) {
        threads.emplace_back([&, c] {
            pin_current_thread(c + 1);
            std::size_t local = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                for (int i = 0; i < 8; ++i) { auto copy = root; escape(copy); }
                local += 8;
                counters[c].copies.store(local, std::memory_order_relaxed);
            }
        });
    }
    auto reader_owner = root;
    for (auto& counter : counters)
        while (counter.copies.load(std::memory_order_relaxed) < 4096) {}
    std::vector<std::size_t> before(copiers);
    for (std::size_t c = 0; c < copiers; ++c) before[c] = counters[c].copies.load(std::memory_order_relaxed);
    std::uint64_t sum = 0;
    auto start = clock_type::now();
    for (std::size_t i = 0; i < reads; ++i) {
        escape(reader_owner);
        sum += reader_owner->value;
    }
    double ns = elapsed_ns(start);
    bool all_progressed = true;
    for (std::size_t c = 0; c < copiers; ++c)
        all_progressed &= counters[c].copies.load(std::memory_order_relaxed) > before[c];
    stop.store(true);
    for (auto& t : threads) t.join();
    if (sum != 5 * reads) throw std::runtime_error("reader checksum");
    if (!all_progressed) return -1;
    return ns / static_cast<double>(reads);
}

// Whether the counts and the payload share a cache line depends on where the
// allocator places the block within a line. Measuring eight objects allocated
// back to back averages over the offsets this allocator actually produces
// instead of reporting whichever offset one allocation happened to get.
template<class Ops>
double reader_with_copiers(std::size_t copiers, std::size_t reads) {
    constexpr std::size_t objects = 8;
    std::vector<typename Ops::owner> roots;
    for (std::size_t i = 0; i < objects; ++i) roots.push_back(Ops::make(5));
    double total = 0;
    for (auto& root : roots) {
        // A copier descheduled for the whole window would leave the line
        // uncontended; such a window is discarded and measured again.
        double ns = -1;
        for (int attempt = 0; attempt < 5 && ns < 0; ++attempt) ns = reader_with_copiers_on(root, copiers, reads);
        if (ns < 0) throw std::runtime_error("a copier made no progress in five timed windows");
        total += ns;
    }
    return total / objects;
}

// Bytes requested from the allocator per make_shared object, and what glibc
// actually reserves for a request of that size.
void print_sizes(std::ofstream* out) {
    const std::size_t own_block = sizeof(own::detail::in_place_control<payload>);
#if defined(__GLIBCXX__)
    const std::size_t std_block = sizeof(std::_Sp_counted_ptr_inplace<payload, std::allocator<payload>,
                                                                     __gnu_cxx::_S_atomic>);
#else
    const std::size_t std_block = 0;
#endif
    auto usable = [](std::size_t bytes) {
        void* p = std::malloc(bytes);
        std::size_t result = malloc_usable_size(p) + sizeof(void*); // plus glibc's size header
        std::free(p);
        return result;
    };
    std::printf("SIZE own_block %zu bytes, malloc chunk %zu\n", own_block, usable(own_block));
    std::printf("SIZE std_block %zu bytes, malloc chunk %zu\n", std_block, usable(std_block));
    if (out) *out << "type,requested_bytes,malloc_chunk_bytes\nown_block," << own_block << ',' << usable(own_block)
                  << "\nstd_block," << std_block << ',' << usable(std_block) << '\n';
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
    const std::size_t scale = arg(argc, argv, "--scale", 1); // QUICK divides work
    const std::string output = arg_text(argc, argv, "--output", "");
    pin_threads = arg(argc, argv, "--pin", 0) != 0;
    pin_current_thread(0);
    const std::size_t hardware = std::max(2u, std::thread::hardware_concurrency());
    const std::size_t copiers = std::min<std::size_t>(3, hardware - 1);
    const std::size_t create_n = 400000 / scale, reads = std::max<std::size_t>(2000000 / scale, 500000);

    std::ofstream sizes;
    if (!output.empty()) sizes.open(output + "/sizes.csv");
    print_sizes(sizes.is_open() ? &sizes : nullptr);

    std::vector<sample_case> cases;
    auto add_impl = [&](const char* impl, auto ops) {
        using Ops = decltype(ops);
        cases.push_back({"create_read_destroy", impl, [=] { return create_destroy<Ops>(create_n); }, {}});
        cases.push_back({"scan_16k_objects", impl, [=] { return scan<Ops>(16384, 64 / scale + 1); }, {}});
        cases.push_back({"scan_256k_objects", impl, [=] { return scan<Ops>(262144, 8 / scale + 1); }, {}});
        cases.push_back({"reader_with_copiers", impl, [=] { return reader_with_copiers<Ops>(copiers, reads); }, {}});
    };
    add_impl("own", own_ops{});
    add_impl("own_a64", own_padded_ops{});
    add_impl("std_a", std_ops{});
    add_impl("std_b", std_ops{});

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

    std::ofstream raw, summary;
    if (!output.empty()) {
        raw.open(output + "/raw.csv");
        summary.open(output + "/summary.csv");
        raw << "case,implementation,sample,ns\n";
        summary << "case,implementation,samples,median_ns,min_ns,p90_ns,cv\n";
    }
    std::printf("%-22s %-6s %10s %10s %10s %7s\n", "case", "impl", "median_ns", "min_ns", "p90_ns", "cv");
    for (auto& c : cases) {
        double mean = 0;
        for (double v : c.samples) mean += v;
        mean /= static_cast<double>(c.samples.size());
        double var = 0;
        for (double v : c.samples) var += (v - mean) * (v - mean);
        double cv = c.samples.size() > 1 ? std::sqrt(var / static_cast<double>(c.samples.size() - 1)) / mean : 0;
        double med = quantile(c.samples, .5), mn = quantile(c.samples, 0), p90 = quantile(c.samples, .9);
        std::printf("%-22s %-6s %10.3f %10.3f %10.3f %7.3f\n", c.name.c_str(), c.implementation.c_str(), med, mn, p90, cv);
        std::printf("RESULT %s/%s %.3f ns\n", c.name.c_str(), c.implementation.c_str(), med);
        if (raw.is_open()) {
            for (std::size_t s = 0; s < c.samples.size(); ++s)
                raw << c.name << ',' << c.implementation << ',' << s << ',' << c.samples[s] << '\n';
            summary << c.name << ',' << c.implementation << ',' << c.samples.size() << ',' << med << ','
                    << mn << ',' << p90 << ',' << cv << '\n';
        }
    }
    std::printf("samples=%zu warmups=%zu scale=%zu copiers=%zu pinned=%d\n", samples, warmups, scale, copiers,
                pin_threads ? 1 : 0);
    std::printf("verification: every sample matched its checksum\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "control_block_layout: %s\n", error.what());
    return 1;
}
