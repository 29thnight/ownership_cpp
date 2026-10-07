// Internal precision study. Workload definitions are copied unchanged from the
// original source, except the worker's process-local affinity hook.
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <time.h>
#include <fstream>
#include <cstdlib>
namespace precision {
inline int main_cpu = 2, worker_cpu = 3;
inline void pin(int cpu) {
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set)) std::abort();
}
inline long long cpu_ns() {
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value)) std::abort();
    return value.tv_sec * 1000000000LL + value.tv_nsec;
}
}
#include "workloads.inc"
namespace bench {
struct std_default_clone : std_default {};
struct std_erased_clone : std_erased {};
}
int main(int argc, char** argv) try {
    using namespace bench;
    std::size_t rounds=31, warmups=3;
    unsigned seed=20261007;
    double target_ms=5;
    std::string run="p0";
    for (int i=1;i<argc;++i) {
        std::string arg=argv[i];
        auto next=[&](){ check(i+1<argc,"missing argument"); return argv[++i]; };
        if(arg=="--rounds") rounds=std::stoul(next());
        else if(arg=="--seed") seed=std::stoul(next());
        else if(arg=="--target-ms") target_ms=std::stod(next());
        else if(arg=="--run") run=next();
        else if(arg=="--main-cpu") precision::main_cpu=std::stoi(next());
        else if(arg=="--worker-cpu") precision::worker_cpu=std::stoi(next());
        else throw std::runtime_error("unknown argument");
    }
    precision::pin(precision::main_cpu);
    counts n;
#ifdef UNIQUE_BENCH_CHECK
    n={16,16,16,2,2,32};
#endif
    workload<std_default> std8a, std8b;
    workload<std_default_clone> std8clone;
    workload<own_unique> own8;
    workload<std_erased> std40a, std40b;
    workload<std_erased_clone> std40clone;
    workload<own_allocated> own40;
    std::vector<sample_case> cases;
    add_cases(cases,std8a,"std8a",n);
    add_cases(cases,std8b,"std8b_same_code",n);
    add_cases(cases,std8clone,"std8clone",n);
    add_cases(cases,own8,"own8",n);
    add_cases(cases,std40a,"std40a",n);
    add_cases(cases,std40b,"std40b_same_code",n);
    add_cases(cases,std40clone,"std40clone",n);
    add_cases(cases,own40,"own40",n);
#ifdef UNIQUE_BENCH_CHECK
    std::cout<<"case,implementation,operations,handle_bytes,borrow_bytes,payload_bytes,constructed,destroyed,checksum\n";
    for(auto& c:cases) {
        births=0; deaths=0;
        auto sum=c.run();
        check(sum==c.expected_checksum,"checksum mismatch");
        check(births==c.expected_objects && deaths==c.expected_objects,"lifetime mismatch");
        std::cout<<c.name<<','<<c.implementation<<','<<c.operations<<','<<c.handle_bytes<<','<<c.view_bytes<<','<<sizeof(resource)<<','<<births<<','<<deaths<<','<<sum<<'\n';
    }
#else
    std::mt19937 rng(seed);
    for(std::size_t r=0;r<warmups;++r) for(auto& c:cases) check(c.run()==c.expected_checksum,"warmup checksum");
    std::array<std::size_t,case_count> repetitions{};
    // One common repetition count per workload, selected from all eight versions
    // equally. Counts preserve original per-call batch semantics.
    for(std::size_t g=0;g<case_count;++g) {
        std::vector<double> durations;
        for(std::size_t k=0;k<8;++k) {
            auto& c=cases[k*case_count+g];
            auto start=clock::now(); auto sum=c.run(); escape(sum); auto end=clock::now();
            check(sum==c.expected_checksum,"calibration checksum");
            double ns=std::chrono::duration<double,std::nano>(end-start).count();
            durations.push_back(ns);
            std::cerr<<"calibration,"<<run<<','<<c.name<<','<<c.implementation<<','<<ns<<'\n';
        }
        std::sort(durations.begin(),durations.end());
        double median=(durations[3]+durations[4])/2;
        repetitions[g]=std::max<std::size_t>(1,static_cast<std::size_t>(target_ms*1e6/median+0.5));
        std::cerr<<"repetitions,"<<run<<','<<cases[g].name<<','<<repetitions[g]<<'\n';
    }
    std::cout<<"run,seed,round,order,pass,case,implementation,repetitions,operations,total_ns,ns_per_operation,cpu_ns,voluntary_switches,involuntary_switches,minor_faults,cpu,checksum\n"<<std::fixed<<std::setprecision(9);
    std::size_t order=0;
    for(std::size_t r=0;r<rounds;++r) {
        std::array<std::size_t,case_count> groups{0,1,2,3,4,5};
        std::shuffle(groups.begin(),groups.end(),rng);
        for(auto g:groups) {
            std::array<std::size_t,8> impls{0,1,2,3,4,5,6,7};
            std::shuffle(impls.begin(),impls.end(),rng);
            // Randomized order then exact reverse: every implementation has
            // the same mean temporal position in this paired block.
            for(std::size_t pass=0;pass<2;++pass) {
                if(pass) std::reverse(impls.begin(),impls.end());
                for(auto k:impls) {
                    auto& c=cases[k*case_count+g];
                    rusage before{},after{}; getrusage(RUSAGE_SELF,&before);
                    auto cpustart=precision::cpu_ns(); auto start=clock::now();
                    std::uint64_t sum=0;
                    for(std::size_t rep=0;rep<repetitions[g];++rep) { auto part=c.run(); escape(part); sum+=part; }
                    auto end=clock::now(); auto cpuend=precision::cpu_ns();
                    getrusage(RUSAGE_SELF,&after);
                    check(sum==c.expected_checksum*repetitions[g],"timing checksum mismatch");
                    auto ns=std::chrono::duration<double,std::nano>(end-start).count();
                    auto ops=c.operations*repetitions[g];
                    std::cout<<run<<','<<seed<<','<<r<<','<<order++<<','<<pass<<','<<c.name<<','<<c.implementation<<','<<repetitions[g]<<','<<ops<<','<<ns<<','<<ns/ops<<','<<cpuend-cpustart<<','<<after.ru_nvcsw-before.ru_nvcsw<<','<<after.ru_nivcsw-before.ru_nivcsw<<','<<after.ru_minflt-before.ru_minflt<<','<<sched_getcpu()<<','<<sum<<'\n';
                }
            }
        }
        std::cout.flush();
    }
#endif
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
