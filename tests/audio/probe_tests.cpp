#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <strings.h>
#include <vector>

static int checks;
#define CHECK(e) do { ++checks; if (!(e)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); std::exit(1); } } while (0)
typedef long long intp;
static std::string output;
static void Msg(const char *format, ...)
{
    char text[2048]; va_list args; va_start(args, format);
    vsnprintf(text, sizeof(text), format, args); va_end(args); output += text;
}
static int Q_strlen(const char *s) { return (int)std::strlen(s); }
static const char *Q_strstr(const char *a, const char *b) { return std::strstr(a, b); }
static int Q_stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
struct Command
{
    std::vector<std::string> values;
    int ArgC() const { return (int)values.size(); }
    const char *operator[](int i) const { return values.at(i).c_str(); }
};
#define CON_COMMAND(name, description) static void name(const Command &args)
struct IO
{
    std::map<std::string, std::vector<unsigned char>> files;
    const std::vector<unsigned char> *active = nullptr;
    unsigned int position = 0;
    int opens = 0, closes = 0;
    intp invalid = 0;
    intp open(const char *name)
    {
        CHECK(!active);
        auto entry = files.find(name);
        if (entry == files.end()) return invalid;
        active = &entry->second; position = 0; ++opens;
        return 0x100000001LL;
    }
    void close(intp handle) { CHECK(handle == 0x100000001LL && active); active = nullptr; ++closes; }
    unsigned int size(intp handle) { CHECK(handle == 0x100000001LL && active); return (unsigned int)active->size(); }
    void seek(intp handle, unsigned int offset) { CHECK(handle == 0x100000001LL && active); position = offset; }
    int read(void *buffer, int requested, intp handle)
    {
        CHECK(handle == 0x100000001LL && active && requested >= 0);
        if (position >= active->size()) return 0;
        int count = (int)std::min((size_t)requested, active->size() - position);
        std::memcpy(buffer, active->data() + position, count); position += count; return count;
    }
};
static IO io;
static IO *g_pSndIO = &io;
#include "probe_under_test.inc"

static std::string Probe(const std::vector<std::string> &args)
{
    output.clear(); snd_probe_mp3(Command{args});
    CHECK(!io.active && io.opens == io.closes); return output;
}
int main()
{
    CHECK(Probe({"snd_probe_mp3"}).find("Usage:") != std::string::npos);
    for (const char *name : {"a", "x.wav", "/x.mp3", "\\x.mp3", "a/../x.mp3", "C:x.mp3"})
        CHECK(Probe({"snd_probe_mp3", name}).find("expected a relative") != std::string::npos);
    CHECK(Probe({"snd_probe_mp3", std::string(256, 'a') + ".mp3"}).find("expected a relative") != std::string::npos);
    for (intp invalid : {0LL, -1LL})
    {
        io.invalid = invalid;
        CHECK(Probe({"snd_probe_mp3", "missing.mp3"}).find("unavailable through GAME") != std::string::npos);
    }
    io.files["empty.mp3"] = {};
    CHECK(Probe({"snd_probe_mp3", "empty.mp3"}).find("bytes=0 header_rate=0") != std::string::npos);
    for (int version : {0, 2, 3}) for (int rate = 0; rate < 3; ++rate)
    {
        io.files["tone.MP3"] = {0xff, (unsigned char)(0xe0 | (version << 3) | 2),
                                (unsigned char)(0x90 | (rate << 2)), 0};
        static const int rates[4][3] = {{11025,12000,8000},{0,0,0},{22050,24000,16000},{44100,48000,32000}};
        CHECK(Probe({"snd_probe_mp3", "tone.MP3"}).find("header_rate=" + std::to_string(rates[version][rate])) != std::string::npos);
    }
    io.files["tag.mp3"] = {'I','D','3',4,0,0,0,0,0,3,1,2,3,0xff,0xfb,0x90,0};
    CHECK(Probe({"snd_probe_mp3", "tag.mp3"}).find("header_rate=44100") != std::string::npos);
    io.files["tag.mp3"][5] = 0x10;
    io.files["tag.mp3"].insert(io.files["tag.mp3"].begin() + 13, 10, 0);
    CHECK(Probe({"snd_probe_mp3", "tag.mp3"}).find("header_rate=44100") != std::string::npos);
    io.files["bounded.mp3"] = std::vector<unsigned char>(5000, 0);
    auto &bounded = io.files["bounded.mp3"];
    bounded.insert(bounded.end(), {0xff,0xfb,0x90,0});
    CHECK(Probe({"snd_probe_mp3", "bounded.mp3"}).find("decoding was not tested") != std::string::npos);
    for (auto header : std::vector<std::vector<unsigned char>>{{0xff,0xeb,0x90,0},{0xff,0xf9,0x90,0},{0xff,0xfb,0xfc,0},{0xff,0xfb,0x9c,0}})
    {
        io.files["bad.mp3"] = header;
        CHECK(Probe({"snd_probe_mp3", "bad.mp3"}).find("header_rate=0") != std::string::npos);
    }
    CHECK(MP3_ReadSampleRateFromHeader("missing.mp3") == 0);
    CHECK(!io.active && io.opens == io.closes);
    std::printf("PASS MP3 diagnostic: %d checks\n", checks);
}
