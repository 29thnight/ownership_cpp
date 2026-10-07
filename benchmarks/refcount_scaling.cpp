// Reference-count scaling: one cache line shared by 1..N threads.
//
// Claim under test: a shared owner copy/drop is a pair of atomic
// read-modify-writes on one control-block line, so its cost is set by how that
// line moves between cores. The increment's instruction sequence (one locked add
// versus a load + compare-exchange retry loop) matters most when several cores
// contend for the line. Private per-thread objects are the no-contention control,
// and weak locking (a compare-exchange in both libraries) is the CAS control.
// std runs twice (std_shared, std_shared_b) as an A/A control; --pin 1 pins
// worker t to CPU t.
//
// Method: each sample starts every participating thread on a shared flag, each
// thread performs `iterations` copy+drop (or lock+drop) pairs, and the sample is
// the slowest thread's elapsed time divided by `iterations`: wall time per pair
// with all threads active. Warmup samples are discarded. Implementations are
// interleaved in a seeded shuffled order each round so frequency drift affects
// both. Every sample is checked: the payload checksum must match the expected
// value and the shared count must return to exactly one.
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
#include <memory>
#include <random>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;

template<class T> inline void escape(const T& value) noexcept {
    asm volatile("" : : "g"(std::addressof(value)) : "memory");
}

// One payload per cache line so private objects never share a line.
struct alignas(128) payload {
    std::uint64_t value;
    explicit payload(std::uint64_t v) noexcept : value(v) {}
};

struct own_ops {
    using owner = own::shared_owner<payload>;
    using weak = own::weak_owner<payload>;
    static owner make(std::uint64_t v) { return own::make_shared<payload>(v); }
    static weak observe(const owner& o) { return weak(o); }
    static std::size_t count(const owner& o) { return o.use_count(); }
};
struct std_ops {
    using owner = std::shared_ptr<payload>;
    using weak = std::weak_ptr<payload>;
    static owner make(std::uint64_t v) { return std::make_shared<payload>(v); }
    static weak observe(const owner& o) { return weak(o); }
    static std::size_t count(const owner& o) { return static_cast<std::size_t>(o.use_count()); }
};

template<class Owner>
[[gnu::noinline]] std::uint64_t copy_drop(const Owner& source, std::size_t iterations) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < iterations; ++i) {
        Owner copy = source;
        escape(copy);
        sum += copy->value;
    }
    return sum;
}

template<class Weak>
[[gnu::noinline]] std::uint64_t lock_drop(const Weak& source, std::size_t iterations) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < iterations; ++i) {
        auto locked = source.lock();
        escape(locked);
        sum += locked->value;
    }
    return sum;
}

// --pin: worker t runs only on online CPU t (mod CPU count). Without it the
// scheduler may migrate workers or stack two on one core mid-sample.
bool pin_threads = false;

void pin_current_thread(std::size_t index) {
#if defined(__linux__)
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
#else
    if (pin_threads) throw std::runtime_error("--pin is only implemented on Linux");
    (void)index;
#endif
}

// Runs body(thread_index) on `threads` threads released together; returns the
// slowest thread's elapsed nanoseconds. Thread creation is outside the timing.
double run_parallel(std::size_t threads, const std::function<std::uint64_t(std::size_t)>& body,
                    std::uint64_t& checksum) {
    std::atomic<std::size_t> ready{0};
    std::atomic<bool> go{false};
    std::vector<double> elapsed(threads);
    std::vector<std::uint64_t> sums(threads);
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (std::size_t t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            pin_current_thread(t);
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (!go.load(std::memory_order_acquire)) {}
            auto start = clock_type::now();
            sums[t] = body(t);
            elapsed[t] = std::chrono::duration<double, std::nano>(clock_type::now() - start).count();
        });
    }
    while (ready.load(std::memory_order_acquire) != threads) {}
    go.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    checksum = 0;
    for (auto sum : sums) checksum += sum;
    return *std::max_element(elapsed.begin(), elapsed.end());
}

struct sample_case {
    std::string name, implementation;
    std::size_t threads;
    std::function<double(std::size_t iterations)> run; // returns ns per pair
    std::vector<double> samples;
};

template<class Ops>
void add_cases(std::vector<sample_case>& cases, const char* implementation, std::size_t threads) {
    constexpr std::uint64_t value = 3;
    cases.push_back({"shared_copy_drop", implementation, threads, [threads](std::size_t iterations) {
        auto root = Ops::make(value);
        std::uint64_t checksum = 0;
        double ns = run_parallel(threads, [&](std::size_t) { return copy_drop(root, iterations); }, checksum);
        if (checksum != value * iterations * threads || Ops::count(root) != 1)
            throw std::runtime_error("shared_copy_drop verification failed");
        return ns / static_cast<double>(iterations);
    }, {}});
    cases.push_back({"private_copy_drop", implementation, threads, [threads](std::size_t iterations) {
        std::vector<typename Ops::owner> roots;
        for (std::size_t t = 0; t < threads; ++t) roots.push_back(Ops::make(value));
        std::uint64_t checksum = 0;
        double ns = run_parallel(threads, [&](std::size_t t) { return copy_drop(roots[t], iterations); }, checksum);
        for (auto& root : roots) if (Ops::count(root) != 1) throw std::runtime_error("private count");
        if (checksum != value * iterations * threads) throw std::runtime_error("private_copy_drop checksum");
        return ns / static_cast<double>(iterations);
    }, {}});
    cases.push_back({"weak_lock_drop", implementation, threads, [threads](std::size_t iterations) {
        auto root = Ops::make(value);
        auto observer = Ops::observe(root);
        std::uint64_t checksum = 0;
        double ns = run_parallel(threads, [&](std::size_t) { return lock_drop(observer, iterations); }, checksum);
        if (checksum != value * iterations * threads || Ops::count(root) != 1)
            throw std::runtime_error("weak_lock_drop verification failed");
        return ns / static_cast<double>(iterations);
    }, {}});
}

double quantile(std::vector<double> values, double probability) {
    std::sort(values.begin(), values.end());
    auto rank = static_cast<std::size_t>(std::ceil(probability * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(rank, 1) - 1];
}

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
} // namespace

int main(int argc, char** argv) try {
    const std::size_t samples = arg(argc, argv, "--samples", 15);
    const std::size_t warmups = arg(argc, argv, "--warmups", 2);
    const std::size_t iterations = arg(argc, argv, "--iterations", 200000);
    const std::size_t hardware = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t max_threads = arg(argc, argv, "--max-threads", hardware);
    const std::string output = arg_text(argc, argv, "--output", "");
    pin_threads = arg(argc, argv, "--pin", 0) != 0;
    if (samples == 0 || iterations == 0) throw std::runtime_error("samples and iterations must be positive");

    std::vector<std::size_t> thread_counts;
    for (std::size_t t = 1; t < max_threads; t *= 2) thread_counts.push_back(t);
    thread_counts.push_back(max_threads);

    std::vector<sample_case> cases;
    for (auto threads : thread_counts) {
        // Two identical std copies form an A/A control: their difference shows
        // how much run-to-run noise the comparison has to exceed.
        add_cases<std_ops>(cases, "std_shared", threads);
        add_cases<std_ops>(cases, "std_shared_b", threads);
        add_cases<own_ops>(cases, "own_shared", threads);
    }

    std::mt19937_64 order_rng(20261007);
    std::vector<std::size_t> order(cases.size());
    for (std::size_t round = 0; round < warmups + samples; ++round) {
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::shuffle(order.begin(), order.end(), order_rng);
        for (auto index : order) {
            double ns = cases[index].run(iterations);
            if (round >= warmups) cases[index].samples.push_back(ns);
        }
    }

    std::ofstream raw, summary;
    if (!output.empty()) {
        raw.open(output + "/raw.csv");
        summary.open(output + "/summary.csv");
        raw << "case,implementation,threads,sample,ns_per_pair\n";
        summary << "case,implementation,threads,samples,iterations,median_ns,min_ns,p90_ns,cv\n";
    }
    std::printf("%-18s %-11s %7s %10s %10s %10s %7s\n", "case", "impl", "threads", "median_ns", "min_ns", "p90_ns", "cv");
    for (auto& c : cases) {
        double mean = 0;
        for (double v : c.samples) mean += v;
        mean /= static_cast<double>(c.samples.size());
        double variance = 0;
        for (double v : c.samples) variance += (v - mean) * (v - mean);
        double cv = c.samples.size() > 1 ? std::sqrt(variance / static_cast<double>(c.samples.size() - 1)) / mean : 0.0;
        double median = quantile(c.samples, 0.5), minimum = quantile(c.samples, 0.0), p90 = quantile(c.samples, 0.9);
        std::printf("%-18s %-11s %7zu %10.2f %10.2f %10.2f %7.3f\n", c.name.c_str(), c.implementation.c_str(),
                    c.threads, median, minimum, p90, cv);
        std::printf("RESULT %s/%s/%zu %.3f ns_per_pair\n", c.name.c_str(), c.implementation.c_str(), c.threads, median);
        if (raw.is_open()) {
            for (std::size_t s = 0; s < c.samples.size(); ++s)
                raw << c.name << ',' << c.implementation << ',' << c.threads << ',' << s << ',' << c.samples[s] << '\n';
            summary << c.name << ',' << c.implementation << ',' << c.threads << ',' << c.samples.size() << ','
                    << iterations << ',' << median << ',' << minimum << ',' << p90 << ',' << cv << '\n';
        }
    }
    std::printf("samples=%zu warmups=%zu iterations=%zu pinned=%d thread_counts=", samples, warmups, iterations,
                pin_threads ? 1 : 0);
    for (std::size_t i = 0; i < thread_counts.size(); ++i) std::printf("%s%zu", i ? "," : "", thread_counts[i]);
    std::printf("\nverification: every sample matched its payload checksum and returned the count to one\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "refcount_scaling: %s\n", error.what());
    return 1;
}
