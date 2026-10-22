#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#define Assert(x) ((void)0)
#define tmZone(...) ((void)0)
#define DevWarning(...) ((void)0)
#define assert_cast static_cast
static inline bool ThreadInMainThread() { return true; }
static void Check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr,"FAIL %s\n",message); std::exit(1); } }
static int textures=0, recipients=0, signals=0;
struct Ref {
 int refs=1;
 virtual ~Ref()=default;
 int AddRef() { return ++refs; }
 int Release() { Check(refs>0,"Release refcount"); int n=--refs; if(!n) delete this; return n; }
 int GetRefCount() const { return refs; }
};
template<class T> void SafeRelease(T **p) { if(*p) (*p)->Release(); *p=nullptr; }
template<class T> void SafeAssign(T **p,T *v) { if(v) v->AddRef(); SafeRelease(p); *p=v; }
using ImageFormat=int;
const int IMAGE_FORMAT_UNKNOWN=-1, TEXTUREFLAGS_SINGLECOPY=1, TEXTUREFLAGS_IGNORE_PICMIP=2, TEXTUREFLAGS_ALL_MIPS=4, TEXTUREFLAGS_NOMIP=8;
const char *TEXTURE_GROUP_RUNTIME_COMPOSITE="test";
struct ITexture:Ref {
 ITexture(){++textures;} ~ITexture() override {--textures;}
 int GetActualWidth(){return 8;} int GetActualHeight(){return 8;} int GetImageFormat(){return 0;}
};
using ITextureInternal=ITexture;
struct IAsyncTextureOperationReceiver:Ref {
 int callbacks=0;
 IAsyncTextureOperationReceiver(){++recipients;} ~IAsyncTextureOperationReceiver() override {--recipients;}
 void OnAsyncCreateComplete(ITexture *p,void*) {Check(p!=nullptr,"completion texture"); ++callbacks;}
};
struct CAsyncCopyRequest:Ref {
 bool signalled=true;
 CAsyncCopyRequest(){refs=0;++signals;} ~CAsyncCopyRequest() override {--signals;}
 bool IsSignalled() const{return signalled;}
};
struct CAsyncMapResult:CAsyncCopyRequest {
 void *m_pMemory=reinterpret_cast<void*>(1); int m_nPitch=32;
 explicit CAsyncMapResult(ITextureInternal*){}
};
template<class T> struct CUtlMemory {
 std::vector<T> values; int Count() const{return values.size();} T *Base(){return values.data();}
 void EnsureCapacity(int n){values.resize(n);}
};
template<class T> struct Queue {
 std::deque<T> values;
 bool IsEmpty() const{return values.empty();} int Count() const{return values.size();}
 T Head(){return values.front();} void Insert(T v){values.push_back(v);} void PushItem(T v){Insert(v);}
 T RemoveAtHead(){T v=Head();values.pop_front();return v;}
 bool RemoveAtHead(T &v){if(IsEmpty()) return false;v=RemoveAtHead();return true;}
 bool PopItem(T *v){return RemoveAtHead(*v);}
};
struct AsyncReadJob_t;
class AsyncReader;
class CTextureManager {
public:
 AsyncReader *m_pAsyncReader=nullptr; bool failAcquire=false; int returns=0,creates=0;
 ITextureInternal *AcquireReadbackTexture(int,int,ImageFormat){return failAcquire?nullptr:new ITextureInternal;}
 void ReleaseReadbackTexture(ITextureInternal *p){++returns;p->Release();}
 void CompleteAsyncRead(AsyncReadJob_t*); void AsyncReadTexture(AsyncReadJob_t*);
} manager;
CTextureManager *g_pTextureManager=&manager;
struct RenderContext {
 int maps=0,unmaps=0,copies=0;
 void AsyncUnmap(ITextureInternal*){++unmaps;}
 void AsyncMap(ITextureInternal*,CAsyncMapResult*,void*){++maps;}
 void AsyncCopyRenderTargetToStagingTexture(ITextureInternal*,ITexture*,CAsyncCopyRequest*,void*){++copies;}
} context;
class CMaterialSystem {
public:
 RenderContext *GetRenderContextInternal(){return &context;}
 ITexture *CreateNamedTextureFromBitsEx(const char*,const char*,int,int,int,ImageFormat,int,unsigned char*,int){++manager.creates;return new ITexture;}
} g_MaterialSystem;
CMaterialSystem *materials=&g_MaterialSystem;
namespace ImageLoader {int GetNumMipMapLevels(int,int){return 4;}}
#include "job.inc"
#include "cancel.inc"
class AsyncReader {
public:
 Queue<AsyncReadJob_t*> m_requestedCopies,m_queuedReads,m_scheduledReads,m_queuedMaps,m_pendingJobs,m_completedJobs;
 void ThreadMain_Update(); void AsyncReadback(AsyncReadJob_t *p){m_requestedCopies.PushItem(p);}
};
#include "methods.inc"
struct Fixture {
 ITexture *source=new ITexture;
 IAsyncTextureOperationReceiver *recipient=new IAsyncTextureOperationReceiver;
 AsyncReader reader;
 Fixture(){manager=CTextureManager();context=RenderContext();manager.m_pAsyncReader=&reader;}
 ~Fixture(){
  Check(source->refs==1,"source reference released");
  if(recipient){Check(recipient->refs==1,"recipient reference released");recipient->Release();}
  source->Release();Check(!textures&&!recipients&&!signals,"no retained resources");
 }
 AsyncReadJob_t *Job(bool staging=true,bool mapped=false){
  source->AddRef();recipient->AddRef();char *name=new char[9];std::strcpy(name,"readback");
  auto *p=new AsyncReadJob_t(source,name,0,false,0,recipient,nullptr);
  if(staging)p->m_pSysmemTex=new ITextureInternal;
  if(mapped){p->m_pAsyncMap=new CAsyncMapResult(p->m_pSysmemTex);p->m_pAsyncMap->AddRef();}
  return p;
 }
 void Cancel(){recipient->Release();recipient=nullptr;}
};
static void Test(int mode){
 Fixture f;
 if(mode==5){manager.failAcquire=true;manager.AsyncReadTexture(f.Job(false));Check(f.reader.m_requestedCopies.IsEmpty(),"failed acquire not queued");return;}
 if(mode==6){
  auto *p=f.Job(false);manager.AsyncReadTexture(p);
  for(int i=0;i<5;++i)f.reader.ThreadMain_Update();
  Check(f.reader.m_pendingJobs.Count()==1,"worker receives job");
  Check(f.source->refs==2&&f.recipient->refs==2,"references retained during work");
  p=f.reader.m_pendingJobs.RemoveAtHead();p->m_finalTexelData.EnsureCapacity(256);
  f.reader.m_completedJobs.PushItem(p);f.reader.ThreadMain_Update();
  Check(f.recipient->callbacks==1&&manager.creates==1,"one completion");
  Check(manager.returns==1&&context.maps==1&&context.unmaps==1&&context.copies==1,"balanced successful pipeline");return;
 }
 auto *p=f.Job(true,mode!=0);
 if(mode==0){f.reader.m_queuedReads.Insert(p);f.Cancel();}
 if(mode==1){f.reader.m_queuedMaps.Insert(p);f.Cancel();}
 if(mode==2){f.reader.m_completedJobs.PushItem(p);f.Cancel();}
 if(mode==3){p->m_pAsyncMap->m_pMemory=nullptr;p->m_pAsyncMap->m_nPitch=0;f.reader.m_queuedMaps.Insert(p);}
 if(mode==4){p->m_pAsyncMap->m_nPitch=0;f.reader.m_queuedMaps.Insert(p);}
 f.reader.ThreadMain_Update();
 Check(manager.returns==1,"staging texture returned");
 Check(context.unmaps==((mode==0||mode==3)?0:1),"Unmap only when required");
 Check(manager.creates==0&&f.reader.m_pendingJobs.IsEmpty(),"cancelled or failed job not published");
}
int main(int argc,char **argv){
 const char *names[]={"cancel-read","cancel-map","cancel-completed","map-failure","pitch-failure","acquire-failure","success"};
 std::string mode=argc>1?argv[1]:"all";bool found=false;
 for(int i=0;i<7;++i)if(mode=="all"||mode==names[i]){Test(i);std::printf("PASS %s\n",names[i]);found=true;}
 if(mode=="all"||mode=="repeat"){for(int n=0;n<1000;++n)for(int i=0;i<7;++i)Test(i);std::puts("PASS 7000 repeated lifecycles");found=true;}
 Check(found,"known case");
}
