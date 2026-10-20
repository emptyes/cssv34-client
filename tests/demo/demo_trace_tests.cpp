#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using FileHandle_t = void *;
#define FILESYSTEM_INVALID_HANDLE (FileHandle_t)0
#define Q_snprintf std::snprintf
#define Q_strlen std::strlen
#define Q_strcmp std::strcmp
#define Q_memcpy std::memcpy
#define MAX_PLAYERS 64
#define MIN(a,b) std::min(a,b)
void Msg(const char *, ...) {}
void Warning(const char *, ...) {}
struct Vector { float x = 0, y = 0, z = 0; };
using QAngle = Vector;
const Vector vec3_origin;
const QAngle vec3_angle;
struct CViewSetup { Vector origin; QAngle angles; };
struct View { CViewSetup setup; const CViewSetup *GetViewSetup() { return &setup; } } viewState;
View *view = &viewState;
struct Globals {
    int framecount = 50, tickcount = 100001, maxClients = 2;
    float realtime = 10, curtime = 1500.25f, frametime = .001f, interval_per_tick = .015f, interpolation_amount = .5f;
} globals;
Globals *gpGlobals = &globals;
struct Engine {
    bool playing = true, timedemo = false, paused = false;
    int tick = 3000;
    bool IsPlayingDemo() { return playing; }
    bool IsPlayingTimeDemo() { return timedemo; }
    int GetDemoPlaybackTick() { return tick; }
    int GetDemoPlaybackTotalTicks() { return 5097; }
    float GetDemoPlaybackTimeScale() { return 1; }
    float Time() { return 1500.25f; }
    float GetLastTimeStamp() { return 1500; }
    bool IsPaused() { return paused; }
} engineState;
Engine *engine = &engineState;
struct File { std::string name; };
struct Filesystem {
    std::map<std::string, std::string> files;
    std::vector<File *> handles;
    int calls = 0, closes = 0, writes = 0, failAfter = -1;
    bool failOpen = false;
    bool FileExists(const char *name, const char *) { ++calls; return files.count(name) != 0; }
    FileHandle_t Open(const char *name, const char *mode, const char *path) {
        ++calls; CHECK(!std::strcmp(mode, "wb") && !std::strcmp(path, "MOD"));
        if (failOpen) return nullptr;
        CHECK(files.count(name) == 0);
        files[name].clear(); File *file = new File{name}; handles.push_back(file); return file;
    }
    int Write(const void *data, int length, FileHandle_t handle) {
        ++calls; ++writes; CHECK(handle);
        if (failAfter >= 0 && writes > failAfter) return length / 2;
        files[static_cast<File *>(handle)->name].append(static_cast<const char *>(data), length); return length;
    }
    void Close(FileHandle_t handle) {
        ++calls; ++closes;
        auto *file = static_cast<File *>(handle);
        auto it = std::find(handles.begin(), handles.end(), file); CHECK(it != handles.end());
        handles.erase(it); delete file;
    }
    ~Filesystem() { CHECK(handles.empty()); }
} fs;
Filesystem *filesystem = &fs;
struct C_BaseEntity {
    static bool valid;
    static bool IsAbsQueriesValid() { return valid; }
    int id = 0;
    int entindex() { return id; }
};
bool C_BaseEntity::valid = true;
struct C_BasePlayer : C_BaseEntity {
    static C_BasePlayer *local;
    C_BaseEntity *target = nullptr;
    Vector network, origin, absolute, render;
    static C_BasePlayer *GetLocalPlayer() { return local; }
    C_BaseEntity *GetObserverTarget() { return target; }
    float GetTimeBase() { return 1500.125f; }
    float GetFinalPredictedTime() { return 1500.375f; }
    int GetObserverMode() { return 5; }
    bool IsDormant() { return false; }
    bool IsAlive() { return id != 1; }
    bool GetPredictable() { return id == 1; }
    float GetSimulationTime() { return 1500; }
    const Vector &GetNetworkOrigin() { return network; }
    const Vector &GetLocalOrigin() { return origin; }
    const Vector &GetAbsOrigin() { CHECK(valid); return absolute; }
    const Vector &GetRenderOrigin() { CHECK(valid); return render; }
};
C_BasePlayer *C_BasePlayer::local = nullptr;
C_BasePlayer players[2];
C_BasePlayer *UTIL_PlayerByIndex(int index) { return index >= 1 && index <= 2 ? &players[index-1] : nullptr; }
float GetClientInterpAmount() { return .1f; }
struct CCommand {
    std::vector<std::string> values;
    int ArgC() const { return int(values.size()); }
    const char *operator[](int i) const { return values[i].c_str(); }
};
#include "trace_under_test.inc"

int main() {
    players[0].id = 1; players[1].id = 2;
    players[0].target = &players[1]; C_BasePlayer::local = &players[0];
    players[1].network.x = 100; players[1].origin.x = 90; players[1].absolute.x = 91; players[1].render.x = 92;
    CDemoPlaybackTrace trace;
    trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(fs.calls == 0);
    engineState.playing = false; trace.Start(); CHECK(!trace.IsActive() && fs.calls == 0);
    engineState.playing = true; engineState.timedemo = true; trace.Start(); CHECK(!trace.IsActive() && fs.calls == 0);
    engineState.timedemo = false;
    fs.files["demo_trace_0001.csv"] = "existing";
    trace.Start(); CHECK(trace.IsActive());
    int calls = fs.calls; trace.Start(); CHECK(fs.calls == calls);
    trace.Capture(CDemoPlaybackTrace::NetworkUpdate);
    calls = fs.calls; trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(calls == fs.calls);
    trace.Capture(CDemoPlaybackTrace::RenderBefore); trace.Capture(CDemoPlaybackTrace::RenderAfter);
    trace.Stop("test"); CHECK(!trace.IsActive());
    CHECK(fs.files["demo_trace_0001.csv"] == "existing");
    std::fputs(fs.files["demo_trace_0002.csv"].c_str(), stdout);
    calls = fs.calls; trace.Stop("again"); trace.Capture(CDemoPlaybackTrace::RenderAfter); CHECK(calls == fs.calls);
    trace.Start(); C_BaseEntity::valid = false; calls = fs.calls;
    trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(calls == fs.calls);
    C_BaseEntity::valid = true; auto *local = C_BasePlayer::local; C_BasePlayer::local = nullptr;
    trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(calls == fs.calls);
    C_BasePlayer::local = local; trace.Capture(CDemoPlaybackTrace::NetworkUpdate);
    calls = fs.calls; engineState.tick += 7; trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(calls == fs.calls);
    ++engineState.tick; trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(fs.calls > calls);
    calls = fs.calls; engineState.tick = 100; trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(fs.calls > calls);
    engineState.playing = false; trace.Capture(CDemoPlaybackTrace::RenderBefore); CHECK(!trace.IsActive());
    engineState.playing = true; fs.failOpen = true; trace.Start(); CHECK(!trace.IsActive()); fs.failOpen = false;
    fs.failAfter = fs.writes; trace.Start(); CHECK(!trace.IsActive()); fs.failAfter = -1;
    trace.Start(); fs.failAfter = fs.writes; trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(!trace.IsActive());
    fs.failAfter = -1;
    trace.Start(); engineState.timedemo = true; trace.Capture(CDemoPlaybackTrace::NetworkUpdate); CHECK(!trace.IsActive());
    engineState.timedemo = false;
    DemoPlaybackTraceCommand(CCommand{{"cl_demo_trace"}}); CHECK(!g_DemoPlaybackTrace.IsActive());
    DemoPlaybackTraceCommand(CCommand{{"cl_demo_trace", "bad"}}); CHECK(!g_DemoPlaybackTrace.IsActive());
    DemoPlaybackTraceCommand(CCommand{{"cl_demo_trace", "1"}}); CHECK(g_DemoPlaybackTrace.IsActive());
    DemoPlaybackTraceCommand(CCommand{{"cl_demo_trace", "0"}}); CHECK(!g_DemoPlaybackTrace.IsActive());
    trace.Start();
    for (int i = 0; i < 50001 && trace.IsActive(); ++i) {
        engineState.tick = i * 8; trace.Capture(CDemoPlaybackTrace::NetworkUpdate);
    }
    CHECK(!trace.IsActive());
    CHECK(fs.files.rbegin()->second.size() < 64u*1024u*1024u);
    return 0;
}
