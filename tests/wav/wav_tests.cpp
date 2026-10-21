#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
using WORD = uint16_t;
using DWORD = uint32_t;
#include "formats.inc"
#include "riff_under_test.inc"
#if __has_include("tier2/waveformat.h")
#include "tier2/waveformat.h"
#endif
static void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
static void Put(std::vector<unsigned char> &out, uint32_t value, int size = 4) { for (int i = 0; i < size; ++i) out.push_back(value >> (8*i)); }
static std::vector<unsigned char> Chunk(uint32_t name, const std::vector<unsigned char> &data) {
    std::vector<unsigned char> out; Put(out, name); Put(out, data.size()); out.insert(out.end(), data.begin(), data.end());
    if (data.size() & 1) out.push_back(0);
    return out;
}
static std::vector<unsigned char> Format(int channels = 2, int bits = 16, unsigned rate = 8000, bool adpcm = false) {
    std::vector<unsigned char> out;
    Put(out, adpcm ? 2 : 1, 2); Put(out, channels, 2); Put(out, rate); Put(out, rate*channels*(bits/8));
    Put(out, adpcm ? 256 : channels*(bits/8), 2); Put(out, bits, 2);
    if (adpcm) { Put(out, 32, 2); Put(out, 2+(256-7*channels)*2/channels, 2); Put(out, 7, 2); for (int i = 0; i < 7; ++i) { Put(out, 256, 2); Put(out, 0, 2); } }
    return out;
}
struct MemoryIO : IFileReadBinary {
    std::vector<unsigned char> data;
    unsigned position = 0;
    int opens = 0, closes = 0;
    bool shortFormat = false, shortStartup = false;
    intp handle = intp(0x1234567887654321ULL);
    intp open(const char *) override { position = 0; ++opens; return handle; }
    int read(void *out, int count, intp file) override {
        Require(file == handle && count >= 0 && position <= data.size() && unsigned(count) <= data.size()-position, "invalid I/O range or x64 handle");
        if ((shortFormat && position == 20) || (shortStartup && opens == 2)) count = std::max(0, count-1);
        if (count) std::memcpy(out, data.data()+position, count);
        position += count; return count;
    }
    void close(intp file) override { Require(file == handle, "close handle"); ++closes; }
    void seek(intp file, int pos) override { Require(file == handle && pos >= 0 && unsigned(pos) <= data.size(), "seek range"); position = pos; }
    unsigned tell(intp file) override { Require(file == handle, "tell handle"); return position; }
    unsigned size(intp file) override { Require(file == handle, "size handle"); return data.size(); }
};
static MemoryIO io;
static IFileReadBinary *g_pSndIO = &io, *g_pFSIOReadBinary = &io;
static void Load(const std::vector<unsigned char> &format, int bytes = 32000) {
    io = MemoryIO(); auto chunks = Chunk(WAVE_FMT, format); auto data = Chunk(WAVE_DATA, std::vector<unsigned char>(bytes, 128)); chunks.insert(chunks.end(), data.begin(), data.end());
    Put(io.data, RIFF_ID); Put(io.data, chunks.size()+4); Put(io.data, RIFF_WAVE); io.data.insert(io.data.end(), chunks.begin(), chunks.end());
}
static void ChunkError(unsigned) {}
static void MaybeReportMissingWav(const char *) {}
void Warning(const char *, ...) {}
static bool IsX360() { return false; }
using std::min;
#define SND_ASYNC_LOOKAHEAD_SECONDS (0.125f)
struct Sfx { const char *GetFileName() { return "test.wav"; } };
struct CAudioSourceWave {
    Sfx sfx; Sfx *m_pSfx = &sfx;
    int m_channels = 0, m_bits = 0, m_rate = 0, m_sampleSize = 0, m_dataStart = 0, m_dataSize = 0, setupCalls = 0, converted = 0;
    bool GetStartupData(void *, int, int &);
    void Setup(char *format, int, IterateRIFF &walk) {
        ++setupCalls; auto *f = reinterpret_cast<WAVEFORMATEX *>(format); m_channels = f->nChannels; m_bits = f->wBitsPerSample; m_rate = f->nSamplesPerSec;
        m_sampleSize = m_channels*(m_bits/8);
        if (f->wFormatTag == WAVE_FORMAT_ADPCM) { m_bits = 16; m_sampleSize = 1; }
        while (walk.ChunkAvailable()) { if (walk.ChunkName() == WAVE_DATA) { m_dataStart = walk.ChunkFilePosition()+8; m_dataSize = walk.ChunkSize(); } walk.ChunkNext(); }
    }
    void ConvertSamples(char *, int count) { Require(count >= 0, "negative sample conversion"); converted += count; }
};
#include "wave_under_test.inc"
#include "utility.inc"
static void Stereo() { Load(Format()); Require(std::fabs(Audio_GetWaveDuration("test")-1.0f) < 0.00001f, "stereo duration doubled"); Require(std::fabs(GetWavSoundDuration("test")-1.0f) < 0.00001f, "tier2 PCM duration"); }
static void ZeroBits() { Load(Format(1, 0)); Require(Audio_GetWaveDuration("test") == 0, "zero bits accepted"); }
static void ZeroChannels() { ADPCMWAVEFORMAT format = {}; format.wSamplesPerBlock = 500; Require(ADPCMSampleCount(&format, 256) == 0, "zero channels accepted"); }
static void UtilityADPCM() { Load(Format(1,4,8000,true),256); Require(std::fabs(GetWavSoundDuration("test")-500.0f/8000) < 0.00001f, "tier2 ADPCM duration lost"); }
static void Reject(const std::vector<unsigned char> &format) {
    Load(format); CAudioSourceWave source; int copied = 123; unsigned char out[64] = {};
    Require(!source.GetStartupData(out, sizeof(out), copied) && copied == 0 && source.setupCalls == 0, "invalid format reached Setup");
    Require(Audio_GetWaveDuration("test") == 0 && GetWavSoundDuration("test") == 0, "invalid format duration");
}
int main(int argc, char **argv) {
    Require(argc == 2, "arguments"); std::string selected = argv[1];
    if (selected == "all" || selected == "pcm-zero-bits") ZeroBits();
    if (selected == "all" || selected == "stereo-duration") Stereo();
    if (selected == "all" || selected == "adpcm-zero-channels") ZeroChannels();
    if (selected == "all" || selected == "tier2-adpcm") UtilityADPCM();
    if (selected != "all") { std::cout << "PASS WAV " << selected << '\n'; return 0; }
    auto pcm = Format();
    for (int size = 0; size < 16; ++size) Reject({pcm.begin(),pcm.begin()+size});
    for (int channels : {0,3,65535}) Reject(Format(channels));
    for (int bits : {0,4,24,32}) Reject(Format(1,bits));
    for (unsigned rate : {0U,0x80000000U,0xffffffffU}) Reject(Format(1,16,rate));
    auto bad = pcm; bad[12] = 0; Reject(bad); bad = pcm; bad[0] = 99; Reject(bad);
    bad = pcm; bad.push_back(0); Reject(bad); bad.push_back(0); bad[16] = 1; Reject(bad);
    auto adpcm = Format(1,4,8000,true);
    for (size_t size = 16; size < adpcm.size(); ++size) Reject({adpcm.begin(),adpcm.begin()+size});
    for (int offset : {12,16,18,20}) { bad = adpcm; bad[offset] = 0; bad[offset+1] = 0; Reject(bad); }
    bad = adpcm; bad[18] = 1; bad[19] = 0; Reject(bad);
    bad = adpcm; bad[12] = 0xff; bad[13] = 0x7f; Reject(bad);
    for (int channels : {1,2}) for (int bits : {8,16}) for (bool extended : {false,true}) {
        auto f = Format(channels,bits); if (extended) Put(f,0,2); Load(f,8000*channels*(bits/8));
        Require(std::fabs(Audio_GetWaveDuration("test")-1) < 0.00001f && std::fabs(GetWavSoundDuration("test")-1) < 0.00001f, "valid PCM duration");
        unsigned char out[128]; int copied; CAudioSourceWave source;
        Require(source.GetStartupData(out,128,copied) && copied == 128 && source.converted == 128/(channels*(bits/8)), "valid PCM startup");
        Require(io.opens == io.closes, "startup leaked handle");
    }
    for (int channels : {1,2}) {
        auto f = Format(channels,4,8000,true); ADPCMWAVEFORMAT header = {}; std::memcpy(&header,f.data(),sizeof(header));
        int samples = 2+(256-7*channels)*2/channels;
        for (int bytes = 0; bytes < 1024; ++bytes) {
            int rem = bytes%256; int expected = (bytes/256)*samples + (rem >= 7*channels ? 2+(rem-7*channels)*2/channels : 0);
            Require(ADPCMSampleCount(&header,bytes) == expected, "partial ADPCM frame count");
            Load(f,bytes); Require(std::fabs(Audio_GetWaveDuration("test")-expected/8000.0f) < 0.00001f && std::fabs(GetWavSoundDuration("test")-expected/8000.0f) < 0.00001f, "ADPCM duration");
        }
        long long expected = static_cast<long long>(INT_MAX/256)*samples;
        int rem = INT_MAX%256;
        if (rem >= 7*channels) expected += 2+(rem-7*channels)*2/channels;
        Require(ADPCMSampleCount(&header,-1) == 0 && ADPCMSampleCount(&header,INT_MAX) == (expected > INT_MAX ? 0 : expected), "ADPCM count overflow");
    }
    Require(ADPCMSampleCount(nullptr,1) == 0, "null ADPCM header");
    Load(pcm); io.shortFormat = true; { CAudioSourceWave s; int n; Require(!s.GetStartupData(nullptr,0,n) && s.setupCalls == 0, "short format read accepted"); }
    Load(pcm); io.shortStartup = true; { CAudioSourceWave s; unsigned char out[64]; int n; Require(!s.GetStartupData(out,64,n) && n == 0 && s.converted == 0, "short startup converted unread data"); }
    Load(pcm); { CAudioSourceWave s; int n; Require(!s.GetStartupData(nullptr,64,n) && n == 0, "null destination"); Require(!s.GetStartupData(nullptr,-1,n), "negative capacity"); Require(s.GetStartupData(nullptr,0,n) && n == 0, "metadata-only startup"); }
    Load(Format(2,16,INT_MAX)); { CAudioSourceWave s; unsigned char out[64]; int n; Require(s.GetStartupData(out,64,n) && n == 64, "startup budget overflow"); }
    std::mt19937 random(0x34025);
    for (int test = 0; test < 10000; ++test) {
        auto f = test%2 ? pcm : adpcm; for (int j = 0; j < 3; ++j) f[random()%f.size()] = random(); Load(f,64);
        float a = Audio_GetWaveDuration("test"), b = GetWavSoundDuration("test"); Require(std::isfinite(a) && a >= 0 && std::isfinite(b) && b >= 0, "non-finite duration");
        CAudioSourceWave s; unsigned char out[64]; int n; s.GetStartupData(out,64,n); Require(n >= 0 && n <= 64, "startup byte count");
    }
    std::cout << "PASS WAV format bounds, PCM/ADPCM durations, partial blocks, startup I/O and 10000 mutated headers\n";
}
