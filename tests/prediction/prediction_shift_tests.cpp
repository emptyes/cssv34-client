// SDK vector is used by the extracted baseline; engine/entity lifecycle is not simulated.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include "tier1/utlvector.h"
#include "tier0/memdbgoff.h"
#include "shift_limit.inc"
static volatile bool tracking=false;
static volatile size_t allocations=0, frees=0, newCalls=0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *,size_t);
extern "C" void __real_free(void *);
extern "C" void *__real__Znwm(size_t);
extern "C" void *__real__Znam(size_t);
extern "C" void *__wrap_malloc(size_t n) { if(tracking) ++allocations; return __real_malloc(n); }
extern "C" void *__wrap_realloc(void *p,size_t n) { if(tracking) ++allocations; return __real_realloc(p,n); }
extern "C" void __wrap_free(void *p) { if(tracking && p) ++frees; __real_free(p); }
extern "C" void *__wrap__Znwm(size_t n) { if(tracking) ++newCalls; return __real__Znwm(n); }
extern "C" void *__wrap__Znam(size_t n) { if(tracking) ++newCalls; return __real__Znam(n); }
static int assertionFailures=0;
#undef Assert
#ifdef TEST_DEBUG_ASSERTS
// Non-fatal instrumentation of production assertions; not the SDK assertion UI.
#define Assert(x) do { if(!(x)) ++assertionFailures; } while(0)
#else
#define Assert(x) ((void)0)
#endif
struct C_BaseEntity {
    unsigned char *m_pOriginalData=nullptr;
    unsigned char *m_pIntermediateData[MULTIPLAYER_BACKUP];
    void ShiftIntermediateDataForward(int,int);
};
#include "shift_under_test.inc"
static unsigned char payload[MULTIPLAYER_BACKUP][16];
static unsigned char original[16];
static unsigned long long digest=1469598103934665603ULL;
static void seed(C_BaseEntity &e,int pattern) {
    e.m_pOriginalData=original;
    for(int i=0;i<MULTIPLAYER_BACKUP;++i) {
        std::memset(payload[i],(i*13+7)&255,sizeof(payload[i]));
        if(pattern==0) e.m_pIntermediateData[i]=payload[i];
        else if(pattern==1) e.m_pIntermediateData[i]=(i%5==0)?nullptr:payload[i/2];
        else e.m_pIntermediateData[i]=payload[(i*37+17)%MULTIPLAYER_BACKUP];
    }
    std::memset(original,91,sizeof(original));
}
static void verifyPayload() {
    for(int i=0;i<MULTIPLAYER_BACKUP;++i) for(unsigned j=0;j<sizeof(payload[i]);++j)
        assert(payload[i][j]==((i*13+7)&255));
    for(unsigned char c:original) assert(c==91);
}
static void check(C_BaseEntity &e,unsigned char *const *before,int n,int k,bool unchanged=false) {
#ifdef NO_ENTITY_PREDICTION
    unchanged=true;
#endif
    for(int i=0;i<MULTIPLAYER_BACKUP;++i) {
        int from=(!unchanged && i<n)?(i+k)%n:i;
        if(e.m_pIntermediateData[i]!=before[from]) std::fprintf(stderr,"Mismatch n=%d k=%d i=%d from=%d unchanged=%d original=%p expected=%p actual=%p\n",n,k,i,from,int(unchanged),static_cast<void*>(e.m_pOriginalData),static_cast<void*>(before[from]),static_cast<void*>(e.m_pIntermediateData[i]));
        assert(e.m_pIntermediateData[i]==before[from]);
        int identity=-1;
        for(int j=0;j<MULTIPLAYER_BACKUP;++j) if(e.m_pIntermediateData[i]==payload[j]) { identity=j; break; }
        digest^=static_cast<unsigned>(identity+1); digest*=1099511628211ULL;
    }
    assert(e.m_pOriginalData==original || e.m_pOriginalData==nullptr);
    verifyPayload();
}
int main(int argc,char **argv) {
    std::string mode=argc>1?argv[1]:"all";
    C_BaseEntity e;
    int cases=0;
    if(mode=="behavior" || mode=="all") {
        for(int pattern=0;pattern<3;++pattern) for(int n=0;n<=MULTIPLAYER_BACKUP;++n)
        for(int k=0;k<=n;++k) {
            seed(e,pattern);
            for(int repeat=0;repeat<3;++repeat) {
                unsigned char *before[MULTIPLAYER_BACKUP];
                std::memcpy(before,e.m_pIntermediateData,sizeof(before));
                assertionFailures=0;
                e.ShiftIntermediateDataForward(k,n);
                assert(assertionFailures==0);
                check(e,before,n,k);
                ++cases;
            }
        }
        // The separate original-data pointer does not control rotation of history slots.
        for(int k:{0,1,32,MULTIPLAYER_BACKUP}) {
            seed(e,0); e.m_pOriginalData=nullptr;
            unsigned char *before[MULTIPLAYER_BACKUP];
            std::memcpy(before,e.m_pIntermediateData,sizeof(before));
            assertionFailures=0;
            e.ShiftIntermediateDataForward(k,MULTIPLAYER_BACKUP);
            check(e,before,MULTIPLAYER_BACKUP,k);
            assert(assertionFailures==0);
            ++cases;
        }
        std::printf("Behavior cases: %d; digest=%llu; backup=%d\n",cases,digest,MULTIPLAYER_BACKUP);
    }
    if(mode=="measure" || mode=="allocations" || mode=="all") {
        for(int k:{0,1,2,16,32,33,89,90}) {
            if(k>MULTIPLAYER_BACKUP) continue;
            seed(e,0); allocations=frees=newCalls=0;
            tracking=true;
            for(int i=0;i<50;++i) e.ShiftIntermediateDataForward(k,MULTIPLAYER_BACKUP);
            tracking=false;
            verifyPayload();
            std::printf("ALLOC k=%d n=%d calls=50 malloc_realloc=%zu free=%zu new=%zu\n",k,MULTIPLAYER_BACKUP,allocations,frees,newCalls);
            if(mode=="allocations" || mode=="all") assert(allocations==0 && frees==0 && newCalls==0);
        }
    }
}
