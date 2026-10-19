#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
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
static uint32 LoadLittleDWord(const uint32 *base, unsigned int index) { return base[index]; }
static void StoreLittleDWord(uint32 *base, unsigned int index, uint32 value) { base[index] = value; }

#include "bitbuf_under_test.h"

#ifdef _DEBUG
void InternalBitBufErrorHandler(BitBufErrorType, const char *) {}
#endif

#include "bitbuf_under_test.inc"

uint32 g_ExtraMasks[33];
uint32 g_LittleBits[32];

static void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static int ReferenceBit(const unsigned char *data, int bit)
{
    return (data[bit / 8] >> (bit % 8)) & 1;
}

static void ReferenceWrite(unsigned char *data, int bit, uint32 value, int width)
{
    for (int i = 0; i < width; ++i)
    {
        const unsigned char mask = static_cast<unsigned char>(1u << ((bit + i) % 8));
        if ((value >> i) & 1u)
            data[(bit + i) / 8] |= mask;
        else
            data[(bit + i) / 8] &= static_cast<unsigned char>(~mask);
    }
}

static uint32 ReferenceRead(const unsigned char *data, int bit, int width)
{
    uint32 result = 0;
    for (int i = 0; i < width; ++i)
        result |= static_cast<uint32>(ReferenceBit(data, bit + i)) << i;
    return result;
}

static int ZeroWidth()
{
    bf_write writer(nullptr, 0);
    bf_read reader("test", nullptr, 0);
    writer.WriteUBitLong(0, 0);
    Require(reader.ReadUBitLong(0) == 0, "zero width result");
    Require(writer.WriteBitsFromBuffer(&reader, 0), "empty buffer transfer");
    Require(!writer.IsOverflowed() && !reader.IsOverflowed(), "zero width overflow");
    Require(writer.GetNumBitsWritten() == 0 && reader.GetNumBitsRead() == 0, "zero width cursor");
    std::array<uint32, 1> word = {{0x12345678u}};
    bf_write endWriter(word.data(), 4);
    bf_read endReader("test", word.data(), 4);
    endWriter.SeekToBit(32);
    Require(endReader.Seek(32), "seek end");
    endWriter.WriteUBitLong(0xffffffffu, 0);
    Require(endReader.ReadUBitLong(0) == 0, "end zero read");
    Require(endWriter.WriteBitsFromBuffer(&endReader, 0), "end zero transfer");
    Require(word[0] == 0x12345678u, "zero width modified memory");
    Require(endWriter.GetNumBitsWritten() == 32 && endReader.GetNumBitsRead() == 32,
            "end zero cursor");
    return 2;
}

static int AlignedReads()
{
    std::array<uint32, 2> data = {{0x938abcdeu, 0x47f30125u}};
    for (int offset : {0, 32})
        for (int width = 1; width <= 32; ++width)
        {
            bf_read reader("test", data.data(), static_cast<int>(sizeof(data)), offset + width);
            Require(reader.Seek(offset), "aligned read seek");
            Require(reader.ReadUBitLong(width) == ReferenceRead(
                        reinterpret_cast<const unsigned char *>(data.data()), offset, width),
                    "aligned read result");
            Require(!reader.IsOverflowed(), "aligned read overflow");
        }
    return 64;
}

static int Fields()
{
    std::mt19937 random(0x34b17u);
    int cases = 0;
    for (int offset = 0; offset < 96; ++offset)
        for (int width = 1; width <= 32; ++width)
            for (int variant = 0; variant < 8; ++variant)
            {
                std::array<uint32, 6> data, expected;
                for (auto &word : data)
                    word = random();
                expected = data;
                const uint32 value = variant == 0 ? 0u : variant == 1 ? 0xffffffffu : random();
                ReferenceWrite(reinterpret_cast<unsigned char *>(expected.data()), offset, value, width);
                bf_write writer(data.data(), static_cast<int>(sizeof(data)), offset + width);
                writer.SeekToBit(offset);
                writer.WriteUBitLong(value, width, false);
                Require(!writer.IsOverflowed(), "valid field write overflow");
                Require(data == expected, "wire bits or adjacent bits changed");
                bf_read reader("test", data.data(), static_cast<int>(sizeof(data)), offset + width);
                Require(reader.Seek(offset), "field seek");
                const uint32 reference = ReferenceRead(reinterpret_cast<const unsigned char *>(data.data()),
                                                       offset, width);
                Require(reader.ReadUBitLong(width) == reference, "field read differs from reference");
                Require(!reader.IsOverflowed(), "valid field read overflow");
                Require(writer.GetNumBitsWritten() == offset + width &&
                        reader.GetNumBitsRead() == offset + width, "field cursor");
                ++cases;
            }
    return cases;
}

static int InvalidWidths()
{
    const int widths[] = {-1, -32, INT_MIN, 33, 64, INT_MAX};
    int cases = 0;
    for (int width : widths)
    {
        std::array<uint32, 2> data = {{0xaabbccddu, 0x12345678u}};
        const auto expected = data;
        bf_write writer(data.data(), static_cast<int>(sizeof(data)));
        bf_read reader("test", data.data(), static_cast<int>(sizeof(data)));
        writer.WriteUBitLong(7, width, false);
        Require(reader.ReadUBitLong(width) == 0, "invalid width result");
        Require(writer.IsOverflowed() && reader.IsOverflowed(), "invalid width not rejected");
        Require(data == expected, "invalid width modified memory");
        ++cases;
    }
    return cases;
}

static int TruncatedFields()
{
    int cases = 0;
    for (int offset = 0; offset < 32; ++offset)
        for (int width = 1; width <= 32; ++width)
        {
            std::array<uint32, 2> data = {{0xaabbccddu, 0x12345678u}};
            const auto expected = data;
            const int end = offset + width - 1;
            bf_write writer(data.data(), static_cast<int>(sizeof(data)), end);
            bf_read reader("test", data.data(), static_cast<int>(sizeof(data)), end);
            writer.SeekToBit(offset);
            Require(reader.Seek(offset), "truncated seek");
            writer.WriteUBitLong(0xffffffffu, width, false);
            Require(reader.ReadUBitLong(width) == 0, "truncated read result");
            Require(writer.IsOverflowed() && reader.IsOverflowed(), "truncated field not rejected");
            Require(data == expected, "truncated write changed memory");
            Require(writer.GetNumBitsWritten() == end && reader.GetNumBitsRead() == end,
                    "truncated cursor");
            ++cases;
        }
    return cases;
}

static int ReferenceVar(unsigned char *out, int offset, uint32 value)
{
    uint64 base = 0;
    int bits = 0;
    while (static_cast<uint64>(value) > base * 2)
    {
        ++bits;
        base = (uint64(1) << bits) - 1;
    }
    ReferenceWrite(out, offset, 0, bits);
    ReferenceWrite(out, offset + bits, 1, 1);
    ReferenceWrite(out, offset + bits + 1, static_cast<uint32>(value - base), bits);
    return bits * 2 + 1;
}

static int VarWire()
{
    std::vector<uint32> values = {0, 1, 2, 3, 6, 7, 14, 15, 0x7ffffffeu,
                                  0x7fffffffu, 0xfffffffeu, 0xffffffffu};
    for (int bit = 1; bit <= 31; ++bit)
    {
        const uint32 boundary = (1u << bit) - 1;
        values.push_back(boundary - 1);
        values.push_back(boundary);
        values.push_back(boundary + 1);
    }
    std::mt19937 random(0x34c0deu);
    for (int i = 0; i < 3000; ++i)
        values.push_back(random());
    int cases = 0;
    for (uint32 value : values)
        for (int offset = 0; offset < 32; ++offset)
        {
            std::array<uint32, 4> data, expected;
            data.fill(0xa5a5a5a5u);
            expected = data;
            const int bits = ReferenceVar(reinterpret_cast<unsigned char *>(expected.data()), offset, value);
            bf_write writer(data.data(), static_cast<int>(sizeof(data)), offset + bits);
            writer.SeekToBit(offset);
            writer.WriteUBitVar(value);
            Require(!writer.IsOverflowed(), "var write overflow");
            Require(data == expected, "var wire encoding differs from reference");
            Require(writer.GetNumBitsWritten() == offset + bits, "var write cursor");
            bf_read reader("test", expected.data(), static_cast<int>(sizeof(expected)), offset + bits);
            Require(reader.Seek(offset), "var seek");
            Require(reader.ReadUBitVar() == value, "var read differs from reference");
            Require(!reader.IsOverflowed(), "var read overflow");
            Require(reader.GetNumBitsRead() == offset + bits, "var read cursor");
            ++cases;
        }
    return cases;
}

static int Unterminated()
{
    std::array<uint32, 4> data = {{0, 0, 0, 0}};
    int cases = 0;
    for (int bits = 0; bits <= 128; ++bits)
    {
        bf_read reader("test", data.data(), static_cast<int>(sizeof(data)), bits);
        Require(reader.ReadUBitVar() == 0, "unterminated var result");
        Require(reader.IsOverflowed(), "unterminated var not rejected");
        Require(reader.GetNumBitsRead() <= bits, "unterminated cursor");
        ++cases;
    }
    return cases;
}

static int TruncatedVars()
{
    const uint32 values[] = {1, 2, 3, 7, 15, 1024, 0x7fffffffu, 0xfffffffeu, 0xffffffffu};
    int cases = 0;
    for (uint32 value : values)
    {
        std::array<uint32, 4> data = {{0, 0, 0, 0}};
        const int bits = ReferenceVar(reinterpret_cast<unsigned char *>(data.data()), 0, value);
        for (int end = 0; end < bits; ++end)
        {
            bf_read reader("test", data.data(), static_cast<int>(sizeof(data)), end);
            Require(reader.ReadUBitVar() == 0, "truncated var result");
            Require(reader.IsOverflowed(), "truncated var not rejected");
            ++cases;
        }
    }
    std::array<uint32, 4> bad = {{0, 3, 0, 0}};
    bf_read reader("test", bad.data(), static_cast<int>(sizeof(bad)), 65);
    Require(reader.ReadUBitVar() == 0 && reader.IsOverflowed(), "out-of-range var not rejected");
    return cases + 1;
}

static int CursorBounds()
{
    const int positions[] = {-1, INT_MIN, 65, INT_MAX};
    for (int position : positions)
    {
        std::array<uint32, 2> data = {{0x12345678u, 0xaabbccddu}};
        const auto expected = data;
        bf_write writer(data.data(), static_cast<int>(sizeof(data)));
        bf_read reader("test", data.data(), static_cast<int>(sizeof(data)));
        writer.SeekToBit(position);
        reader.m_iCurBit = position;
        writer.WriteUBitLong(1, 1, false);
        Require(reader.ReadUBitLong(1) == 0, "invalid cursor result");
        Require(writer.IsOverflowed() && reader.IsOverflowed(), "invalid cursor not rejected");
        Require(data == expected, "invalid cursor modified memory");
    }
    bf_write writer;
    bf_read reader;
    writer.WriteUBitLong(1, 1, false);
    Require(reader.ReadUBitLong(1) == 0, "uninitialized buffer result");
    Require(writer.IsOverflowed() && reader.IsOverflowed(), "uninitialized buffer not rejected");
    return 5;
}

static int WordEdges()
{
    int cases = 0;
    for (int width = 1; width <= 32; ++width)
    {
        std::array<uint32, 1> data = {{0xa5a5a5a5u}};
        auto expected = data;
        ReferenceWrite(reinterpret_cast<unsigned char *>(expected.data()), 32 - width, 0x12345678u, width);
        bf_write writer(data.data(), 4);
        writer.SeekToBit(32 - width);
        writer.WriteUBitLong(0x12345678u, width, false);
        Require(data == expected && !writer.IsOverflowed(), "last word write");
        bf_read reader("test", data.data(), 4);
        Require(reader.Seek(32 - width), "last word seek");
        Require(reader.ReadUBitLong(width) == ReferenceRead(reinterpret_cast<const unsigned char *>(data.data()),
                                                           32 - width, width), "last word read");
        Require(!reader.IsOverflowed(), "last word read overflow");
        ++cases;
    }
    return cases;
}

static int RandomStreams()
{
    std::mt19937 random(0x34bad01u);
    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        std::array<uint32, 4> data;
        for (auto &word : data)
            word = random();
        const int begin = random() % 32;
        const int end = begin + random() % 97;
        const auto *bytes = reinterpret_cast<const unsigned char *>(data.data());
        int cursor = begin, prefix = 0;
        bool valid = false;
        uint64 expected = 0;
        while (cursor < end && prefix <= 32)
        {
            if (ReferenceBit(bytes, cursor++))
            {
                if (end - cursor >= prefix)
                {
                    expected = ((uint64(1) << prefix) - 1) + ReferenceRead(bytes, cursor, prefix);
                    cursor += prefix;
                    valid = expected <= 0xffffffffu;
                }
                break;
            }
            ++prefix;
        }
        bf_read reader("test", data.data(), static_cast<int>(sizeof(data)), end);
        Require(reader.Seek(begin), "random stream seek");
        const uint32 value = reader.ReadUBitVar();
        Require(value == (valid ? static_cast<uint32>(expected) : 0), "random var result");
        Require(reader.IsOverflowed() == !valid, "random var overflow state");
        Require(reader.GetNumBitsRead() <= end, "random var cursor exceeded end");
        if (valid)
            Require(reader.GetNumBitsRead() == cursor, "random var consumed bits");
    }
    std::array<uint32, 1> data = {{1}};
    bf_read reader("test", data.data(), 4);
    reader.SetOverflowFlag();
    Require(reader.ReadUBitVar() == 0 && reader.GetNumBitsRead() == 0,
            "var read continued after overflow");
    return 20001;
}

int main(int argc, char **argv)
{
    static_assert(sizeof(uint32) == 4 && sizeof(unsigned int) == 4 && sizeof(uintp) == 8, "64-bit test required");
    for (int i = 0; i < 32; ++i)
    {
        g_ExtraMasks[i] = (uint32(1) << i) - 1;
        g_LittleBits[i] = uint32(1) << i;
    }
    g_ExtraMasks[32] = 0xffffffffu;
    const std::string selection = argc > 1 ? argv[1] : "all";
    struct Suite { const char *name; int (*run)(); };
    const Suite suites[] = {{"zero-width", ZeroWidth}, {"read-aligned", AlignedReads}, {"fields", Fields},
                             {"invalid-widths", InvalidWidths}, {"cursor-bounds", CursorBounds},
                             {"word-edges", WordEdges}, {"truncated-fields", TruncatedFields},
                             {"var-wire", VarWire}, {"unterminated", Unterminated},
                             {"truncated-vars", TruncatedVars}, {"random-streams", RandomStreams}};
    int count = 0, cases = 0;
    try
    {
        for (const auto &suite : suites)
            if (selection == "all" || selection == suite.name)
            {
                cases += suite.run();
                ++count;
                std::cout << "PASS " << suite.name << std::endl;
            }
        Require(count > 0, "unknown test suite");
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Passed " << count << " suites, " << cases << " cases\n";
    return 0;
}
