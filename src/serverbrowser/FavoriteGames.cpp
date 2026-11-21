//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "pch_serverbrowser.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CFavoriteGames::CFavoriteGames(vgui::Panel *parent) : 
	CBaseGamesPage(parent, "FavoriteGames", eFavoritesServer )
{
	m_bRefreshOnListReload = false;
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CFavoriteGames::~CFavoriteGames()
{
}

//-----------------------------------------------------------------------------
// Purpose: loads favorites list from disk
//-----------------------------------------------------------------------------
KeyValues *CFavoriteGames::GetAddressList()
{
	return ServerBrowserDialog().GetFavoritesData();
}

//-----------------------------------------------------------------------------
// Purpose: keep the saved name in sync with what the server reports
//-----------------------------------------------------------------------------
void CFavoriteGames::OnAddressListServerResponded( newgameserver_t &server )
{
	ServerBrowserDialog().UpdateSavedServerName( GetAddressList(), server.m_NetAdr, server.m_szServerName );
}

static bool FavoritesListIsEmpty()
{
	KeyValues *pList = ServerBrowserDialog().GetFavoritesData();
	return !pList || !pList->GetFirstTrueSubKey();
}

void CFavoriteGames::LoadFavoritesList()
{
	if ( FavoritesListIsEmpty() )
	{
		// set empty message
		m_pGameList->SetEmptyListText("#ServerBrowser_NoFavoriteServers");
	}
	else
	{
		m_pGameList->SetEmptyListText("#ServerBrowser_NoInternetGamesResponded");

	}

	if ( m_bRefreshOnListReload )
	{
		m_bRefreshOnListReload = false;
		StartRefresh();
	}
}


//-----------------------------------------------------------------------------
// Purpose: returns true if the game list supports the specified ui elements
//-----------------------------------------------------------------------------
bool CFavoriteGames::SupportsItem(InterfaceItem_e item)
{
	switch (item)
	{
	case FILTERS:
	case ADDSERVER:
		return true;

	case ADDCURRENTSERVER:
		return !IsSteam() && BFiltersVisible();
	
	case GETNEWLIST:
	default:
		return false;
	}
}


//-----------------------------------------------------------------------------
// Purpose: called when the current refresh list is complete
//-----------------------------------------------------------------------------
void CFavoriteGames::RefreshComplete( NServerResponse response )
{
	SetRefreshing(false);
	if ( FavoritesListIsEmpty() )
	{
		// set empty message
		m_pGameList->SetEmptyListText("#ServerBrowser_NoFavoriteServers");
	}
	else
	{
		m_pGameList->SetEmptyListText("#ServerBrowser_NoInternetGamesResponded");

	}
	m_pGameList->SortList();

	BaseClass::RefreshComplete( response );
}

//-----------------------------------------------------------------------------
// Purpose: opens context menu (user right clicked on a server)
//-----------------------------------------------------------------------------
void CFavoriteGames::OnOpenContextMenu(int itemID)
{
	CServerContextMenu *menu = ServerBrowserDialog().GetContextMenu(GetActiveList());

	// get the server
	int serverID = GetSelectedServerID();

	if ( serverID != -1 )
	{
		// Activate context menu
		menu->ShowMenu(this, serverID, true, true, true, false);
		menu->AddMenuItem("RemoveServer", "#ServerBrowser_RemoveServerFromFavorites", new KeyValues("RemoveFromFavorites"), this);
	}
	else
	{
		// no selected rows, so don't display default stuff in menu
		menu->ShowMenu( this,(uint32)-1, false, false, false, false );
	}
	
	menu->AddMenuItem("AddServerByName", "#ServerBrowser_AddServerByIP", new KeyValues("AddServerByName"), this);
}


//-----------------------------------------------------------------------------
// Purpose: removes a server from the favorites
//-----------------------------------------------------------------------------
void CFavoriteGames::OnRemoveFromFavorites()
{
	// copy the selection first, removing rows changes it
	CUtlVector<netadr_t> addresses;
	GetSelectedAddresses( addresses );

	FOR_EACH_VEC( addresses, i )
	{
		ServerBrowserDialog().RemoveServerFromFavorites( addresses[i] );
		RemoveServerByAddress( addresses[i] );
	}

	if ( FavoritesListIsEmpty() )
		m_pGameList->SetEmptyListText("#ServerBrowser_NoFavoriteServers");

	UpdateStatus();	
	InvalidateLayout();
	Repaint();
}

//-----------------------------------------------------------------------------
// Purpose: Adds a server by IP address
//-----------------------------------------------------------------------------
void CFavoriteGames::OnAddServerByName()
{
	// open the add server dialog
	CDialogAddServer *dlg = new CDialogAddServer( &ServerBrowserDialog(), this );
	dlg->MoveToCenterOfScreen();
	dlg->DoModal();
}

//-----------------------------------------------------------------------------
// Purpose: Adds the currently connected server to the list
//-----------------------------------------------------------------------------
void CFavoriteGames::OnAddCurrentServer()
{
	// saves it and refreshes this tab
	ServerBrowserDialog().AddCurrentServerToFavorites();
}

//-----------------------------------------------------------------------------
// Purpose: Parse posted messages
//			 
//-----------------------------------------------------------------------------
void CFavoriteGames::OnCommand(const char *command)
{
	if (!Q_stricmp(command, "AddServerByName"))
	{
		OnAddServerByName();
	}
	else if (!Q_stricmp(command, "AddCurrentServer" ))
	{
		OnAddCurrentServer();
	}
	else
	{
		BaseClass::OnCommand(command);
	}
}

//-----------------------------------------------------------------------------
// Purpose: enables adding server
//-----------------------------------------------------------------------------
void CFavoriteGames::OnConnectToGame()
{
	m_pAddCurrentServer->SetEnabled( ServerBrowserDialog().IsConnectedToServer() );
}

//-----------------------------------------------------------------------------
// Purpose: disables adding current server
//-----------------------------------------------------------------------------
void CFavoriteGames::OnDisconnectFromGame( void )
{
	m_pAddCurrentServer->SetEnabled( false );
}
