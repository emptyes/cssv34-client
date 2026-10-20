#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define VPROF(...) ((void)0)
#define Q_snprintf std::snprintf
using std::fabs;
enum { FCVAR_HIDDEN_FROM_SERVER = 1, FCVAR_SERVER_CANNOT_QUERY = 2, FCVAR_NEVER_AS_STRING = 4 };
enum EQueryCvarValueStatus { eQueryCvarValueStatus_ValueIntact, eQueryCvarValueStatus_CvarNotFound,
    eQueryCvarValueStatus_NotACvar, eQueryCvarValueStatus_CvarProtected };
struct ConVar {
    int flags = 0; bool enabled = false; float number = 0; std::string value;
    bool IsFlagSet(int flag) const { return (flags & flag) != 0; }
    bool GetBool() const { return enabled; }
    float GetFloat() const { return number; }
    int GetInt() const { return static_cast<int>(number); }
    const char *GetString() const { return value.c_str(); }
};
static ConVar cl_query_trace;
struct SVC_GetCvarValue { int m_iCookie; const char *m_szCvarName; };
struct CLC_RespondCvarValue {
    int m_iCookie; const char *m_szCvarName; const char *m_szCvarValue; EQueryCvarValueStatus m_eStatusCode;
};
struct Registry {
    ConVar *var = nullptr; bool command = false;
    const ConVar *FindVar(const char *) const { return var; }
    const void *FindCommand(const char *) const { return command ? this : nullptr; }
};
static Registry registry;
static Registry *g_pCVar = &registry;
struct ClientModManager {
    int calls = 0;
    void FillRespondCvarValue(SVC_GetCvarValue *, CLC_RespondCvarValue &) { ++calls; }
};
static ClientModManager manager;
static ClientModManager *g_pClientModManager = &manager;
struct Channel {
    bool accept = true, playback = false; int calls = 0, cookie = 0; EQueryCvarValueStatus status;
    std::string name, value;
    bool SendNetMsg(CLC_RespondCvarValue &msg) {
        ++calls; cookie = msg.m_iCookie; status = msg.m_eStatusCode;
        name = msg.m_szCvarName; value = msg.m_szCvarValue; return accept;
    }
    bool IsPlayback() const { return playback; }
};
static std::vector<std::string> messages, warnings;
static std::string Format(const char *format, va_list args) {
    char output[2048]; std::vsnprintf(output, sizeof(output), format, args); return output;
}
static void Msg(const char *format, ...) {
    va_list args; va_start(args, format); messages.push_back(Format(format, args)); va_end(args);
}
static void Warning(const char *format, ...) {
    va_list args; va_start(args, format); warnings.push_back(Format(format, args)); va_end(args);
}
struct CBaseClientState {
    Channel *m_NetChannel;
    bool ProcessGetCvarValue(SVC_GetCvarValue *);
};
#include "query_under_test.inc"
static void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    int count = 0;
    for (int branch = 0; branch < 7; ++branch)
        for (bool trace : {false, true})
            for (bool accept : {false, true})
                for (bool playback : {false, true})
                    for (int cookie : {-2147483647 - 1, -1080903, -1, 0, 2147483647}) {
                        ConVar var; var.value = "private-value-not-for-log";
                        registry = Registry(); manager = ClientModManager();
                        cl_query_trace.enabled = trace; messages.clear(); warnings.clear();
                        EQueryCvarValueStatus expected = eQueryCvarValueStatus_ValueIntact;
                        std::string expectedValue = var.value;
                        if (branch == 0) registry.var = &var;
                        if (branch == 1) { registry.var = &var; var.flags = FCVAR_HIDDEN_FROM_SERVER;
                            expected = eQueryCvarValueStatus_CvarNotFound; expectedValue.clear(); }
                        if (branch == 2) { registry.var = &var; var.flags = FCVAR_SERVER_CANNOT_QUERY;
                            expected = eQueryCvarValueStatus_CvarProtected; expectedValue.clear(); }
                        if (branch == 3) { registry.var = &var; var.flags = FCVAR_NEVER_AS_STRING;
                            var.number = 66; expectedValue = "66"; }
                        if (branch == 4) { registry.var = &var; var.flags = FCVAR_NEVER_AS_STRING;
                            var.number = 0.5f; expectedValue = "0.500000"; }
                        if (branch == 5) { registry.command = true;
                            expected = eQueryCvarValueStatus_NotACvar; expectedValue.clear(); }
                        if (branch == 6) { expected = eQueryCvarValueStatus_CvarNotFound; expectedValue.clear(); }
                        Channel channel; channel.accept = accept; channel.playback = playback;
                        CBaseClientState state; state.m_NetChannel = &channel;
                        SVC_GetCvarValue request = {cookie, "cl_mouseenable"};
                        Require(state.ProcessGetCvarValue(&request), "handler return changed");
                        Require(channel.calls == 1 && manager.calls == 1, "response count");
                        Require(channel.cookie == cookie && channel.name == request.m_szCvarName, "request identity");
                        Require(channel.status == expected && channel.value == expectedValue, "response changed");
                        Require(messages.size() == (trace ? 1u : 0u), "trace default or count");
                        Require(warnings.size() == (accept ? 0u : 1u), "failure warning count");
                        for (const auto &line : messages) {
                            Require(line.find(var.value) == std::string::npos, "value leaked to trace");
                            Require(line.find("cookie=" + std::to_string(cookie)) != std::string::npos, "trace cookie");
                            Require(line.find("status=" + std::to_string(expected)) != std::string::npos, "trace status");
                            Require(line.find(accept ? "queued=1" : "queued=0") != std::string::npos, "trace queue result");
                            Require(line.find(playback ? "playback=1" : "playback=0") != std::string::npos, "trace playback");
                        }
                        for (const auto &line : warnings)
                            Require(line.find(var.value) == std::string::npos, "value leaked to warning");
                        ++count;
                    }
    std::cout << "PASS cvar query handler cases: " << count << '\n';
}
