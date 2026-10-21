#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using byte = unsigned char;
using FSAsyncControl_t = void *;
enum FSAsyncStatus_t { FSASYNC_OK, FSASYNC_ERR_READING, FSASYNC_ERR_FILEOPEN };
struct FileAsyncRequest_t { void *pData = nullptr; int nOffset = 0; int nBytes = 0; };
static bool pc = true;
static bool IsPC() { return pc; }
static bool IsX360() { return !pc; }
#define SndAlignReads() 1
#define Assert(x) ((void)0)
#define MAX_PATH 260
static double Plat_FloatTime() { return 1; }
static double realtime = 0;
static void Warning(const char *, ...) {}
static void DevMsg(const char *, ...) {}
static void MaybeReportMissingWav(const char *) {}
static bool SndAsyncSpewBlocking() { return false; }
struct ConVar { bool GetBool() const { return false; } } snd_async_spew;
struct FileSystem {
    int releases = 0, nullReleases = 0, finishes = 0;
    void AsyncFinish(void *, bool) { ++finishes; }
    void AsyncRelease(void *handle) { ++releases; if (!handle) ++nullReleases; }
    bool String(int, char *, int) { return false; }
} filesystem, *g_pFileSystem = &filesystem;
#define Q_memcpy std::memcpy
#define MIN(a, b) ((a) < (b) ? (a) : (b))
struct CAsyncWaveData {
    int m_nDataSize = 0, m_nReadSize = 0;
    void *m_pvData = nullptr;
    byte *m_pAlloc = nullptr;
    FileAsyncRequest_t m_async;
    void *m_hAsyncControl = reinterpret_cast<void *>(1);
    float m_arrival = 0, m_start = 0;
    bool m_bLoaded = true, m_bMissing = false, m_bPostProcessed = true;
    int m_hFileNameHandle = 0;
    const char *GetFileName() { return "test.wav"; }
    void OnAsyncCompleted(const FileAsyncRequest_t *, int, FSAsyncStatus_t);
    bool BlockingCopyData(void *, int, int, int);
    bool BlockingGetDataPointer(void **);
};
#include "wavecache_under_test.inc"
static void Check(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
static CAsyncWaveData Data(std::vector<byte> &bytes, int offset = 44) {
    CAsyncWaveData data;
    data.m_nReadSize = data.m_nDataSize = static_cast<int>(bytes.size());
    data.m_pAlloc = bytes.data(); data.m_pvData = bytes.data();
    data.m_async.nOffset = offset;
    return data;
}
static void Valid() {
    std::vector<byte> bytes(64); for (int i = 0; i < 64; ++i) bytes[i] = static_cast<byte>(i);
    auto data = Data(bytes); byte dest[16] = {};
    Check(data.BlockingCopyData(dest, 16, 52, 16), "valid absolute file offset");
    Check(!std::memcmp(dest, bytes.data() + 8, 16), "valid copy contents");
    data = Data(bytes); Check(data.BlockingCopyData(dest, 16, 92, 16), "copy ending exactly at source end");
    Check(!std::memcmp(dest, bytes.data() + 48, 16), "source end contents");
    data = Data(bytes); void *ptr = nullptr;
    Check(data.BlockingGetDataPointer(&ptr) && ptr == bytes.data(), "complete direct pointer");
    data = Data(bytes); data.m_bLoaded = false;
    Check(!data.BlockingCopyData(dest, 16, 44, 16) && filesystem.finishes == 1, "unfinished load rejected");
    for (int offset : {0, 1, 44, 4096, INT_MAX - 64}) {
        for (int pos = 0; pos <= 64; ++pos) {
            for (int count = 0; count <= 16; ++count) {
                data = Data(bytes, offset); std::memset(dest, 0xa5, sizeof(dest));
                const bool expected = count <= 64 - pos;
                Check(data.BlockingCopyData(dest, 16, offset + pos, count) == expected, "range matrix result");
                if (expected) Check(!std::memcmp(dest, bytes.data() + pos, count), "range matrix contents");
                else Check(dest[0] == 0xa5, "range matrix no partial writes");
            }
        }
    }
}
static void Destination() {
    std::vector<byte> bytes(64, 0x42); auto data = Data(bytes);
    std::vector<byte> dest(8, 0xa5);
    Check(!data.BlockingCopyData(dest.data(), 8, 44, 16), "destination capacity rejected");
    Check(dest[0] == 0xa5, "rejected destination unchanged");
}
static void Source() {
    std::vector<byte> bytes(16, 0x42); auto data = Data(bytes, 0); byte dest[16] = {};
    Check(!data.BlockingCopyData(dest, 16, 12, 8), "source tail rejected");
}
static void Partial() {
    std::vector<byte> bytes(64, 0x42); auto data = Data(bytes); data.m_nReadSize = 8;
    void *ptr = reinterpret_cast<void *>(1);
    Check(!data.BlockingGetDataPointer(&ptr) && ptr == nullptr, "partial direct pointer rejected and cleared");
    byte dest[16]; std::memset(dest, 0xa5, sizeof(dest));
    data = Data(bytes); data.m_nReadSize = 8;
    Check(!data.BlockingCopyData(dest, 16, 44, 16), "partial boolean copy rejected");
    Check(dest[0] == 0xa5 && dest[15] == 0xa5, "partial copy leaves destination unchanged");
    data = Data(bytes); data.m_nReadSize = 8;
    Check(data.BlockingCopyData(dest, 16, 44, 8), "available prefix remains readable");
}
static void Callback() {
    for (bool usePC : {true, false}) {
        pc = usePC;
        for (int size : {-1, 0, 10, 43, 44, 45, 60, 64, 80}) {
            std::vector<byte> bytes(100, 0x42); CAsyncWaveData data;
            data.m_nDataSize = 20; data.m_async.nBytes = 64;
            data.m_pvData = bytes.data(); FileAsyncRequest_t request; request.pData = bytes.data();
            data.OnAsyncCompleted(&request, size, FSASYNC_OK);
            const int expected = usePC ? std::min(20, std::max(0, size - 44)) : std::min(20, std::max(0, size));
            Check(data.m_nReadSize == expected, "callback clamps actual data length");
            if (usePC && !expected) Check(data.m_pvData == nullptr, "no pointer past short allocation");
        }
    }
    pc = true; CAsyncWaveData data; data.m_nDataSize = 20; data.m_async.nBytes = 64;
    FileAsyncRequest_t request;
    data.OnAsyncCompleted(&request, 64, FSASYNC_ERR_READING);
    Check(!data.m_pvData && !data.m_nReadSize, "null callback allocation");
}
static void Invalid() {
    std::vector<byte> bytes(64, 0x42); byte dest[16] = {};
    for (int offset : {-1, 0, 43, INT_MIN}) {
        auto data = Data(bytes); Check(!data.BlockingCopyData(dest, 16, offset, 1), "offset before source rejected");
    }
    auto data = Data(bytes); Check(!data.BlockingCopyData(dest, -1, 44, 1), "negative capacity rejected");
    data = Data(bytes); Check(!data.BlockingCopyData(dest, 16, 44, -1), "negative count rejected");
    data = Data(bytes); Check(!data.BlockingCopyData(nullptr, 16, 44, 1), "null destination rejected");
    data = Data(bytes); data.m_pvData = nullptr;
    Check(!data.BlockingCopyData(dest, 16, 44, 1), "null source rejected");
    data = Data(bytes); Check(!data.BlockingCopyData(dest, 16, INT_MAX, 1), "extreme offset rejected");
    data = Data(bytes); Check(!data.BlockingGetDataPointer(nullptr), "null pointer output rejected");
}
static void Handles() {
    std::vector<byte> bytes(64); byte dest[16]; auto data = Data(bytes);
    filesystem = FileSystem();
    Check(data.BlockingCopyData(dest, 16, 44, 16), "first copy");
    Check(data.BlockingCopyData(dest, 16, 44, 16), "cached copy");
    void *ptr = nullptr;
    Check(data.BlockingGetDataPointer(&ptr), "cached pointer");
    Check(filesystem.releases == 1 && filesystem.nullReleases == 0, "release async handle exactly once");
}
int main(int argc, char **argv) {
    const std::string test = argc > 1 ? argv[1] : "all";
    for (auto entry : {std::make_pair("valid", Valid), {"destination", Destination}, {"source", Source},
                       {"partial", Partial}, {"callback", Callback}, {"invalid", Invalid}, {"handles", Handles}}) {
        if (test == "all" || test == entry.first) { entry.second(); std::printf("PASS %s\n", entry.first); }
    }
}
