#include <arpa/inet.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class bf_read;
class bf_write;
using uint = unsigned int;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
static uint32 BigLong(uint32 value) { return htonl(value); }
static uint32 BigDWord(uint32 value) { return htonl(value); }
static uint16 BigShort(uint16 value) { return htons(value); }
static void CopyString(char *dest, const char *source, size_t size)
{
    if (size) { std::strncpy(dest, source, size - 1); dest[size - 1] = '\0'; }
}
#define V_strncpy CopyString
#define Q_strncpy CopyString
#define V_snprintf std::snprintf
#include "address_under_test.inc"

class KeyValues
{
public:
    std::string name;
    std::map<std::string, std::string> strings;
    std::map<std::string, int> integers;
    std::vector<std::unique_ptr<KeyValues>> children;
    KeyValues *next = nullptr;
    explicit KeyValues(const char *value) : name(value) {}
    KeyValues(const char *value, const char *k1, int v1, const char *k2, int v2)
        : name(value) { SetInt(k1, v1); SetInt(k2, v2); }
    int GetInt(const char *key, int fallback = 0) const
    {
        auto it = integers.find(key); return it == integers.end() ? fallback : it->second;
    }
    void SetInt(const char *key, int value) { integers[key] = value; }
    const char *GetString(const char *key, const char *fallback = "") const
    {
        auto it = strings.find(key); return it == strings.end() ? fallback : it->second.c_str();
    }
    void SetString(const char *key, const char *value) { strings[key] = value; }
    void AddSubKey(KeyValues *child)
    {
        if (!children.empty()) children.back()->next = child;
        children.emplace_back(child);
    }
    KeyValues *GetFirstTrueSubKey() { return children.empty() ? nullptr : children.front().get(); }
    KeyValues *GetNextTrueSubKey() { return next; }
    void RemoveSubKey(KeyValues *child)
    {
        for (auto it = children.begin(); it != children.end(); ++it)
            if (it->get() == child) { it->release(); children.erase(it); break; }
        for (size_t i = 0; i < children.size(); ++i)
            children[i]->next = i + 1 < children.size() ? children[i + 1].get() : nullptr;
    }
    void deleteThis() { delete this; }
};

struct Page
{
    int loads = 0, refreshes = 0, connects = 0, disconnects = 0;
    bool quick = false;
    void LoadHistoryList() { ++loads; }
    void StartRefresh() { ++refreshes; }
    void OnConnectToGame() { ++connects; }
    void OnDisconnectFromGame() { ++disconnects; }
    bool IsQuickListButtonChecked() const { return quick; }
};
struct TabPanel { Page *active = nullptr; Page *GetActivePage() { return active; } };
struct SteamAddress
{
    uint32 ip;
    uint16 query, connection;
    void SetIP(uint32 value) { ip = value; }
    void SetQueryPort(uint16 value) { query = value; }
    void SetConnectionPort(uint16 value) { connection = value; }
};
struct gameserveritem_t { SteamAddress m_NetAdr; };
namespace vgui
{
struct Panel { int GetVPanel() const { return 1; } };
struct VGui
{
    int posted = 0;
    void PostMessage(int, KeyValues *message, void *) { ++posted; delete message; }
};
static VGui gui;
static VGui *ivgui() { return &gui; }
}
struct Dialogs : std::vector<vgui::Panel *> { int Count() const { return static_cast<int>(size()); } };
struct QuickSetting { int value = 0; void SetValue(int v) { value = v; } };
static QuickSetting sb_quick_list_bit_field;
static bool GameSupportsReplay() { return false; }
struct ConVarRef
{
    explicit ConVarRef(const char *) {}
    bool IsValid() const { return false; }
    bool GetBool() const { return false; }
};
static const int MAX_SERVER_NAME = 256;
static const int MAX_HISTORY_SERVERS = 256;

struct CServerBrowserDialog
{
    netadr_t m_CurrentServerAdr;
    gameserveritem_t m_CurrentConnection = {};
    bool m_bCurrentlyConnected = false, visible = false;
    KeyValues history{"History"}, favorites{"Favorites"};
    KeyValues *m_pHistoryData = &history, *m_pFavoritesData = &favorites;
    Page historyPage, favoritesPage, lanPage, internetPage;
    Page *m_pHistory = &historyPage, *m_pFavorites = &favoritesPage;
    Page *m_pLanGames = &lanPage, *m_pInternetGames = &internetPage;
    TabPanel tabs;
    TabPanel *m_pTabPanel = &tabs;
    Dialogs m_GameInfoDialogs;
    int saves = 0;
    std::map<std::string, std::pair<std::string, int>> saved;
    CServerBrowserDialog() { m_CurrentServerAdr.Clear(); }
    bool IsVisible() const { return visible; }
    void OnClose() {}
    bool IsConnectedToServer() const { return m_bCurrentlyConnected && m_CurrentServerAdr.IsValid(); }
    const char *FindServerName(const netadr_t &) { return "Test server"; }
    KeyValues *FindSavedServer(KeyValues *list, const netadr_t &address)
    {
        if (!list) return nullptr;
        std::string key = address.ToString();
        for (auto &entry : list->children)
            if (key == entry->GetString("address")) return entry.get();
        return nullptr;
    }
    void SaveServerLists()
    {
        ++saves;
        saved.clear();
        for (auto &entry : history.children)
            saved[entry->GetString("address")] = {entry->GetString("name"), entry->GetInt("lastplayed")};
    }
    void OnConnectToGame(KeyValues *);
    void OnDisconnectFromGame();
    void AddServerToHistory(const netadr_t &, const char *);
};
#include "history_under_test.inc"

static unsigned checks = 0;
static void Check(bool condition, const char *message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static void Connect(CServerBrowserDialog &dialog, uint32 hostIP, uint16 port, int query = 0)
{
    KeyValues event("ConnectedToGame");
    event.SetInt("ip", static_cast<int>(htonl(hostIP)));
    event.SetInt("connectionport", static_cast<short>(port));
    event.SetInt("queryport", query);
    dialog.OnConnectToGame(&event);
}
static void Verify(CServerBrowserDialog &dialog, uint32 ip, uint16 port)
{
    Check(dialog.IsConnectedToServer(), "current server remains invalid after connection");
    Check(dialog.m_CurrentServerAdr.GetType() == NA_IP, "current address is not NA_IP");
    Check(dialog.m_CurrentServerAdr.GetIPHostByteOrder() == ip, "IP byte order changed");
    Check(dialog.m_CurrentServerAdr.GetPort() == port, "connection port changed");
    auto *entry = dialog.FindSavedServer(dialog.m_pHistoryData, dialog.m_CurrentServerAdr);
    Check(entry != nullptr, "connected server was not added to history");
    Check(entry->GetInt("lastplayed") > 0, "history timestamp missing");
    Check(std::string(entry->GetString("name")) == "Test server", "server name lost");
    Check(dialog.saved.count(dialog.m_CurrentServerAdr.ToString()) == 1, "save hook not reached");
}
int main()
{
    try
    {
        const uint32 ips[] = {0x01020304u, 0xcb007107u, 0xc0a80102u, 0x0a000001u, 0xffffffffu};
        const uint16 ports[] = {1, 27015, 32767, 32768, 65535};
        for (uint32 ip : ips) for (uint16 port : ports)
        {
            CServerBrowserDialog dialog;
            Check(!dialog.m_CurrentServerAdr.IsValid(), "initial address must be cleared");
            Connect(dialog, ip, port);
            Verify(dialog, ip, port);
            Check(dialog.m_CurrentConnection.m_NetAdr.query == port, "query port fallback failed");
            Check(dialog.history.children.size() == 1, "unexpected initial history size");
            Connect(dialog, ip, port, 28015);
            Verify(dialog, ip, port);
            Check(dialog.history.children.size() == 1, "reconnect duplicated history");
            Check(dialog.m_CurrentConnection.m_NetAdr.query == 28015, "explicit query port lost");
            dialog.OnDisconnectFromGame();
            Check(!dialog.IsConnectedToServer(), "disconnect left current server active");
            Check(dialog.m_CurrentServerAdr.GetType() == NA_NULL, "disconnect did not clear address");
            Connect(dialog, ip, port);
            Verify(dialog, ip, port);
            Check(dialog.history.children.size() == 1, "disconnect reconnect duplicated history");
        }
        CServerBrowserDialog dialog;
        Connect(dialog, 0, 27015);
        Connect(dialog, 0x01020304u, 0);
        Check(dialog.history.children.empty() && dialog.saves == 0, "incomplete event saved history");
        Connect(dialog, 0x7f000001u, 27015);
        Check(dialog.history.children.empty(), "localhost added to history");
        Check(dialog.IsConnectedToServer(), "localhost current address invalid");
        dialog.OnDisconnectFromGame();
        dialog.visible = true;
        dialog.tabs.active = &dialog.historyPage;
        Connect(dialog, 0x01020304u, 27015);
        Connect(dialog, 0x01020305u, 27016);
        Check(dialog.history.children.size() == 2, "different servers merged");
        Check(dialog.historyPage.refreshes == 2, "visible history not refreshed");
        auto *favorite = new KeyValues("server");
        favorite->SetString("address", dialog.m_CurrentServerAdr.ToString());
        favorite->SetString("name", "Saved favorite");
        dialog.favorites.AddSubKey(favorite);
        Connect(dialog, 0x01020305u, 27016);
        Check(favorite->GetInt("lastplayed") > 0, "favorite timestamp not updated");
        Check(std::string(favorite->GetString("name")) == "Saved favorite", "favorite name changed");
        for (int i = 1; i <= MAX_HISTORY_SERVERS + 10; ++i)
            Connect(dialog, 0x0a000000u + static_cast<uint32>(i), 27015);
        Check(dialog.history.children.size() == MAX_HISTORY_SERVERS, "history limit exceeded");
        Check(dialog.saved.size() == MAX_HISTORY_SERVERS, "save hook contains excess history");
        std::cout << "PASS history connection regression: " << checks << " checks\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
