#include "studio.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#undef Assert
#define Assert(x) ((void)0)
#undef MemAlloc_PushAllocDbgInfo
#undef MemAlloc_PopAllocDbgInfo
#define MemAlloc_PushAllocDbgInfo(...) ((void)0)
#define MemAlloc_PopAllocDbgInfo(...) ((void)0)
#define MdlCacheMsg(...) ((void)0)
#define Warning(...) ((void)0)
using MDLHandle_t = unsigned short;
static MDLHandle_t VoidPtrToMDLHandle(void *) { return 0; }
static const int MDLCACHE_VERTEXES = 1;
static int MakeCacheID(MDLHandle_t, int) { return 0; }
struct CTempAllocHelper {
    void *ptr = nullptr;
    ~CTempAllocHelper() { std::free(ptr); }
    void Alloc(int size) { ptr = std::malloc(size); }
    void *Get() { return ptr; }
};
struct CLZMA {
    static bool IsCompressed(unsigned char *) { return false; }
    static unsigned int GetActualSize(unsigned char *) { return 0; }
    static unsigned int Uncompress(unsigned char *, unsigned char *) { return 0; }
};
struct Hardware { int level = 90; int GetDXSupportLevel() { return level; } } hardware;
static Hardware *g_pMaterialSystemHardwareConfig = &hardware;
struct Section { int locks = 0; void BeginFrameLocking() { ++locks; } void EndFrameLocking() { --locks; } };
static const int STUDIODATA_FLAGS_NO_VERTEX_DATA = 0x10;
struct StudioData { void *m_VertexCache = nullptr; int m_nFlags = 0; };
class CMDLCache {
public:
    StudioData entry;
    StudioData *m_MDLDict[1] = {&entry};
    Section section;
    int allocCalls = 0, cacheCalls = 0;
    bool failAllocation = false;
    bool ProcessVertexData(studiohdr_t *, void *, int, bool);
    std::vector<void *> allocations;
    ~CMDLCache() { for (void *p : allocations) std::free(p); }
    const char *GetModelName(MDLHandle_t) { return "fixture.mdl"; }
    Section *GetCacheSection(int) { return &section; }
    void *AllocData(int, int bytes) {
        ++allocCalls;
        if (failAllocation || bytes <= 0 || bytes > 1000000) return nullptr;
        void *p = nullptr;
        if (posix_memalign(&p, 32, bytes)) return nullptr;
        std::memset(p, 0xcd, bytes); allocations.push_back(p); return p;
    }
    void CacheData(void **target, void *data, int, const char *, int, int) { ++cacheCalls; *target = data; }
#if VVD_HAS_SIZE
    vertexFileHeader_t *BuildAndCacheVertexData(studiohdr_t *, vertexFileHeader_t *, int nDataSize);
#else
    vertexFileHeader_t *BuildAndCacheVertexData(studiohdr_t *, vertexFileHeader_t *);
#endif
};
#include "vertex_under_test.inc"
static void Check(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
struct Fixture {
    std::vector<byte> bytes;
    studiohdr_t model;
    Fixture(bool fixups = false) {
        std::memset(&model, 0, sizeof(model)); model.checksum = 123;
        std::strcpy(model.name, "fixture.mdl");
        const int vertexStart = sizeof(vertexFileHeader_t) + (fixups ? 3 * sizeof(vertexFileFixup_t) : 0);
        bytes.resize(vertexStart + 4 * (sizeof(mstudiovertex_t) + sizeof(Vector4D)), 0);
        auto *h = Header(); h->id = MODEL_VERTEX_FILE_ID; h->version = MODEL_VERTEX_FILE_VERSION;
        h->checksum = model.checksum; h->numLODs = 3;
        h->numLODVertexes[0] = 4; h->numLODVertexes[1] = 3; h->numLODVertexes[2] = 1;
        h->vertexDataStart = vertexStart; h->tangentDataStart = vertexStart + 4 * sizeof(mstudiovertex_t);
        if (fixups) {
            h->numFixups = 3; h->fixupTableStart = sizeof(vertexFileHeader_t);
            const vertexFileFixup_t table[] = {{0, 0, 1}, {1, 1, 2}, {2, 3, 1}};
            std::memcpy(bytes.data() + h->fixupTableStart, table, sizeof(table));
        }
        for (int i = 0; i < 4; ++i) {
            std::memset(bytes.data() + vertexStart + i * sizeof(mstudiovertex_t), i + 1, sizeof(mstudiovertex_t));
            std::memset(bytes.data() + h->tangentDataStart + i * sizeof(Vector4D), i + 10, sizeof(Vector4D));
        }
    }
    vertexFileHeader_t *Header() { return reinterpret_cast<vertexFileHeader_t *>(bytes.data()); }
    vertexFileFixup_t *Fixups() { return reinterpret_cast<vertexFileFixup_t *>(bytes.data() + Header()->fixupTableStart); }
    vertexFileHeader_t *Load(CMDLCache &cache, int size = -1) {
#if VVD_HAS_SIZE
        return cache.BuildAndCacheVertexData(&model, Header(), size < 0 ? static_cast<int>(bytes.size()) : size);
#else
        return cache.BuildAndCacheVertexData(&model, Header());
#endif
    }
    void Rejected(const char *message) {
        CMDLCache cache; Check(!Load(cache), message);
        Check(!cache.allocCalls && !cache.cacheCalls && !cache.section.locks, "invalid data rejected before allocation");
    }
};
static void Valid() {
    static_assert(sizeof(vertexFileHeader_t) == 64, "VVD disk header");
    static_assert(sizeof(vertexFileFixup_t) == 12, "VVD disk fixup");
    for (bool fixups : {false, true}) for (int level : {70, 90}) for (int lod : {0, 1, 2, 7}) {
        hardware.level = level; Fixture f(fixups); f.model.rootLOD = lod;
        CMDLCache cache; auto *out = f.Load(cache); Check(out != nullptr, "valid model loads");
        const int root = std::min(lod, 2), count = f.Header()->numLODVertexes[root];
        Check(cache.allocCalls == 1 && cache.cacheCalls == 1 && !cache.section.locks, "cache lifecycle balanced");
        Check(out->numFixups == 0, "fixups applied");
        const int first = fixups ? (root == 0 ? 0 : (root == 1 ? 1 : 3)) : 0;
        for (int i = 0; i < count; ++i) {
            const auto *vertex = reinterpret_cast<byte *>(out) + out->vertexDataStart + i * sizeof(mstudiovertex_t);
            for (unsigned j = 0; j < sizeof(mstudiovertex_t); ++j) Check(vertex[j] == first + i + 1, "vertex bytes preserved");
            if (level >= 80) {
                const auto *tangent = reinterpret_cast<byte *>(out) + out->tangentDataStart + i * sizeof(Vector4D);
                for (unsigned j = 0; j < sizeof(Vector4D); ++j) Check(tangent[j] == first + i + 10, "tangent bytes preserved");
            }
        }
        Check(level >= 80 || out->tangentDataStart == 0, "DX7 tangent-free path");
    }
    hardware.level = 70; Fixture f; f.Header()->tangentDataStart = 0;
    f.bytes.resize(f.Header()->vertexDataStart + 4 * sizeof(mstudiovertex_t));
    CMDLCache cache; Check(f.Load(cache) != nullptr, "vertex-only model without tangent requirement");
    hardware.level = 90;
    Fixture empty; empty.Header()->numLODs = 1; empty.Header()->numLODVertexes[0] = 0;
    empty.Header()->vertexDataStart = empty.Header()->tangentDataStart = sizeof(vertexFileHeader_t);
    empty.bytes.resize(sizeof(vertexFileHeader_t)); CMDLCache emptyCache;
    Check(empty.Load(emptyCache) != nullptr, "zero-vertex model remains loadable");
}
static void ShortHeader() {
    for (unsigned size = 0; size < sizeof(vertexFileHeader_t); ++size) {
        Fixture f; f.bytes.resize(size); f.bytes.shrink_to_fit(); f.Rejected("short VVD header rejected");
    }
}
static void Lod() { Fixture f; f.Header()->numLODs = 9; f.model.rootLOD = 8; f.Rejected("LOD count outside array rejected"); }
static void Overflow() { Fixture f; f.Header()->numLODVertexes[0] = INT_MAX; f.Rejected("cache size overflow rejected"); }
static void Source() { Fixture f; f.bytes.resize(f.Header()->vertexDataStart + sizeof(mstudiovertex_t)); f.bytes.shrink_to_fit(); f.Rejected("short vertex block rejected"); }
static void Tangent() { Fixture f; f.bytes.resize(f.Header()->tangentDataStart); f.bytes.shrink_to_fit(); f.Rejected("short tangent block rejected"); }
static void FixupSource() { Fixture f(true); f.Fixups()[2].sourceVertexID = 4; f.Rejected("fixup source beyond vertex pool rejected"); }
static void FixupTarget() {
    hardware.level = 70; Fixture f(true); f.Fixups()[0].numVertexes = 4;
    f.Rejected("fixup sum exceeds output vertex count rejected"); hardware.level = 90;
}
static void Allocation() {
    Fixture f; CMDLCache cache; cache.failAllocation = true;
    Check(!f.Load(cache), "failed allocation rejected");
    Check(!cache.cacheCalls && !cache.section.locks, "failed allocation not cached or locked");
}
static void Dispatch() {
    for (int mode = 0; mode < 4; ++mode) {
        Fixture f; f.model.numbodyparts = 1; CMDLCache cache;
        if (mode == 1) f.Header()->id = 0;
        if (mode == 2) cache.failAllocation = true;
        const bool ok = cache.ProcessVertexData(&f.model, f.Header(), f.bytes.size(), mode != 3);
        Check(ok == (mode == 0), "dispatch propagates load failure");
        Check((cache.entry.m_nFlags & STUDIODATA_FLAGS_NO_VERTEX_DATA) == (mode == 0 ? 0 : 0x10), "dispatch marks unavailable data");
        Check(cache.cacheCalls == (mode == 0 ? 1 : 0), "dispatch does not publish invalid data");
    }
}
static void Invalid() {
    for (int n : {-1, 0, 9, INT_MAX}) { Fixture f; f.Header()->numLODs = n; f.Rejected("invalid LOD count"); }
    for (int n : {-1, INT_MAX}) { Fixture f; f.Header()->numLODVertexes[1] = n; f.Rejected("invalid LOD vertex count"); }
    for (int n : {-1, 0, 63, 65, INT_MAX}) { Fixture f; f.Header()->vertexDataStart = n; f.Rejected("invalid vertex offset"); }
    for (int n : {-1, 0, 63, 65, INT_MAX}) { Fixture f; f.Header()->tangentDataStart = n; f.Rejected("invalid tangent offset"); }
    for (int n : {-1, INT_MAX}) { Fixture f(true); f.Header()->numFixups = n; f.Rejected("invalid fixup count"); }
    for (int n : {-1, 0, 65, INT_MAX}) { Fixture f(true); f.Header()->fixupTableStart = n; f.Rejected("invalid fixup offset"); }
    for (int n : {-1, INT_MAX}) { Fixture f(true); f.Fixups()[0].sourceVertexID = n; f.Rejected("invalid fixup source"); }
    for (int n : {-1, INT_MAX}) { Fixture f(true); f.Fixups()[0].numVertexes = n; f.Rejected("invalid fixup length"); }
    for (int n : {-1, MAX_NUM_LODS, INT_MAX}) { Fixture f(true); f.Fixups()[0].lod = n; f.Rejected("invalid fixup LOD"); }
    Fixture f(true); f.Fixups()[0].numVertexes = 0; f.Rejected("incomplete fixup output");
    Fixture checksum; ++checksum.Header()->checksum; checksum.Rejected("checksum mismatch");
#if VVD_HAS_SIZE
    CMDLCache cache;
    Check(!cache.BuildAndCacheVertexData(&f.model, nullptr, 100), "null VVD");
    Check(!cache.BuildAndCacheVertexData(&f.model, f.Header(), -1), "negative file size");
#endif
}
static void Fuzz() {
    uint32_t seed = 123456789;
    for (int i = 0; i < 12000; ++i) {
        Fixture f(i % 2 != 0);
        seed = seed * 1664525u + 1013904223u;
        const int position = 12 + ((seed >> 8) % (f.Header()->vertexDataStart - 12));
        f.bytes[position] ^= static_cast<byte>(1 + (seed >> 24));
        CMDLCache cache; auto *result = f.Load(cache);
        Check(!cache.section.locks && cache.cacheCalls == (result ? 1 : 0), "mutated model cache lifecycle");
    }
}
int main(int argc, char **argv) {
    const std::string test = argc > 1 ? argv[1] : "all";
    for (auto entry : {std::make_pair("valid", Valid), {"short-header", ShortHeader}, {"lod", Lod},
         {"size-overflow", Overflow}, {"source", Source}, {"tangent", Tangent}, {"fixup-source", FixupSource},
         {"fixup-target", FixupTarget}, {"allocation", Allocation}, {"dispatch", Dispatch}, {"invalid", Invalid}, {"fuzz", Fuzz}}) {
        if (test == "all" || test == entry.first) { entry.second(); std::printf("PASS %s\n", entry.first); }
    }
}
