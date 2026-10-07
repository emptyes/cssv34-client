#include "GameEventManager.h"
#include "net_ws_headers.h"
#include "clientmodmgr.h"
#include "server.h"

class CClientModManager : public IClientModManager
{
public:
	CClientModManager();
	~CClientModManager();

	// Check for exploits
	virtual bool CheckFragment(uint8 cmd, bf_read& buf, bf_read& fallback);

	// Add clientmod cvars to ConVar Update Message
	virtual void FillConVarUpdateMsg(NET_SetConVar* cvarMsg);

	// Fill clc_RespondCvarValue
	virtual void FillRespondCvarValue(SVC_GetCvarValue* inMsg, CLC_RespondCvarValue& returnMsg);

private:
	const char* client_major_version = "2.0";
	const char* client_version = "3.0.1.1943";
	const char* connect_method = "8";
};

ConVar cm_enabled("cm_enabled", "1", FCVAR_HIDDEN_FROM_SERVER, "Enable/Disable clientmod emulation");

static CClientModManager s_mgr;
IClientModManager* g_pClientModManager = (IClientModManager*)&s_mgr;

CClientModManager::CClientModManager() {
	// nothing to do here
}

CClientModManager::~CClientModManager() {
	// nothing to do here
}

// Check for exploits
// buf/fallback point right after the message type. Returning false means the
// message is dropped, so buf must be left exactly at the start of the next message.
bool CClientModManager::CheckFragment(uint8 cmd, bf_read& buf, bf_read& fallback) 
{
	if (cmd == svc_GameEvent)
	{
		// SVC_GameEvent: length (NETMSG_LENGTH_BITS) + event data (length bits)
		int length = buf.ReadUBitLong(NETMSG_LENGTH_BITS);
		if (buf.IsOverflowed() || length > buf.GetNumBitsLeft())
		{
			buf = fallback; // let ReadFromBuffer report the broken message
			return true;
		}

		// inspect the event in a separate reader, never past this message
		bf_read event = buf;
		const int dataEndBit = buf.GetNumBitsRead() + length;
		int eventid = event.ReadUBitLong(MAX_EVENT_BITS);
		CGameEventDescriptor* descriptor = g_GameEventManager.GetEventDescriptor(eventid);
		const char* name = descriptor ? descriptor->name : NULL; // unknown event id: avoid null dereference crash

		//DevMsg("svc_GameEvent: %s (%d)\n", name, eventid);

		bool reject = false;

		if (name && !strcmp(name, "player_disconnect"))
		{
			short userid = (short)event.ReadWord();
			char reason[1024];
			event.ReadString(reason, sizeof(reason));
			char playername[1024];
			event.ReadString(playername, sizeof(playername));
			char networkid[1024];
			event.ReadString(networkid, sizeof(networkid));
			//DevMsg("player_disconnect %d name %s reason %s networkid %s\n", userid, playername, reason, networkid);

			V_strlower(playername);

			if (userid < 1 || strstr(playername, "unconnected"))
				reject = true;
		}
		else if (name && !strcmp(name, "player_info"))
		{
			char databuf[1024];
			event.ReadString(databuf, sizeof(databuf));

			if (strstr(databuf, "{}") && strstr(databuf, "?"))
			{
				//DevMsg("player_info buffer %s\n", databuf);
				reject = true;
			}
		}

		if (reject)
		{
			// skip the whole message, so the next one is parsed from the right bit
			buf.Seek(dataEndBit);
			return false;
		}

		buf = fallback;
	}

	// svc_UserMessage is not inspected here: buf is left untouched.
	// (the old check "msgType < 0" could never be true for an unsigned byte,
	// and its assert did not compile in debug builds)

	if (!sv.IsActive())
	{
		if (cmd == svc_Menu)
		{
			short Type = (short)buf.ReadUBitLong(16);
			auto dataLength = buf.ReadUBitLong(16);
			// Skip the payload without copying it: dataLength comes from the server
			// (up to 65535 bytes) and used to overflow a 4096-byte stack buffer.
			buf.SeekRelative(dataLength * 8);
			//DevMsg("svc_Menu Rejected: type %d dataLength %d\n", Type, dataLength);
			return false;
		}
	}

	return true;
}

// Add clientmod cvars to ConVar Update Message
void CClientModManager::FillConVarUpdateMsg(NET_SetConVar* cvarMsg) 
{
	if (!cm_enabled.GetBool())
		return;

	// Hardcoded clientmod cvars
	NET_SetConVar::cvar_t cmcvar;

	Q_strncpy(cmcvar.name, "~clientmod", MAX_OSPATH);
	Q_strncpy(cmcvar.value, client_major_version, MAX_OSPATH);
	cvarMsg->m_ConVars.AddToTail(cmcvar);

	Q_strncpy(cmcvar.name, "_client_version", MAX_OSPATH);
	Q_strncpy(cmcvar.value, client_version, MAX_OSPATH);
	cvarMsg->m_ConVars.AddToTail(cmcvar);

	Q_strncpy(cmcvar.name, "_connectmethod", MAX_OSPATH);
	Q_strncpy(cmcvar.value, connect_method, MAX_OSPATH);
	cvarMsg->m_ConVars.AddToTail(cmcvar);
}

// Fill clc_RespondCvarValue
void CClientModManager::FillRespondCvarValue(SVC_GetCvarValue* inMsg, CLC_RespondCvarValue& returnMsg) 
{
	if (!cm_enabled.GetBool())
		return;

	if (!strcmp(inMsg->m_szCvarName, "~clientmod"))
	{
		returnMsg.m_eStatusCode = eQueryCvarValueStatus_ValueIntact;
		returnMsg.m_szCvarValue = client_major_version;
	}
	else if (!strcmp(inMsg->m_szCvarName, "_client_version"))
	{
		returnMsg.m_eStatusCode = eQueryCvarValueStatus_ValueIntact;
		returnMsg.m_szCvarValue = client_version;
	}
	else if (!strcmp(inMsg->m_szCvarName, "_connectmethod"))
	{
		returnMsg.m_eStatusCode = eQueryCvarValueStatus_ValueIntact;
		returnMsg.m_szCvarValue = connect_method;
	}
}