#include <algorithm>
#include <array>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int32 = int32_t;
using int64 = int64_t;
using uintp = uintptr_t;
#define FORCEINLINE inline
#define RESTRICT
#define VALVE_LITTLE_ENDIAN 1
#define Assert(...) ((void)0)
#define AssertMsg(...) ((void)0)
#define AssertMsg2(...) ((void)0)
#define Q_memcpy std::memcpy
#define Q_strcmp std::strcmp
#define IsPC() true
#define MEM_ALLOC_CREDIT() ((void)0)
#define VPROF(...) ((void)0)
static uint32 LoadLittleDWord(const uint32 *base, unsigned int index) { return base[index]; }
static void StoreLittleDWord(uint32 *base, unsigned int index, uint32 value) { base[index] = value; }
template <typename T> static T Min(T a, T b) { return std::min(a, b); }
uint32 g_BitWriteMasks[32][33], g_ExtraMasks[33], g_LittleBits[32];
#include "bitbuf_under_test.h"
#include "bitbuf_under_test.inc"
#include "definitions.inc"
static int warnings = 0;
static void Warning(const char *, ...) { ++warnings; }
static int Q_strlen(const char *s) { return static_cast<int>(std::strlen(s)); }
static void Q_strncpy(char *dest, const char *source, size_t count) {
    if (count) { const size_t n = std::min(std::strlen(source), count - 1); std::memcpy(dest, source, n); dest[n] = 0; }
}
static const int COPY_ALL_CHARACTERS = -1;
static void Q_strncat(char *dest, const char *source, size_t count, int) {
    const size_t used = std::strlen(dest);
    if (used < count) Q_strncpy(dest + used, source, count - used);
}
static void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
static int Q_log2(int value) { int n = 0; while (value >>= 1) ++n; return n; }
template <typename T> struct CUtlVector {
    std::vector<T> values;
    void SetCount(int count) { values.resize(count); }
    int Count() const { return static_cast<int>(values.size()); }
    T *Base() { return values.data(); }
    void AddToTail(const T &value) { values.push_back(value); }
    void Remove(int index) { values.erase(values.begin() + index); }
    T &operator[](int index) { return values.at(index); }
};
struct Entry {
    std::string name;
    std::vector<unsigned char> data;
    bool operator==(const Entry &b) const { return name == b.name && data == b.data; }
};
struct CNetworkStringTable {
    int id = 0, maximum = 8192, fixedBytes = 0, fixedBits = 0;
    bool nullData = false;
    std::vector<Entry> values;
    const char *GetTableName() const { return "recording-test"; }
    int GetTableId() const { return id; }
    int GetNumStrings() const { return static_cast<int>(values.size()); }
    int GetMaxStrings() const { return maximum; }
    int GetEntryBits() const { return Q_log2(maximum); }
    bool IsUserDataFixedSize() const { return fixedBytes > 0; }
    int GetUserDataSize() const { return fixedBytes; }
    int GetUserDataSizeBits() const { return fixedBits; }
    const char *GetString(int index) const { return values.at(index).name.c_str(); }
    const void *GetStringUserData(int index, int *length) const {
        *length = static_cast<int>(values.at(index).data.size());
        return nullData ? nullptr : values.at(index).data.data();
    }
    void SetStringUserData(int index, int length, const void *data) {
        if (!length) values.at(index).data.clear();
        else { const auto *p = static_cast<const unsigned char *>(data); values.at(index).data.assign(p, p + length); }
    }
    int AddString(bool, const char *name, int length, const void *data) {
        values.push_back({name, {}}); SetStringUserData(GetNumStrings() - 1, length, data); return GetNumStrings() - 1;
    }
    void ParseUpdate(bf_read &, int);
};
#include "table_under_test.inc"
struct SVC_UpdateStringTable {
    int m_nTableID = 0, m_nChangedEntries = 0, m_nLength = 0;
    bf_write m_DataOut;
    bf_read m_DataIn;
    int GetType() const { return 13; }
    bool WriteToBuffer(bf_write &);
    bool ReadFromBuffer(bf_read &);
};
#include "messages_under_test.inc"
struct Container {
    std::vector<CNetworkStringTable *> tables;
    int GetNumTables() const { return static_cast<int>(tables.size()); }
    CNetworkStringTable *GetTable(int index) { return tables.at(index); }
};
static Container container;
static Container *networkStringTableContainerClient = &container;
struct Packet { int bits; std::vector<uint32> words; };
struct CDemoRecorder {
    bool m_bRecording = true;
    std::vector<Packet> packets;
    void RecordStringTables();
    void StopRecording() { m_bRecording = false; }
    void WriteMessages(bf_write &buffer) {
        Packet packet; packet.bits = buffer.GetNumBitsWritten();
        packet.words.resize((packet.bits + 31) / 32);
        std::memcpy(packet.words.data(), buffer.GetData(), (packet.bits + 7) / 8);
        packets.push_back(std::move(packet));
    }
};
#include "recorder_under_test.inc"
static void InitializeMasks() {
    for (unsigned int start = 0; start < 32; ++start)
        for (unsigned int width = 0; width <= 32; ++width) {
            const unsigned int end = start + width;
            g_BitWriteMasks[start][width] = (1u << start) - 1;
            if (end < 32) g_BitWriteMasks[start][width] |= ~((1u << end) - 1);
        }
    for (int i = 0; i < 32; ++i) { g_ExtraMasks[i] = (1u << i) - 1; g_LittleBits[i] = 1u << i; }
    g_ExtraMasks[32] = ~0u;
}
static std::vector<CNetworkStringTable> Restore(const CDemoRecorder &recorder, const std::vector<CNetworkStringTable> &source, bool stale) {
    auto restored = source;
    for (auto &table : restored) {
        if (!stale) table.values.clear();
        else for (auto &entry : table.values) entry.data.assign(table.fixedBytes ? table.fixedBytes : 3, 0xff);
    }
    for (const auto &packet : recorder.packets) {
        bf_read input(packet.words.data(), static_cast<int>(packet.words.size() * 4), packet.bits);
        Require(input.ReadUBitLong(NETMSG_TYPE_BITS) == 13, "non-v34 message");
        SVC_UpdateStringTable message;
        Require(message.ReadFromBuffer(input), "message read");
        Require(message.m_nLength <= 65535 && message.m_nChangedEntries > 0, "wire bounds");
        Require(message.m_nTableID >= 0 && message.m_nTableID < static_cast<int>(restored.size()), "table id");
        restored.at(message.m_nTableID).ParseUpdate(message.m_DataIn, message.m_nChangedEntries);
        Require(!message.m_DataIn.IsOverflowed(), "table read overflow");
        Require(message.m_DataIn.GetNumBitsLeft() == 0 && input.GetNumBitsLeft() == 0, "payload consumption");
    }
    return restored;
}
static CDemoRecorder RoundTrip(std::vector<CNetworkStringTable> &tables) {
    container.tables.clear();
    for (auto &table : tables) container.tables.push_back(&table);
    warnings = 0;
    CDemoRecorder recorder; recorder.RecordStringTables();
    Require(recorder.m_bRecording && warnings == 0, "valid snapshot rejected");
    for (bool stale : {false, true}) {
        auto restored = Restore(recorder, tables, stale);
        for (size_t i = 0; i < tables.size(); ++i) Require(restored[i].values == tables[i].values, "snapshot mismatch");
    }
    return recorder;
}
static uint32 Read32(std::istream &stream) {
    unsigned char b[4]; stream.read(reinterpret_cast<char *>(b), 4);
    Require(static_cast<bool>(stream), "fixture integer");
    return uint32(b[0]) | uint32(b[1]) << 8 | uint32(b[2]) << 16 | uint32(b[3]) << 24;
}
static void Put32(std::ostream &stream, uint32 value) {
    unsigned char b[4] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
    stream.write(reinterpret_cast<const char *>(b), 4);
}
static std::vector<unsigned char> ReadBytes(std::istream &stream) {
    const uint32 length = Read32(stream); Require(length <= 65536, "fixture length");
    std::vector<unsigned char> bytes(length); stream.read(reinterpret_cast<char *>(bytes.data()), length);
    Require(static_cast<bool>(stream), "fixture bytes"); return bytes;
}
int main(int argc, char **argv) {
    InitializeMasks();
    int cases = 0;
    for (int count : {0, 1, 64, 8192}) {
        std::vector<CNetworkStringTable> tables(1);
        for (int i = 0; i < count; ++i) tables[0].values.push_back({"models/shared/prefix/" + std::to_string(i), std::vector<unsigned char>(i % 7, static_cast<unsigned char>(i))});
        auto recorder = RoundTrip(tables);
        if (count == 8192) Require(recorder.packets.size() > 1, "large table not split");
        ++cases;
    }
    for (int fixedBits : {1, 7, 8, 9, 15}) {
        std::vector<CNetworkStringTable> tables(1); tables[0].fixedBytes = (fixedBits + 7) / 8; tables[0].fixedBits = fixedBits;
        for (int i = 0; i < 256; ++i) {
            std::vector<unsigned char> data(tables[0].fixedBytes, 0x7f);
            data.back() &= static_cast<unsigned char>((1u << ((fixedBits - 1) % 8 + 1)) - 1);
            if (i % 5 == 0) data.clear();
            tables[0].values.push_back({std::to_string(i), data});
        }
        RoundTrip(tables); ++cases;
    }
    {
        std::vector<CNetworkStringTable> tables(32);
        for (int id = 0; id < 32; ++id) {
            tables[id].id = id;
            for (int i = 0; i < 25; ++i) tables[id].values.push_back({std::string(1000, 'a') + std::to_string(i), std::vector<unsigned char>(4095, static_cast<unsigned char>(i))});
        }
        RoundTrip(tables); ++cases;
    }
    for (int invalid = 0; invalid < 6; ++invalid) {
        CNetworkStringTable table; table.values.push_back({"entry", {1}});
        if (invalid == 0) table.id = 32;
        if (invalid == 1) table.values[0].name.assign(1024, 'a');
        if (invalid == 2) table.values[0].data.resize(4096);
        if (invalid == 3) table.nullData = true;
        if (invalid == 4) { table.fixedBytes = 2; table.fixedBits = 16; }
        if (invalid == 5) { table.fixedBytes = 1; table.fixedBits = -1; }
        container.tables = {&table}; warnings = 0; CDemoRecorder recorder; recorder.RecordStringTables();
        Require(!recorder.m_bRecording && warnings > 0 && recorder.packets.empty(), "invalid snapshot not stopped"); ++cases;
    }
    if (argc == 3) {
        std::ifstream input(argv[1], std::ios::binary); const uint32 count = Read32(input); Require(count <= 32, "fixture tables");
        std::vector<CNetworkStringTable> tables(count);
        for (uint32 i = 0; i < count; ++i) {
            auto &table = tables[i]; table.id = static_cast<int>(i); table.maximum = Read32(input); table.fixedBytes = Read32(input); table.fixedBits = Read32(input);
            const uint32 entries = Read32(input); Require(entries <= static_cast<uint32>(table.maximum), "fixture entries");
            for (uint32 j = 0; j < entries; ++j) { auto name = ReadBytes(input); auto data = ReadBytes(input); table.values.push_back({std::string(name.begin(), name.end()), data}); }
        }
        auto recorder = RoundTrip(tables);
        std::ofstream output(argv[2], std::ios::binary); Put32(output, recorder.packets.size());
        for (const auto &packet : recorder.packets) { Put32(output, packet.bits); output.write(reinterpret_cast<const char *>(packet.words.data()), (packet.bits + 7) / 8); }
        Require(static_cast<bool>(output), "packet output"); ++cases;
    }
    std::cout << "PASS demo string table recording cases: " << cases << '\n';
}
