#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <new>
#include <string>
#include "mathlib/mathlib.h"
#include "tier1/rangecheckedvar.h"
#include "lerp_functions.h"
#include "tier0/memdbgoff.h"
// A scoped stand-in for the SDK's external range-check toggle; arithmetic/types are real.
static int rangeScopes=0;
CDisableRangeChecks::CDisableRangeChecks() { ++rangeScopes; }
CDisableRangeChecks::~CDisableRangeChecks() { --rangeScopes; }
static volatile bool tracking=false;
static volatile size_t allocations=0,frees=0;
extern "C" void *__real__Znam(size_t);
extern "C" void __real__ZdaPv(void *);
extern "C" void __real__ZdaPvm(void *,size_t);
extern "C" void *__wrap__Znam(size_t n) { if(tracking) ++allocations; return __real__Znam(n); }
extern "C" void __wrap__ZdaPv(void *p) { if(tracking && p) ++frees; __real__ZdaPv(p); }
extern "C" void __wrap__ZdaPvm(void *p,size_t n) { if(tracking && p) ++frees; __real__ZdaPvm(p,n); }
#undef Assert
#ifdef TEST_DEBUG_ASSERTS
#define Assert(x) assert(x)
#else
#define Assert(x) ((void)0)
#endif
#include "entries.inc"
template<typename Type,bool IS_ARRAY> class CInterpolatedVarArrayBase {
public:
    using CInterpolatedVarEntry=CInterpolatedVarEntryBase<Type,IS_ARRAY>;
    int m_nMaxCount=1;
    unsigned char m_bLooping[256]{};
#include "declarations.inc"
};
#include "methods.inc"
static unsigned long long digest=1469598103934665603ULL;
static int cases=0;
static void hash(float f) { uint32_t bits; std::memcpy(&bits,&f,sizeof(bits)); digest^=bits; digest*=1099511628211ULL; }
static void hash(const Vector &v) { hash(v.x); hash(v.y); hash(v.z); }
using Ranged=CRangeCheckedVar<float,-2,2,0>;
static void hash(const Ranged &r) { hash(r.GetRaw()); }
static void same(float a,float b) { assert(std::memcmp(&a,&b,sizeof(a))==0); }
static void same(const Vector &a,const Vector &b) { same(a.x,b.x);same(a.y,b.y);same(a.z,b.z); }
static void same(const Ranged &a,const Ranged &b) { same(a.GetRaw(),b.GetRaw()); }
template<typename T> static T value(int sample,int i);
template<> float value<float>(int sample,int i) { return ((i*7+sample*13)%97-48)*0.03125f; }
template<> Vector value<Vector>(int sample,int i) { return Vector(value<float>(sample,i),value<float>(sample+1,i),value<float>(sample+2,i)); }
template<> Ranged value<Ranged>(int sample,int i) { return Ranged(value<float>(sample,i)); }
struct Times { float prev,start,end; };
static Times timings[]={{0,0.015625f,0.03125f},{0,0.015625f,0.046875f},
    {0,0.03125f,0.046875f},{0,0.0001f,0.015725f},
    {0,0.000099f,0.015724f},{0,0.000101f,0.015726f},
    {0,0.015625f,0.0313499f},{0,0.015625f,0.0313501f},
    {100,100.015625f,100.03125f},{-1,-0.984375f,-0.953125f},
    {0,0,0.015625f},{0.015625f,0,0.03125f}};
// Reference is eager but owns storage; it preserves the pre-change mathematical expressions.
template<typename T> static void oracle(T *out,int n,const unsigned char *loop,float f,Times t,int method) {
    T previous[256],start[256],end[256];
    for(int i=0;i<n;++i) { previous[i]=value<T>(0,i);start[i]=value<T>(1,i);end[i]=value<T>(2,i); }
    float dt1=t.end-t.start,dt2=t.start-t.prev;
    float previousTime=t.prev;
    if(std::fabs(dt1-dt2)>0.0001f && dt2>0.0001f) {
        float ratio=dt1/dt2;
        previousTime=t.start-dt1;
        for(int i=0;i<n;++i) previous[i]=loop[i]?LoopingLerp(1-ratio,previous[i],start[i]):Lerp(1-ratio,previous[i],start[i]);
    }
    for(int i=0;i<n;++i) {
        if(method==0) { out[i]=loop[i]?LoopingLerp_Hermite(f,previous[i],start[i],end[i]):Lerp_Hermite(f,previous[i],start[i],end[i]); Lerp_Clamp(out[i]); }
        else if(method==1) {
            T prevVel=(start[i]-previous[i])/(t.start-previousTime);
            T curVel=(end[i]-start[i])/(t.end-t.start);
            out[i]=Lerp(f,prevVel,curVel);
        } else { out[i]=Derivative_Hermite(f,previous[i],start[i],end[i]);out[i]*=1.0f/(t.end-t.start); }
    }
}
template<typename T,bool Array> static void behavior(bool direct) {
    for(int n:{0,1,4,24,32,128,255}) {
        if(!Array && n!=1) continue;
        for(Times t:timings) for(float f:{0.0f,0.125f,0.5f,0.875f,1.0f})
        for(int pattern=0;pattern<3;++pattern) for(int method=0;method<(direct?3:2);++method) {
            if(method!=0 && pattern!=0) continue; // production derivative asserts non-looping
            if(method==1 && (t.start==t.prev || t.end==t.start)) continue;
            if(method==2 && t.end==t.start) continue;
            CInterpolatedVarArrayBase<T,Array> var;var.m_nMaxCount=n;
            for(int i=0;i<n;++i) var.m_bLooping[i]=(pattern==1 || (pattern==2 && i%2));
            using Entry=CInterpolatedVarEntryBase<T,Array>;
            Entry a,b,c; a.Init(n);b.Init(n);c.Init(n);
            a.changetime=t.prev;b.changetime=t.start;c.changetime=t.end;
            for(int i=0;i<n;++i) { a.GetValue()[i]=value<T>(0,i);b.GetValue()[i]=value<T>(1,i);c.GetValue()[i]=value<T>(2,i); }
            T output[257],expected[256];
            for(int j=0;j<257;++j) output[j]=value<T>(9,j);
            for(int j=0;j<256;++j) expected[j]=value<T>(9,j);
            output[0]=value<T>(7,0);output[n+1]=value<T>(8,0);
            oracle(expected,n,var.m_bLooping,f,t,method);
            for(int repeat=0;repeat<2;++repeat) {
                if(method==0) var._Interpolate_Hermite(output+1,f,&a,&b,&c);
                else if(method==1) var._Derivative_Hermite_SmoothVelocity(output+1,f,&a,&b,&c);
#ifdef TEST_DIRECT_DERIVATIVE
                else var._Derivative_Hermite(output+1,f,&a,&b,&c);
#endif
                for(int i=0;i<n;++i) { same(output[i+1],expected[i]);hash(output[i+1]); }
                same(output[0],value<T>(7,0));same(output[n+1],value<T>(8,0));
                same(a.changetime,t.prev);same(b.changetime,t.start);same(c.changetime,t.end);
                for(int i=0;i<n;++i) { same(a.GetValue()[i],value<T>(0,i));same(b.GetValue()[i],value<T>(1,i));same(c.GetValue()[i],value<T>(2,i)); }
                assert(rangeScopes==0);++cases;
            }
        }
    }
}
static void measure(bool requireZero) {
    for(int n:{0,1,4,24,128,255}) for(int uneven=0;uneven<2;++uneven) for(int method=0;method<2;++method) {
        CInterpolatedVarArrayBase<float,true> var;var.m_nMaxCount=n;
        CInterpolatedVarEntryBase<float,true> a,b,c;a.Init(n);b.Init(n);c.Init(n);
        a.changetime=0;b.changetime=0.015625f;c.changetime=uneven?0.046875f:0.03125f;
        for(int i=0;i<n;++i) { a.value[i]=value<float>(0,i);b.value[i]=value<float>(1,i);c.value[i]=value<float>(2,i); }
        float output[256];allocations=frees=0;tracking=true;
        for(int i=0;i<50;++i) {
            if(method==0) var._Interpolate_Hermite(output,0.5f,&a,&b,&c);
            else var._Derivative_Hermite_SmoothVelocity(output,0.5f,&a,&b,&c);
            for(int j=0;j<n;++j) hash(output[j]);
        }
        tracking=false;
        std::printf("ALLOC count=%d uneven=%d method=%d calls=50 new_array=%zu delete_array=%zu\n",n,uneven,method,size_t(allocations),size_t(frees));
        if(requireZero && !uneven) assert(allocations==0 && frees==0);
        if(requireZero && uneven && n>0) assert(allocations==50 && frees==50);
        assert(allocations==frees);
    }
}
int main(int argc,char **argv) {
    std::string mode=argc>1?argv[1]:"all";
    if(mode=="behavior" || mode=="all" || mode=="direct") {
        bool direct=mode=="all" || mode=="direct";
        behavior<float,true>(direct);behavior<float,false>(direct);
        behavior<Vector,true>(direct);behavior<Vector,false>(direct);
        behavior<Ranged,true>(false);behavior<Ranged,false>(false);
        std::printf("Behavior cases: %d; digest=%llu\n",cases,digest);
    }
    if(mode=="measure" || mode=="allocations" || mode=="all") measure(mode!="measure");
}
