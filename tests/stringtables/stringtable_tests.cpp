#include <algorithm>
#include <array>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

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
#define VPROF(...) ((void)0)
#define PAD_NUMBER(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#define LOG_CLIENT 0
#define IsPC() true
static uint32 LoadLittleDWord(const uint32 *base, unsigned int index) { return base[index]; }
static void StoreLittleDWord(uint32 *base, unsigned int index, uint32 value) { base[index] = value; }

static uint32 BitForBitnum(int bit) { return uint32(1) << bit; }

#include "bitbuf_under_test.h"
#include "bitbuf_under_test.inc"
#include "stringtable_definitions.inc"

uint32 g_ExtraMasks[33];
uint32 g_LittleBits[32];
static int warnings = 0;
void Warning(const char *, ...) { ++warnings; }
static void Log_Warning(int, const char *, ...) { ++warnings; }
static void COM_TimestampedLog(const char *, ...) {}
void Msg(const char *, ...) {}
int Q_strlen(const char *s) { return static_cast<int>(std::strlen(s)); }
static void Q_strncpy(char *dest, const char *source, size_t count)
{
    if (!count) return;
    const size_t length = std::min(count - 1, std::strlen(source));
    std::memcpy(dest, source, length);
    dest[length] = 0;
}
static const int COPY_ALL_CHARACTERS = -1;
static void Q_strncat(char *dest, const char *source, size_t count, int)
{
    const size_t length = std::strlen(dest);
    if (length < count) Q_strncpy(dest + length, source, count - length);
}
template <typename T> static T Min(T a, T b) { return std::min(a, b); }
[[noreturn]] void Host_Error(const char *, ...) { throw std::runtime_error("Host_Error"); }
#define ErrorIfNot(condition, message) do { if (!(condition)) throw std::runtime_error("ErrorIfNot"); } while (false)

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static int Q_log2(int value)
{
    int result = 0;
    while (value >>= 1) ++result;
    return result;
}

template <typename T> struct CUtlVector
{
    std::vector<T> values;
    int Count() const { return static_cast<int>(values.size()); }
    void Remove(int index) { values.erase(values.begin() + index); }
    void AddToTail(const T &value) { values.push_back(value); }
    T &operator[](int index) { return values.at(index); }
};

struct Entry
{
    std::string name;
    std::vector<unsigned char> data;
    bool operator==(const Entry &other) const { return name == other.name && data == other.data; }
};

struct CNetworkStringTable
{
    std::vector<Entry> values;
    int maximum = 64, fixedBytes = 0, fixedBits = 0;
    bool fixed = false;
    int writes = 0;
    const char *GetTableName() const { return "test"; }
    int GetMaxStrings() const { return maximum; }
    int GetEntryBits() const { return Q_log2(maximum); }
    int GetNumStrings() const { return static_cast<int>(values.size()); }
    bool IsUserDataFixedSize() const { return fixed; }
    int GetUserDataSize() const { return fixedBytes; }
    int GetUserDataSizeBits() const { return fixedBits; }
    void SetTick(int) {}
    void SetStringUserData(int index, int bytes, const void *data)
    {
        ++writes;
        Require(bytes >= 0 && bytes <= CNetworkStringTableItem::MAX_USERDATA_SIZE, "storage length");
        const auto *p = static_cast<const unsigned char *>(data);
        if (bytes) values.at(index).data.assign(p, p + bytes);
        else values.at(index).data.clear();
    }
    const char *GetString(int index) { return values.at(index).name.c_str(); }
    int AddString(bool, const char *name, int bytes, const void *data)
    {
        Require(GetNumStrings() < maximum, "table capacity");
        Entry item;
        item.name = name;
        values.push_back(item);
        SetStringUserData(GetNumStrings() - 1, bytes, data);
        return GetNumStrings() - 1;
    }
    void ParseUpdate(bf_read &buf, int entries);
};

static size_t zeroedBytes = 0;
static void *TrackedMemset(void *data, int value, size_t bytes)
{
    zeroedBytes += bytes;
    return std::memset(data, value, bytes);
}
#define memset TrackedMemset
#include "stringtable_under_test.inc"
#undef memset

struct SVC_CreateStringTable
{
    const char *m_szTableName = nullptr;
    int m_nMaxEntries = 0, m_nNumEntries = 0;
    bool m_bUserDataFixedSize = false;
    int m_nUserDataSize = 0, m_nUserDataSizeBits = 0;
    bool m_bIsFilenames = false;
    int m_nLength = 0;
    bf_read m_DataIn;
    bool m_bDataCompressed = false;
    char m_szTableNameBuffer[256] = {};
    bool ReadFromBuffer(bf_read &buffer);
};
struct SVC_UpdateStringTable
{
    int m_nTableID = 0, m_nChangedEntries = 0, m_nLength = 0;
    bf_read m_DataIn;
    bool ReadFromBuffer(bf_read &buffer);
};
#include "messages_under_test.inc"

struct Container
{
    std::vector<std::unique_ptr<CNetworkStringTable>> tables;
    bool creation = false, failCreation = false;
    void AllowCreation(bool allowed) { creation = allowed; }
    CNetworkStringTable *CreateStringTableEx(const char *, int maximum, int bytes, int bits, bool)
    {
        if (failCreation) return nullptr;
        Require(creation, "creation not enabled");
        std::unique_ptr<CNetworkStringTable> table(new CNetworkStringTable);
        table->maximum = maximum;
        table->fixed = bytes != 0;
        table->fixedBytes = bytes;
        table->fixedBits = bits;
        tables.push_back(std::move(table));
        return tables.back().get();
    }
    CNetworkStringTable *GetTable(int index)
    {
        return index >= 0 && index < static_cast<int>(tables.size()) ? tables[index].get() : nullptr;
    }
};
struct CBaseClientState
{
    Container *m_StringTableContainer = nullptr;
    int GetServerTickCount() const { return 10; }
    void HookClientStringTable(const char *) {}
    bool ProcessCreateStringTable(SVC_CreateStringTable *msg);
    bool ProcessUpdateStringTable(SVC_UpdateStringTable *msg);
};
static bool decompressSuccess = true;
static int decompressCalls = 0;
static bool COM_BufferToBufferDecompress(char *output, unsigned int *outputSize,
                                        const char *input, unsigned int inputSize)
{
    ++decompressCalls;
    if (!decompressSuccess || *outputSize != inputSize) return false;
    if (inputSize) std::memcpy(output, input, inputSize);
    return true;
}
#include "client_under_test.inc"

struct Bits
{
    std::vector<uint32> words = std::vector<uint32>(10000, 0);
    int count = 0;
    void Put(uint32 value, int width)
    {
        for (int i = 0; i < width; ++i)
        {
            const uint32 mask = uint32(1) << (count % 32);
            if ((value >> i) & 1u) words.at(count / 32) |= mask;
            else words.at(count / 32) &= ~mask;
            ++count;
        }
    }
    void Text(const std::string &text)
    {
        for (unsigned char ch : text) Put(ch, 8);
        Put(0, 8);
    }
    void Bytes(const std::vector<unsigned char> &bytes)
    {
        for (unsigned char value : bytes) Put(value, 8);
    }
    void Append(const Bits &other)
    {
        for (int i = 0; i < other.count; ++i) Put((other.words[i / 32] >> (i % 32)) & 1u, 1);
    }
    bf_read Reader(int length = -1)
    {
        return bf_read("test", words.data(), static_cast<int>(words.size() * 4), length < 0 ? count : length);
    }
};

static void AddEntry(Bits &bits, const std::string &name, const std::vector<unsigned char> &data,
                     bool fixed = false, int fixedBits = 0, int index = -1, int entryBits = 6)
{
    bits.Put(index < 0 ? 1 : 0, 1);
    if (index >= 0) bits.Put(index, entryBits);
    bits.Put(1, 1);
    bits.Put(0, 1);
    bits.Text(name);
    bits.Put(!data.empty(), 1);
    if (!data.empty())
    {
        if (fixed)
        {
            for (int i = 0; i < fixedBits; ++i) bits.Put((data.at(i / 8) >> (i % 8)) & 1u, 1);
        }
        else
        {
            bits.Put(static_cast<uint32>(data.size()), 12);
            bits.Bytes(data);
        }
    }
}
static void UpdateEntry(Bits &bits, int index, const std::vector<unsigned char> &data)
{
    bits.Put(0, 1); bits.Put(index, 6); bits.Put(0, 1); bits.Put(!data.empty(), 1);
    if (!data.empty()) { bits.Put(static_cast<uint32>(data.size()), 12); bits.Bytes(data); }
}
static Bits CreateMessage(const Bits &payload, int entries, int maximum = 64,
                          int bytes = 0, int fixedBits = 0, const std::string &name = "test")
{
    Bits bits;
    bits.Text(name); bits.Put(maximum, 16); bits.Put(entries, Q_log2(maximum) + 1);
    bits.Put(payload.count, NET_MAX_PAYLOAD_BITS_V23 + 3);
    bits.Put(bytes != 0, 1);
    if (bytes) { bits.Put(bytes, 12); bits.Put(fixedBits, 4); }
    bits.Append(payload);
    return bits;
}
static Bits UpdateMessage(const Bits &payload, int entries, int table = 0)
{
    Bits bits;
    bits.Put(table, Q_log2(MAX_TABLES)); bits.Put(entries != 1, 1);
    if (entries != 1) bits.Put(entries, 16);
    bits.Put(payload.count, 16); bits.Append(payload);
    return bits;
}

static int ValidTables()
{
    int cases = 0;
    for (int size : {0, 1, 2, 127, 4095})
    {
        std::vector<unsigned char> data(size);
        for (int i = 0; i < size; ++i) data[i] = static_cast<unsigned char>(i * 37 + 1);
        Bits bits; AddEntry(bits, "models/player/test.mdl", data);
        CNetworkStringTable table;
        auto reader = bits.Reader(); table.ParseUpdate(reader, 1);
        Require(!reader.IsOverflowed() && table.GetNumStrings() == 1, "valid table rejected");
        Require(table.values[0].name == "models/player/test.mdl" && table.values[0].data == data,
                "valid table changed");
        Require(reader.GetNumBitsRead() == bits.count, "valid table cursor");
        ++cases;
    }
    for (int size : {1, 2, 32, 4096})
        for (int width : {0, 1, 7, 8, 12})
        {
            if (width > size * 8) continue;
            std::vector<unsigned char> source = {0xd5, 0x0b};
            Bits bits; AddEntry(bits, "fixed", source, true, width);
            CNetworkStringTable table;
            table.fixed = true; table.fixedBytes = size; table.fixedBits = width;
            auto reader = bits.Reader(); table.ParseUpdate(reader, 1);
            Require(!reader.IsOverflowed(), "fixed table rejected");
            std::vector<unsigned char> expected(size, 0);
            for (int i = 0; i < width; ++i) expected[i / 8] |= ((source[i / 8] >> (i % 8)) & 1u) << (i % 8);
            Require(table.values.at(0).data == expected, "fixed padding not zero");
            ++cases;
        }
    Bits bits; AddEntry(bits, "first", {1, 2}); AddEntry(bits, "second", {});
    CNetworkStringTable table; auto reader = bits.Reader(); table.ParseUpdate(reader, 2);
    Bits update; UpdateEntry(update, 0, {3, 4, 5});
    auto changed = update.Reader(); table.ParseUpdate(changed, 1);
    Require(!changed.IsOverflowed() && table.values.at(0).data == std::vector<unsigned char>({3, 4, 5}),
            "existing data update");
    return cases + 1;
}

static int Truncation()
{
    int cases = 0;
    for (int mode = 0; mode < 3; ++mode)
    {
        Bits bits;
        if (mode == 0) AddEntry(bits, "models/test.mdl", {1, 2, 3, 4});
        if (mode == 1) AddEntry(bits, "fixed", {0xaa, 0x0b}, true, 12);
        if (mode == 2) UpdateEntry(bits, 0, {1, 2, 3, 4});
        for (int cut = 0; cut < bits.count; ++cut)
        {
            CNetworkStringTable table;
            if (mode == 1) { table.fixed = true; table.fixedBytes = 4; table.fixedBits = 12; }
            if (mode == 2) table.values.push_back(Entry{"old", {9, 8, 7}});
            const auto before = table.values;
            auto reader = bits.Reader(cut); table.ParseUpdate(reader, 1);
            Require(reader.IsOverflowed(), "truncated entry not rejected");
            Require(table.values == before && table.writes == 0, "truncated entry committed");
            ++cases;
        }
    }
    Bits bits; AddEntry(bits, "first", {1}); const int first = bits.count;
    AddEntry(bits, "second", {2, 3});
    for (int cut = first; cut < bits.count; ++cut)
    {
        CNetworkStringTable table; auto reader = bits.Reader(cut); table.ParseUpdate(reader, 2);
        Require(reader.IsOverflowed(), "truncated second entry not rejected");
        Require(table.values.size() == 1 && table.values[0].name == "first" && table.writes == 1,
                "incomplete second entry committed");
        ++cases;
    }
    return cases;
}

static void HistoryEntry(Bits &bits, int history, int length, const std::string &suffix)
{
    bits.Put(1, 1); bits.Put(1, 1); bits.Put(1, 1);
    bits.Put(history, 5); bits.Put(length, SUBSTRING_BITS); bits.Text(suffix); bits.Put(0, 1);
}
static int History()
{
    Bits bits; AddEntry(bits, "models/test_a", {}); HistoryEntry(bits, 0, 12, "b");
    CNetworkStringTable table; auto reader = bits.Reader(); table.ParseUpdate(reader, 2);
    Require(!reader.IsOverflowed() && table.values.at(1).name == "models/test_b", "valid history");
    int cases = 1;
    for (int mode = 0; mode < 4; ++mode)
    {
        Bits bad; AddEntry(bad, "abc", {});
        HistoryEntry(bad, mode == 0 ? 1 : 0, mode == 1 ? 4 : 3,
                     mode == 2 ? std::string(1021, 'x') : mode == 3 ? std::string(1024, 'x') : "x");
        CNetworkStringTable t; auto r = bad.Reader(); t.ParseUpdate(r, 2);
        Require(r.IsOverflowed() && t.values.size() == 1, "bad history committed");
        ++cases;
    }
    Bits rolling; std::vector<std::string> history;
    AddEntry(rolling, "item000", {}); history.push_back("item000");
    std::vector<std::string> expected = history;
    for (int i = 1; i < 60; ++i)
    {
        HistoryEntry(rolling, 0, 4, std::to_string(i));
        expected.push_back(history[0].substr(0, 4) + std::to_string(i));
        if (history.size() > 31) history.erase(history.begin());
        history.push_back(expected.back());
    }
    CNetworkStringTable t; auto r = rolling.Reader(); t.ParseUpdate(r, 60);
    Require(!r.IsOverflowed() && t.values.size() == expected.size(), "rolling history");
    for (size_t i = 0; i < expected.size(); ++i) Require(t.values[i].name == expected[i], "rolling history name");
    return cases + 1;
}

static int InvalidEntries()
{
    int cases = 0;
    for (int mode = 0; mode < 5; ++mode)
    {
        Bits bits;
        if (mode == 0) AddEntry(bits, "gap", {}, false, 0, 4);
        if (mode == 1) { bits.Put(1, 1); bits.Put(0, 1); bits.Put(0, 1); }
        if (mode >= 2) AddEntry(bits, "fixed", {1, 2}, true, 12);
        CNetworkStringTable table;
        if (mode >= 2)
        {
            table.fixed = true;
            table.fixedBytes = mode == 2 ? 0 : mode == 3 ? 1 : 4097;
            table.fixedBits = 12;
        }
        auto reader = bits.Reader(); table.ParseUpdate(reader, 1);
        Require(reader.IsOverflowed() && table.values.empty() && table.writes == 0, "bad entry accepted");
        ++cases;
    }
    Bits bits; CNetworkStringTable table; auto reader = bits.Reader(); table.ParseUpdate(reader, -1);
    Require(reader.IsOverflowed(), "negative entry count");
    Bits full; AddEntry(full, "one", {}); AddEntry(full, "two", {}); AddEntry(full, "three", {});
    CNetworkStringTable capacity; capacity.maximum = 2;
    auto limited = full.Reader(); capacity.ParseUpdate(limited, 3);
    Require(limited.IsOverflowed() && capacity.values.size() == 2, "table overfill accepted");
    return cases + 2;
}

static int MessageBounds()
{
    int cases = 0;
    Bits payload; AddEntry(payload, "test", {1, 2, 3});
    for (int mode = 0; mode < 2; ++mode)
        for (int offset = 0; offset < 32; ++offset)
        {
            Bits message = mode ? UpdateMessage(payload, 1) : CreateMessage(payload, 1);
            Bits envelope; envelope.Put(0, offset); envelope.Append(message);
            const int end = envelope.count; envelope.Put(0xfedcba98u, 32);
            auto reader = envelope.Reader(); Require(reader.Seek(offset), "envelope seek");
            bf_read data;
            if (mode)
            {
                SVC_UpdateStringTable msg; Require(msg.ReadFromBuffer(reader), "valid update header"); data = msg.m_DataIn;
            }
            else
            {
                SVC_CreateStringTable msg; Require(msg.ReadFromBuffer(reader), "valid create header"); data = msg.m_DataIn;
            }
            Require(reader.GetNumBitsRead() == end && data.GetNumBitsLeft() == payload.count,
                    "payload reader extends into following message");
            Require(data.Seek(end), "payload end seek");
            Require(data.ReadOneBit() == 0 && data.IsOverflowed(), "read beyond message accepted");
            Require(reader.ReadUBitLong(32) == 0xfedcba98u, "following message changed");
            ++cases;
        }
    for (int mode = 0; mode < 2; ++mode)
    {
        Bits message = mode ? UpdateMessage(payload, 1) : CreateMessage(payload, 1);
        const int header = message.count - payload.count;
        for (int cut = 0; cut < message.count; ++cut)
        {
            auto reader = message.Reader(cut);
            if (mode) { SVC_UpdateStringTable msg; Require(!msg.ReadFromBuffer(reader), "truncated update accepted"); }
            else { SVC_CreateStringTable msg; Require(!msg.ReadFromBuffer(reader), "truncated create accepted"); }
            Require(reader.GetNumBitsRead() <= cut || cut < header, "header cursor");
            ++cases;
        }
    }
    return cases;
}

static int InvalidHeaders()
{
    int cases = 0;
    Bits payload; AddEntry(payload, "test", {});
    for (int maximum : {0, 3, 7, 65535})
    {
        Bits bits = CreateMessage(payload, 1, maximum);
        auto reader = bits.Reader(); SVC_CreateStringTable msg;
        Require(!msg.ReadFromBuffer(reader), "invalid table capacity accepted");
        ++cases;
    }
    for (int mode = 0; mode < 3; ++mode)
    {
        Bits bits = CreateMessage(payload, mode == 0 ? 65 : 1, 64, mode == 1 ? 1 : 0,
                                  mode == 1 ? 12 : 0, mode == 2 ? std::string(256, 'x') : "test");
        auto reader = bits.Reader(); SVC_CreateStringTable msg;
        Require(!msg.ReadFromBuffer(reader), "invalid create header accepted");
        ++cases;
    }
    Bits zero;
    zero.Text("fixed"); zero.Put(64, 16); zero.Put(0, 7);
    zero.Put(0, NET_MAX_PAYLOAD_BITS_V23 + 3); zero.Put(1, 1); zero.Put(0, 12); zero.Put(0, 4);
    auto empty = zero.Reader(); SVC_CreateStringTable invalid;
    Require(!invalid.ReadFromBuffer(empty), "zero fixed-data size accepted");
    return cases + 1;
}

static int ClientHandlers()
{
    int cases = 0;
    for (int malformed = 0; malformed < 2; ++malformed)
    {
        Bits payload; AddEntry(payload, "valid", {1, 2, 3});
        if (malformed) --payload.count;
        Bits bits = CreateMessage(payload, 1); auto reader = bits.Reader();
        SVC_CreateStringTable msg; Require(msg.ReadFromBuffer(reader), "handler create header");
        Container container; CBaseClientState client; client.m_StringTableContainer = &container;
        Require(client.ProcessCreateStringTable(&msg) == !malformed, "create overflow not propagated");
        Require(!container.creation, "creation flag not cleared");
        Require(container.tables.at(0)->values.size() == static_cast<size_t>(!malformed), "bad creation data committed");
        Bits update; UpdateEntry(update, 0, {9, 8, 7});
        if (malformed) --update.count;
        Bits updated = UpdateMessage(update, 1); auto changed = updated.Reader();
        SVC_UpdateStringTable updateMsg; Require(updateMsg.ReadFromBuffer(changed), "handler update header");
        container.tables[0]->values = {Entry{"valid", {1, 2, 3}}};
        const auto before = container.tables[0]->values;
        Require(client.ProcessUpdateStringTable(&updateMsg) == !malformed, "update overflow not propagated");
        if (malformed) Require(container.tables[0]->values == before, "bad update committed");
        cases += 2;
    }
    Bits empty; Bits bits = UpdateMessage(empty, 0, 31); auto reader = bits.Reader();
    SVC_UpdateStringTable msg; Require(msg.ReadFromBuffer(reader), "missing table header");
    Container container; CBaseClientState client; client.m_StringTableContainer = &container;
    Require(!client.ProcessUpdateStringTable(&msg), "missing table not rejected");
    return cases + 1;
}

static int Limits()
{
    int cases = 0;
    for (int maximum : {1, 2, 64, 32768})
        for (int mode = 0; mode < 2; ++mode)
        {
            Bits payload; AddEntry(payload, std::string(1023, 'x'), {}, false, 0, 0, Q_log2(maximum));
            Bits bits = CreateMessage(payload, 1, maximum, 0, 0, mode ? ":files" : std::string(255, 'n'));
            auto reader = bits.Reader(); SVC_CreateStringTable msg;
            Require(msg.ReadFromBuffer(reader), "valid header limit rejected");
            Require(msg.m_nMaxEntries == maximum && msg.m_nNumEntries == 1 && msg.m_bIsFilenames == (mode != 0),
                    "valid header fields");
            if (mode) Require(std::string(msg.m_szTableName) == "files", "filename table prefix");
            CNetworkStringTable table; table.maximum = maximum;
            table.ParseUpdate(msg.m_DataIn, 1);
            Require(!msg.m_DataIn.IsOverflowed() && table.values.at(0).name.size() == 1023, "maximum string");
            ++cases;
        }
    Bits payload; AddEntry(payload, std::string(1024, 'x'), {});
    CNetworkStringTable table; auto reader = payload.Reader(); table.ParseUpdate(reader, 1);
    Require(reader.IsOverflowed() && table.values.empty(), "oversized string silently truncated");
    return cases + 1;
}

static int ZeroingWork()
{
    Bits bits; AddEntry(bits, "variable", {1, 2, 3}); AddEntry(bits, "empty", {});
    zeroedBytes = 0;
    CNetworkStringTable table; auto reader = bits.Reader(); table.ParseUpdate(reader, 2);
    Require(!reader.IsOverflowed() && zeroedBytes == 0, "unnecessary variable-data zeroing");
    Bits fixed; AddEntry(fixed, "fixed", {0xaa, 0x0b}, true, 12);
    CNetworkStringTable t; t.fixed = true; t.fixedBytes = 4; t.fixedBits = 12;
    zeroedBytes = 0;
    auto r = fixed.Reader(); t.ParseUpdate(r, 1);
    Require(!r.IsOverflowed() && zeroedBytes == 4, "fixed-data zeroing exceeds stored bytes");
    Require(t.values.at(0).data == std::vector<unsigned char>({0xaa, 0x0b, 0, 0}), "fixed padding changed");
    return 2;
}

static int MessageIsolation()
{
    int cases = 0;
    for (int mode = 0; mode < 2; ++mode)
    {
        Bits payload; AddEntry(payload, "broken", {1, 2, 3}); payload.count -= 8;
        Bits bits = mode ? UpdateMessage(payload, 1) : CreateMessage(payload, 1);
        const int boundary = bits.count;
        bits.Put(0xabcdef00u, 32);
        auto reader = bits.Reader();
        Container container; CBaseClientState client; client.m_StringTableContainer = &container;
        if (mode)
        {
            container.tables.emplace_back(new CNetworkStringTable);
            container.tables[0]->values = {Entry{"existing", {9}}};
            SVC_UpdateStringTable msg; Require(msg.ReadFromBuffer(reader), "isolation update header");
            Require(!client.ProcessUpdateStringTable(&msg), "update borrowed following message bits");
            Require(container.tables[0]->values == std::vector<Entry>({Entry{"existing", {9}}}),
                    "borrowed bytes committed in update");
        }
        else
        {
            SVC_CreateStringTable msg; Require(msg.ReadFromBuffer(reader), "isolation create header");
            Require(!client.ProcessCreateStringTable(&msg), "creation borrowed following message bits");
            Require(container.tables[0]->values.empty(), "borrowed bytes committed in creation");
        }
        Require(reader.GetNumBitsRead() == boundary && reader.ReadUBitLong(32) == 0xabcdef00u,
                "following message reader changed");
        ++cases;
    }
    return cases;
}

static int CompressedHandlers()
{
    for (int mode = 0; mode < 6; ++mode)
    {
        Bits payload; AddEntry(payload, "valid", mode == 2 ? std::vector<unsigned char>({1, 2, 3, 4})
                                                         : std::vector<unsigned char>({1, 2, 3}));
        if (mode == 2) payload.count -= 8;
        Require((payload.count & 7) == 0, "compressed fixture alignment");
        const int bytes = payload.count / 8;
        Bits packed;
        packed.Put(bytes + (mode == 3), 32);
        if (mode != 5)
        {
            packed.Put(bytes + (mode == 4), 32);
            const auto *data = reinterpret_cast<const unsigned char *>(payload.words.data());
            packed.Bytes(std::vector<unsigned char>(data, data + bytes));
        }
        SVC_CreateStringTable msg;
        msg.m_szTableName = "test"; msg.m_nMaxEntries = 64; msg.m_nNumEntries = 1;
        msg.m_nLength = packed.count; msg.m_bDataCompressed = true; msg.m_DataIn = packed.Reader();
        Container container; CBaseClientState client; client.m_StringTableContainer = &container;
        decompressSuccess = mode != 1; decompressCalls = 0;
        Require(client.ProcessCreateStringTable(&msg) == (mode == 0), "compressed failure not propagated");
        Require(!container.creation, "compressed creation flag");
        Require(container.tables.at(0)->values.size() == static_cast<size_t>(mode == 0),
                "bad compressed entry committed");
        if (mode >= 4) Require(decompressCalls == 0, "decompressed beyond declared payload");
    }
    decompressSuccess = true;
    return 6;
}

static int MissingStorage()
{
    Bits payload; AddEntry(payload, "valid", {});
    Bits bits = CreateMessage(payload, 1); auto reader = bits.Reader();
    SVC_CreateStringTable msg; Require(msg.ReadFromBuffer(reader), "missing storage header");
    CBaseClientState client;
    Require(!client.ProcessCreateStringTable(&msg), "missing creation container");
    Container container; container.failCreation = true; client.m_StringTableContainer = &container;
    Require(!client.ProcessCreateStringTable(&msg) && !container.creation, "missing created table");
    return 2;
}

static int RandomTables()
{
    std::mt19937 random(0x34abcdu);
    for (int iteration = 0; iteration < 2000; ++iteration)
    {
        Bits bits; std::vector<Entry> expected;
        const int entries = 1 + random() % 50;
        for (int i = 0; i < entries; ++i)
        {
            Entry item; item.name = "item_" + std::to_string(iteration) + "_" + std::to_string(i);
            item.data.resize(random() % 80);
            for (auto &value : item.data) value = random();
            AddEntry(bits, item.name, item.data); expected.push_back(item);
        }
        CNetworkStringTable table; auto reader = bits.Reader(); table.ParseUpdate(reader, entries);
        Require(!reader.IsOverflowed() && table.values == expected, "random valid table mismatch");
    }
    return 2000;
}

int main(int argc, char **argv)
{
    static_assert(sizeof(uint32) == 4 && sizeof(uintp) == 8, "64-bit harness required");
    for (int i = 0; i < 32; ++i) { g_ExtraMasks[i] = (uint32(1) << i) - 1; g_LittleBits[i] = uint32(1) << i; }
    g_ExtraMasks[32] = 0xffffffffu;
    struct Suite { const char *name; int (*run)(); };
    const Suite suites[] = {{"valid", ValidTables}, {"truncation", Truncation}, {"history", History},
                             {"invalid-entries", InvalidEntries}, {"message-bounds", MessageBounds},
                             {"invalid-headers", InvalidHeaders}, {"client-handlers", ClientHandlers},
                             {"limits", Limits}, {"zeroing", ZeroingWork}, {"message-isolation", MessageIsolation},
                             {"compressed-handlers", CompressedHandlers}, {"missing-storage", MissingStorage},
                             {"random-tables", RandomTables}};
    const std::string selection = argc > 1 ? argv[1] : "all";
    int count = 0, cases = 0;
    try
    {
        for (const auto &suite : suites)
            if (selection == "all" || selection == suite.name)
            {
                cases += suite.run(); ++count;
                std::cout << "PASS " << suite.name << std::endl;
            }
        Require(count > 0, "unknown suite");
    }
    catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << std::endl; return 1; }
    std::cout << "Passed " << count << " suites, " << cases << " cases\n";
    return 0;
}
