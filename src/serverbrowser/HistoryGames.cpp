//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#include "pch_serverbrowser.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHistoryGames::CHistoryGames(vgui::Panel *parent) : 
	CBaseGamesPage(parent, "HistoryGames", eHistoryServer )
{
	m_bRefreshOnListReload = false;
	m_pGameList->AddColumnHeader(10, "LastPlayed", "#ServerBrowser_LastPlayed", 100);
	m_pGameList->SetSortFunc(10, LastPlayedCompare);
	m_pGameList->SetSortColumn(10);
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CHistoryGames::~CHistoryGames()
{
}		

//-----------------------------------------------------------------------------
// Purpose: loads favorites list from disk
//-----------------------------------------------------------------------------
KeyValues *CHistoryGames::GetAddressList()
{
	return ServerBrowserDialog().GetHistoryData();
}

//-----------------------------------------------------------------------------
// Purpose: fills the "Last played" column
//-----------------------------------------------------------------------------
void CHistoryGames::OnAddressListEntryAdded( KeyValues *pEntry, KeyValues *pRow )
{
	int nLastPlayed = pEntry->GetInt( "lastplayed", 0 );
	pRow->SetInt( "LastPlayedTime", nLastPlayed );

	char szTime[64] = "";
	if ( nLastPlayed > 0 )
	{
		time_t t = (time_t)nLastPlayed;
		struct tm *pTime = localtime( &t );
		if ( pTime )
			strftime( szTime, sizeof(szTime), "%d.%m.%Y %H:%M", pTime );
	}
	pRow->SetString( "LastPlayed", szTime );
}

void CHistoryGames::OnAddressListServerResponded( newgameserver_t &server )
{
	ServerBrowserDialog().UpdateSavedServerName( GetAddressList(), server.m_NetAdr, server.m_szServerName );
}

void CHistoryGames::LoadHistoryList()
{
	if ( m_bRefreshOnListReload )
	{
		m_bRefreshOnListReload = false;
		StartRefresh();
	}
}


//-----------------------------------------------------------------------------
// Purpose: returns true if the game list supports the specified ui elements
//-----------------------------------------------------------------------------
bool CHistoryGames::SupportsItem(InterfaceItem_e item)
{
	switch (item)
	{
	case FILTERS:
		return true;
	
	case ADDSERVER:
	case GETNEWLIST:
	default:
		return false;
	}
}


//-----------------------------------------------------------------------------
// Purpose: called when the current refresh list is complete
//-----------------------------------------------------------------------------
void CHistoryGames::RefreshComplete( NServerResponse response )
{
	SetRefreshing(false);
	m_pGameList->SetEmptyListText("#ServerBrowser_NoServersPlayed");
	m_pGameList->SortList();

	BaseClass::RefreshComplete( response );
}

//-----------------------------------------------------------------------------
// Purpose: opens context menu (user right clicked on a server)
//-----------------------------------------------------------------------------
void CHistoryGames::OnOpenContextMenu(int itemID)
{
	CServerContextMenu *menu = ServerBrowserDialog().GetContextMenu(GetActiveList());

	// get the server
	int serverID = GetSelectedServerID();

	if(  serverID != -1 )
	{
		// Activate context menu
		menu->ShowMenu(this, serverID, true, true, true, true);
		menu->AddMenuItem("RemoveServer", "#ServerBrowser_RemoveServerFromHistory", new KeyValues("RemoveFromHistory"), this);
	}
	else
	{
		// no selected rows, so don't display default stuff in menu
		menu->ShowMenu(this, (uint32)-1, false, false, false, false);
	}
}


//-----------------------------------------------------------------------------
// Purpose: removes a server from the favorites
//-----------------------------------------------------------------------------
void CHistoryGames::OnRemoveFromHistory()
{
	// copy the selection first, removing rows changes it
	CUtlVector<netadr_t> addresses;
	GetSelectedAddresses( addresses );

	FOR_EACH_VEC( addresses, i )
	{
		ServerBrowserDialog().RemoveServerFromHistory( addresses[i] );
		RemoveServerByAddress( addresses[i] );
	}

	UpdateStatus();	
	InvalidateLayout();
	Repaint();
}

