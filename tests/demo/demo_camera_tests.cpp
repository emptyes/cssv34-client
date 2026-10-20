#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#define Assert(x) assert(x)
#define FDEMO_NORMAL 0
#define FDEMO_USE_ORIGIN2 1
#define FDEMO_USE_ANGLES2 2
#define FDEMO_NOINTERP 4

struct Vector
{
    float x = 0, y = 0, z = 0;
    Vector() = default;
    Vector(float a, float b, float c) : x(a), y(b), z(c) {}
    void Init() { x = y = z = 0; }
    float& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
    float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    Vector operator+(const Vector& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vector operator-(const Vector& v) const { return {x - v.x, y - v.y, z - v.z}; }
    bool operator!=(const Vector& v) const { return x != v.x || y != v.y || z != v.z; }
    float Length() const { return std::sqrt(x*x + y*y + z*z); }
};
using QAngle = Vector;
using Quaternion = Vector;
static Vector operator*(float f, const Vector& v) { return {f*v.x, f*v.y, f*v.z}; }
static void VectorSubtract(const Vector& a, const Vector& b, Vector& out) { out = a-b; }
static void VectorCopy(const Vector& a, Vector& b) { b = a; }
static void AngleQuaternion(const Vector& a, Vector& b) { b = a; }
static void QuaternionAngles(const Vector& a, Vector& b) { b = a; }
static void QuaternionSlerp(const Vector& a, const Vector& b, float f, Vector& out) { out = a + f*(b-a); }
static float AngleNormalizePositive(float a) { a = std::fmod(a, 360.0f); return a < 0 ? a+360 : a; }
static float AngleNormalize(float a) { a = AngleNormalizePositive(a); return a > 180 ? a-360 : a; }
static const Vector vec3_origin, vec3_angle;
using std::clamp;
using std::fabs;
static constexpr float interval = 1.0f/66.0f;
#define TICKS_TO_TIME(t) (interval * (t))
#define TIME_TO_TICKS(t) (static_cast<int>(0.5f + (t)/interval))
struct Cvar
{
    float value;
    bool GetBool() const { return value != 0; }
    float GetFloat() const { return value; }
};
static Cvar demo_legacy_rollback{1}, demo_interpolateview{1}, demo_interplimit{4000}, demo_avellimit{2000};
static struct Client
{
    int m_nMaxClients = 42;
    float m_tickRemainder = 0;
    QAngle viewangles;
    float GetClientInterpAmount() const { return 0.1f; }
} cl;
static struct Prediction
{
    Vector origin, angles, localAngles;
    void SetViewOrigin(const Vector& v) { origin = v; }
    void SetViewAngles(const Vector& v) { angles = v; }
    void SetLocalViewAngles(const Vector& v) { localAngles = v; }
} prediction;
static Prediction* g_pClientSidePrediction = &prediction;

template<class T> struct CUtlVector : std::vector<T>
{
    using std::vector<T>::operator=;
    int Count() const { return static_cast<int>(this->size()); }
};
struct democmdinfo_t;
struct DemoCommandQueue;
class CDemoPlayer
{
public:
    CDemoPlayer();
    ~CDemoPlayer();
    CUtlVector<DemoCommandQueue>& m_DestCmdInfo;
    democmdinfo_t& m_LastCmdInfo;
    int m_nPreviousTick = 0;
    int m_nLastCmdInfoTick = 0;
    bool m_bHasLastCmdInfoTick = false;
    bool m_bLastCmdInfoTickHasFlags = false;
    bool m_bInterpolateView = true;
    bool m_bResetInterpolation = false;
    int tick = 0;
    bool playing = true;
    bool IsPlayingBack() const { return playing; }
    int GetPlaybackTick() const { return tick; }
    bool OverrideView(democmdinfo_t&) { return false; }
    void StoreDemoCommandInfo(int tick, const democmdinfo_t& info);
    void InterpolateDemoCommand(int tick, DemoCommandQueue& prev, DemoCommandQueue& next);
    void InterpolateViewpoint();
};

#include "camera_under_test.inc"

CDemoPlayer::CDemoPlayer() : m_DestCmdInfo(*new CUtlVector<DemoCommandQueue>), m_LastCmdInfo(*new democmdinfo_t) {}
CDemoPlayer::~CDemoPlayer() { delete &m_DestCmdInfo; delete &m_LastCmdInfo; }
static void Check(bool value)
{
    if (!value) { std::cerr << "FAIL: demo camera regression\n"; std::exit(1); }
}
static DemoCommandQueue Sample(int tick, float x, int flags = 0)
{
    DemoCommandQueue q;
    q.tick = tick;
    q.info.viewOrigin = {x, 10, 20};
    q.info.viewOrigin2 = {x+100, 20, 30};
    q.info.flags = flags;
    return q;
}
static void Near(float a, float b) { Check(std::fabs(a-b) < 0.002f); }
static void DuplicateTicks()
{
    CDemoPlayer p;
    p.m_DestCmdInfo = {Sample(100, 100), Sample(100, 40), Sample(101, 110), Sample(101, 50), Sample(102, 120)};
    DemoCommandQueue prev, next;
    p.InterpolateDemoCommand(100, prev, next);
    Near(prev.info.viewOrigin.x, 100);
    Near(next.info.viewOrigin.x, 110);
    p.StoreDemoCommandInfo(100, p.m_DestCmdInfo[0].info);
    p.StoreDemoCommandInfo(100, p.m_DestCmdInfo[1].info);
    Near(p.m_LastCmdInfo.viewOrigin.x, 100);
    p.tick = 100;
    cl.m_tickRemainder = interval * 0.5f;
    p.InterpolateViewpoint();
    Near(prediction.origin.x, 105);
    p.m_bInterpolateView = false;
    p.InterpolateViewpoint();
    Near(prediction.origin.x, 100);
    p.m_bInterpolateView = true;
    demo_interpolateview.value = 0;
    p.InterpolateViewpoint();
    Near(prediction.origin.x, 100);
    demo_interpolateview.value = 1;
    p.StoreDemoCommandInfo(101, p.m_DestCmdInfo[2].info);
    Near(p.m_LastCmdInfo.viewOrigin.x, 110);
    p.m_bHasLastCmdInfoTick = false;
    p.StoreDemoCommandInfo(101, Sample(101, 700).info);
    Near(p.m_LastCmdInfo.viewOrigin.x, 700);
    p.StoreDemoCommandInfo(1, Sample(1, 5).info);
    Near(p.m_LastCmdInfo.viewOrigin.x, 5);
}
static void FlaggedTicks()
{
    for (int flags : {FDEMO_USE_ORIGIN2, FDEMO_USE_ANGLES2, FDEMO_NOINTERP, 8})
    {
        CDemoPlayer p;
        p.m_DestCmdInfo = {Sample(100, 100), Sample(100, 40, flags), Sample(100, 60), Sample(101, 110)};
        DemoCommandQueue prev, next;
        p.m_nPreviousTick = 100;
        p.InterpolateDemoCommand(100, prev, next);
        Near(prev.info.viewOrigin.x, 60);
        for (const auto& q : p.m_DestCmdInfo)
            if (q.tick == 100) p.StoreDemoCommandInfo(q.tick, q.info);
        Near(p.m_LastCmdInfo.viewOrigin.x, 60);
        p.StoreDemoCommandInfo(100, Sample(100, 70).info);
        Near(p.m_LastCmdInfo.viewOrigin.x, 70);
        p.StoreDemoCommandInfo(101, Sample(101, 110).info);
        p.StoreDemoCommandInfo(101, Sample(101, 80).info);
        Near(p.m_LastCmdInfo.viewOrigin.x, 110);
    }
    CDemoPlayer p;
    p.m_DestCmdInfo = {Sample(99, 10), Sample(100, 100, FDEMO_NOINTERP), Sample(101, 110)};
    p.m_nPreviousTick = 98;
    DemoCommandQueue prev, next;
    p.InterpolateDemoCommand(100, prev, next);
    Check(prev.tick == 99 && next.tick == 100 && next.info.flags == FDEMO_NOINTERP);
}
static void DistinctTicks()
{
    CDemoPlayer p;
    p.m_DestCmdInfo = {Sample(100, 100), Sample(102, 120), Sample(104, 140)};
    p.StoreDemoCommandInfo(100, p.m_DestCmdInfo[0].info);
    p.tick = 101;
    cl.m_tickRemainder = interval*0.5f;
    p.InterpolateViewpoint();
    Near(prediction.origin.x, 115);
    p.m_bResetInterpolation = true;
    p.InterpolateViewpoint();
    Near(prediction.origin.x, 100);
    Check(!p.m_bResetInterpolation);
    p.m_DestCmdInfo[1].info.viewOrigin.x = 10000;
    p.InterpolateViewpoint();
    Near(prediction.origin.x, 100);
}
static int RealFlight(const std::vector<DemoCommandQueue>& samples)
{
    CDemoPlayer p;
    p.m_DestCmdInfo.assign(samples.begin(), samples.end());
    size_t read = 0;
    Vector previous;
    bool hasPrevious = false;
    int backwards = 0;
    float maximumError = 0;
    for (int tick = 2644; tick <= 2940; ++tick)
    {
        while (read < samples.size() && samples[read].tick <= tick)
        {
            p.StoreDemoCommandInfo(samples[read].tick, samples[read].info);
            ++read;
        }
        const DemoCommandQueue* a = nullptr;
        const DemoCommandQueue* b = nullptr;
        for (size_t n = 0; n < samples.size(); ++n)
        {
            if (n && samples[n-1].tick == samples[n].tick) continue;
            if (samples[n].tick <= tick) a = &samples[n];
            else { b = &samples[n]; break; }
        }
        Check(a && b);
        Vector direction = b->info.viewOrigin - a->info.viewOrigin;
        for (int frame = 0; frame < 16; ++frame)
        {
            p.tick = tick;
            cl.m_tickRemainder = interval * frame / 16.0f;
            p.InterpolateViewpoint();
            float fraction = (tick-a->tick + frame/16.0f)/(b->tick-a->tick);
            Vector expected = a->info.viewOrigin + fraction*direction;
            maximumError = std::max(maximumError, (prediction.origin-expected).Length());
            if (hasPrevious && direction.Length() > 1)
            {
                Vector delta = prediction.origin-previous;
                float dot = delta.x*direction.x + delta.y*direction.y + delta.z*direction.z;
                if (dot / direction.Length() < -1) ++backwards;
            }
            previous = prediction.origin;
            hasPrevious = true;
        }
    }
    std::cout << "Recorded free-flight backward jumps: " << backwards << '\n';
    std::cout << "Maximum position error against first-sample path: " << maximumError << '\n';
    return backwards || maximumError > 0.02f;
}
int main(int argc, char** argv)
{
    if (argc == 2)
    {
        std::ifstream file(argv[1]);
        std::vector<DemoCommandQueue> samples;
        DemoCommandQueue q;
        while (file >> q.tick >> q.info.flags)
        {
            for (Vector* v : {&q.info.viewOrigin, &q.info.viewAngles, &q.info.localViewAngles,
                              &q.info.viewOrigin2, &q.info.viewAngles2, &q.info.localViewAngles2})
                file >> v->x >> v->y >> v->z;
            Check(bool(file));
            samples.push_back(q);
        }
        Check(samples.size() > 200);
        return RealFlight(samples) != 0;
    }
    DuplicateTicks();
    FlaggedTicks();
    DistinctTicks();
    std::cout << "Demo camera tests passed: duplicate ticks, flags, fallback, reset and interpolation.\n";
}
