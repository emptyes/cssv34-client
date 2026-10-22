// Real SDK containers and extracted production methods; engine interfaces are test doubles.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "tier1/utlvector.h"
#include "tier0/memdbgoff.h"
static bool tracking = false;
static size_t allocations = 0, frees = 0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t n) { if (tracking) ++allocations; return __real_malloc(n); }
extern "C" void *__wrap_realloc(void *p, size_t n) { if (tracking) ++allocations; return __real_realloc(p,n); }
extern "C" void __wrap_free(void *p) { if (tracking && p) ++frees; __real_free(p); }
#define VPROF(a) ((void)0)
#define tmZone(...) ((void)0)
#define PIXEVENT(...) ((void)0)
static std::vector<int> trace;
static void event(int e) { trace.push_back(e); }
static bool tools = false, player = true;
static bool ToolsEnabled() { return tools; }
static void UpdateRefractTexture() { event(70); }
enum { MATERIAL_PROJECTION=1, STUDIO_RENDER=1, STUDIO_TRANSPARENCY=2,
       STEREO_EYE_MONO=0, RENDER_GROUP_VIEW_MODEL_OPAQUE=1, RENDER_GROUP_OPAQUE_ENTITY=2 };
struct ITexture {};
struct CViewSetup { float zNear=1,zFar=2,fov=3,zNearViewmodel=4,zFarViewmodel=5,fovViewmodel=6,m_flAspectRatio=1; int m_eStereoEye=0; };
struct Context {
    void MatrixMode(int) { event(10); }
    void PushMatrix() { event(11); }
    void PopMatrix() { event(12); }
    void DepthRange(float a,float b) { event(a==0 && b==0.1f ? 13 : 14); }
} context;
static void *materials = nullptr;
struct CMatRenderContextPtr { explicit CMatRenderContextPtr(void *) {} Context *operator->() { return &context; } };
struct Engine { float GetScreenAspectRatio() { event(15); return 1.5f; } } engineObject;
static Engine *engine=&engineObject;
struct Renderer {
    void Push3DView(const CViewSetup &v,int,ITexture*,void*,ITexture*) { assert(v.zNear==4 && v.zFar==5 && v.fov==6 && v.m_flAspectRatio==1.5f); event(16); }
    void PopView(void*) { event(17); }
} renderObject;
static Renderer *render=&renderObject;
struct ISourceVirtualReality { enum VREye { Eye0, Eye1 }; enum { RT_Color,RT_Depth }; };
struct VR { ITexture *GetRenderTarget(ISourceVirtualReality::VREye,int) { event(18); return nullptr; } } vr;
static VR *g_pSourceVR=&vr;
struct IClientRenderable;
struct IClientUnknown {
    IClientRenderable *renderable=nullptr;
    void *GetBaseEntity();
    IClientRenderable *GetClientRenderable() { return renderable; }
};
struct IClientRenderable {
    IClientUnknown unknown;
    int id=0, group=1, newGroup=-1;
    bool entity=true, draw=true, refract=false, nested=false;
    IClientRenderable() { unknown.renderable=this; }
    IClientUnknown *GetIClientUnknown() { return &unknown; }
    void ComputeFxBlend();
    bool UsesPowerOfTwoFrameBufferTexture() { event(20000+id); return refract; }
    bool ShouldDraw() { event(30000+id); return draw; }
    void DrawModel(int flags);
};
void *IClientUnknown::GetBaseEntity() { return renderable->entity ? renderable : nullptr; }
struct Entry { IClientRenderable *m_pRenderable=nullptr; int m_RenderGroup=1; };
using ClientRenderHandle_t=int;
struct CClientLeafSystem {
    using RenderableInfo_t=Entry;
    CUtlVector<int> m_ViewModels;
    std::vector<Entry> m_Renderables;
#include "leaf_declaration.inc"
} leaf;
static CClientLeafSystem *ClientLeafSystem() { return &leaf; }
struct CViewRender {
    void *m_pCurrentlyDrawingEntity=nullptr;
    bool ShouldDrawViewModel(bool b) { event(19); return b && player; }
    void *GetFrustum() { return nullptr; }
#include "view_declarations.inc"
} viewRenderer;
static bool inNested=false;
void IClientRenderable::ComputeFxBlend() {
    event(10000+id);
    if (newGroup>=0) leaf.m_Renderables[id].m_RenderGroup=newGroup;
}
void IClientRenderable::DrawModel(int flags) {
    event(40000+id*4+flags);
    if (nested && !inNested) {
        inNested=true;
        // A separate render instance avoids violating the production entity-drawing assertion.
        CViewRender other;
        other.DrawViewModels(CViewSetup(),true);
        inNested=false;
    }
}
#include "view_methods.inc"
static void prepare(std::vector<IClientRenderable> &items,int n,int pattern) {
    items.resize(n);
    leaf.m_ViewModels.RemoveAll();
    leaf.m_Renderables.resize(n);
    for(int i=0;i<n;++i) {
        auto &r=items[i]; r.unknown.renderable=&r; r.id=i;
        r.group=(pattern==0 ? 1 : pattern==1 ? 3 : (i%2 ? 3:1));
        r.newGroup=(pattern==3 && i%3==0) ? 2 : -1;
        r.entity=(i%3!=0); r.draw=(pattern==3 ? i%4!=0 : true);
        r.refract=(pattern==3 && i%5==0); r.nested=false;
        leaf.m_ViewModels.AddToTail(i); leaf.m_Renderables[i]={&r,r.group};
    }
}
static unsigned long long hashTrace() {
    unsigned long long h=1469598103934665603ULL;
    for(int v:trace) { h^=static_cast<unsigned>(v); h*=1099511628211ULL; }
    return h;
}
int main(int argc,char **argv) {
    std::string mode=argc>1?argv[1]:"all";
    trace.reserve(300000);
    int cases=0;
    if(mode=="behavior" || mode=="all") {
        for(int n:{0,1,31,32,33,64,65,4096}) for(int pattern=0;pattern<4;++pattern)
        for(int settings=0;settings<8;++settings) {
            std::vector<IClientRenderable> items;
            prepare(items,n,pattern);
            tools=(settings&1)!=0; player=(settings&2)!=0;
            CViewSetup setup; setup.m_eStereoEye=(settings&4)?1:0;
            trace.clear(); viewRenderer.DrawViewModels(setup,true);
            assert(viewRenderer.m_pCurrentlyDrawingEntity==nullptr);
            auto first=trace;
            trace.clear(); viewRenderer.DrawViewModels(setup,true);
            assert(trace==first);
            std::printf("TRACE %d %d %d %zu %llu\n",n,pattern,settings,trace.size(),hashTrace());
            ++cases;
        }
        std::vector<IClientRenderable> items; prepare(items,33,2);
        tools=true; player=true; items[5].nested=true;
        trace.clear(); viewRenderer.DrawViewModels(CViewSetup(),true);
        std::printf("NESTED %zu %llu\n",trace.size(),hashTrace()); ++cases;
        trace.clear(); tools=false; viewRenderer.DrawViewModels(CViewSetup(),false);
        std::printf("DISABLED %zu %llu\n",trace.size(),hashTrace()); ++cases;
        std::printf("Behavior cases: %d\n",cases);
    }
    if(mode=="measure" || mode=="allocations" || mode=="all") {
        std::printf("SIZE pointer=%zu regular=%zu inline=%zu two_list_delta=%zu\n",sizeof(void*),sizeof(CUtlVector<IClientRenderable*>),sizeof(CUtlVectorFixedGrowable<IClientRenderable*,32>),2*(sizeof(CUtlVectorFixedGrowable<IClientRenderable*,32>)-sizeof(CUtlVector<IClientRenderable*>)));
        for(int n:{0,1,31,32,33,64,65,4096}) for(int pattern=0;pattern<3;++pattern) {
            std::vector<IClientRenderable> items; prepare(items,n,pattern);
            tools=true; player=true; allocations=0; frees=0;
            tracking=true;
            for(int i=0;i<50;++i) { trace.clear(); viewRenderer.DrawViewModels(CViewSetup(),true); }
            tracking=false;
            std::printf("ALLOC n=%d pattern=%d alloc=%zu free=%zu\n",n,pattern,allocations,frees);
            if((mode=="allocations" || mode=="all") && (n<=32 || (pattern==2 && n<=64))) assert(allocations==0 && frees==0);
        }
    }
}
