#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <bzlib.h>

#define PAD_NUMBER(number, alignment) (((number) + (alignment) - 1) & ~((alignment) - 1))
#define Assert(value) assert(value)
#define Q_memcpy std::memcpy
#define MEM_ALLOC_CREDIT()
#define CHECK(value) do { if (!(value)) throw std::runtime_error(std::string(__func__) + ": " + #value); } while (0)

struct netadr_t
{
    int id;
    bool CompareAdr(const netadr_t& other) const { return id == other.id; }
    const char* ToString() const { return "test-peer"; }
};

struct netpacket_t
{
    unsigned char* data;
    int size;
    netadr_t from;
    int wiresize;
};

template <typename T> class CUtlVector
{
public:
    int Count() const { return static_cast<int>(values.size()); }
    void AddToTail(const T& value) { values.push_back(value); }
    void PurgeAndDeleteElements()
    {
        for (T value : values) delete value;
        values.clear();
        values.shrink_to_fit();
    }
    void Remove(int index) { values.at(index); values.erase(values.begin() + index); }
    T& operator[](int index) { return values.at(index); }
private:
    std::vector<T> values;
};

struct TestConVar { int GetInt() const { return 0; } bool GetBool() const { return false; } };
static TestConVar net_showsplits;
static TestConVar net_showudp;
static float net_time = 10.0f;
static short LittleShort(short value) { return value; }
static int LittleLong(int value) { return value; }
template <typename... T> static void NET_Error(const char*, T...) {}
template <typename... T> static void NET_Warning(const char*, T...) {}
template <typename... T> static void NET_Complete(const char*, T...) {}

typedef unsigned int CRC32_t;
static CRC32_t CRC32_ProcessSingleBuffer(const void* data, int size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    CRC32_t crc = 0xffffffffu;
    for (int i = 0; i < size; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

typedef unsigned char byte;
template <typename T, int Capacity> class CUtlMemoryFixedGrowable
{
public:
    explicit CUtlMemoryFixedGrowable(int) {}
    void EnsureCapacity(int size) { values.resize(size, 0xa5); }
    T* Base() { return values.data(); }
private:
    std::vector<T> values;
};

static std::vector<unsigned char> decoded;
static int advertisedSize;
static int returnedSize;
static bool decodeSuccess;
static unsigned int expectedInputSize;
static int decodeCalls;
static int COM_GetUncompressedSize(const void*, unsigned int size)
{
    CHECK(size == expectedInputSize);
    return advertisedSize;
}
static bool COM_BufferToBufferDecompress(void* output, unsigned int* length,
                                       const void*, unsigned int size)
{
    CHECK(size == expectedInputSize);
    ++decodeCalls;
    if (!decodeSuccess) return false;
    std::memcpy(output, decoded.data(), std::min<size_t>(*length, decoded.size()));
    *length = returnedSize;
    return true;
}

#include "network_under_test.inc"

static std::vector<unsigned char> scratch(NET_MAX_MESSAGE);
static int assertions = 0;

static void Reset()
{
    NET_ClearSplitPackets();
    net_splitpackets = CUtlVector<vecSplitPacketEntries_t>();
    net_splitpackets.AddToTail(vecSplitPacketEntries_t());
    net_time = 10.0f;
    std::fill(scratch.begin(), scratch.end(), 0xcd);
}

static std::vector<unsigned char> Payload(int length, unsigned seed = 1)
{
    std::mt19937 random(seed);
    std::vector<unsigned char> data(length);
    for (int i = 0; i < length; ++i)
        data[i] = static_cast<unsigned char>(random());
    return data;
}

static std::vector<unsigned char> Fragment(int sequence, int number, int count,
                                           const std::vector<unsigned char>& payload)
{
    SPLITPACKET header = { -2, sequence, static_cast<short>((number << 8) | count) };
    std::vector<unsigned char> packet(sizeof(header) + payload.size());
    std::memcpy(packet.data(), &header, sizeof(header));
    if (!payload.empty())
        std::memcpy(packet.data() + sizeof(header), payload.data(), payload.size());
    return packet;
}

static bool Receive(const std::vector<unsigned char>& data, int peer = 1,
                    std::vector<unsigned char>* output = NULL)
{
    CHECK(data.size() <= scratch.size());
    std::copy(data.begin(), data.end(), scratch.begin());
    netpacket_t packet = { scratch.data(), static_cast<int>(data.size()), { peer }, static_cast<int>(data.size()) };
    bool complete = NET_GetLong(0, &packet);
    if (complete && output)
        output->assign(scratch.begin(), scratch.begin() + packet.size);
    return complete;
}

static std::vector<std::vector<unsigned char> > Split(const std::vector<unsigned char>& data,
                                                     int sequence)
{
    int count = (data.size() + SPLIT_SIZE - 1) / SPLIT_SIZE;
    std::vector<std::vector<unsigned char> > fragments;
    for (int i = 0; i < count; ++i)
    {
        int offset = i * SPLIT_SIZE;
        int length = std::min(static_cast<int>(SPLIT_SIZE), static_cast<int>(data.size()) - offset);
        fragments.push_back(Fragment(sequence, i, count,
            std::vector<unsigned char>(data.begin() + offset, data.begin() + offset + length)));
    }
    return fragments;
}

static std::vector<unsigned char> Compress(const std::vector<unsigned char>& data)
{
    unsigned int length = data.size() + data.size() / 100 + 601;
    std::vector<unsigned char> result(sizeof(SPLITPACKET_COMPRESSED) + length);
    CHECK(BZ2_bzBuffToBuffCompress(reinterpret_cast<char*>(result.data()) + sizeof(SPLITPACKET_COMPRESSED),
          &length, const_cast<char*>(reinterpret_cast<const char*>(data.data())), data.size(), 9, 0, 30) == BZ_OK);
    SPLITPACKET_COMPRESSED header = { static_cast<int>(data.size()),
        static_cast<int>(CRC32_ProcessSingleBuffer(data.data(), data.size())) };
    std::memcpy(result.data(), &header, sizeof(header));
    result.resize(sizeof(header) + length);
    return result;
}

static bool Deliver(const std::vector<unsigned char>& data, int sequence,
                    std::vector<unsigned char>* result = NULL)
{
    std::vector<std::vector<unsigned char> > fragments = Split(data, sequence);
    for (size_t i = 0; i + 1 < fragments.size(); ++i)
        CHECK(!Receive(fragments[i]));
    return Receive(fragments.back(), 1, result);
}

static void ValidPackets()
{
    const int lengths[] = { NET_MIN_MESSAGE, 1390, 1391, 2780, 96000, 180000, NET_MAX_PAYLOAD, NET_MAX_MESSAGE };
    for (int length : lengths)
    {
        Reset();
        std::vector<unsigned char> data = Payload(length), result;
        CHECK(Deliver(data, 12, &result));
        CHECK(result == data);
        ++assertions;
    }
}

static void ReorderedPackets()
{
    for (int length : { 1391, 96000, 180000, NET_MAX_PAYLOAD })
    {
        Reset();
        std::vector<unsigned char> data = Payload(length), result;
        auto fragments = Split(data, 32);
        std::reverse(fragments.begin(), fragments.end());
        for (size_t i = 0; i < fragments.size(); ++i)
            CHECK(Receive(fragments[i], 1, &result) == (i + 1 == fragments.size()));
        CHECK(result == data);
        ++assertions;
    }
}

static void ZeroSequence()
{
    Reset();
    auto data = Payload(4000);
    std::vector<unsigned char> result;
    CHECK(Deliver(data, 0, &result));
    CHECK(result == data);
}

static void DuplicateDoesNotOverwrite()
{
    Reset();
    auto data = Payload(4000);
    auto fragments = Split(data, 7);
    CHECK(!Receive(fragments[0]));
    auto duplicate = fragments[0];
    std::fill(duplicate.begin() + sizeof(SPLITPACKET), duplicate.end(), 0x5a);
    CHECK(!Receive(duplicate));
    CHECK(!Receive(fragments[1]));
    std::vector<unsigned char> result;
    CHECK(Receive(fragments[2], 1, &result));
    CHECK(result == data);
}

static void InvalidMetadata()
{
    const int cases[][3] = {
        {0, 0, 12}, {1, 1, 12}, {5, 2, 1390}, {128, 129, 1390},
        {255, 255, 12}, {254, 255, 1390}, {207, 208, 1390},
        {0, 2, 1389}, {0, 1, 1391}, {0, 1, 0}, {0, 1, 4}
    };
    for (const auto& test : cases)
    {
        Reset();
        CHECK(!Receive(Fragment(19, test[0], test[1], Payload(test[2]))));
        ++assertions;
    }
    for (int length = 0; length < static_cast<int>(sizeof(SPLITPACKET)); ++length)
    {
        Reset();
        CHECK(!Receive(Payload(length)));
        ++assertions;
    }
}

static void BufferOverflow()
{
    Reset();
    CHECK(!Receive(Fragment(19, 127, 128, Payload(120000))));
}

static void UnsignedFragmentNumber()
{
    Reset();
    auto data = Payload(180000);
    auto fragments = Split(data, 21);
    std::vector<unsigned char> result;
    CHECK(!Receive(fragments[128]));
    for (size_t i = 0; i < fragments.size(); ++i)
    {
        if (i == 128)
            continue;
        CHECK(Receive(fragments[i], 1, &result) == (i + 1 == fragments.size()));
    }
    CHECK(result == data);
}

static void InconsistentCounts()
{
    Reset();
    auto data = Payload(4000);
    auto fragments = Split(data, 7);
    CHECK(!Receive(fragments[0]));
    CHECK(!Receive(Fragment(7, 1, 2, Payload(100))));
    CHECK(net_splitpackets[0][0]->netsplit.splitCount == 2);
    CHECK(!Receive(fragments[1]));
    std::vector<unsigned char> result;
    CHECK(Receive(fragments[2], 1, &result));
    CHECK(result == data);
}

static void SequenceTransitions()
{
    Reset();
    auto data = Payload(4000);
    auto fragments = Split(data, 0);
    CHECK(!Receive(fragments[0]));
    CHECK(!Receive(Split(data, 1)[1]));
    CHECK(!Receive(fragments[1]));
    CHECK(!Receive(fragments[2]));
    std::vector<unsigned char> result;
    CHECK(Receive(fragments[0], 1, &result));
    CHECK(result == data);
    CHECK(Deliver(data, 0, &result));
    CHECK(result == data);
}

static void CompressedPackets()
{
    for (int sequence : { static_cast<int>(0x80000001u), -1 })
    {
        for (int length : { 5, 1390, 96000, 180000, NET_MAX_PAYLOAD })
        {
            Reset();
            auto data = length == NET_MAX_PAYLOAD ? std::vector<unsigned char>(length, 0x34) : Payload(length);
            auto compressed = Compress(data);
            std::vector<unsigned char> result;
            CHECK(Deliver(compressed, sequence, &result));
            CHECK(result == data);
            ++assertions;
        }
    }
}

static void InvalidCompression()
{
    for (int size : { 0, 4, NET_MAX_PAYLOAD + 1, 3999, 4001 })
    {
        Reset();
        auto compressed = Compress(Payload(4000));
        std::memcpy(compressed.data(), &size, sizeof(size));
        CHECK(!Deliver(compressed, static_cast<int>(0x80000003u)));
        ++assertions;
    }
    for (int length = 5; length <= 8; ++length)
    {
        Reset();
        CHECK(!Deliver(Payload(length), static_cast<int>(0x80000003u)));
        ++assertions;
    }
    Reset();
    auto compressed = Compress(Payload(4000));
    compressed[sizeof(int)] ^= 1;
    CHECK(!Deliver(compressed, static_cast<int>(0x80000003u)));
    Reset();
    compressed = Compress(Payload(4000));
    compressed[sizeof(SPLITPACKET_COMPRESSED)] ^= 0xff;
    CHECK(!Deliver(compressed, static_cast<int>(0x80000003u)));
    Reset();
    compressed = Compress(Payload(4000));
    compressed.resize(compressed.size() - 8);
    CHECK(!Deliver(compressed, static_cast<int>(0x80000003u)));
}

static void PeerIsolation()
{
    Reset();
    auto first = Payload(4000, 1), second = Payload(4000, 2);
    auto a = Split(first, 7), b = Split(second, 7);
    std::vector<unsigned char> result;
    for (size_t i = 0; i < a.size(); ++i)
    {
        bool complete = i + 1 == a.size();
        CHECK(Receive(a[i], 1, &result) == complete);
        if (complete) CHECK(result == first);
        CHECK(Receive(b[i], 2, &result) == complete);
        if (complete) CHECK(result == second);
    }
}

static void TrackingLimit()
{
    Reset();
    auto packet = Fragment(1, 0, 2, Payload(SPLIT_SIZE));
    for (int peer = 1; peer <= SPLIT_PACKET_TRACKING_MAX + 20; ++peer)
    {
        CHECK(!Receive(packet, peer));
        CHECK(net_splitpackets[0].Count() <= SPLIT_PACKET_TRACKING_MAX);
    }
    NET_DiscardStaleSplitpackets(0);
    CHECK(net_splitpackets[0].Count() == SPLIT_PACKET_TRACKING_MAX);
    net_time += SPLIT_PACKET_STALE_TIME + 1.0f;
    NET_DiscardStaleSplitpackets(0);
    CHECK(net_splitpackets[0].Count() == 0);
}

static void StableEntryAddresses()
{
    Reset();
    netadr_t address = {1};
    CSplitPacketEntry* first = NET_FindOrCreateSplitPacketEntry(0, &address);
    first->netsplit.buffer[0] = 0x34;
    for (int peer = 2; peer < SPLIT_PACKET_TRACKING_MAX; ++peer)
    {
        netadr_t other = {peer};
        NET_FindOrCreateSplitPacketEntry(0, &other);
        CHECK(NET_FindOrCreateSplitPacketEntry(0, &address) == first);
        CHECK(first->netsplit.buffer[0] == 0x34);
    }
}

static void ClearAndReuse()
{
    Reset();
    net_splitpackets.AddToTail(vecSplitPacketEntries_t());
    netadr_t address = {1};
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        NET_FindOrCreateSplitPacketEntry(0, &address);
        NET_FindOrCreateSplitPacketEntry(1, &address);
        NET_ClearSplitPackets();
        CHECK(net_splitpackets.Count() == 2);
        CHECK(net_splitpackets[0].Count() == 0);
        CHECK(net_splitpackets[1].Count() == 0);
    }
    NET_ClearSplitPackets();
    NET_FindOrCreateSplitPacketEntry(1, &address);
    CHECK(net_splitpackets[1].Count() == 1);
}

static netpacket_t CompressedFixture()
{
    Reset();
    decoded = Payload(6000);
    advertisedSize = decoded.size();
    returnedSize = advertisedSize;
    decodeSuccess = true;
    expectedInputSize = 4000;
    decodeCalls = 0;
    std::fill(scratch.begin(), scratch.begin() + expectedInputSize + 4, 0x12);
    netpacket_t packet = {scratch.data(), static_cast<int>(expectedInputSize) + 4, {1}, 100};
    return packet;
}

static void DatagramDecompression()
{
    auto packet = CompressedFixture();
    CHECK(NET_DecompressPacket(&packet));
    CHECK(packet.wiresize == 100);
    CHECK(packet.size == static_cast<int>(decoded.size()));
    CHECK(std::equal(decoded.begin(), decoded.end(), scratch.begin()));
    CHECK(decodeCalls == 1);
}

static void FailedDecompression()
{
    auto packet = CompressedFixture();
    decodeSuccess = false;
    std::vector<unsigned char> before(scratch.begin(), scratch.begin() + packet.size);
    int originalSize = packet.size;
    CHECK(!NET_DecompressPacket(&packet));
    CHECK(packet.size == originalSize);
    CHECK(std::equal(before.begin(), before.end(), scratch.begin()));
    CHECK(decodeCalls == 1);
    packet = CompressedFixture();
    returnedSize = advertisedSize - 1;
    CHECK(!NET_DecompressPacket(&packet));
    CHECK(packet.size == static_cast<int>(expectedInputSize) + 4);
    for (int size : {-1, 0, 4, NET_MAX_PAYLOAD + 1})
    {
        packet = CompressedFixture();
        advertisedSize = size;
        CHECK(!NET_DecompressPacket(&packet));
        CHECK(decodeCalls == 0);
    }
    for (int size : {0, 1, 2, 3, 4, NET_MAX_MESSAGE + 1})
    {
        packet = CompressedFixture();
        packet.size = size;
        CHECK(!NET_DecompressPacket(&packet));
        CHECK(decodeCalls == 0);
    }
}

static void RandomizedPackets()
{
    std::mt19937 random(0x34);
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        Reset();
        auto data = Payload(5 + random() % NET_MAX_PAYLOAD, random());
        auto fragments = Split(data, iteration);
        std::shuffle(fragments.begin(), fragments.end(), random);
        std::vector<unsigned char> result;
        for (size_t i = 0; i < fragments.size(); ++i)
        {
            CHECK(Receive(fragments[i], 1, &result) == (i + 1 == fragments.size()));
            if (i + 1 < fragments.size())
                CHECK(!Receive(fragments[i]));
        }
        CHECK(result == data);
        ++assertions;
    }
    Reset();
    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        int length = random() % 1450;
        auto bytes = Payload(length, random());
        if (length >= static_cast<int>(sizeof(SPLITPACKET)))
        {
            SPLITPACKET header = { -2, static_cast<int>(random()), static_cast<short>(random()) };
            std::memcpy(bytes.data(), &header, sizeof(header));
        }
        Receive(bytes);
        ++assertions;
    }
}

int main(int argc, char** argv)
{
    struct Cleanup { ~Cleanup() { NET_ClearSplitPackets(); } } cleanup;
    CHECK(CRC32_ProcessSingleBuffer("123456789", 9) == 0xcbf43926u);
    const std::pair<const char*, std::function<void()> > tests[] = {
        {"valid", ValidPackets}, {"reordered", ReorderedPackets},
        {"zero-sequence", ZeroSequence}, {"duplicates", DuplicateDoesNotOverwrite},
        {"invalid-metadata", InvalidMetadata}, {"buffer-overflow", BufferOverflow},
        {"unsigned-number", UnsignedFragmentNumber},
        {"inconsistent-counts", InconsistentCounts}, {"sequence-transitions", SequenceTransitions},
        {"compressed", CompressedPackets}, {"invalid-compression", InvalidCompression},
        {"peer-isolation", PeerIsolation}, {"tracking-limit", TrackingLimit},
        {"stable-addresses", StableEntryAddresses}, {"clear-and-reuse", ClearAndReuse},
        {"datagram-decompression", DatagramDecompression}, {"failed-decompression", FailedDecompression},
        {"randomized", RandomizedPackets}
    };
    std::string selected = argc > 1 ? argv[1] : "all";
    int passed = 0;
    try
    {
        for (const auto& test : tests)
        {
            if (selected != "all" && selected != test.first)
                continue;
            test.second();
            std::printf("PASS %s\n", test.first);
            ++passed;
        }
        CHECK(passed > 0);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
    std::printf("Passed %d suites, %d parameterized/random cases\n", passed, assertions);
    return 0;
}
