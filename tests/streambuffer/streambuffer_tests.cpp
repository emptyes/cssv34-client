#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static int checks;
#define CHECK(e) do { ++checks; if (!(e)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); std::exit(1); } } while (0)
#define Assert(e) CHECK(e)
using std::min;
typedef int FileHandle_t;
static const int FILESYSTEM_INVALID_HANDLE = 0;
static const int FILESYSTEM_SEEK_HEAD = 0;
static const int DEFAULT_STREAM_CHUNK_SIZE = 16384;
static int Q_strlen(const char *s) { return (int)std::strlen(s); }
static void Q_strcpy(char *a, const char *b) { std::strcpy(a, b); }
static char *V_strdup(const char *s) { char *p = new char[std::strlen(s) + 1]; std::strcpy(p, s); return p; }
static void Warning(const char *, ...) {}
static void Error(const char *, ...) { throw std::runtime_error("stream filesystem failure"); }
struct FileSystem
{
    std::vector<unsigned char> bytes;
    int cursor = 0;
    int writes = 0;
    int opens = 0;
    bool shortWrite = false;
    bool failOpen = false;
    FileHandle_t Open(const char *, const char *mode, const char *)
    {
        ++opens;
        if (failOpen) return 0;
        cursor = 0;
        if (mode[0] == 'w') bytes.clear();
        return 1;
    }
    int Size(FileHandle_t) { return (int)bytes.size(); }
    int Read(void *out, int n, FileHandle_t)
    {
        n = min(n, (int)bytes.size() - cursor);
        if (n > 0) std::memcpy(out, bytes.data() + cursor, n);
        cursor += n;
        return n;
    }
    int Write(const void *data, int n, FileHandle_t)
    {
        CHECK(n >= 0 && cursor >= 0);
        if (shortWrite && n > 0) --n;
        if (cursor + n > (int)bytes.size()) bytes.resize(cursor + n);
        if (n) std::memcpy(bytes.data() + cursor, data, n);
        cursor += n;
        ++writes;
        return n;
    }
    void Seek(FileHandle_t, int position, int) { CHECK(position >= 0); cursor = position; }
    void Close(FileHandle_t) {}
};
static FileSystem filesystem;
static FileSystem *g_pFullFileSystem = &filesystem;
struct Memory
{
    std::vector<unsigned char> data;
    explicit Memory(int size) : data(size) {}
    int NumAllocated() const { return (int)data.size(); }
    void Grow(int n) { data.resize(data.size() + n); }
    unsigned char &operator[](int i) { CHECK(i >= 0 && i < NumAllocated()); return data[i]; }
};
class CUtlBuffer
{
public:
    enum { PUT_OVERFLOW = 1, GET_OVERFLOW = 2, MAX_ERROR_FLAG = 2, READ_ONLY = 1 };
    enum SeekType_t { SEEK_HEAD, SEEK_CURRENT, SEEK_TAIL };
    typedef bool (CUtlBuffer::*UtlBufferOverflowFunc_t)(int);
    CUtlBuffer(int, int size, int flags) : m_Memory(size), m_Put(0), m_Get(0),
        m_nOffset(0), m_nMaxPut(0), m_Error(0), m_Flags(flags), m_nTab(0), putOverflow(NULL) {}
    bool IsValid() const { return m_Error == 0; }
    bool IsReadOnly() const { return (m_Flags & READ_ONLY) != 0; }
    bool IsText() const { return false; }
    bool ContainsCRLF() const { return false; }
    int Size() const { return m_Memory.NumAllocated(); }
    int TellPut() const { return m_Put; }
    int TellGet() const { return m_Get; }
    int TellMaxPut() const { return m_nMaxPut; }
    void *Base() { return m_Memory.data.data(); }
    void *PeekPut(int offset = 0) { return &m_Memory[m_Put - m_nOffset + offset]; }
    void SetOverflowFuncs(UtlBufferOverflowFunc_t, UtlBufferOverflowFunc_t put) { putOverflow = put; }
    bool OnPutOverflow(int n) { return (this->*putOverflow)(n); }
    bool CheckPut(int);
    void Put(const void *, int);
    void SeekPut(SeekType_t, int);
    void AddNullTermination(int);
    void AddNullTermination() { AddNullTermination(m_Put); }
protected:
    Memory m_Memory;
    int m_Put, m_Get, m_nOffset, m_nMaxPut, m_Error, m_Flags, m_nTab;
    UtlBufferOverflowFunc_t putOverflow;
};
#define SetUtlBufferOverflowFuncs(get, put) SetOverflowFuncs(static_cast<UtlBufferOverflowFunc_t>(get), static_cast<UtlBufferOverflowFunc_t>(put))
#include "stream_declaration.inc"
#include "stream_under_test.inc"
bool CUtlStreamBuffer::StreamGetOverflow(int) { return false; }

static void RewriteHeader(int length, int chunk, bool delayed)
{
    filesystem = FileSystem();
    std::vector<unsigned char> expected(length);
    for (int i = 0; i < length; ++i) expected[i] = (unsigned char)(i * 37 + 11);
    {
        CUtlStreamBuffer buffer("test.dem", NULL, 0, delayed);
        for (int i = 0; i < length; i += chunk)
            buffer.Put(expected.data() + i, min(chunk, length - i));
        CHECK(buffer.IsValid());
        int headerLength = min(1072, length);
        buffer.SeekPut(CUtlBuffer::SEEK_HEAD, 0);
        std::fill(expected.begin(), expected.begin() + headerLength, 0x5a);
        buffer.Put(expected.data(), headerLength);
        buffer.Close();
        CHECK(filesystem.bytes == expected);
    }
    CHECK(filesystem.bytes == expected);
}
int main()
{
    const int sizes[] = {1, 2, 5, 1072, 1073, 16382, 16383, 16384, 16385, 32767, 65539};
    const int chunks[] = {1, 3, 7, 1072, 16384, 19001};
    for (int length : sizes)
        for (int chunk : chunks)
            for (bool delayed : {false, true}) RewriteHeader(length, chunk, delayed);
    for (int seed = 0; seed < 64; ++seed)
    {
        filesystem = FileSystem();
        std::mt19937 rng(seed);
        std::vector<unsigned char> expected(70000, 0);
        CUtlStreamBuffer buffer("random.bin", NULL);
        buffer.Put(expected.data(), (int)expected.size());
        for (int step = 0; step < 100; ++step)
        {
            int position = (int)(rng() % (expected.size() - 256));
            int length = 1 + (int)(rng() % 256);
            std::vector<unsigned char> data(length);
            for (auto &byte : data) byte = (unsigned char)rng();
            buffer.SeekPut(CUtlBuffer::SEEK_HEAD, position);
            buffer.Put(data.data(), length);
            std::copy(data.begin(), data.end(), expected.begin() + position);
        }
        buffer.Close();
        CHECK(filesystem.bytes == expected);
    }
    filesystem = FileSystem();
    {
        CUtlStreamBuffer buffer("last.dem", NULL);
        unsigned char end[] = {7, 0x35, 0xc1, 1, 0};
        buffer.Put(end, sizeof(end));
        buffer.SeekPut(CUtlBuffer::SEEK_HEAD, 0);
        unsigned char head = 7;
        buffer.Put(&head, 1);
    }
    CHECK(filesystem.bytes == std::vector<unsigned char>({7, 0x35, 0xc1, 1, 0}));
    filesystem = FileSystem();
    {
        CUtlStreamBuffer buffer("empty.bin", NULL, 0, true);
        buffer.Close();
    }
    CHECK(filesystem.opens == 0);
    filesystem = FileSystem();
    filesystem.bytes = {1, 2, 3};
    {
        CUtlStreamBuffer buffer("readonly.bin", NULL, CUtlBuffer::READ_ONLY);
        CHECK(buffer.IsValid());
    }
    CHECK(filesystem.bytes == std::vector<unsigned char>({1, 2, 3}));
    CHECK(filesystem.writes == 0);
    filesystem = FileSystem();
    {
        CUtlStreamBuffer buffer("reopen.bin", NULL);
        unsigned char value = 123;
        buffer.Put(&value, 1);
        buffer.Close();
        buffer.Open("reopen.bin", NULL, 0);
        value = 45;
        buffer.Put(&value, 1);
    }
    CHECK(filesystem.bytes == std::vector<unsigned char>({45}));
    filesystem = FileSystem();
    {
        CUtlStreamBuffer buffer("short.bin", NULL);
        std::vector<unsigned char> data(32000, 8);
        buffer.Put(data.data(), (int)data.size());
        filesystem.shortWrite = true;
        bool caught = false;
        try { buffer.Close(); } catch (const std::runtime_error &) { caught = true; }
        CHECK(caught);
        filesystem.shortWrite = false;
    }
    std::printf("PASS stream writes: %d checks\n", checks);
}
