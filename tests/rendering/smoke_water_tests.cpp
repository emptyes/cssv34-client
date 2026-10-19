#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define MIN std::min
#define VPROF_BUDGET(a, b) ((void)0)
#define DEG2RAD(a) ((a) * 3.14159265358979323846f / 180.0f)
template<class T> static T clamp(T value, T low, T high) { return std::max(low, std::min(high, value)); }
struct Vector
{
    float x, y, z;
    Vector(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
    Vector operator+(const Vector &b) const { return {x+b.x, y+b.y, z+b.z}; }
    Vector operator-(const Vector &b) const { return {x-b.x, y-b.y, z-b.z}; }
    Vector operator*(float s) const { return {x*s, y*s, z*s}; }
    Vector &operator+=(const Vector &b) { *this = *this + b; return *this; }
    float Dot(const Vector &b) const { return x*b.x + y*b.y + z*b.z; }
    float Length() const { return std::sqrt(Dot(*this)); }
    float DistTo(const Vector &b) const { return (*this-b).Length(); }
    const float *Base() const { return &x; }
};
static Vector camera;
static const Vector &CurrentViewOrigin() { return camera; }
Vector CurrentViewForward() { return {0, 0, 1}; }
float Bias(float t, float bias) { return t / ((1.0f / bias - 2.0f) * (1.0f - t) + 1.0f); }
float anglemod(float value) { return std::fmod(value + 36000.0f, 360.0f); }
void VectorVectors(const Vector &, Vector &right, Vector &up) { right = {1,0,0}; up = {0,1,0}; }
struct Context
{
    bool overrideDepth = false, writes = true;
    int changes = 0;
    void OverrideDepthEnable(bool enable, bool write) { overrideDepth=enable; writes=write; ++changes; }
};
static Context context;
int *materials = nullptr;
struct CMatRenderContextPtr
{
    explicit CMatRenderContextPtr(int *) {}
    Context *operator->() { return &context; }
};
static std::vector<int> drawOrder;
static float depth, color;
struct IClientRenderable
{
    virtual ~IClientRenderable() {}
    int id = 0;
    Vector origin, extent;
    bool power = false, full = false, smoke = false, cull = false;
    float pixelDepth = 100, alpha = 1;
    bool UsesPowerOfTwoFrameBufferTexture() const { return power; }
    bool UsesFullFrameBufferTexture() const { return full; }
    void GetRenderBoundsWorldspace(Vector &mins, Vector &maxs) { mins=origin-extent; maxs=origin+extent; }
};
static void DrawTranslucentRenderable(IClientRenderable *p, bool, bool, bool)
{
    drawOrder.push_back(p->id);
    if (p->pixelDepth < depth)
    {
        if (p->smoke) color = color * (1-p->alpha);
        if (!p->smoke && (p->power || p->full) && (!context.overrideDepth || context.writes))
            depth = p->pixelDepth;
    }
}
static int copies = 0, drawsAtCopy = -1;
void UpdateRefractTexture() { ++copies; drawsAtCopy=static_cast<int>(drawOrder.size()); }
void UpdateScreenEffectTexture() { ++copies; drawsAtCopy=static_cast<int>(drawOrder.size()); }
struct CParticleEffectBinding : IClientRenderable
{
    void GetRenderBounds(Vector &mins, Vector &maxs) { mins=extent*-1; maxs=extent; }
    const Vector &GetRenderOrigin() const { return origin; }
};
struct Manager
{
    std::vector<CParticleEffectBinding *> effects;
    int GetDrawAfterTranslucentsEffects(CParticleEffectBinding **out, int max)
    {
        int n=0; for (auto *p : effects) { if(n==max) break; out[n++]=p; } return n;
    }
};
static Manager manager;
static Manager *ParticleMgr() { return &manager; }
struct Engine { bool CullBox(const Vector &, const Vector &) { return false; } };
static Engine engineObject;
static Engine *engine=&engineObject;
#include "smoke_under_test.inc"

struct Particle { Vector m_Pos; };
struct Vertex { Vector pos, normal; std::array<float,4> color; std::array<float,2> uv; };
static std::vector<Vertex> vertices;
struct CMeshBuilder
{
    Vertex current;
    void Position3fv(const float *v) { current.pos={v[0],v[1],v[2]}; }
    void Normal3fv(const float *v) { current.normal={v[0],v[1],v[2]}; }
    void TexCoord2f(int, float u, float v) { current.uv={{u,v}}; }
    void Color4fv(const float *v) { std::copy(v,v+4,current.color.begin()); }
    void AdvanceVertex() { vertices.push_back(current); }
    void End(bool, bool) {}
};
struct SubTexture { float m_tCoordMins[2]={0.2f,0.3f}, m_tCoordMaxs[2]={0.7f,0.9f}; };
struct ParticleDraw
{
    CMeshBuilder builder;
    SubTexture texture;
    SubTexture *m_pSubTexture=&texture;
    CMeshBuilder *GetMeshBuilder() { return &builder; }
};
struct CParticleRenderIterator
{
    Particle *particle;
    ParticleDraw draw;
    float m_zCoords[1]={0}, m_MinZ=0, m_MaxZ=0;
    int m_nZCoords=0;
    const Particle *GetFirst() { return particle; }
    const Particle *GetNext(float) { return nullptr; }
    ParticleDraw *GetParticleDraw() { return &draw; }
};
struct CParticleSimulateIterator
{
    Particle *particle;
    float dt;
    bool removed=false;
    Particle *GetFirst() { return particle; }
    Particle *GetNext() { return nullptr; }
    float GetTimeDelta() const { return dt; }
    void RemoveParticle(Particle *) { removed=true; }
};
struct Binding
{
    bool cameraSpace=true;
    Vector mins, maxs;
    void SetEffectCameraSpace(bool value) { cameraSpace=value; }
    void SetBBox(const Vector &a,const Vector &b) { mins=a; maxs=b; }
};
struct CParticleEffect
{
    Binding binding;
    Particle particle;
    bool allocated=false;
    explicit CParticleEffect(const char *) {}
    virtual ~CParticleEffect() {}
    virtual void SimulateParticles(CParticleSimulateIterator *)=0;
    virtual void RenderParticles(CParticleRenderIterator *)=0;
    Binding &GetBinding() { return binding; }
    void SetSortOrigin(const Vector &) {}
    int GetPMaterial(const char *) { return 1; }
    Particle *AddParticle(unsigned int, int, const Vector &v) { particle.m_Pos=v; allocated=true; return &particle; }
};
static std::unique_ptr<CParticleEffect> created;
template<class T> struct CSmartPtr
{
    T *p;
    CSmartPtr(T *value) : p(value) { created.reset(value); }
    T *operator->() { return p; }
};
struct trace_t { float fraction=0.5f; Vector endpos={1,2,3}; struct { Vector normal={0,0,1}; } plane; };
static void UTIL_TraceLine(const Vector &,const Vector &,int,void *,int,trace_t *) {}
static const int MASK_WATER=1, COLLISION_GROUP_NONE=0;
struct Random { float RandomFloat(float a,float b) { return (a+b)*0.5f; } };
static Random randomObject;
#define random (&randomObject)
static int lateQuads=0;
#define FXQUAD_BIAS_SCALE 1
#define FXQUAD_BIAS_ALPHA 2
void FX_AddQuad(const Vector &,const Vector &,float,float,float,float,float,float,float,float,const Vector &,float,const char *,int) { ++lateQuads; }
#include "water_under_test.inc"

namespace MaterialTest
{
struct CParticleRenderIterator;
struct IMesh {};
struct Material
{
    bool power=false, full=false;
    bool NeedsPowerOfTwoFrameBufferTexture() { return power; }
    bool NeedsFullFrameBufferTexture() { return full; }
};
struct Group { Material *m_pPageMaterial=nullptr; };
struct CEffectMaterial { Group *m_pGroup; };
struct Simulation
{
    bool writeAtDraw=true;
    void RenderParticles(CParticleRenderIterator *) { writeAtDraw=!context.overrideDepth || context.writes; }
};
struct CParticleEffectBinding
{
    struct Mgr { int *m_pMaterialSystem=nullptr; } mgr;
    Mgr *m_pParticleMgr=&mgr;
    Simulation simulation;
    Simulation *m_pSim=&simulation;
    int m_nActiveParticles=1;
    int DrawMaterialParticles(bool,CEffectMaterial *,float,bool);
    void StartDrawMaterialParticles(CEffectMaterial *,float,IMesh *&,CMeshBuilder &,ParticleDraw &,bool) {}
    void DoBucketSort(CEffectMaterial *,float *,int,float,float) {}
};
struct CParticleRenderIterator
{
    CParticleEffectBinding *m_pEffectBinding;
    CEffectMaterial *m_pMaterial;
    ParticleDraw *m_pParticleDraw;
    CMeshBuilder *m_pMeshBuilder;
    IMesh *m_pMesh;
    bool m_bBucketSort;
    float m_zCoords[1]={0},m_MinZ=0,m_MaxZ=0;
    int m_nZCoords=0;
};
static const int MAX_TOTAL_PARTICLES=10000;
static int g_nParticlesDrawn=0;
static void Error(const char *,int,int) { throw std::runtime_error("particle limit"); }
#include "particles_under_test.inc"
}
static unsigned checks=0;
static void Check(bool value,const char *why) { ++checks; if(!value) throw std::runtime_error(why); }
static void Reset() { context=Context();drawOrder.clear();depth=1000;color=1;copies=0;drawsAtCopy=-1;manager.effects.clear(); }
static void Depth()
{
    for (int f=0;f<2;++f)
    {
        Reset(); CParticleEffectBinding smoke; smoke.smoke=true;smoke.id=1;smoke.origin={0,0,10};smoke.extent={1,1,1};smoke.pixelDepth=10;
        manager.effects.push_back(&smoke); CSmokeTranslucentOrder order;order.Init(true);
        IClientRenderable refract;refract.id=2;refract.origin={0,0,7};refract.extent={3,3,3};refract.pixelDepth=7;refract.power=f==0;refract.full=f==1;
        Check(order.QueueRenderable(&smoke,false),"smoke must be queued");
        Check(!order.QueueRenderable(&refract,false),"intersecting effect should remain in original pass");
#if HAS_DEPTH_GUARD
        order.DrawRenderable(&refract,false,false);
#else
        DrawTranslucentRenderable(&refract,false,false,false);
#endif
        order.DrawQueued(false);
        Check(color==0,"refractive rectangle prevented smoke coverage");
        Check(!context.overrideDepth,"depth override leaked");
    }
}
static void Front()
{
    for(int f=0;f<2;++f)
    {
        Reset(); CParticleEffectBinding smoke;smoke.smoke=true;smoke.id=1;smoke.origin={0,0,100};smoke.extent={1,1,1};smoke.pixelDepth=100;
        manager.effects.push_back(&smoke);CSmokeTranslucentOrder order;order.Init(true);
        IClientRenderable front;front.id=2;front.origin={0,0,5};front.extent={1,1,1};front.pixelDepth=5;front.power=f==0;front.full=f==1;
        Check(order.QueueRenderable(&smoke,false),"smoke not queued");
        Check(order.QueueRenderable(&front,true),"foreground refraction excluded from deferred front list");
        order.DrawQueued(false);
        Check(drawOrder==std::vector<int>({1,2}),"foreground effect rendered before smoke");
        Check(copies==1 && drawsAtCopy==1,"front refraction did not capture smoke");
        order.DrawQueued(false);
        Check(drawOrder.size()==2,"queue drawn twice");
    }
}
static void Limits()
{
    Reset();
    std::array<CParticleEffectBinding, 20> smokes;
    for (auto &smoke : smokes)
    {
        smoke.smoke=true;smoke.origin={0,0,100};smoke.extent={1,1,1};
        manager.effects.push_back(&smoke);
    }
    CSmokeTranslucentOrder order;order.Init(true);
    for (int i=0;i<20;++i)
        Check(order.QueueRenderable(&smokes[i],false)==(i<16),"smoke capacity changed");
    std::array<IClientRenderable, 260> fronts;
    for (int i=0;i<260;++i)
        Check(order.QueueRenderable(&fronts[i],false)==(i<256),"foreground capacity changed");
    order.DrawQueued(false);
    Check(drawOrder.size()==272,"queue capacity draw count changed");
    Reset();order.Init(false);
    Check(!order.QueueRenderable(&smokes[0],false),"disabled pass queued smoke");
#if HAS_DEPTH_GUARD
    IClientRenderable effect;effect.power=true;
    order.DrawRenderable(&effect,false,true);
    Check(context.changes==0,"shadow pass changed depth override");
    order.Init(false);order.DrawRenderable(&effect,false,false);
    Check(context.changes==0,"no-smoke pass changed depth override");
    Reset();manager.effects.push_back(&smokes[0]);order.Init(true);
    fronts[0].power=true;
    Check(order.QueueRenderable(&smokes[0],false),"water pass smoke not queued");
    Check(order.QueueRenderable(&fronts[0],false),"water pass front effect not queued");
    order.DrawQueued(false,true);
    Check(drawOrder.size()==1 && copies==0,"water target used recursive refraction");
#endif
}
static void Water()
{
    Reset();created.reset();lateQuads=0;
    FX_WaterRipple({1,2,3},1,nullptr,1.5f,0.8f);
    Check(lateQuads==0,"water ripple still uses late unsorted client-effects pass");
    Check(created && created->allocated,"ripple particle not created");
    Check(!created->binding.cameraSpace,"ripple is not in world space");
    auto *effect=created.get();
    vertices.clear();CParticleRenderIterator draw;draw.particle=&effect->particle;effect->RenderParticles(&draw);
    Check(vertices.size()==4,"ripple must produce one quad");
    Check(std::abs(vertices[0].color[3]-0.8f)<0.0001f,"initial alpha changed");
    float initialSide=vertices[0].pos.DistTo(vertices[1].pos);
    Check(std::abs(initialSide-16)<0.001f,"initial ripple diameter changed");
    Check(vertices[0].uv==std::array<float,2>({0.7f,0.9f}),"atlas UVs ignored");
    CParticleSimulateIterator simulation;simulation.particle=&effect->particle;simulation.dt=0.75f;
    effect->SimulateParticles(&simulation);Check(!simulation.removed,"ripple expired too soon");
    vertices.clear();effect->RenderParticles(&draw);
    Check(vertices.size()==4,"midlife quad missing");
    float side=vertices[0].pos.DistTo(vertices[1].pos);
    Check(std::abs(side-(16+112*0.7f))<0.001f,"ripple size bias changed");
    Check(std::abs(vertices[0].color[3]-0.6f)<0.001f,"ripple alpha bias changed");
    auto before=vertices;vertices.clear();effect->RenderParticles(&draw);
    Check(vertices[0].pos.DistTo(before[0].pos)<0.0001f,"another view advanced simulation");
    effect->SimulateParticles(&simulation);Check(simulation.removed,"expired ripple not removed");
    created.reset();FX_WaterRipple({1,2,3},0,nullptr,1.5f,0.8f);Check(!created,"zero scale accepted");
    FX_WaterRipple({1,2,3},1,nullptr,0,0.8f);Check(!created,"zero lifetime accepted");
}
static void Particles()
{
    using namespace MaterialTest;
    for(int power=0;power<2;++power)for(int full=0;full<2;++full)for(int wire=0;wire<2;++wire)
    {
        Reset();Material material;material.power=power;material.full=full;
        Group group;group.m_pPageMaterial=&material;CEffectMaterial entry;entry.m_pGroup=&group;
        MaterialTest::CParticleEffectBinding effect;
        Check(effect.DrawMaterialParticles(false,&entry,0,wire)==1,"particle result changed");
        Check(effect.simulation.writeAtDraw==(!(power||full)||wire),"refraction particles still write depth");
        Check(!context.overrideDepth,"particle depth override leaked");
    }
}
int main(int argc,char **argv)
{
    try
    {
        const std::string test=argc>1?argv[1]:"all";
        if(test=="all")Limits();
        if(test=="all"||test=="depth")Depth();
        if(test=="all"||test=="front")Front();
        if(test=="all"||test=="water")Water();
        if(test=="all"||test=="particles")Particles();
        std::cout<<"PASS smoke/water regression: "<<checks<<" checks\n";
        return 0;
    }
    catch(const std::exception &error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
