//======177== (C) Copyright 1999, 2000 Valve, L.L.C. All rights reserved. ========
//
// The copyright to the contents herein is the property of Valve, L.L.C.
// The contents may be used and/or copied only with the written permission of
// Valve, L.L.C., or in accordance with the terms and conditions stipulated in
// the agreement/contract under which the contents have been supplied.
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================
#include "quakedef.h"
#include "server.h"
#include "master.h"
#include "proto_oob.h"
#include "host.h"
#include "sys_dll.h"
#include "eiface.h"
#include "utlmap.h"
#include "net_ws_headers.h"
#include "sv_steamauth.h"
#include "tier1/KeyValues.h"
#include "filesystem_engine.h"

extern ConVar sv_tags;
extern ConVar sv_visiblemaxplayers;
extern ConVar sv_lan;

#define S2A_EDF_GAMEPORT 0x80
#define S2A_EDF_STEAMID 0x10
#define S2A_EDF_SOURCETV 0x40
#define S2A_EDF_GAMETAGS 0x20
#define S2A_EDF_GAMEID 0x01

#define RETRY_INFO_REQUEST_TIME 0.4 // seconds
#define MASTER_RESPONSE_TIMEOUT 1.5 // seconds
#define INFO_REQUEST_TIMEOUT 5.0 // seconds

#define LAN_BROADCAST_RETRY_TIME 0.5 // seconds
#define LAN_REFRESH_TIMEOUT 1.5 // seconds
#define LAN_FIRST_PORT 27015
#define LAN_LAST_PORT 27020

// Built-in master server, always queried in addition to cfg/MasterServers.vdf
#define DEFAULT_MASTER_SERVER "78.154.103.37:10232"
#define MASTER_SERVERS_FILE "cfg/MasterServers.vdf"
#define MAX_MASTER_SERVERS 16

#ifdef DEDICATED
#define IsLan() false
#else
#define IsLan() sv_lan.GetInt()
#endif

//-----------------------------------------------------------------------------
// Purpose: Implements the master server interface
//-----------------------------------------------------------------------------
class CMaster : public IMaster, public IServersInfo, public IConnectionlessPacketHandler
{
public:
	CMaster( void );
	virtual ~CMaster( void );

	// Heartbeat functions.
	void Init( void );
	void Shutdown( void );
	// Sets up master address
	void ShutdownConnection(void);

	bool ProcessConnectionlessPacket( netpacket_t *packet );

	void RunFrame();
	void RetryServersInfoRequest();

	void ReplyInfo( const netadr_t &adr );
	newgameserver_t &ProcessInfo( bf_read &buf );

	// SeversInfo
	void RequestInternetServerList( const char *gamedir, IServerListResponse *response );
	void RequestLANServerList( const char *gamedir, IServerListResponse *response );
	void RequestServerInfo( const netadr_t &adr );
	void StopRefresh();

	// master server list (cfg/MasterServers.vdf + built-in default)
	void LoadMasterServers();
	void AddMasterServer( const char *pszAddress );
	bool IsMasterServer( const netadr_t &adr ) const;
	void SendMasterQuery( const netadr_t &master, const char *pszStartAddress );
	void SendMasterQueries();

	// LAN
	void BroadcastLanQuery();

	// a new list request replaces the current one: let the old listener finish its UI state
	void BeginListRefresh( IServerListResponse *response );

	void PingServer(uint32 unIP, uint16 usPort, IServerPingResponse* pRequestServersResponse);
	void PlayerDetails(uint32 unIP, uint16 usPort, IServerPlayersResponse* pRequestServersResponse);
	virtual void RemoveResponse(IServerPingResponse* resp, IServerPlayersResponse* resp2);
private:
	SOCKET m_nSocket;

	bool m_bInitialized;
	bool m_bRefreshing;

	int m_iServersResponded;

	double m_flStartRequestTime;
	double m_flRetryRequestTime;
	double m_flMasterRequestTime;

	char m_szGameDir[256];

	CUtlMap<netadr_t, bool> m_serverAddresses;
	CUtlMap<uint, double> m_serversRequestTime;

	CUtlVector<IServerPingResponse*> m_ServerPings;
	CUtlVector<IServerPlayersResponse*> m_ServerPlayers;

	// servers we've asked for A2S_PLAYER, so a S2C_CHALLENGE reply can be answered correctly
	CUtlMap<netadr_t, bool> m_PendingPlayerRequests;

	CUtlVector<netadr_t> m_MasterServers;

	bool m_bLanRefresh;
	double m_flLanBroadcastTime;

	IServerListResponse *m_serverListResponse;
};

static CMaster s_MasterServer;
IMaster *master = (IMaster *)&s_MasterServer;

IServersInfo *g_pServersInfo = (IServersInfo*)&s_MasterServer;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CMaster, IServersInfo, SERVERLIST_INTERFACE_VERSION, s_MasterServer );

#define	HEARTBEAT_SECONDS	140.0

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CMaster::CMaster( void )
{
	m_bInitialized = false;
	m_iServersResponded = 0;

	m_serverListResponse = NULL;
	SetDefLessFunc( m_serverAddresses );
	SetDefLessFunc( m_serversRequestTime );
	SetDefLessFunc( m_PendingPlayerRequests );
	m_bRefreshing = false;
	m_bLanRefresh = false;
	m_flLanBroadcastTime = 0.0;

	Init();
}

CMaster::~CMaster( void )
{
}

void CMaster::RunFrame()
{
	{
		static sockaddr fromAddr{};
		static char buffer[2048];
		int fromLen = sizeof(fromAddr);
		int nBytes = 0;
#ifndef ANDROID
		// other: read until no packets
		while ((nBytes = recvfrom(m_nSocket, buffer, sizeof(buffer), 0, &fromAddr, &fromLen)) > 0)
#else
		// android: only 1 packet per frame
		if ((nBytes = recvfrom(m_nSocket, buffer, sizeof(buffer), 0, &fromAddr, &fromLen)) > 0)
#endif
		{
			if (*(unsigned int*)buffer == CONNECTIONLESS_HEADER)
			{
				static netpacket_t packet;
				packet.data = (unsigned char*)buffer;
				packet.from.SetFromSockadr(&fromAddr);
				packet.message.StartReading(buffer, nBytes);
				packet.message.ReadLong();
				packet.pNext = nullptr;
				packet.received = net_time;
				packet.size = nBytes;
				packet.source = NS_CLIENT;
				packet.stream = false;
				packet.wiresize = nBytes;
				ProcessConnectionlessPacket(&packet);
			}
		}
	}

	if( !m_bRefreshing )
		return;

	if( m_bLanRefresh )
	{
		if( m_flStartRequestTime < Plat_FloatTime() - LAN_REFRESH_TIMEOUT )
		{
			StopRefresh();
			m_serverListResponse->RefreshComplete( NServerResponse::nServerResponded );
			return;
		}

		// UDP broadcast can be lost, send it once more
		if( m_flRetryRequestTime < Plat_FloatTime() - LAN_BROADCAST_RETRY_TIME )
		{
			m_flRetryRequestTime = Plat_FloatTime();
			BroadcastLanQuery();
		}
		return;
	}

	if( m_serverListResponse &&
		m_flStartRequestTime < Plat_FloatTime()-INFO_REQUEST_TIMEOUT )
	{
		// partial results are still a valid list, only report a failure when the masters never answered
		NServerResponse response = NServerResponse::nServerResponded;
		if( m_serverAddresses.Count() == 0 )
			response = NServerResponse::nServerFailedToRespond;

		StopRefresh();
		m_serverListResponse->RefreshComplete( response );
		return;
	}

	if( m_iServersResponded > 0 &&
			m_iServersResponded >= m_serverAddresses.Count() &&
			m_flMasterRequestTime < Plat_FloatTime() - MASTER_RESPONSE_TIMEOUT )
	{
		StopRefresh();
		m_serverListResponse->RefreshComplete( NServerResponse::nServerResponded );
		return;
	}

	if( m_flRetryRequestTime < Plat_FloatTime() - RETRY_INFO_REQUEST_TIME )
	{
		m_flRetryRequestTime = Plat_FloatTime();

		if( m_serverAddresses.Count() == 0 ) // Retry masterserver request
		{
			SendMasterQueries();
			return;
		}

		if( m_iServersResponded < m_serverAddresses.Count() )
			RetryServersInfoRequest();
	}
}

void CMaster::StopRefresh()
{
	if( !m_bRefreshing )
		return;

	m_iServersResponded = 0;
	m_bRefreshing = false;
	m_bLanRefresh = false;
	m_serverAddresses.RemoveAll();
	m_serversRequestTime.RemoveAll();
}

void CMaster::BeginListRefresh( IServerListResponse *response )
{
	IServerListResponse *pOldResponse = m_bRefreshing ? m_serverListResponse : NULL;

	StopRefresh();

	if( pOldResponse && pOldResponse != response )
		pOldResponse->RefreshComplete( NServerResponse::nServerResponded );

	m_bRefreshing = true;
	m_serverListResponse = response;
	m_flRetryRequestTime = m_flStartRequestTime = m_flMasterRequestTime = Plat_FloatTime();
}

//-----------------------------------------------------------------------------
// Purpose: Master servers come from cfg/MasterServers.vdf (same format as the
//			original engine used) plus the built-in default. All of them are
//			queried at once and the results are merged.
//
//	"MasterServers"
//	{
//		"hl2"
//		{
//			"0"	{ "addr"	"1.2.3.4:27011" }
//		}
//	}
//-----------------------------------------------------------------------------
void CMaster::AddMasterServer( const char *pszAddress )
{
	if ( !pszAddress || !pszAddress[0] || m_MasterServers.Count() >= MAX_MASTER_SERVERS )
		return;

	// old configs still point to Valve's masters: they list Steam servers that v34 can't join
	if ( V_stristr( pszAddress, "steampowered.com" ) )
	{
		DevMsg( "Master server %s skipped (Steam master)\n", pszAddress );
		return;
	}

	netadr_t adr;
	if ( !adr.SetFromString( pszAddress, true ) || !adr.IsValid() )
	{
		DevMsg( "Master server %s can't be resolved\n", pszAddress );
		return;
	}

	if ( m_MasterServers.Find( adr ) != m_MasterServers.InvalidIndex() )
		return;

	m_MasterServers.AddToTail( adr );
}

void CMaster::LoadMasterServers()
{
	m_MasterServers.RemoveAll();

	KeyValues *kv = new KeyValues( "MasterServers" );
	if ( kv->LoadFromFile( g_pFileSystem, MASTER_SERVERS_FILE, "MOD" ) ||
		 kv->LoadFromFile( g_pFileSystem, MASTER_SERVERS_FILE, "GAME" ) )
	{
		for ( KeyValues *pGroup = kv->GetFirstTrueSubKey(); pGroup; pGroup = pGroup->GetNextTrueSubKey() )
		{
			for ( KeyValues *pEntry = pGroup->GetFirstTrueSubKey(); pEntry; pEntry = pEntry->GetNextTrueSubKey() )
			{
				AddMasterServer( pEntry->GetString( "addr", "" ) );
			}
		}
	}
	kv->deleteThis();

	AddMasterServer( DEFAULT_MASTER_SERVER );
}

bool CMaster::IsMasterServer( const netadr_t &adr ) const
{
	FOR_EACH_VEC( m_MasterServers, i )
	{
		if ( m_MasterServers[i].CompareAdr( adr ) )
			return true;
	}
	return false;
}

void CMaster::SendMasterQuery( const netadr_t &master, const char *pszStartAddress )
{
	ALIGN4 char buf[256] ALIGN4_POST;
	bf_write msg( buf, sizeof(buf) );

	msg.WriteByte( C2M_CLIENTQUERY );
	msg.WriteByte( 255 ); // region always 255 (all)
	msg.WriteString( pszStartAddress );
	msg.WriteString( va( "\\gamedir\\%s", COM_GetModDirectory() ) );

	sockaddr to;
	master.ToSockadr( &to );

	sendto( m_nSocket, (const char*)msg.GetData(), msg.GetNumBytesWritten(), 0, &to, sizeof(to) );
}

void CMaster::SendMasterQueries()
{
	FOR_EACH_VEC( m_MasterServers, i )
	{
		SendMasterQuery( m_MasterServers[i], "0.0.0.0:0" );
	}
}

void CMaster::BroadcastLanQuery()
{
	static ALIGN4 char string[256] ALIGN4_POST;
	bf_write msg( string, sizeof(string) );

	msg.WriteLong( CONNECTIONLESS_HEADER );
	msg.WriteByte( A2S_INFO );
	msg.WriteString( A2S_KEY_STRING );

	m_flLanBroadcastTime = Plat_FloatTime();

	for ( int port = LAN_FIRST_PORT; port <= LAN_LAST_PORT; port++ )
	{
		netadr_t adr( 0xFFFFFFFF, port ); // 255.255.255.255
		sockaddr to;
		adr.ToSockadr( &to );

		sendto( m_nSocket, (const char*)msg.GetData(), msg.GetNumBytesWritten(), 0, &to, sizeof(to) );
	}
}

void CMaster::ReplyInfo( const netadr_t &adr )
{
	if (serverGameDLL && serverGameDLL->ShouldHideServer())
		return;

	byte	data[1400];
	char	gd[MAX_OSPATH];

	bf_write buf("SVC_Info->buf", data, sizeof(data));

	buf.WriteLong(CONNECTIONLESS_HEADER);
	buf.WriteByte(S2A_INFO_SRC);

	buf.WriteByte(PROTOCOL_VERSION); // Hardcoded protocol version number

	buf.WriteString(sv.GetName());
	buf.WriteString(sv.GetMapName());
	Q_FileBase(com_gamedir, gd, sizeof(gd));
	buf.WriteString(gd);
	buf.WriteString(serverGameDLL->GetGameDescription());

	uint appID = GetSteamAppID();
	buf.WriteShort(appID);

	// this is a quick workaround from goldsrc for the admin mod reserved slots UI problem
	int visibleClients = sv.GetMaxClients();
	if (!sv.IsHLTV() && sv_visiblemaxplayers.GetInt() > 0 && sv_visiblemaxplayers.GetInt() < sv.GetMaxClients())
	{
		visibleClients = sv_visiblemaxplayers.GetInt();
	}

	// player info
	buf.WriteByte(sv.GetNumClients());
	buf.WriteByte(visibleClients);
	buf.WriteByte(sv.GetNumFakeClients());

	// Additional info....
	if (sv.IsHLTV())
		buf.WriteByte('p');	// p = SourceTV proxy
	else if (sv.IsDedicated())
		buf.WriteByte('d');	// d = dedicated server
	else
		buf.WriteByte('l');	// l = listen server

#if defined(_WIN32)
	buf.WriteByte('w');
#else // LINUX?
	buf.WriteByte('l');
#endif

	// Password?
	buf.WriteByte(sv.GetPassword() ? 1 : 0);

#ifndef _XBOX
	// VAC state, needs to be hooked up
	bool bIsSecure = Steam3Server().SteamGameServer()->BSecure() && !sv.IsHLTV();
#else
	bool bIsSecure = true;
#endif
	buf.WriteByte(bIsSecure ? 1 : 0);

	char verString[40];
	Q_snprintf(verString, sizeof(verString), "%s", GetSteamInfIDVersionInfo().szVersionString);
	buf.WriteString(verString);

	// Write a byte with some flags that describe what is to follow.
	byte nNewFlags = 0;

	const char* pchGameType = sv_tags.GetString();
	if (pchGameType && Q_strlen(pchGameType) > 0)
		nNewFlags |= S2A_EDF_GAMETAGS;

	buf.WriteByte(nNewFlags);

	// Write the gametags.
	if (nNewFlags & S2A_EDF_GAMETAGS)
	{
		buf.WriteString(pchGameType);
	}

	sockaddr to;
	adr.ToSockadr(&to);

	sendto(m_nSocket, (const char*)buf.GetData(), buf.GetNumBytesWritten(), 0, &to, sizeof(to));
}

newgameserver_t &CMaster::ProcessInfo(bf_read &buf)
{
	static newgameserver_t s;
	memset(&s, 0, sizeof(s));

	s.m_nProtocolVersion = buf.ReadByte();

	buf.ReadString(s.m_szServerName, sizeof(s.m_szServerName));
	buf.ReadString(s.m_szMap, sizeof(s.m_szMap));
	buf.ReadString(s.m_szGameDir, sizeof(s.m_szGameDir));

	buf.ReadString(s.m_szGameDescription, sizeof(s.m_szGameDescription));
	s.m_nAppID = buf.ReadShort();

	// player info
	s.m_nPlayers = buf.ReadByte();
	s.m_nMaxPlayers = buf.ReadByte();
	s.m_nBotPlayers = buf.ReadByte();

	// Password?
	buf.ReadByte(); // server type
	buf.ReadByte(); // env

	s.m_bPassword = buf.ReadByte();
	s.m_bSecure = buf.ReadByte();
	buf.ReadString(s.m_szGameVersion, sizeof(s.m_szGameVersion));

	s.m_nFlags = buf.ReadByte();

	if (s.m_nFlags & S2A_EDF_GAMEPORT)
	{
		s.m_NetAdr.SetPort(buf.ReadShort());
	}

	if (s.m_nFlags & S2A_EDF_STEAMID)
	{
		uint64 ulSteamID;
		buf.ReadBytes(&ulSteamID, 8);
	}

	if (s.m_nFlags & S2A_EDF_SOURCETV)
	{
		char str[64];
		buf.ReadShort(); // spectator port (unused)
		buf.ReadString(str, sizeof(str)); // spectator sv name
	}

	if (s.m_nFlags & S2A_EDF_GAMETAGS)
	{
		buf.ReadString(s.m_szGameTags, sizeof(s.m_szGameTags));
	}

	if (s.m_nFlags & S2A_EDF_GAMEID)
	{
		uint64 ulGameID;
		buf.ReadBytes(&ulGameID, 8);
	}

	return s;
}

bool CMaster::ProcessConnectionlessPacket( netpacket_t *packet )
{
	static ALIGN4 char string[2048] ALIGN4_POST;    // Buffer for sending heartbeat

	uint ip = 0; uint16 port = 0;

	bf_read msg = packet->message;
	char c = msg.ReadChar();

	if ( c == 0  )
		return true;

	switch( c )
	{
		case 0x41: // S2C_CHALLENGE
		{
			int challenge = msg.ReadLong();

			static ALIGN4 char string[256] ALIGN4_POST;
			bf_write reply(string, sizeof(string));

			reply.WriteLong(CONNECTIONLESS_HEADER);

			if ( m_PendingPlayerRequests.Find( packet->from ) != m_PendingPlayerRequests.InvalidIndex() )
			{
				// answer to our A2S_PLAYER
				reply.WriteByte(A2S_PLAYER);
				reply.WriteLong(challenge);
			}
			else if ( m_serversRequestTime.Find( packet->from.GetIPHostByteOrder() + packet->from.GetPort() ) != m_serversRequestTime.InvalidIndex() )
			{
				// server wants a challenge for A2S_INFO (anti-reflection patches)
				reply.WriteByte(A2S_INFO);
				reply.WriteString(A2S_KEY_STRING);
				reply.WriteLong(challenge);
			}
			else
			{
				break; // we never asked this host for anything
			}

			sockaddr to;
			packet->from.ToSockadr(&to);

			sendto(m_nSocket, (const char*)reply.GetData(), reply.GetNumBytesWritten(), 0, &to, sizeof(to));

			break;
		}

		case 0x44: // S2A_PLAYER
		{
			m_PendingPlayerRequests.Remove( packet->from );

			int nPlayers = msg.ReadByte();
			for (int iPlayer = 0; iPlayer < nPlayers; ++iPlayer)
			{
				char str[256];

				msg.ReadByte();
				msg.ReadString(str, sizeof(str));
				int score = msg.ReadLong();
				float duration = msg.ReadFloat();

				if ( msg.IsOverflowed() )
					break; // truncated packet

				if (str[0] != 0)
				{
					FOR_EACH_VEC(m_ServerPlayers, i)
					{
						IServerPlayersResponse* resp = m_ServerPlayers[i];

						if (!resp->IsForThisServer(packet->from)) continue;

						resp->AddPlayerToList(str, score, duration);
					}
				}
			}

			FOR_EACH_VEC(m_ServerPlayers, i)
			{
				IServerPlayersResponse* resp = m_ServerPlayers[i];

				if (!resp->IsForThisServer(packet->from)) continue;

				resp->PlayersRefreshComplete();
			}
			break; // was falling through into M2A_SERVER_BATCH
		}

		case M2A_SERVER_BATCH:
		{
			if( !m_bRefreshing || m_bLanRefresh )
				break;

			// only trust server lists from our masters
			if( !IsMasterServer( packet->from ) )
				break;

			msg.ReadByte();

			while (true)
			{
				if (msg.GetNumBytesLeft() < 6)
				{
					break;
				}

				ip = msg.ReadLong();
				port = msg.ReadShort();
				if (ip == 0 || port == 0) {
					break;
				}

				netadr_t adr(BigLong(ip), BigShort(port));

				unsigned short index = m_serverAddresses.Find(adr);
				if (index != m_serverAddresses.InvalidIndex())
				{
					continue;
				}

				m_serverAddresses.Insert(adr, false);
				RequestServerInfo(adr);
			}

			if (ip != 0 && port != 0)
			{
				// ask the same master for the next batch
				netadr_t startadr(BigLong(ip), BigShort(port));
				SendMasterQuery( packet->from, startadr.ToString() );
			}

			break;
		}
		case C2S_INFOREQUEST:
		{
			ReplyInfo(packet->from);
			break;
		}
		case S2A_INFO_SRC:
		{
			newgameserver_t &s = ProcessInfo( msg );

			unsigned short rindex = m_serversRequestTime.Find(packet->from.GetIPHostByteOrder() + packet->from.GetPort());

			double requestTime;
			if (rindex != m_serversRequestTime.InvalidIndex())
			{
				requestTime = m_serversRequestTime[rindex];
			}
			else if (m_bRefreshing && m_bLanRefresh)
			{
				// answer to our LAN broadcast
				requestTime = m_flLanBroadcastTime;
			}
			else
			{
				break;
			}

			s.m_nPing = (Plat_FloatTime() - requestTime) * 1000.0;
			s.m_NetAdr = packet->from;
			s.m_bHadSuccessfulResponse = true;

			FOR_EACH_VEC(m_ServerPings, i)
			{
				IServerPingResponse* resp = m_ServerPings[i];
				resp->ServerResponded(s);
			}

			if (!m_bRefreshing)
				break;

			if (m_bLanRefresh)
			{
				// only servers of our game
				if (Q_stricmp(s.m_szGameDir, m_szGameDir))
					break;

				if (m_serverAddresses.Find(packet->from) == m_serverAddresses.InvalidIndex())
					m_serverAddresses.Insert(packet->from, false);
			}

			unsigned short index = m_serverAddresses.Find(packet->from);

			if( index == m_serverAddresses.InvalidIndex() )
				break;

			if( m_serverAddresses[index] ) // shit happens
				return true;

			m_serverAddresses[index] = true;
			m_serverListResponse->ServerResponded( s );

			m_iServersResponded++;
			break;
		}
	}
	return true;
}

void CMaster::RequestServerInfo( const netadr_t &adr )
{
	static ALIGN4 char string[256] ALIGN4_POST;    // Buffer for sending heartbeat
	bf_write msg( string, sizeof(string) );

	msg.WriteLong( CONNECTIONLESS_HEADER );
	msg.WriteByte( A2S_INFO );
	msg.WriteString( A2S_KEY_STRING );
	m_serversRequestTime.Insert(adr.GetIPHostByteOrder()+adr.GetPort(), Plat_FloatTime());

	sockaddr to;
	adr.ToSockadr(&to);

	sendto(m_nSocket, (const char*)msg.GetData(), msg.GetNumBytesWritten(), 0, &to, sizeof(to));
}

void CMaster::RetryServersInfoRequest()
{
	FOR_EACH_MAP_FAST( m_serverAddresses, i )
	{
		bool bResponded = m_serverAddresses.Element(i);
		if( bResponded )
			continue;

		const netadr_t adr = m_serverAddresses.Key(i);
		RequestServerInfo( adr );
	}
}

void CMaster::PingServer(uint32 unIP, uint16 usPort, IServerPingResponse* pRequestServersResponse)
{
	netadr_t adr(unIP, usPort);
	if (m_ServerPings.Find(pRequestServersResponse) == m_ServerPings.InvalidIndex())
	{
		m_ServerPings.AddToTail(pRequestServersResponse);
	}

	static ALIGN4 char string[256] ALIGN4_POST;
	bf_write msg(string, sizeof(string));

	msg.WriteLong(CONNECTIONLESS_HEADER);
	msg.WriteByte(A2S_INFO);
	msg.WriteString(A2S_KEY_STRING);
	
	unsigned int index = m_serversRequestTime.Find(adr.GetIPHostByteOrder() + adr.GetPort());
	if (index == m_serversRequestTime.InvalidIndex()) {
		m_serversRequestTime.Insert(adr.GetIPHostByteOrder() + adr.GetPort(), Plat_FloatTime());
	}
	else {
		m_serversRequestTime[index] = Plat_FloatTime();
	}

	sockaddr to;
	adr.ToSockadr(&to);

	sendto(m_nSocket, (const char*)msg.GetData(), msg.GetNumBytesWritten(), 0, &to, sizeof(to));
}

void CMaster::PlayerDetails(uint32 unIP, uint16 usPort, IServerPlayersResponse* pRequestServersResponse)
{
	netadr_t adr(unIP, usPort);

	if (m_ServerPlayers.Find(pRequestServersResponse) == m_ServerPlayers.InvalidIndex())
	{
		m_ServerPlayers.AddToTail(pRequestServersResponse);
	}

	if (m_PendingPlayerRequests.Count() > 256)
		m_PendingPlayerRequests.RemoveAll(); // servers that never answered

	if (m_PendingPlayerRequests.Find(adr) == m_PendingPlayerRequests.InvalidIndex())
		m_PendingPlayerRequests.Insert(adr, true);

	static ALIGN4 char string[256] ALIGN4_POST;
	bf_write msg(string, sizeof(string));

	msg.WriteLong(CONNECTIONLESS_HEADER);
	msg.WriteByte(A2S_PLAYER);
	msg.WriteLong(-1);

	sockaddr to;
	adr.ToSockadr(&to);

	sendto(m_nSocket, (const char*)msg.GetData(), msg.GetNumBytesWritten(), 0, &to, sizeof(to));
}

void CMaster::RemoveResponse(IServerPingResponse* resp, IServerPlayersResponse* resp2)
{
	m_ServerPings.FindAndFastRemove(resp);
	m_ServerPlayers.FindAndFastRemove(resp2);
}

//-----------------------------------------------------------------------------
// Purpose: Server is shutting down, unload master servers list, tell masters that we are closing the server
//-----------------------------------------------------------------------------
void CMaster::ShutdownConnection( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Adds master server console commands
//-----------------------------------------------------------------------------
void CMaster::Init( void )
{
	// Already able to initialize at least once?
	if ( m_bInitialized )
		return;

	// So we don't do this a send time.sv_mas
	m_bInitialized = true;
	m_nSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	unsigned int opt = 1; // make it non-blocking
	ioctlsocket(m_nSocket, FIONBIO, (unsigned long*)&opt);

	// needed for the LAN tab
	int bBroadcast = 1;
	setsockopt(m_nSocket, SOL_SOCKET, SO_BROADCAST, (char *)&bBroadcast, sizeof(bBroadcast));
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CMaster::Shutdown(void)
{
	closesocket(m_nSocket);
}

// ServersInfo
void CMaster::RequestInternetServerList(const char *gamedir, IServerListResponse *response)
{
	Q_strncpy( m_szGameDir, gamedir, sizeof(m_szGameDir) );

	if( response )
	{
		BeginListRefresh( response );
		LoadMasterServers(); // pick up cfg/MasterServers.vdf changes on every refresh
	}

	SendMasterQueries();
}

void CMaster::RequestLANServerList(const char* gamedir, IServerListResponse* response)
{
	if ( !response )
		return;

	Q_strncpy( m_szGameDir, gamedir, sizeof(m_szGameDir) );

	BeginListRefresh( response );
	m_bLanRefresh = true;

	BroadcastLanQuery();
}