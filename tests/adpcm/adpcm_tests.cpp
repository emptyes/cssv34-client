#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "tier2/waveformat.h"

#define MAX_BLOCK_SIZE 4096
#define AUDIOSOURCE_COPYBUF_SIZE 4096
#define Assert(x) do { if (!(x)) throw std::runtime_error("assert"); } while (0)

#pragma pack(push, 1)
struct WAVEFORMATEX {
    uint16_t wFormatTag, nChannels;
    uint32_t nSamplesPerSec, nAvgBytesPerSec;
    uint16_t nBlockAlign, wBitsPerSample, cbSize;
};
struct ADPCMCOEFSET { int16_t iCoef1, iCoef2; };
struct ADPCMWAVEFORMAT {
    WAVEFORMATEX wfx;
    uint16_t wSamplesPerBlock, wNumCoef;
    ADPCMCOEFSET aCoef[1];
};
#pragma pack(pop)

struct CAudioSource {
    virtual ~CAudioSource() {}
    virtual int ZeroCrossingAfter(int value) { return value; }
};

struct CAudioSourceWave : CAudioSource {
    std::vector<unsigned char> format;
    std::vector<unsigned char> bytes;
    int loopStart = -1;
    void *GetHeader() { return format.empty() ? nullptr : format.data(); }
    int DataSize() const { return static_cast<int>(bytes.size()); }
    int GetLoopingInfo(int *, int *, int *) { return loopStart; }
    int ConvertLoopedPosition(int value) {
        if (loopStart >= 0 && value >= static_cast<int>(bytes.size())) {
            int span = static_cast<int>(bytes.size()) - loopStart;
            if (span > 0) value = loopStart + (value - loopStart) % span;
        }
        return value;
    }
};

struct IWaveData {
    CAudioSourceWave source;
    int maxRead = INT_MAX;
    int stopAfter = INT_MAX;
    virtual ~IWaveData() {}
    virtual CAudioSource &Source() { return source; }
    virtual int ReadSourceData(void **out, int offset, int count, char *) {
        if (offset < 0 || count < 0 || offset > static_cast<int>(source.bytes.size())) return -1;
        if (offset >= stopAfter) { *out = nullptr; return 0; }
        int available = std::min(count, static_cast<int>(source.bytes.size()) - offset);
        available = std::min(available, maxRead);
        available = std::min(available, stopAfter - offset);
        *out = available ? source.bytes.data() + offset : nullptr;
        return available;
    }
};

struct CAudioMixerWave {
    IWaveData *m_pData;
    int m_sample_max_loaded = 0;
    int m_sample_loaded_index = 0;
    explicit CAudioMixerWave(IWaveData *data) : m_pData(data) {}
    void SetSampleStart(int value) {
        value = m_pData->Source().ZeroCrossingAfter(value);
        m_sample_loaded_index = value;
        m_sample_max_loaded = value + 1;
    }
};

struct CAudioMixerWaveADPCM : CAudioMixerWave {
    explicit CAudioMixerWaveADPCM(IWaveData *);
    ~CAudioMixerWaveADPCM();
    int GetOutputData(void **, int, char[AUDIOSOURCE_COPYBUF_SIZE]);
    void SetSampleStart(int);
    bool DecodeBlock();
    int NumChannels();
    void DecompressBlockMono(short *, const char *, int);
    void DecompressBlockStereo(short *, const char *, int);
    const ADPCMWAVEFORMAT *m_pFormat;
    const ADPCMCOEFSET *m_pCoefficients;
    short *m_pSamples;
    int m_sampleCount;
    int m_samplePosition;
    int m_blockSize;
    int m_offset;
    int m_totalBytes;
};

#include "mixer_under_test.inc"

static void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static void Put16(std::vector<unsigned char> &out, int value) {
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
}

static IWaveData MakeData(int channels = 1, int samples = 4, int coef1 = 256, int coef2 = 0) {
    IWaveData data;
    int block = (samples - 2) * channels / 2 + 7 * channels;
    data.source.format.resize(sizeof(ADPCMWAVEFORMAT));
    auto *format = reinterpret_cast<ADPCMWAVEFORMAT *>(data.source.format.data());
    std::memset(format, 0, sizeof(*format));
    format->wfx.wFormatTag = 2;
    format->wfx.nChannels = channels;
    format->wfx.nBlockAlign = block;
    format->wfx.wBitsPerSample = 4;
    format->wSamplesPerBlock = samples;
    format->wNumCoef = 1;
    format->aCoef[0].iCoef1 = coef1;
    format->aCoef[0].iCoef2 = coef2;
    return data;
}

static std::vector<unsigned char> MonoBlock(int predictor = 0, int delta = 16, int sample1 = 100, int sample2 = -100) {
    std::vector<unsigned char> out;
    out.push_back(predictor);
    Put16(out, delta);
    Put16(out, sample1);
    Put16(out, sample2);
    out.push_back(0);
    return out;
}

static void Seek() {
    auto data = MakeData();
    auto block = MonoBlock();
    data.source.bytes = block;
    data.source.bytes.insert(data.source.bytes.end(), block.begin(), block.end());
    CAudioMixerWaveADPCM mixer(&data);
    mixer.SetSampleStart(3);
    Require(mixer.m_sampleCount == 4 && mixer.m_samplePosition == 3, "ADPCM seek skipped the selected block");
}

static void ShortBlock() {
    for (int size = 0; size < 7; ++size) {
        auto data = MakeData();
        auto block = MonoBlock();
        data.source.bytes.assign(block.begin(), block.begin() + size);
        CAudioMixerWaveADPCM mixer(&data);
        void *output = nullptr;
        Require(mixer.GetOutputData(&output, 4, nullptr) == 0, "short ADPCM header decoded");
    }
    auto data = MakeData();
    auto block = MonoBlock();
    data.source.bytes.assign(block.begin(), block.begin() + 7);
    CAudioMixerWaveADPCM mixer(&data);
    void *output = nullptr;
    Require(mixer.GetOutputData(&output, 4, nullptr) == 2, "header-only ADPCM block rejected");

    data = MakeData();
    data.source.bytes = MonoBlock();
    data.maxRead = 3;
    CAudioMixerWaveADPCM chunked(&data);
    Require(chunked.GetOutputData(&output, 4, nullptr) == 4, "chunked ADPCM read failed");

    data = MakeData();
    data.source.bytes = MonoBlock();
    data.maxRead = 3;
    data.stopAfter = 3;
    CAudioMixerWaveADPCM interrupted(&data);
    Require(interrupted.GetOutputData(&output, 4, nullptr) == 0 && interrupted.m_offset == 0, "interrupted ADPCM read was decoded");
}

static void Predictor() {
    auto data = MakeData();
    data.source.bytes = MonoBlock(1);
    CAudioMixerWaveADPCM mixer(&data);
    void *output = nullptr;
    Require(mixer.GetOutputData(&output, 4, nullptr) == 0, "invalid ADPCM predictor decoded");
    Require(mixer.GetOutputData(&output, 4, nullptr) == 0, "invalid ADPCM predictor exposed stale samples");
}

static void Overflow() {
    auto data = MakeData(1, 4, -32768, -32768);
    data.source.bytes = MonoBlock(0, 16, -32768, -32768);
    CAudioMixerWaveADPCM mixer(&data);
    void *output = nullptr;
    Require(mixer.GetOutputData(&output, 4, nullptr) == 4, "extreme ADPCM coefficients rejected");
    auto *samples = static_cast<short *>(output);
    Require(samples[2] == 32767, "ADPCM prediction was not clamped");
}

static void Loop() {
    auto data = MakeData();
    auto first = MonoBlock(0, 16, 100, 10);
    auto second = MonoBlock(0, 16, 200, 20);
    data.source.bytes = first;
    data.source.bytes.insert(data.source.bytes.end(), second.begin(), second.end());
    data.source.loopStart = 5;
    CAudioMixerWaveADPCM mixer(&data);
    mixer.m_offset = data.source.DataSize();
    Require(mixer.DecodeBlock(), "looping ADPCM block was not decoded");
    Require(mixer.m_offset == 16 && mixer.m_samplePosition == 1, "ADPCM loop used byte units");
}

int main(int argc, char **argv) {
    Require(argc == 2, "arguments");
    std::string selected = argv[1];
    if (selected == "all" || selected == "seek") Seek();
    if (selected == "all" || selected == "short-block") ShortBlock();
    if (selected == "all" || selected == "predictor") Predictor();
    if (selected == "all" || selected == "overflow") Overflow();
    if (selected == "all" || selected == "loop") Loop();
    std::cout << "PASS ADPCM block bounds, predictors, arithmetic, seeking and looping\n";
}