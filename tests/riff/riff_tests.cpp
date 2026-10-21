#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "riff_under_test.inc"
static void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
static void Word(std::vector<unsigned char> &out, uint32_t value) { for (int i = 0; i < 4; ++i) out.push_back(value >> (8*i)); }
static std::vector<unsigned char> Chunk(uint32_t name, const std::vector<unsigned char> &data) {
    std::vector<unsigned char> out; Word(out, name); Word(out, data.size()); out.insert(out.end(), data.begin(), data.end());
    if (data.size() & 1) out.push_back(0);
    return out;
}
static std::vector<unsigned char> Wave(const std::vector<unsigned char> &chunks, uint32_t size = 0) {
    std::vector<unsigned char> out; Word(out, RIFF_ID); Word(out, size ? size : chunks.size() + 4); Word(out, RIFF_WAVE);
    out.insert(out.end(), chunks.begin(), chunks.end()); return out;
}
struct MemoryIO : IFileReadBinary {
    std::vector<unsigned char> data;
    unsigned int position = 0, reportedSize = 0;
    bool missing = false;
    int shortRead = -1;
    int closes = 0;
    intp handle = intp(0x1234567887654321ULL);
    intp open(const char *) override { position = 0; return missing ? 0 : handle; }
    int read(void *out, int count, intp file) override {
        Require(file == handle && count >= 0 && position <= data.size() && unsigned(count) <= data.size() - position, "out-of-file or negative read");
        if (shortRead >= 0) count = std::min(count, shortRead);
        if (count) std::memcpy(out, data.data() + position, count);
        position += count; return count;
    }
    void close(intp file) override { Require(file == handle || file == 0, "truncated file handle"); if (file) ++closes; }
    void seek(intp file, int pos) override { Require(file == handle && pos >= 0 && unsigned(pos) <= data.size(), "invalid seek"); position = pos; }
    unsigned int tell(intp file) override { Require(file == handle, "tell handle"); return position; }
    unsigned int size(intp file) override { Require(file == handle, "size handle"); return reportedSize ? reportedSize : data.size(); }
};
static const uint32_t DATA = MAKEID('d','a','t','a'), JUNK = MAKEID('J','U','N','K');
struct CAudioSourceWave {
    int m_loopStart = -1;
    void ParseCueChunk(IterateRIFF &);
    void ParseSamplerChunk(IterateRIFF &);
};
#include "loops_under_test.inc"
static void Loops() {
    for (int kind = 0; kind < 2; ++kind) {
        std::vector<unsigned char> payload;
        if (kind == 0) { Word(payload, 1); for (int i = 0; i < 5; ++i) Word(payload, 0); Word(payload, 123); }
        else { for (int i = 0; i < 7; ++i) Word(payload, 0); Word(payload, 1); Word(payload, 0); Word(payload, 0); Word(payload, 0); Word(payload, 123); for (int i = 0; i < 3; ++i) Word(payload, 0); }
        for (size_t length = 0; length <= payload.size(); ++length) {
            auto chunks = Chunk(kind == 0 ? MAKEID('c','u','e',' ') : MAKEID('s','m','p','l'), std::vector<unsigned char>(payload.begin(), payload.begin()+length));
            auto next = Chunk(DATA, std::vector<unsigned char>(100, 0xff)); chunks.insert(chunks.end(), next.begin(), next.end());
            MemoryIO io; io.data = Wave(chunks); InFileRIFF file("loops", io); IterateRIFF walk(file, file.RIFFSize()); CAudioSourceWave wave;
            if (kind == 0) wave.ParseCueChunk(walk); else wave.ParseSamplerChunk(walk);
            Require(wave.m_loopStart == (length == payload.size() ? 123 : -1), "truncated loop metadata applied");
            Require(walk.ChunkNext() && walk.ChunkName() == DATA, "loop parse crossed next chunk");
        }
    }
}
struct TailIO : MemoryIO {
    unsigned int size(intp file) override { Require(file == handle, "tail size handle"); return INT_MAX; }
    void seek(intp file, int pos) override { Require(file == handle && pos >= 0, "tail seek"); position = pos; }
    int read(void *out, int count, intp file) override {
        if (position == 0) return MemoryIO::read(out, count, file);
        const auto tail = Chunk(DATA, {42}); const unsigned int start = INT_MAX - 9;
        Require(file == handle && position >= start && position <= INT_MAX && count >= 0 && unsigned(count) <= INT_MAX - position, "tail read outside file");
        std::memcpy(out, tail.data() + position - start, count); position += count; return count;
    }
};
static void Reject(const std::vector<unsigned char> &chunks) {
    MemoryIO io; io.data = Wave(chunks); InFileRIFF file("test", io); IterateRIFF walk(file, file.RIFFSize());
    Require(!walk.ChunkAvailable(), "malformed chunk accepted");
}
static void Negative() { std::vector<unsigned char> chunk; Word(chunk, DATA); Word(chunk, 0xfffffffeU); Reject(chunk); }
static void Truncated() { auto chunk = Chunk(DATA, {1,2,3,4}); chunk.resize(chunk.size()-1); Reject(chunk); }
static void RootOverflow() {
    MemoryIO io; io.data = Wave({}, 0x80000000U); InFileRIFF file("test", io);
    Require(file.RIFFName() == RIFF_WAVE && file.RIFFSize() == 0, "legacy declared size handling changed");
}
int main(int argc, char **argv) {
    Require(argc == 2, "arguments"); const std::string selected = argv[1];
    if (selected == "all" || selected == "negative") Negative();
    if (selected == "all" || selected == "truncated") Truncated();
    if (selected == "all" || selected == "root-overflow") RootOverflow();
    if (selected == "loops") Loops();
    if (selected != "all") { std::cout << "PASS RIFF " << selected << '\n'; return 0; }
    for (int count = 0; count < 12; ++count) {
        MemoryIO io; io.data = Wave({}); io.data.resize(count); InFileRIFF file("short", io);
        Require(file.RIFFName() == 0 && file.RIFFSize() == 0, "short RIFF header accepted");
        IterateRIFF walk(file, file.RIFFSize()); Require(!walk.ChunkAvailable(), "short RIFF iterated");
    }
    for (uint32_t size : {0x80000000U, 0xffffffffU, 0x10000000U, 100U}) {
        std::vector<unsigned char> chunk; Word(chunk, DATA); Word(chunk, size); Reject(chunk);
    }
    for (int count = 1; count < 8; ++count) Reject(std::vector<unsigned char>(count, 0));
    MemoryIO io; auto a = Chunk(JUNK, {1,2,3}); auto b = Chunk(DATA, {4,5,6,7}); a.insert(a.end(), b.begin(), b.end()); io.data = Wave(a, 4);
    {
        InFileRIFF file("legacy-lwv", io); Require(file.RIFFSize() == a.size(), "legacy trailing chunks lost");
        IterateRIFF walk(file, file.RIFFSize()); Require(walk.ChunkAvailable() && walk.ChunkName() == JUNK && walk.ChunkSize() == 3, "odd chunk");
        unsigned char buffer[8]; std::memset(buffer, 0xaa, sizeof(buffer));
        Require(walk.ChunkReadPartial(buffer, 8) == 3 && buffer[0] == 1 && buffer[2] == 3 && buffer[3] == 0xaa, "partial read crossed chunk boundary");
        Require(walk.ChunkReadPartial(buffer, 1) == 0 && walk.ChunkReadInt() == 0, "read beyond current chunk");
        Require(walk.ChunkNext() && walk.ChunkName() == DATA, "odd padding skipped wrong bytes");
        Require(uint32_t(walk.ChunkReadInt()) == 0x07060504U, "read int");
        Require(!walk.ChunkNext(), "iterator did not terminate");
    }
    Require(io.closes == 1, "file not closed");
    io = MemoryIO(); io.data = Wave(Chunk(DATA, {}));
    { InFileRIFF file("empty-chunk", io); IterateRIFF walk(file, file.RIFFSize()); Require(walk.ChunkAvailable() && walk.ChunkSize() == 0 && !walk.ChunkNext(), "zero-length chunk changed"); }
    io = MemoryIO(); io.data = Wave(Chunk(DATA, {8})); io.data.pop_back();
    { InFileRIFF file("no-final-pad", io); IterateRIFF walk(file, file.RIFFSize()); Require(walk.ChunkAvailable() && !walk.ChunkNext(), "legacy final odd chunk rejected"); }
    io = MemoryIO(); io.data = Wave(Chunk(DATA, {1,2,3,4})); io.reportedSize = 0x80000000U;
    { InFileRIFF file("oversized-file", io); Require(file.RIFFSize() == 0 && file.RIFFName() == 0, "unrepresentable file size accepted"); }
    io = MemoryIO(); io.data = Wave(Chunk(DATA, {1,2,3,4})); io.shortRead = 3;
    { InFileRIFF file("short-read", io); Require(file.RIFFName() == 0, "incomplete header read accepted"); }
    io = MemoryIO(); io.data = Wave(Chunk(DATA, {1,2,3,4}));
    { InFileRIFF file("subrange", io); IterateRIFF walk(file, INT_MAX); Require(!walk.ChunkAvailable(), "iterator outside file accepted"); }
    auto child = Chunk(DATA, {9,8,7,6}); std::vector<unsigned char> list; Word(list, MAKEID('I','N','F','O')); list.insert(list.end(), child.begin(), child.end());
    io = MemoryIO(); io.data = Wave(Chunk(MAKEID('L','I','S','T'), list));
    { InFileRIFF file("nested", io); IterateRIFF parent(file, file.RIFFSize()); Require(uint32_t(parent.ChunkReadInt()) == MAKEID('I','N','F','O'), "LIST type"); IterateRIFF nested(parent); Require(nested.ChunkAvailable() && nested.ChunkName() == DATA && nested.ChunkSize() == 4, "nested iterator boundary"); Require(uint32_t(nested.ChunkReadInt()) == 0x06070809U && !nested.ChunkNext(), "nested iteration"); }
    Loops();
    TailIO tail; tail.data = Wave({});
    { InFileRIFF file("large-tail", tail); file.PositionSet(INT_MAX - 9); IterateRIFF walk(file, 9); unsigned char value = 0; Require(walk.ChunkAvailable() && walk.ChunkRead(&value) == 1 && value == 42 && !walk.ChunkNext(), "large-file alignment overflow"); }
    io = MemoryIO(); io.missing = true;
    { InFileRIFF file("missing", io); Require(!file.IsValid() && file.ReadInt() == 0, "missing file read"); }
    std::mt19937 random(0x34);
    for (int test = 0; test < 10000; ++test) {
        std::vector<unsigned char> chunks(random() % 128); for (auto &value : chunks) value = random();
        if (test % 3 == 0 && chunks.size() >= 8) { uint32_t n = random() % 150; for (int j = 0; j < 4; ++j) chunks[4+j] = n >> (j*8); }
        io = MemoryIO(); io.data = Wave(chunks); InFileRIFF file("random", io); IterateRIFF walk(file, file.RIFFSize());
        int steps = 0; while (walk.ChunkAvailable()) { Require(++steps < 32, "iterator did not advance"); unsigned char buffer[256]; const int count = walk.ChunkReadPartial(buffer, 256); Require(count >= 0 && count <= 128, "invalid byte count"); walk.ChunkNext(); }
    }
    std::cout << "PASS RIFF boundaries, partial reads, padding, nested LIST, WAV loop metadata, LWV compatibility, x64 handles and 10000 malformed inputs\n";
}
