#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "codec_under_test.inc"

static int checks;
#define CHECK(e) do { ++checks; if (!(e)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); std::exit(1); } } while (0)

class Source : public IAudioStreamEvent
{
public:
    std::vector<unsigned char> bytes;
    explicit Source(const std::vector<unsigned char> &input) : bytes(input) {}
    int StreamRequestData(void *buffer, int requested, int offset) override
    {
        CHECK(requested >= 0 && offset >= 0);
        if ((size_t)offset >= bytes.size()) return 0;
        const int count = (int)std::min((size_t)requested, bytes.size() - offset);
        if (count) std::memcpy(buffer, bytes.data() + offset, count);
        return count;
    }
};

static std::vector<short> Decode(IAudioStream &decoder)
{
    std::vector<short> result;
    short pcm[4096];
    for (int step = 0; step < 100; ++step)
    {
        int bytes = decoder.Decode(pcm, sizeof(pcm));
        CHECK(bytes >= 0 && bytes <= (int)sizeof(pcm) && bytes % 2 == 0);
        if (!bytes) return result;
        result.insert(result.end(), pcm, pcm + bytes / 2);
    }
    CHECK(false);
    return result;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    CHECK(input.good());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    CHECK(bytes.size() > 100);
    Source source(bytes);
    CVAudio audio;
    std::vector<short> reference;
    for (int round = 0; round < 100; ++round)
    {
        IAudioStream *decoder = audio.CreateMP3StreamDecoder(&source);
        CHECK(decoder != nullptr);
        CHECK(decoder->GetOutputBits() == 16);
        CHECK(decoder->GetOutputRate() == 44100);
        CHECK(decoder->GetOutputChannels() == 2);
        auto decoded = Decode(*decoder);
        CHECK(decoded.size() > 1000);
        CHECK(std::any_of(decoded.begin(), decoded.end(), [](short value) { return value != 0; }));
        if (round == 0) reference = decoded;
        CHECK(reference == decoded);
        audio.DestroyMP3StreamDecoder(decoder);
    }
#ifndef BASELINE_LIFETIME_ONLY
    CHECK(audio.CreateMP3StreamDecoder(nullptr) == nullptr);
    CMiniMP3 reusable;
    for (int round = 0; round < 20; ++round)
    {
        CHECK(reusable.Init(&source));
        CHECK(Decode(reusable) == reference);
        CHECK(!reusable.Init(nullptr));
    }
    for (int length : {0, 1, 10, 64, 1024})
    {
        Source garbage(std::vector<unsigned char>(length, 0x5a));
        for (int round = 0; round < 5; ++round)
        {
            IAudioStream *decoder = audio.CreateMP3StreamDecoder(&garbage);
            if (decoder) { CHECK(Decode(*decoder).empty()); audio.DestroyMP3StreamDecoder(decoder); }
        }
    }
    for (size_t length = 0; length < bytes.size(); length += 31)
    {
        Source truncated(std::vector<unsigned char>(bytes.begin(), bytes.begin() + length));
        IAudioStream *decoder = audio.CreateMP3StreamDecoder(&truncated);
        if (decoder) { Decode(*decoder); audio.DestroyMP3StreamDecoder(decoder); }
    }
    audio.DestroyMP3StreamDecoder(nullptr);
#endif
    std::printf("PASS real MP3 decoding and lifetime: %d checks\n", checks);
}
