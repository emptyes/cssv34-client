// Uses the real SDK vector/allocator and extracted production simulation methods.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>
#include <climits>
#include "tier1/utlvector.h"
#include "tier0/memdbgoff.h"

static bool trackAllocations = false;
static size_t allocations = 0, frees = 0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t n) {
    if (trackAllocations) ++allocations;
    return __real_malloc(n);
}
extern "C" void *__wrap_realloc(void *p, size_t n) {
    if (trackAllocations) ++allocations;
    return __real_realloc(p, n);
}
extern "C" void __wrap_free(void *p) {
    if (trackAllocations && p) ++frees;
    __real_free(p);
}
#define VPROF_BUDGET(a, b) ((void)0)
struct ConVar {
    float value = 0;
    float GetFloat() const { return value; }
    int GetInt() const { return static_cast<int>(value); }
    bool GetBool() const { return value != 0; }
};
static ConVar r_particle_timescale{1}, cl_particle_stats_trigger_count{0};
static ConVar r_threaded_particles{0}, particle_sim_alt_cores{0};
static bool g_bMeasureParticlePerformance = false;
static int g_nNumParticlesSimulated = 0;
static float s_flThreadedPSystemTimeStep = 0;
struct Globals { float curtime = 5; } globals;
static Globals *gpGlobals = &globals;
struct SystemManager {
    float time = 0;
    bool warning = false;
    void SetLastSimulationTime(float t) { time = t; }
    float GetLastSimulationTime() const { return time; }
    bool Debug_FrameWarningNeededTestAndReset() { bool v = warning; warning = false; return v; }
} systemManager;
static SystemManager *g_pParticleSystemMgr = &systemManager;
struct CParticleCollection {
    int m_nActiveParticles = 1;
    CParticleCollection *m_pNext = nullptr;
    struct { CParticleCollection *m_pHead = nullptr; } m_Children;
};
static std::vector<int> trace;
struct CNewParticleEffect : CParticleCollection {
    CNewParticleEffect *m_pNext = nullptr;
    int id = 0;
    float m_flNextSleepTime = 100;
    bool remove = false;
    int updates = 0, simulations = 0, detections = 0;
    float dt = 0;
    bool GetRemoveFlag() const { return remove; }
    void Update(float t) { ++updates; dt = t; trace.push_back(id * 10 + 1); }
    void DetectChanges() { ++detections; trace.push_back(id * 10 + 3); }
};
static int begins = 0, ends = 0, spatial = 0, parallelCalls = 0, alternateCalls = 0;
static bool xbox = false;
#undef IsX360
static bool IsX360() { return xbox; }
static void BeginSimulateParticles() { ++begins; }
static void EndSimulateParticles() { ++ends; }
static void UpdateDirtySpatialPartitionEntities() { ++spatial; }
static void ProcessPSystem(CNewParticleEffect *&p) {
    ++p->simulations; trace.push_back(p->id * 10 + 2);
}
struct IThreadPool {};
// Joined worker checks that list storage survives until processing is complete.
static void ParallelProcess(const char *, CNewParticleEffect **items, unsigned count,
                            void (*fn)(CNewParticleEffect *&)) {
    ++parallelCalls;
    std::thread worker([=] { for (unsigned i = 0; i < count; ++i) fn(items[i]); });
    worker.join();
}
template<class T> struct CFuncJobItemProcessor {
    void (*fn)(T &) = nullptr;
    void Init(void (*f)(T &), void *, void *) { fn = f; }
};
template<class T, class Processor> struct CParallelProcessor {
    Processor m_ItemProcessor;
    explicit CParallelProcessor(const char *) {}
    void Run(T *items, unsigned count, int, IThreadPool *) {
        ++alternateCalls;
        std::thread worker([=] { for (unsigned i = 0; i < count; ++i) m_ItemProcessor.fn(items[i]); });
        worker.join();
    }
};
struct CParticleMgr {
#include "sim_declaration.inc"
    struct { CNewParticleEffect *m_pHead = nullptr; } m_NewEffects;
    IThreadPool *m_pThreadPool[2] = {nullptr, nullptr};
    bool m_bStatsRunning = false;
    int m_nStatsFramesSinceLastAlert = 0, accumulates = 0, resets = 0, spews = 0;
    bool retire = false;
    void UpdateNewEffects(float);
    bool EarlyRetireParticleSystems(int n, CNewParticleEffect **items) {
        if (retire && n) { items[0]->remove = true; retire = false; return true; }
        return false;
    }
    void StatsAccumulateActiveParticleSystems() { ++accumulates; }
    void StatsReset() { ++resets; }
    void StatsSpewResults() { ++spews; }
};
#include "sim_methods.inc"

static void reset() {
    trace.clear(); trace.reserve(50000);
    begins = ends = spatial = parallelCalls = alternateCalls = 0;
    allocations = frees = 0; trackAllocations = false;
    r_particle_timescale.value = 1; cl_particle_stats_trigger_count.value = 0;
    r_threaded_particles.value = 0; particle_sim_alt_cores.value = 0;
    xbox = false; g_bMeasureParticlePerformance = false;
    g_nNumParticlesSimulated = 0; systemManager.warning = false;
}
static void link(CParticleMgr &mgr, std::vector<CNewParticleEffect> &effects) {
    for (size_t i = 0; i < effects.size(); ++i) {
        effects[i].id = static_cast<int>(i);
        effects[i].m_pNext = i + 1 < effects.size() ? &effects[i + 1] : nullptr;
    }
    mgr.m_NewEffects.m_pHead = effects.empty() ? nullptr : &effects[0];
}
static void dispatch(int mode, int count, bool retire) {
    reset(); CParticleMgr mgr; IThreadPool pool;
    std::vector<CNewParticleEffect> effects(count); link(mgr, effects);
    mgr.retire = retire;
    r_threaded_particles.value = mode != 0;
    if (mode >= 2) { xbox = true; particle_sim_alt_cores.value = mode; mgr.m_pThreadPool[1] = &pool; }
    r_particle_timescale.value = 0.5f;
    mgr.UpdateNewEffects(0.02f);
    assert(begins == 1 && ends == 1);
    int surviving = count - (retire && count ? 1 : 0);
    assert(spatial == (surviving != 0));
    assert(parallelCalls == (mode == 1 && surviving ? 1 : 0));
    assert(alternateCalls == (mode >= 2 && surviving ? 1 : 0));
    size_t pos = 0;
    for (int i = 0; i < count; ++i) assert(trace[pos++] == i * 10 + 1);
    for (int phase : {2, 3})
        for (int i = retire && count ? 1 : 0; i < count; ++i) assert(trace[pos++] == i * 10 + phase);
    assert(pos == trace.size());
    for (int i = 0; i < count; ++i) {
        assert(effects[i].updates == 1 && effects[i].dt == 0.01f);
        assert(effects[i].simulations == !(retire && i == 0));
        assert(effects[i].detections == !(retire && i == 0));
    }
}
static void filters() {
    reset(); CParticleMgr mgr; std::vector<CNewParticleEffect> e(5); link(mgr, e);
    e[0].m_flNextSleepTime = 5; // active and asleep: excluded (inclusive boundary)
    e[1].remove = true;
    e[2].m_nActiveParticles = 0; e[2].m_flNextSleepTime = 4; // empty: retained
    e[3].m_flNextSleepTime = 5.001f; // awake: retained
    e[4].m_flNextSleepTime = 4;
    g_bMeasureParticlePerformance = true;
    mgr.UpdateNewEffects(0.01f);
    assert(g_nNumParticlesSimulated == 1);
    for (int i = 0; i < 5; ++i) assert(e[i].simulations == (i == 2 || i == 3));
    // Next frame must rebuild, not retain stale pointers or previous flags.
    e[3].remove = true; e[1].remove = false; e[0].m_flNextSleepTime = 6;
    mgr.UpdateNewEffects(0.01f);
    assert(e[0].simulations == 1 && e[1].simulations == 1);
    assert(e[2].simulations == 2 && e[3].simulations == 1);
}
static void stats() {
    reset(); CParticleMgr mgr; std::vector<CNewParticleEffect> e(1); link(mgr, e);
    CParticleCollection child; child.m_nActiveParticles = 7;
    e[0].m_Children.m_pHead = &child;
    cl_particle_stats_trigger_count.value = 8;
    mgr.m_nStatsFramesSinceLastAlert = 899;
    mgr.UpdateNewEffects(0.01f);
    assert(mgr.m_bStatsRunning && mgr.accumulates == 1 && mgr.spews == 1 && mgr.resets == 2);
    mgr.UpdateNewEffects(0.01f);
    assert(mgr.accumulates == 2 && mgr.spews == 1);
}
static void allocationTest(int count, bool enforce) {
    reset(); CParticleMgr mgr; std::vector<CNewParticleEffect> e(count); link(mgr, e);
    const int frames = 50;
    trackAllocations = true;
    for (int frame = 0; frame < frames; ++frame) mgr.UpdateNewEffects(0.01f);
    trackAllocations = false;
    for (const auto &effect : e) assert(effect.simulations == frames && effect.detections == frames);
    if (enforce && count <= 128) assert(allocations == 0 && frees == 0);
    else if (enforce) assert(allocations > 0 && frees == frames);
    std::printf("list=%d frames=%d allocator_calls=%zu frees=%zu\n", count, frames, allocations, frees);
}
int main(int argc, char **argv) {
    std::string which = argc > 1 ? argv[1] : "all";
    assert(which == "all" || which == "behavior" || which == "allocations" || which == "measure");
    if (which != "allocations" && which != "measure") {
        for (int mode = 0; mode < 4; ++mode)
            for (int n : {0, 1, 127, 128, 129, 4096})
                for (bool retired : {false, true}) dispatch(mode, n, retired);
        filters(); stats();
        std::puts("PASS particle filters, order, retirement, dispatch and statistics (50 scenarios)");
    }
    if (which != "behavior") for (int n : {0, 1, 127, 128, 129, 4096}) allocationTest(n, which != "measure");
}
