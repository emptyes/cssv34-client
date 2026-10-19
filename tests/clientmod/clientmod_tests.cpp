#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

struct player_info_t
{
    char name[32];
    int userID;
    char guid[33];
    uint32_t friendsID;
    char friendsName[32];
    bool fakeplayer;
    bool ishltv;
};

static int checks;
#define CHECK(expression) do { ++checks; if (!(expression)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); std::exit(1); } } while (0)
struct Engine
{
    player_info_t players[65];
    bool present[65];
    int calls[65];
    bool GetPlayerInfo(int slot, player_info_t *result)
    {
        CHECK(slot >= 1 && slot <= 64);
        ++calls[slot];
        if (!present[slot])
            return false;
        *result = players[slot];
        return true;
    }
};
static Engine storage;
static Engine *engine = &storage;
struct Globals { int maxClients; } globals;
static Globals *gpGlobals = &globals;
static std::string printed;
static void Msg(const char *format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    int length = std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    CHECK(length >= 0 && length < (int)sizeof(text));
    printed += text;
}
static int Q_strcmp(const char *a, const char *b) { return std::strcmp(a, b); }
struct CCommand
{
    std::vector<std::string> values;
    CCommand(std::initializer_list<const char *> input) : values(input.begin(), input.end()) {}
    int ArgC() const { return (int)values.size(); }
    const char *operator[](int index) const { return values.at(index).c_str(); }
};
#define CON_COMMAND(name, description) static void name(const CCommand &args)
#include "clientmod_under_test.inc"

static void Reset(int maxClients = 64)
{
    std::memset(&storage, 0, sizeof(storage));
    globals.maxClients = maxClients;
    printed.clear();
}
static void Add(int slot, bool marker, uint32_t id = 0)
{
    storage.present[slot] = true;
    player_info_t &pi = storage.players[slot];
    pi.userID = slot + 100;
    pi.friendsID = id;
    pi.friendsName[0] = marker ? 8 : 0;
    std::snprintf(pi.name, sizeof(pi.name), "Player%d", slot);
}
static bool Contains(const char *text) { return printed.find(text) != std::string::npos; }

int main()
{
    Reset();
    player_info_t pi = {};
    const uint32_t ids[] = {0, 1, 0x80000000u, 0xffffffffu};
    for (uint32_t id : ids)
        for (int marker = 0; marker < 256; ++marker)
            for (int flags = 0; flags < 4; ++flags)
            {
                pi.friendsID = id;
                pi.friendsName[0] = (char)marker;
                pi.fakeplayer = (flags & 1) != 0;
                pi.ishltv = (flags & 2) != 0;
                CHECK(HasClientModMarker(pi) == (marker == 8 && flags == 0));
            }
    Add(1, true);
    CHECK(IsClientModPlayer(1));
    CHECK(!IsClientModPlayer(2));
    Add(1, false, 1234);
    CHECK(!IsClientModPlayer(1));
    Add(1, true);
    storage.players[1].fakeplayer = true;
    CHECK(!IsClientModPlayer(1));
    storage.players[1].fakeplayer = false;
    storage.players[1].ishltv = true;
    CHECK(!IsClientModPlayer(1));
    Reset();
    Add(1, true);
    Add(3, true, 0xffffffffu);
    Add(4, false, 88);
    Add(5, true);
    storage.players[5].fakeplayer = true;
    Add(6, true);
    storage.players[6].ishltv = true;
    Add(64, false);
    cl_clientmod_list(CCommand{"cl_clientmod_list"});
    CHECK(Contains("ClientMod markers: 2 / 4; Unknown: 2"));
    CHECK(Contains("Unknown"));
    CHECK(!Contains("userid="));
    CHECK(!Contains("Original"));
    CHECK(!Contains("Player5\n"));
    CHECK(!Contains("Player6\n"));
    for (int slot = 1; slot <= 64; ++slot)
        CHECK(storage.calls[slot] == 1);
    printed.clear();
    storage.players[1].friendsName[1] = (char)0xff;
    storage.players[1].friendsName[2] = (char)0x80;
    std::memset(storage.players[1].name, '%', sizeof(storage.players[1].name));
    cl_clientmod_list(CCommand{"cl_clientmod_list", "1"});
    CHECK(Contains("friendsID=0 friendsName[0..7]=08 FF 80 00 00 00 00 00"));
    CHECK(Contains("friendsID=4294967295"));
    CHECK(Contains("userid=101"));
    CHECK(Contains(std::string(32, '%').c_str()));
    for (int slot = 1; slot <= 64; ++slot)
        CHECK(storage.calls[slot] == 2);
    Reset(1);
    Add(1, true);
    cl_clientmod_list(CCommand{"cl_clientmod_list"});
    CHECK(Contains("ClientMod markers: 1 / 1; Unknown: 0"));
    storage.present[1] = false;
    printed.clear();
    cl_clientmod_list(CCommand{"cl_clientmod_list"});
    CHECK(Contains("ClientMod markers: 0 / 0; Unknown: 0"));
    Add(1, false);
    printed.clear();
    cl_clientmod_list(CCommand{"cl_clientmod_list"});
    CHECK(Contains("ClientMod markers: 0 / 1; Unknown: 1"));
    Reset(0);
    cl_clientmod_list(CCommand{"cl_clientmod_list", "1"});
    CHECK(Contains("ClientMod markers: 0 / 0; Unknown: 0"));
    const CCommand invalid[] = {CCommand{"cl_clientmod_list", "0"},
        CCommand{"cl_clientmod_list", "01"}, CCommand{"cl_clientmod_list", "1", "extra"}};
    for (const CCommand &args : invalid)
    {
        Reset(64);
        cl_clientmod_list(args);
        CHECK(printed == "Usage: cl_clientmod_list [1]\n");
        for (int slot = 1; slot <= 64; ++slot)
            CHECK(storage.calls[slot] == 0);
    }
    std::printf("PASS: %d checks\n", checks);
}
