#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include "tier1/utlvector.h"
#include "tier0/memdbgoff.h"
static bool tracking;
static unsigned allocations, frees;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t n) { if (tracking) ++allocations; return __real_malloc(n); }
extern "C" void *__wrap_realloc(void *p, size_t n) { if (tracking) ++allocations; return __real_realloc(p,n); }
extern "C" void __wrap_free(void *p) { if (tracking && p) ++frees; __real_free(p); }
static void require(bool ok) { if (!ok) { std::fprintf(stderr,"scene queue assertion failed\n"); std::exit(1); } }
struct CChoreoScene { int id; };
struct CChoreoEvent { int id; };
#include "scene_entry.inc"
static bool equal(const QueuedEvents_t &a, const QueuedEvents_t &b) {
    return a.starttime == b.starttime && a.scene == b.scene && a.event == b.event;
}
class C_SceneEntity {
public:
    CUtlVector<QueuedEvents_t> m_QueuedEvents;
    std::vector<QueuedEvents_t> trace;
    CChoreoScene *otherScene = NULL;
    CChoreoEvent *extraEvent = NULL;
    int mode = 0, depth = 0;
    bool nestedDone = false;
    void CheckQueuedEvents();
    void WipeQueuedEvents();
    void QueueStartEvent(float starttime, CChoreoScene *scene, CChoreoEvent *event);
    void StartEvent(float starttime, CChoreoScene *scene, CChoreoEvent *event);
};
#include "scene_methods.inc"
struct Reference {
    std::vector<QueuedEvents_t> queue, trace;
    CChoreoScene *otherScene = NULL;
    CChoreoEvent *extraEvent = NULL;
    int mode = 0, depth = 0;
    bool nestedDone = false;
    void QueueStartEvent(float time, CChoreoScene *scene, CChoreoEvent *event) {
        for (const auto &q : queue) if (q.scene == scene && q.event == event) return;
        queue.push_back({time,scene,event});
    }
    void WipeQueuedEvents() { queue.clear(); }
    void StartEvent(float, CChoreoScene *, CChoreoEvent *);
    void CheckQueuedEvents() {
        const std::vector<QueuedEvents_t> snapshot = queue;
        queue.clear();
        for (auto q : snapshot) StartEvent(q.starttime,q.scene,q.event);
    }
};
// Shared dispatch policy double; queue algorithms themselves are independent.
template<class T> static void dispatch(T &t, float time, CChoreoScene *scene, CChoreoEvent *event) {
    t.trace.push_back({time,scene,event});
    switch(t.mode) {
    case 1: t.QueueStartEvent(time,scene,event); break;
    case 2: if (event->id % 2) t.QueueStartEvent(time+0.25f,scene,event); break;
    case 3:
        t.QueueStartEvent(time,scene,event); t.QueueStartEvent(time+1,scene,event);
        t.QueueStartEvent(time,t.otherScene,t.extraEvent);
        t.QueueStartEvent(time+1,t.otherScene,t.extraEvent); break;
    case 4:
        if (event->id % 2) t.WipeQueuedEvents();
        t.QueueStartEvent(time,scene,event); break;
    case 5:
        if (!t.nestedDone && t.depth == 0) {
            t.nestedDone = true;
            t.QueueStartEvent(time+0.5f,t.otherScene,t.extraEvent);
            ++t.depth; t.CheckQueuedEvents(); --t.depth;
        }
        break;
    default: break;
    }
}
void C_SceneEntity::StartEvent(float t, CChoreoScene *s, CChoreoEvent *e) { dispatch(*this,t,s,e); }
void Reference::StartEvent(float t, CChoreoScene *s, CChoreoEvent *e) { dispatch(*this,t,s,e); }
static void check(const C_SceneEntity &actual,const Reference &expected) {
    require(actual.trace.size()==expected.trace.size());
    for (size_t i=0;i<actual.trace.size();++i) require(equal(actual.trace[i],expected.trace[i]));
    require(static_cast<size_t>(actual.m_QueuedEvents.Count())==expected.queue.size());
    for (int i=0;i<actual.m_QueuedEvents.Count();++i) require(equal(actual.m_QueuedEvents[i],expected.queue[i]));
}
static void behavior() {
    unsigned scenarios = 0, frames = 0;
    for (int count : {0,1,15,16,17,64,4096}) for (int mode=0;mode<6;++mode) {
        CChoreoScene scenes[2]={{0},{1}};
        std::vector<CChoreoEvent> events(count+1);
        for(int i=0;i<=count;++i) events[i].id=i;
        C_SceneEntity actual; Reference expected;
        actual.mode=expected.mode=mode;
        actual.otherScene=expected.otherScene=&scenes[1];
        actual.extraEvent=expected.extraEvent=&events[count];
        for (int i=0;i<count;++i) {
            auto scene=&scenes[i%2]; auto event=&events[i]; float time=i*0.125f;
            actual.QueueStartEvent(time,scene,event); expected.QueueStartEvent(time,scene,event);
            // Same pointer pair is duplicate even if timestamp differs.
            actual.QueueStartEvent(time+1,scene,event); expected.QueueStartEvent(time+1,scene,event);
        }
        check(actual,expected);
        for (int frame=0;frame<5;++frame) {
            actual.CheckQueuedEvents(); expected.CheckQueuedEvents(); check(actual,expected); ++frames;
        }
        actual.WipeQueuedEvents(); expected.WipeQueuedEvents(); check(actual,expected);
        // Reuse after Purge, including zero-size initial queues.
        actual.QueueStartEvent(3,&scenes[0],&events[0]); expected.QueueStartEvent(3,&scenes[0],&events[0]);
        actual.CheckQueuedEvents(); expected.CheckQueuedEvents(); check(actual,expected);
        ++scenarios;
    }
    std::printf("PASS scene queue scenarios=%u frames=%u: order, dedup, requeue, nested calls, clear/reuse\n",scenarios,frames);
}
static void measure(bool enforce) {
    for (int count : {0,1,15,16,17,64,4096}) {
        CChoreoScene scene{1}; std::vector<CChoreoEvent> events(count);
        C_SceneEntity target; target.trace.reserve(count);
        allocations=frees=0;
        for (int frame=0;frame<50;++frame) {
            target.trace.clear();
            for(int i=0;i<count;++i) {
                events[i].id=i; QueuedEvents_t e{float(i),&scene,&events[i]};
                target.m_QueuedEvents.AddToTail(e);
            }
            tracking=true; target.CheckQueuedEvents(); tracking=false;
            require(target.trace.size()==static_cast<size_t>(count) && target.m_QueuedEvents.Count()==0);
        }
        std::printf("scene snapshot count=%d calls=50 malloc/realloc=%u free=%u\n",count,allocations,frees);
        if (enforce && count<=16) require(allocations==0 && frees==0);
        if (enforce && count>16) require(allocations==50 && frees==50);
    }
}
int main(int argc,char **argv) {
    std::string mode=argc>1?argv[1]:"all";
    if(mode=="all"||mode=="behavior") behavior();
    if(mode=="all"||mode=="measure"||mode=="allocations") measure(mode!="measure");
}
