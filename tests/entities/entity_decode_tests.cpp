#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
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
#define IsPC() true
static uint32 LoadLittleDWord(const uint32 *base, unsigned int index) { return base[index]; }
static void StoreLittleDWord(uint32 *base, unsigned int index, uint32 value) { base[index] = value; }
#include "bitbuf_under_test.h"
#ifdef _DEBUG
void InternalBitBufErrorHandler(BitBufErrorType, const char *) {}
#endif
#include "definitions_under_test.inc"
#include "bitbuf_under_test.inc"
uint32 g_ExtraMasks[33];
uint32 g_LittleBits[32];
class RecvProp;
class SendProp;
struct DVariant
{
    union { int m_Int; float m_Float; float m_Vector[3]; const char *m_pString; };
};
#include "classes_under_test.inc"
using RecvVarProxyFn = void (*)(const CRecvProxyData *, void *, void *);
using ArrayLengthRecvProxyFn = void (*)(void *, int, int);
struct SendProp
{
    int m_nBits = 8, flags = SPROP_UNSIGNED, elements = 3;
    float m_fLowValue = -1, m_fHighValue = 1;
    SendPropType type = DPT_Int;
    SendProp *child = nullptr;
    int GetFlags() const { return flags; }
    SendPropType GetType() const { return type; }
    SendProp *GetArrayProp() const { return child; }
    int GetNumElements() const { return elements; }
    int GetNumArrayLengthBits() const
    {
        int bits = 1, value = elements;
        while (value >>= 1) ++bits;
        return bits;
    }
};
struct RecvProp
{
    int elements = 3, stride = sizeof(int), offset = 0;
    RecvProp *child = nullptr;
    RecvVarProxyFn proxy = nullptr;
    ArrayLengthRecvProxyFn length = nullptr;
    RecvProp *GetArrayProp() const { return child; }
    RecvVarProxyFn GetProxyFn() const { return proxy; }
    ArrayLengthRecvProxyFn GetArrayLengthProxy() const { return length; }
    int GetNumElements() const { return elements; }
    int GetElementStride() const { return stride; }
    int GetOffset() const { return offset; }
    const char *GetName() const { return "test"; }
};
struct PropTypeFns
{
    void (*Decode)(DecodeInfo *) = nullptr;
    int (*CompareDeltas)(const SendProp *, bf_read *, bf_read *) = nullptr;
    bool (*IsEncodedZero)(const SendProp *, bf_read *) = nullptr;
    void (*SkipProp)(const SendProp *, bf_read *) = nullptr;
};
PropTypeFns g_PropTypeFns[DPT_NUMSendPropTypes];
void Warning(const char *, ...) {}
using std::min;
using std::max;
static void SkipPropData(bf_read *reader, const SendProp *prop)
{
    g_PropTypeFns[prop->GetType()].SkipProp(prop, reader);
}
#include "decode_under_test.inc"
#include "delta_under_test.inc"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
struct Bits
{
    std::vector<uint32> words = std::vector<uint32>(2048, 0);
    int count = 0;
    void Put(uint32 value, int width)
    {
        for (int i = 0; i < width; ++i, ++count)
            if ((value >> i) & 1u) words[count / 32] |= uint32(1) << (count % 32);
    }
    void Bytes(const std::string &s) { for (unsigned char c : s) Put(c, 8); }
    bf_read Reader(int limit = -1) const
    {
        bf_read reader("test", words.data(), static_cast<int>(words.size() * 4), limit < 0 ? count : limit);
        reader.SetAssertOnOverflow(false);
        return reader;
    }
};
struct State
{
    int calls = 0, lengthCalls = 0, length = -1, object = -1;
    std::vector<int> values, indices;
    std::string text;
    float scalar = 0;
    std::array<float, 3> vector = {{0, 0, 0}};
    std::vector<void *> addresses;
    std::array<int, 32> storage = {{0}};
    int stackInitializations = 0, stackSeeks = 0;
};
static void IntProxy(const CRecvProxyData *data, void *object, void *output)
{
    State &state = *static_cast<State *>(object);
    ++state.calls; state.values.push_back(data->m_Value.m_Int);
    state.indices.push_back(data->m_iElement); state.addresses.push_back(output);
    state.object = data->m_ObjectID;
    if (output) std::memcpy(output, &data->m_Value.m_Int, sizeof(int));
}
static void StringProxy(const CRecvProxyData *data, void *object, void *)
{
    State &state = *static_cast<State *>(object);
    ++state.calls; state.text = data->m_Value.m_pString;
}
static void FloatProxy(const CRecvProxyData *data, void *object, void *)
{
    State &state = *static_cast<State *>(object);
    ++state.calls; state.scalar = data->m_Value.m_Float;
}
static void VectorProxy(const CRecvProxyData *data, void *object, void *)
{
    State &state = *static_cast<State *>(object);
    ++state.calls;
    std::copy(data->m_Value.m_Vector, data->m_Value.m_Vector + 3, state.vector.begin());
}
static void LengthProxy(void *object, int id, int length)
{
    State &state = *static_cast<State *>(object);
    ++state.lengthCalls; state.length = length; state.object = id;
}
static DecodeInfo Info(SendProp &send, RecvProp *recv, bf_read &reader, State &state, void *out = nullptr)
{
    DecodeInfo info;
    info.m_pProp = &send; info.m_pRecvProp = recv; info.m_pIn = &reader;
    info.m_pStruct = &state; info.m_pData = out; info.m_ObjectID = 34; info.m_iElement = 0;
    std::memset(info.m_TempStr, 0xcd, sizeof(info.m_TempStr));
    return info;
}
struct ArrayProps
{
    SendProp send, child;
    RecvProp recv, childRecv;
    explicit ArrayProps(int capacity)
    {
        send.type = DPT_Array; send.elements = capacity; send.child = &child;
        recv.elements = capacity; recv.child = &childRecv; recv.length = LengthProxy;
        childRecv.proxy = IntProxy;
    }
};
static Bits ArrayBits(const ArrayProps &props, int length)
{
    Bits bits; bits.Put(length, props.send.GetNumArrayLengthBits());
    for (int i = 0; i < length; ++i) bits.Put((i * 17 + 3) & 255, 8);
    return bits;
}
struct CRecvDecoder
{
    std::vector<SendProp *> sends;
    std::vector<RecvProp *> recvs;
    int GetNumProps() const { return static_cast<int>(sends.size()); }
    const SendProp *GetSendProp(int i) const { return sends.at(i); }
    const RecvProp *GetProp(int i) const { return recvs.at(i); }
};
struct RecvTable
{
    CRecvDecoder *m_pDecoder = nullptr;
    const char *GetName() const { return "test"; }
};
struct CClientDatatableStack
{
    CRecvDecoder *decoder;
    unsigned char *base;
    CClientDatatableStack(CRecvDecoder *d, unsigned char *p, int) : decoder(d), base(p) {}
    void Init() { ++reinterpret_cast<State *>(base)->stackInitializations; }
    void SeekToProp(unsigned int i)
    {
        Require(i < static_cast<unsigned int>(decoder->GetNumProps()), "out-of-range property reached stack");
        ++reinterpret_cast<State *>(base)->stackSeeks;
    }
    unsigned char *GetCurStructBase() const { return base; }
    bool IsCurProxyValid() const { return true; }
};
#define ErrorIfNot(condition, message) do { if (!(condition)) throw std::runtime_error("ErrorIfNot"); } while (false)
static bool g_bDTIEnabled = false;
static int g_nPropsDecoded = 0;
static void DTI_HookDeltaBits(CRecvDecoder *, unsigned int, int, int) {}
#include "recv_under_test.inc"

static int ValidArrays()
{
    int cases = 0;
    for (int capacity : {1, 2, 3, 8, 17, 64, 255, 1024})
        for (int length : {0, 1, capacity})
            for (int offset = 0; offset < 32; ++offset)
            {
                ArrayProps props(capacity);
                Bits bits; bits.Put(0, offset);
                bits.Put(length, props.send.GetNumArrayLengthBits());
                for (int i = 0; i < length; ++i) bits.Put((i * 17 + 3) & 255, 8);
                const int finish = bits.count; bits.Put(0x93, 8);
                bf_read reader = bits.Reader(); Require(reader.Seek(offset), "array seek");
                State state;
                std::vector<int> output(capacity + 2, -12345);
                auto info = Info(props.send, &props.recv, reader, state, output.data() + 1);
                Array_Decode(&info);
                Require(!reader.IsOverflowed() && reader.GetNumBitsRead() == finish, "valid array cursor");
                Require(state.calls == length && state.lengthCalls == 1 && state.length == length,
                        "valid array callbacks");
                Require(output.front() == -12345 && output.back() == -12345, "array canary");
                for (int i = 0; i < length; ++i)
                    Require(output[i + 1] == ((i * 17 + 3) & 255) && state.indices[i] == i &&
                            state.addresses[i] == output.data() + i + 1, "array values or stride");
                Require(reader.ReadUBitLong(8) == 0x93, "array sentinel");
                ++cases;
            }
    return cases;
}
static int OversizedArrays()
{
    int cases = 0;
    for (int capacity : {1, 2, 3, 8, 17, 64})
        for (int length = capacity + 1; length < (1 << ArrayProps(capacity).send.GetNumArrayLengthBits()); ++length)
        {
            ArrayProps props(capacity); Bits bits = ArrayBits(props, length); auto reader = bits.Reader();
            State state; std::array<int, 256> output; output.fill(-12345);
            auto info = Info(props.send, &props.recv, reader, state, output.data());
            Array_Decode(&info);
            Require(reader.IsOverflowed(), "oversized array accepted");
            Require(state.calls == 0 && state.lengthCalls == 0, "oversized array invoked proxy");
            Require(std::all_of(output.begin(), output.end(), [](int v) { return v == -12345; }),
                    "oversized array changed storage");
            ++cases;
        }
    return cases;
}
static int TruncatedArrays()
{
    int cases = 0;
    for (int length : {0, 1, 2, 8, 17})
    {
        ArrayProps props(17); Bits bits = ArrayBits(props, length);
        for (int cut = 0; cut < bits.count; ++cut)
        {
            auto reader = bits.Reader(cut); State state; std::array<int, 20> output; output.fill(-12345);
            auto info = Info(props.send, &props.recv, reader, state, output.data());
            Array_Decode(&info);
            const int header = props.send.GetNumArrayLengthBits();
            int complete = cut < header ? 0 : (cut - header) / 8;
            Require(reader.IsOverflowed(), "truncated array not rejected");
            Require(state.calls == complete, "truncated element invoked proxy");
            Require(state.lengthCalls == (cut < header ? 0 : 1), "truncated length proxy");
            for (int i = complete; i < 20; ++i) Require(output[i] == -12345, "truncated tail changed");
            ++cases;
        }
    }
    return cases;
}
static int Strings()
{
    int cases = 0; SendProp send; send.type = DPT_String;
    RecvProp recv; recv.proxy = StringProxy;
    for (int length : {0, 1, 7, 31, 255, 511})
    {
        std::string text(length, 'q');
        for (int i = 0; i < length; ++i) text[i] = char(33 + i % 90);
        Bits bits; bits.Put(length, DT_MAX_STRING_BITS); bits.Bytes(text);
        auto valid = bits.Reader(); State state; auto info = Info(send, &recv, valid, state);
        String_Decode(&info);
        Require(!valid.IsOverflowed() && state.calls == 1 && state.text == text, "valid string mismatch");
        ++cases;
        for (int cut = 0; cut < bits.count; ++cut)
        {
            auto reader = bits.Reader(cut); State truncated; auto item = Info(send, &recv, reader, truncated);
            String_Decode(&item);
            Require(reader.IsOverflowed() && truncated.calls == 0, "truncated string invoked proxy");
            ++cases;
        }
        auto skip = bits.Reader(); State skipped; auto item = Info(send, nullptr, skip, skipped);
        String_Decode(&item);
        Require(!skip.IsOverflowed() && skip.GetNumBitsRead() == bits.count && skipped.calls == 0,
                "consume string without proxy");
        ++cases;
    }
    return cases;
}
static int ScalarCallbacks()
{
    int cases = 0;
    for (int type : {DPT_Int, DPT_Float, DPT_Vector})
    {
        SendProp send; send.type = static_cast<SendPropType>(type); send.m_nBits = 32;
        send.flags = type == DPT_Int ? SPROP_UNSIGNED : SPROP_NOSCALE;
        RecvProp recv; recv.proxy = type == DPT_Int ? IntProxy : type == DPT_Float ? FloatProxy : VectorProxy;
        const int width = type == DPT_Vector ? 96 : 32;
        Bits bits;
        bits.Put(0x3f800000, 32); if (type == DPT_Vector) { bits.Put(0x40000000, 32); bits.Put(0xbf800000, 32); }
        for (int cut = 0; cut <= width; ++cut)
        {
            auto reader = bits.Reader(cut); State state; auto info = Info(send, &recv, reader, state);
            g_PropTypeFns[type].Decode(&info);
            Require(state.calls == (cut == width ? 1 : 0), "truncated scalar invoked proxy");
            Require(reader.IsOverflowed() == (cut < width), "scalar overflow flag");
            if (cut == width && type == DPT_Float) Require(state.scalar == 1.0f, "float decode");
            if (cut == width && type == DPT_Vector)
                Require(state.vector == std::array<float, 3>{{1, 2, -1}}, "vector decode");
            ++cases;
        }
    }
    return cases;
}
static int ArrayTraversal()
{
    int cases = 0;
    for (int capacity : {1, 2, 3, 8, 17})
        for (int length = 0; length < (1 << ArrayProps(capacity).send.GetNumArrayLengthBits()); ++length)
        {
            ArrayProps props(capacity); auto bits = ArrayBits(props, length);
            auto skip = bits.Reader(); Array_SkipProp(&props.send, &skip);
            Require(skip.IsOverflowed() == (length > capacity), "skip accepted invalid array length");
            auto zero = bits.Reader(); bool empty = Array_IsEncodedZero(&props.send, &zero);
            Require(zero.IsOverflowed() == (length > capacity) && empty == (length == 0),
                    "zero traversal array length");
            auto a = bits.Reader(), b = bits.Reader(); int different = Array_CompareDeltas(&props.send, &a, &b);
            Require((different != 0) == (length > capacity), "compare array length");
            if (length <= capacity)
                Require(skip.GetNumBitsRead() == bits.count && zero.GetNumBitsRead() == bits.count &&
                        a.GetNumBitsRead() == bits.count && b.GetNumBitsRead() == bits.count, "traversal cursor");
            else Require(a.IsOverflowed() && b.IsOverflowed(), "invalid compare flag");
            ++cases;
        }
    ArrayProps props(17);
    for (int left = 0; left <= 17; ++left)
        for (int right = 0; right <= 17; ++right)
        {
            auto leftBits = ArrayBits(props, left), rightBits = ArrayBits(props, right);
            auto a = leftBits.Reader(), b = rightBits.Reader();
            Require(bool(Array_CompareDeltas(&props.send, &a, &b)) == (left != right), "different length compare");
            Require(!a.IsOverflowed() && !b.IsOverflowed() && a.GetNumBitsRead() == leftBits.count &&
                    b.GetNumBitsRead() == rightBits.count, "different length cursor");
            ++cases;
        }
    auto bits = ArrayBits(props, 3);
    for (int cut = 0; cut < bits.count; ++cut)
    {
        auto skip = bits.Reader(cut); Array_SkipProp(&props.send, &skip);
        auto zero = bits.Reader(cut); Array_IsEncodedZero(&props.send, &zero);
        auto a = bits.Reader(cut), b = bits.Reader(); Array_CompareDeltas(&props.send, &a, &b);
        Require(skip.IsOverflowed() && zero.IsOverflowed() && a.IsOverflowed(), "truncated traversal flag");
        ++cases;
    }
    return cases;
}
static int Metadata()
{
    int cases = 0;
    for (int mode = 0; mode < 7; ++mode)
    {
        ArrayProps props(3);
        if (mode == 0) props.send.child = nullptr;
        if (mode == 1) props.child.type = DPT_DataTable;
        if (mode == 2) props.child.type = static_cast<SendPropType>(DPT_NUMSendPropTypes);
        if (mode == 3) props.child.type = static_cast<SendPropType>(DPT_NUMSendPropTypes + 1);
        if (mode == 4) props.recv.child = nullptr;
        if (mode == 5) props.recv.stride = -1;
        if (mode == 6) props.recv.elements = 1;
        auto bits = ArrayBits(props, 2); auto reader = bits.Reader(); State state;
        auto info = Info(props.send, &props.recv, reader, state);
        Array_Decode(&info);
        Require(reader.IsOverflowed() && state.calls == 0 && state.lengthCalls == 0, "invalid array metadata");
        ++cases;
    }
    return cases;
}
static int ConsumeOnly()
{
    int cases = 0;
    for (int length : {0, 1, 3, 17})
    {
        ArrayProps props(17); auto bits = ArrayBits(props, length); auto reader = bits.Reader(); State state;
        auto info = Info(props.send, nullptr, reader, state);
        Array_Decode(&info);
        Require(!reader.IsOverflowed() && reader.GetNumBitsRead() == bits.count && state.calls == 0,
                "consume array without receive property");
        ++cases;
    }
    ArrayProps props(3); props.recv.stride = 8; props.childRecv.offset = 4;
    auto bits = ArrayBits(props, 3); auto reader = bits.Reader(); State state;
    std::array<int, 8> output; output.fill(-12345);
    auto info = Info(props.send, &props.recv, reader, state, output.data());
    Array_Decode(&info);
    for (int i = 0; i < 8; ++i)
        Require(output[i] == (i == 1 || i == 3 || i == 5 ? ((i - 1) / 2 * 17 + 3) : -12345),
                "offset and padded stride");
    return cases + 1;
}
static int FloatModes()
{
    int cases = 0;
    for (int mode = 0; mode < 5; ++mode)
    {
        SendProp send; RecvProp recv; recv.proxy = mode == 4 ? VectorProxy : FloatProxy;
        send.type = mode == 4 ? DPT_Vector : DPT_Float;
        Bits bits; float expected = 0;
        if (mode == 0)
        {
            send.flags = SPROP_COORD;
            bits.Put(1, 1); bits.Put(1, 1); bits.Put(1, 1);
            bits.Put(11, COORD_INTEGER_BITS); bits.Put(16, COORD_FRACTIONAL_BITS);
            expected = -12.5f;
        }
        if (mode == 1)
        {
            send.flags = SPROP_NORMAL; bits.Put(1, 1); bits.Put(1024, NORMAL_FRACTIONAL_BITS);
            expected = -1024.0f / NORMAL_DENOMINATOR;
        }
        if (mode == 2)
        {
            send.flags = 0; send.m_nBits = 8; bits.Put(127, 8);
            expected = -1.0f + 2.0f * (127.0f / 255.0f);
        }
        if (mode == 3)
        {
            send.flags = SPROP_COORD; bits.Put(0, 2); expected = 0;
        }
        if (mode == 4)
        {
            send.flags = SPROP_NORMAL;
            bits.Put(0, NORMAL_FRACTIONAL_BITS + 1); bits.Put(0, NORMAL_FRACTIONAL_BITS + 1);
            bits.Put(1, 1);
        }
        for (int cut = 0; cut <= bits.count; ++cut)
        {
            auto reader = bits.Reader(cut); State state; auto info = Info(send, &recv, reader, state);
            g_PropTypeFns[send.type].Decode(&info);
            Require(state.calls == (cut == bits.count ? 1 : 0), "partial special float invoked proxy");
            Require(reader.IsOverflowed() == (cut < bits.count), "special float overflow");
            if (cut == bits.count)
            {
                if (mode == 4) Require(state.vector == std::array<float, 3>{{0, 0, -1}}, "normal vector");
                else Require(std::fabs(state.scalar - expected) < 0.000001f, "special float value");
            }
            ++cases;
        }
    }
    for (int width : {1, 7, 8, 16, 31, 32})
    {
        SendProp send; send.flags = 0; send.m_nBits = width;
        RecvProp recv; recv.proxy = IntProxy;
        Bits bits; bits.Put(UINT32_MAX, width);
        for (int cut = 0; cut <= width; ++cut)
        {
            auto reader = bits.Reader(cut); State state; auto info = Info(send, &recv, reader, state);
            Int_Decode(&info);
            Require(state.calls == (cut == width ? 1 : 0), "partial signed int invoked proxy");
            if (cut == width) Require(state.values == std::vector<int>{-1}, "signed integer value");
            ++cases;
        }
    }
    return cases;
}
static int StringArrays()
{
    ArrayProps props(3); props.child.type = DPT_String; props.childRecv.proxy = StringProxy;
    props.recv.stride = DT_MAX_STRING_BUFFERSIZE;
    const std::array<std::string, 3> values = {{"", "resource_name", std::string(511, 'z')}};
    Bits bits; bits.Put(3, props.send.GetNumArrayLengthBits());
    std::array<int, 3> endings;
    for (int i = 0; i < 3; ++i)
    {
        bits.Put(values[i].size(), DT_MAX_STRING_BITS); bits.Bytes(values[i]); endings[i] = bits.count;
    }
    for (int cut = 0; cut <= bits.count; ++cut)
    {
        auto reader = bits.Reader(cut); State state; auto info = Info(props.send, &props.recv, reader, state);
        Array_Decode(&info);
        const int complete = std::count_if(endings.begin(), endings.end(), [cut](int n) { return n <= cut; });
        Require(state.calls == complete, "partial string array invoked proxy");
        Require(reader.IsOverflowed() == (cut < bits.count), "string array overflow flag");
        if (complete) Require(state.text == values[complete - 1], "string array last value");
    }
    auto a = bits.Reader(), b = bits.Reader();
    Require(Array_CompareDeltas(&props.send, &a, &b) == 0 && a.GetNumBitsRead() == bits.count &&
            b.GetNumBitsRead() == bits.count, "string array compare");
    return bits.count + 2;
}
static int Preoverflow()
{
    int cases = 0;
    for (int type : {DPT_Int, DPT_Float, DPT_Vector, DPT_String, DPT_Array})
    {
        ArrayProps props(3); Bits bits = ArrayBits(props, 2); auto reader = bits.Reader();
        props.send.type = static_cast<SendPropType>(type);
        props.send.flags = type == DPT_Int ? SPROP_UNSIGNED : SPROP_NOSCALE;
        State state; RecvProp recv;
        recv.proxy = type == DPT_Int ? IntProxy : type == DPT_Float ? FloatProxy :
                     type == DPT_Vector ? VectorProxy : StringProxy;
        auto info = Info(props.send, type == DPT_Array ? &props.recv : &recv, reader, state);
        reader.SetOverflowFlag();
        g_PropTypeFns[type].Decode(&info);
        Require(reader.IsOverflowed() && state.calls == 0 && state.lengthCalls == 0, "preoverflow proxy");
        ++cases;
    }
    return cases;
}
static void PutUBitVar(Bits &bits, uint32 value)
{
    const uint32 shifted = value + 1;
    int width = 0;
    for (uint32 n = shifted; n; n >>= 1) ++width;
    if (value == UINT32_MAX) width = 33;
    for (int i = 1; i < width; ++i) bits.Put(0, 1);
    bits.Put(1, 1);
    if (width > 1) bits.Put(shifted, width - 1);
}
static void PutIndex(Bits &bits, uint32 delta)
{
    bits.Put(1, 1); PutUBitVar(bits, delta);
}
static int DeltaIndices()
{
    int cases = 0;
    for (uint32 delta : {0u, 1u, 17u, 511u, 1023u, 1024u, 65535u, 0x7fffffffu, UINT32_MAX})
    {
        Bits bits; PutIndex(bits, delta); bits.Put(0, 1); auto reader = bits.Reader();
        CDeltaBitsReader iterator(&reader); unsigned int index = iterator.ReadNextPropIndex();
        if (delta < MAX_DATATABLE_PROPS)
        {
            Require(index == delta && !reader.IsOverflowed(), "valid property delta");
            Require(iterator.ReadNextPropIndex() == UINT32_MAX, "property delta terminator");
        }
        else Require(index == UINT32_MAX && reader.IsOverflowed(), "invalid property delta accepted");
        ++cases;
    }
    Bits sequence;
    for (int i = 0; i < MAX_DATATABLE_PROPS; ++i) PutIndex(sequence, 0);
    PutIndex(sequence, 0);
    auto reader = sequence.Reader(); CDeltaBitsReader iterator(&reader);
    for (unsigned int i = 0; i < MAX_DATATABLE_PROPS; ++i)
        Require(iterator.ReadNextPropIndex() == i, "consecutive property index");
    Require(iterator.ReadNextPropIndex() == UINT32_MAX && reader.IsOverflowed(), "cumulative index overflow");
    ++cases;
    Bits encoded; PutIndex(encoded, 511);
    for (int cut = 0; cut < encoded.count; ++cut)
    {
        auto truncated = encoded.Reader(cut); CDeltaBitsReader item(&truncated);
        Require(item.ReadNextPropIndex() == UINT32_MAX && truncated.IsOverflowed(), "truncated property delta");
        ++cases;
    }
    return cases;
}
static int LargeDeltas()
{
    int cases = 0;
    for (uint32 delta : {0x7fffffffu, 0x80000000u, 0xfffffffeu, UINT32_MAX})
    {
        Bits bits; PutIndex(bits, delta); auto reader = bits.Reader(); CDeltaBitsReader iterator(&reader);
        Require(iterator.ReadNextPropIndex() == UINT32_MAX && reader.IsOverflowed(),
                "large delta not rejected before arithmetic");
        ++cases;
    }
    return cases;
}
static int EntityLoop()
{
    int cases = 0;
    for (int mode = 0; mode < 6; ++mode)
    {
        State state; SendProp first, second; RecvProp recvFirst, recvSecond;
        recvFirst.proxy = IntProxy; recvSecond.proxy = IntProxy;
        recvFirst.offset = static_cast<int>(reinterpret_cast<char *>(state.storage.data()) - reinterpret_cast<char *>(&state));
        recvSecond.offset = recvFirst.offset + sizeof(int);
        CRecvDecoder decoder; decoder.sends = {&first, &second}; decoder.recvs = {&recvFirst, &recvSecond};
        RecvTable table; table.m_pDecoder = &decoder;
        Bits bits;
        if (mode == 0) { PutIndex(bits, 0); bits.Put(37, 8); PutIndex(bits, 0); bits.Put(91, 8); bits.Put(0, 1); }
        if (mode == 1) { PutIndex(bits, 2); bits.Put(37, 8); bits.Put(0, 1); }
        if (mode == 2) { bits.Put(0, 1); }
        if (mode == 3) { PutIndex(bits, 0); bits.Put(37, 3); }
        if (mode == 4) { PutIndex(bits, 0); bits.Put(37, 8); bits.Put(1, 1); }
        if (mode == 5) { PutIndex(bits, UINT32_MAX); bits.Put(0, 1); }
        auto reader = bits.Reader(); if (mode == 2) reader.SetOverflowFlag();
        const bool result = RecvTable_Decode(&table, &state, &reader, 34, false);
        if (mode == 0)
            Require(result && state.values == std::vector<int>{37, 91} && state.stackSeeks == 2,
                    "valid entity decode loop");
        else
        {
            Require(!result && reader.IsOverflowed(), "invalid entity accepted");
            Require(state.calls == (mode == 4 ? 1 : 0), "invalid entity proxy");
            if (mode == 1 || mode == 5) Require(state.stackSeeks == 0, "invalid entity index reached stack");
            if (mode == 2) Require(state.stackInitializations == 0, "preoverflow entity initialized proxies");
        }
        ++cases;
    }
    ArrayProps props(2); SendProp second; RecvProp recvSecond; recvSecond.proxy = IntProxy;
    State state;
    props.recv.offset = static_cast<int>(reinterpret_cast<char *>(state.storage.data()) - reinterpret_cast<char *>(&state));
    recvSecond.offset = props.recv.offset + 16;
    CRecvDecoder decoder; decoder.sends = {&props.send, &second}; decoder.recvs = {&props.recv, &recvSecond};
    RecvTable table; table.m_pDecoder = &decoder;
    Bits bits; PutIndex(bits, 0); bits.Put(3, props.send.GetNumArrayLengthBits());
    bits.Put(255, 8); PutIndex(bits, 0); bits.Put(99, 8); bits.Put(0, 1);
    auto reader = bits.Reader();
    Require(!RecvTable_Decode(&table, &state, &reader, 34, false), "oversized entity array accepted");
    Require(state.stackSeeks == 1 && state.calls == 0 && state.lengthCalls == 0,
            "entity loop continued after invalid array");
    return cases + 1;
}
static int RandomArrays()
{
    std::mt19937 random(0x34c1e17u);
    for (int trial = 0; trial < 2000; ++trial)
    {
        int capacity = 1 + random() % 64;
        ArrayProps props(capacity); int length = random() % (capacity + 1);
        Bits bits; bits.Put(length, props.send.GetNumArrayLengthBits());
        std::vector<int> expected(length);
        for (int &value : expected) { value = random() & 255; bits.Put(value, 8); }
        auto reader = bits.Reader(); State state; auto info = Info(props.send, &props.recv, reader, state);
        Array_Decode(&info);
        Require(!reader.IsOverflowed() && state.values == expected && state.length == length, "random valid array");
    }
    return 2000;
}
int main(int argc, char **argv)
{
    for (int bit = 0; bit <= 32; ++bit) g_ExtraMasks[bit] = bit == 32 ? UINT32_MAX : (uint32(1) << bit) - 1;
    for (int bit = 0; bit < 32; ++bit) g_LittleBits[bit] = uint32(1) << bit;
    g_PropTypeFns[DPT_Int].Decode = Int_Decode;
    g_PropTypeFns[DPT_Int].CompareDeltas = Int_CompareDeltas;
    g_PropTypeFns[DPT_Int].IsEncodedZero = Int_IsEncodedZero;
    g_PropTypeFns[DPT_Int].SkipProp = Int_SkipProp;
    g_PropTypeFns[DPT_Float].Decode = Float_Decode;
    g_PropTypeFns[DPT_Vector].Decode = Vector_Decode;
    g_PropTypeFns[DPT_String].Decode = String_Decode;
    g_PropTypeFns[DPT_String].CompareDeltas = String_CompareDeltas;
    g_PropTypeFns[DPT_String].IsEncodedZero = String_IsEncodedZero;
    g_PropTypeFns[DPT_String].SkipProp = String_SkipProp;
    g_PropTypeFns[DPT_Array].Decode = Array_Decode;
    g_PropTypeFns[DPT_Array].CompareDeltas = Array_CompareDeltas;
    g_PropTypeFns[DPT_Array].IsEncodedZero = Array_IsEncodedZero;
    g_PropTypeFns[DPT_Array].SkipProp = Array_SkipProp;
    struct Suite { const char *name; int (*run)(); };
    const Suite suites[] = {{"valid-arrays", ValidArrays}, {"oversized-arrays", OversizedArrays},
        {"truncated-arrays", TruncatedArrays}, {"strings", Strings}, {"scalar-callbacks", ScalarCallbacks},
        {"array-traversal", ArrayTraversal}, {"metadata", Metadata}, {"consume-only", ConsumeOnly},
        {"random-arrays", RandomArrays}, {"float-modes", FloatModes},
        {"string-arrays", StringArrays}, {"preoverflow", Preoverflow},
        {"delta-indices", DeltaIndices}, {"entity-loop", EntityLoop}, {"large-deltas", LargeDeltas}};
    int count = 0, cases = 0; const std::string selected = argc > 1 ? argv[1] : "all";
    try
    {
        for (const auto &suite : suites)
            if (selected == "all" || selected == suite.name)
            { cases += suite.run(); ++count; std::cout << "PASS " << suite.name << '\n'; }
        Require(count != 0, "unknown test case");
    }
    catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
    std::cout << "Passed " << count << " suites, " << cases << " cases\n";
}
