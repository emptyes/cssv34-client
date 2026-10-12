//========= Copyright Valve Corporation, All rights reserved. ============//
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
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================
#include "pch_serverbrowser.h"

#if defined( _X360 )
#include "xbox/xbox_win32stubs.h"
#endif

#if defined( _WIN32 ) && !defined( _X360 )
#define WIN32_LEAN_AND_MEAN
#include <winsock.h>
#endif
#if defined(LINUX) || defined(PLATFORM_BSD)
#include <arpa/inet.h>
#endif

using namespace vgui;

ConVar sb_quick_list_bit_field( "sb_quick_list_bit_field", "-1" );

#define SERVERBROWSER_FILE		"ServerBrowser.vdf"
#define MAX_HISTORY_SERVERS		100

static CServerBrowserDialog *s_InternetDlg = NULL;

CServerBrowserDialog &ServerBrowserDialog()
{
	return *CServerBrowserDialog::GetInstance();
}


// Returns a list of the ports that we hit when looking for 
void GetMostCommonQueryPorts( CUtlVector<uint16> &ports )
{
	for ( int i=0; i <= 5; i++ )
	{
		ports.AddToTail( 27015 + i );
		ports.AddToTail( 26900 + i );
	}

	ports.AddToTail(4242); //RDKF
	ports.AddToTail(27215); //Lost Planet
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CServerBrowserDialog::CServerBrowserDialog(vgui::Panel *parent) : Frame(parent, "CServerBrowserDialog")
{
	SetProportional( NeedProportional() );
	s_InternetDlg = this;

	m_szGameName[0] = 0;
	m_szModDir[0] = 0;
	m_pSavedData = NULL;
	m_pFilterData = NULL;
	m_pFavoritesData = NULL;
	m_pHistoryData = NULL;
	m_pFavorites = NULL;
	m_pHistory = NULL;
	m_pLanGames = NULL;
	m_bCurrentlyConnected = false;
	memset( &m_CurrentConnection, 0, sizeof(gameserveritem_t) );
	m_CurrentServerAdr.Clear();

	LoadUserData();

	m_pInternetGames = new CInternetGames(this);
	m_pFavorites = new CFavoriteGames(this);
	m_pHistory = new CHistoryGames(this);
	//m_pSpectateGames = new CSpectateGames(this);
	m_pLanGames = new CLanGames(this);

	int w = 640; int h = 384;

	w = IsProportional() ? vgui::scheme()->GetProportionalScaledValue(w) : w;
	h = IsProportional() ? vgui::scheme()->GetProportionalScaledValue(h) : h;

	SetMinimumSize( w, h );
	SetSize( w, h );

	m_pGameList = m_pInternetGames;

	m_pContextMenu =  new CServerContextMenu(this);;

	// property sheet
	m_pTabPanel = new PropertySheet(this, "GameTabs");
	m_pTabPanel->SetTabWidth(72);
	m_pTabPanel->AddPage(m_pInternetGames, "#ServerBrowser_InternetTab");
	m_pTabPanel->AddPage(m_pFavorites, "#ServerBrowser_FavoritesTab");
	m_pTabPanel->AddPage(m_pHistory, "#ServerBrowser_HistoryTab");
	//m_pTabPanel->AddPage(m_pSpectateGames, "#ServerBrowser_SpectateTab");
	m_pTabPanel->AddPage(m_pLanGames, "#ServerBrowser_LanTab");

	m_pTabPanel->AddActionSignalTarget(this);

	m_pStatusLabel = new Label(this, "StatusLabel", "");

	LoadControlSettingsAndUserConfig("Servers/DialogServerBrowser.res");

	m_pStatusLabel->SetText("");

	// load current tab
	const char *gameList = m_pSavedData->GetString("GameList");

	if (!Q_stricmp(gameList, "favorites"))
	{
		m_pTabPanel->SetActivePage(m_pFavorites);
	}
	else if (!Q_stricmp(gameList, "history"))
	{
		m_pTabPanel->SetActivePage(m_pHistory);
	}
	else if (!Q_stricmp(gameList, "lan"))
	{
		m_pTabPanel->SetActivePage(m_pLanGames);
	}
	else
	{
		m_pTabPanel->SetActivePage(m_pInternetGames);
	}

	m_pFavorites->LoadFavoritesList();
	m_pHistory->LoadHistoryList();

	ivgui()->AddTickSignal( GetVPanel() );
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CServerBrowserDialog::~CServerBrowserDialog()
{
	delete m_pContextMenu;

	SaveUserData();

  	if (m_pSavedData)
  		m_pSavedData->deleteThis();

	if( m_pFilterData )
		m_pFilterData->deleteThis();

	if( m_pFavoritesData )
		m_pFavoritesData->deleteThis();

	if( m_pHistoryData )
		m_pHistoryData->deleteThis();
}


//-----------------------------------------------------------------------------
// Purpose: Called once to set up
//-----------------------------------------------------------------------------
void CServerBrowserDialog::Initialize()
{
	SetTitle("#ServerBrowser_Servers", true);
	SetVisible(false);
}


//-----------------------------------------------------------------------------
// Purpose: returns a server in the list
//-----------------------------------------------------------------------------
newgameserver_t *CServerBrowserDialog::GetServer( unsigned int serverID )
{
	if (m_pGameList)
		return m_pGameList->GetServer( serverID );
	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Activates and gives the tab focus
//-----------------------------------------------------------------------------
void CServerBrowserDialog::Open()
{	
	BaseClass::Activate();
	m_pTabPanel->RequestFocus();

	MoveToCenterOfScreen();
}


//-----------------------------------------------------------------------------
// Purpose: Called every frame, updates animations for this module
//-----------------------------------------------------------------------------
void CServerBrowserDialog::OnTick()
{
	BaseClass::OnTick();
	vgui::GetAnimationController()->UpdateAnimations( system()->GetFrameTime() );
}


//-----------------------------------------------------------------------------
// Purpose: Loads filter settings from disk
//-----------------------------------------------------------------------------
void CServerBrowserDialog::LoadUserData()
{
  	// free any old filters
  	if (m_pSavedData)
  	{
  		m_pSavedData->deleteThis();
  	}

	m_pSavedData = new KeyValues("Filters");
	if (!m_pSavedData->LoadFromFile( g_pFullFileSystem, SERVERBROWSER_FILE, "CONFIG"))
	{
		// doesn't matter if the file is not found, defaults will work successfully and file will be created on exit
	}

	// favorites / history lists
	if ( m_pFavoritesData )
		m_pFavoritesData->deleteThis();
	if ( m_pHistoryData )
		m_pHistoryData->deleteThis();

	KeyValues *favorites = m_pSavedData->FindKey( "Favorites", false );
	m_pFavoritesData = favorites ? favorites->MakeCopy() : new KeyValues( "Favorites" );

	KeyValues *history = m_pSavedData->FindKey( "History", false );
	m_pHistoryData = history ? history->MakeCopy() : new KeyValues( "History" );

	KeyValues *filters = m_pSavedData->FindKey( "Filters", false );
	if ( filters )
	{
		m_pFilterData = filters->MakeCopy();
		m_pSavedData->RemoveSubKey( filters );
	}
	else
	{
		m_pFilterData = new KeyValues( "Filters" );
	}


	// reload all the page settings if necessary
	if (m_pHistory)
	{
		// history
		m_pHistory->LoadHistoryList();
		if ( IsVisible() && m_pHistory->IsVisible() )
			m_pHistory->StartRefresh();
	}

	if (m_pFavorites)
	{
		// favorites
		m_pFavorites->LoadFavoritesList();

		// filters
		ReloadFilterSettings();

		if ( IsVisible() && m_pFavorites->IsVisible() )
			m_pFavorites->StartRefresh();
	}

	InvalidateLayout();
	Repaint();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CServerBrowserDialog::SaveUserData()
{
	m_pSavedData->Clear();
	m_pSavedData->LoadFromFile( g_pFullFileSystem, SERVERBROWSER_FILE, "CONFIG");

	// set the current tab
	if (m_pGameList == m_pFavorites)
	{
		m_pSavedData->SetString("GameList", "favorites");
	}
	else if (m_pGameList == m_pLanGames)
	{
		m_pSavedData->SetString("GameList", "lan");
	}
	else if (m_pGameList == m_pHistory)
	{
		m_pSavedData->SetString("GameList", "history");
	}
	else
	{
		m_pSavedData->SetString("GameList", "internet");
	}

	ReplaceSavedSubKey( m_pSavedData, m_pFilterData ); // remove the saved subkey and add our subkey
	ReplaceSavedSubKey( m_pSavedData, m_pFavoritesData );
	ReplaceSavedSubKey( m_pSavedData, m_pHistoryData );
	m_pSavedData->SaveToFile( g_pFullFileSystem, SERVERBROWSER_FILE, "CONFIG");

	// save per-page config
	SaveUserConfig();
}

//-----------------------------------------------------------------------------
// Purpose: refreshes the page currently visible
//-----------------------------------------------------------------------------
void CServerBrowserDialog::RefreshCurrentPage()
{
	if (m_pGameList)
	{
		m_pGameList->StartRefresh();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Updates status test at bottom of window
//-----------------------------------------------------------------------------
void CServerBrowserDialog::UpdateStatusText(const char *fmt, ...)
{
	if ( !m_pStatusLabel )
		return;

	if ( fmt && strlen(fmt) > 0 )
	{
		char str[ 1024 ];
		va_list argptr;
		va_start( argptr, fmt );
		_vsnprintf( str, sizeof(str), fmt, argptr );
		va_end( argptr );

		m_pStatusLabel->SetText( str );
	}
	else
	{
		// clear
		m_pStatusLabel->SetText( "" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Updates status test at bottom of window
// Input  : wchar_t* (unicode string) - 
//-----------------------------------------------------------------------------
void CServerBrowserDialog::UpdateStatusText(wchar_t *unicode)
{
	if ( !m_pStatusLabel )
		return;

	if ( unicode && wcslen(unicode) > 0 )
	{
		m_pStatusLabel->SetText( unicode );
	}
	else
	{
		// clear
		m_pStatusLabel->SetText( "" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CServerBrowserDialog::OnGameListChanged()
{
	m_pGameList = dynamic_cast<IGameList *>(m_pTabPanel->GetActivePage());

	UpdateStatusText("");

	InvalidateLayout();
	Repaint();
}

//-----------------------------------------------------------------------------
// Purpose: returns a pointer to a static instance of this dialog
//-----------------------------------------------------------------------------
CServerBrowserDialog *CServerBrowserDialog::GetInstance()
{
	return s_InternetDlg;
}

//-----------------------------------------------------------------------------
// Purpose: Adds a server to the list of favorites
//-----------------------------------------------------------------------------
void CServerBrowserDialog::AddServerToFavorites(newgameserver_t &server)
{
	if ( !server.m_NetAdr.IsValid() || !m_pFavoritesData )
		return;

	// copy: the name may live in the very entry we're about to change
	char szName[MAX_SERVER_NAME];
	const char *pszFound = server.m_szServerName[0] ? server.m_szServerName : FindServerName( server.m_NetAdr );
	Q_strncpy( szName, pszFound ? pszFound : "", sizeof(szName) );
	const char *pszName = szName;

	KeyValues *pEntry = FindSavedServer( m_pFavoritesData, server.m_NetAdr );
	if ( !pEntry )
	{
		pEntry = new KeyValues( "server" );
		pEntry->SetString( "address", server.m_NetAdr.ToString() );
		pEntry->SetInt( "lastplayed", 0 );
		m_pFavoritesData->AddSubKey( pEntry );
	}

	if ( pszName && pszName[0] )
		pEntry->SetString( "name", pszName );
	else if ( !pEntry->GetString( "name", NULL ) )
		pEntry->SetString( "name", server.m_NetAdr.ToString() );

	SaveServerLists();

	if ( m_pFavorites )
	{
		m_pFavorites->LoadFavoritesList();

		// the favorites tab shows the new server right away
		if ( IsVisible() && m_pTabPanel->GetActivePage() == m_pFavorites )
			m_pFavorites->StartRefresh();
	}
}

void CServerBrowserDialog::RemoveServerFromFavorites( const netadr_t &adr )
{
	KeyValues *pEntry = FindSavedServer( m_pFavoritesData, adr );
	if ( !pEntry )
		return;

	m_pFavoritesData->RemoveSubKey( pEntry );
	pEntry->deleteThis();
	SaveServerLists();
}

void CServerBrowserDialog::AddServerToHistory( const netadr_t &adr, const char *pszNameIn )
{
	if ( !adr.IsValid() || !m_pHistoryData )
		return;

	// copy: the name may live in the very entry we're about to change
	char szName[MAX_SERVER_NAME];
	Q_strncpy( szName, pszNameIn ? pszNameIn : "", sizeof(szName) );
	const char *pszName = szName;

	KeyValues *pEntry = FindSavedServer( m_pHistoryData, adr );
	if ( !pEntry )
	{
		pEntry = new KeyValues( "server" );
		pEntry->SetString( "address", adr.ToString() );
		pEntry->SetString( "name", adr.ToString() );
		m_pHistoryData->AddSubKey( pEntry );
	}

	if ( pszName && pszName[0] )
		pEntry->SetString( "name", pszName );

	pEntry->SetInt( "lastplayed", (int)time( NULL ) );

	// keep the list short: drop the oldest entries
	int nCount = 0;
	for ( KeyValues *pCur = m_pHistoryData->GetFirstTrueSubKey(); pCur; pCur = pCur->GetNextTrueSubKey() )
		nCount++;

	while ( nCount > MAX_HISTORY_SERVERS )
	{
		KeyValues *pOldest = NULL;
		for ( KeyValues *pCur = m_pHistoryData->GetFirstTrueSubKey(); pCur; pCur = pCur->GetNextTrueSubKey() )
		{
			if ( !pOldest || pCur->GetInt( "lastplayed" ) < pOldest->GetInt( "lastplayed" ) )
				pOldest = pCur;
		}

		if ( !pOldest )
			break;

		m_pHistoryData->RemoveSubKey( pOldest );
		pOldest->deleteThis();
		nCount--;
	}

	SaveServerLists();

	// favorites keep the time we last played on them too
	KeyValues *pFavorite = FindSavedServer( m_pFavoritesData, adr );
	if ( pFavorite )
	{
		pFavorite->SetInt( "lastplayed", (int)time( NULL ) );
		SaveServerLists();
	}

	if ( m_pHistory )
	{
		m_pHistory->LoadHistoryList();

		if ( IsVisible() && m_pTabPanel->GetActivePage() == m_pHistory )
			m_pHistory->StartRefresh();
	}
}

void CServerBrowserDialog::RemoveServerFromHistory( const netadr_t &adr )
{
	KeyValues *pEntry = FindSavedServer( m_pHistoryData, adr );
	if ( !pEntry )
		return;

	m_pHistoryData->RemoveSubKey( pEntry );
	pEntry->deleteThis();
	SaveServerLists();
}

void CServerBrowserDialog::UpdateSavedServerName( KeyValues *pList, const netadr_t &adr, const char *pszName )
{
	if ( !pszName || !pszName[0] )
		return;

	KeyValues *pEntry = FindSavedServer( pList, adr );
	if ( !pEntry || !Q_strcmp( pEntry->GetString( "name", "" ), pszName ) )
		return;

	char szName[MAX_SERVER_NAME];
	Q_strncpy( szName, pszName, sizeof(szName) );
	pEntry->SetString( "name", szName );
	SaveServerLists();
}

void CServerBrowserDialog::AddCurrentServerToFavorites()
{
	if ( !IsConnectedToServer() )
		return;

	newgameserver_t server;
	memset( &server, 0, sizeof(server) );
	server.m_NetAdr = m_CurrentServerAdr;

	const char *pszName = FindServerName( m_CurrentServerAdr );
	if ( pszName )
		Q_strncpy( server.m_szServerName, pszName, sizeof(server.m_szServerName) );

	AddServerToFavorites( server );
}

KeyValues *CServerBrowserDialog::FindSavedServer( KeyValues *pList, const netadr_t &adr )
{
	if ( !pList )
		return NULL;

	for ( KeyValues *pEntry = pList->GetFirstTrueSubKey(); pEntry; pEntry = pEntry->GetNextTrueSubKey() )
	{
		netadr_t entryAdr;
		if ( entryAdr.SetFromString( pEntry->GetString( "address", "" ) ) && entryAdr.CompareAdr( adr ) )
			return pEntry;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: best known name for a server: any tab that lists it, then saved lists
//-----------------------------------------------------------------------------
const char *CServerBrowserDialog::FindServerName( const netadr_t &adr )
{
	CBaseGamesPage *pPages[] = { m_pInternetGames, m_pLanGames, m_pFavorites, m_pHistory };
	for ( int i = 0; i < ARRAYSIZE( pPages ); i++ )
	{
		if ( !pPages[i] )
			continue;

		newgameserver_t *pServer = pPages[i]->FindServerByAddress( adr );
		if ( pServer && pServer->m_bHadSuccessfulResponse && pServer->m_szServerName[0] )
			return pServer->m_szServerName;
	}

	KeyValues *pEntry = FindSavedServer( m_pFavoritesData, adr );
	if ( !pEntry )
		pEntry = FindSavedServer( m_pHistoryData, adr );

	if ( pEntry )
	{
		const char *pszName = pEntry->GetString( "name", "" );
		if ( pszName[0] )
			return pszName;
	}

	return NULL;
}

void CServerBrowserDialog::ReplaceSavedSubKey( KeyValues *pFile, KeyValues *pData )
{
	if ( !pFile || !pData )
		return;

	KeyValues *pOld = pFile->FindKey( pData->GetName() );
	if ( pOld )
	{
		pFile->RemoveSubKey( pOld );
		pOld->deleteThis();
	}

	pFile->AddSubKey( pData->MakeCopy() );
}

//-----------------------------------------------------------------------------
// Purpose: writes favorites/history right away, so they survive a crash
//-----------------------------------------------------------------------------
void CServerBrowserDialog::SaveServerLists()
{
	KeyValues *pFile = new KeyValues( "Filters" );
	pFile->LoadFromFile( g_pFullFileSystem, SERVERBROWSER_FILE, "CONFIG" );

	ReplaceSavedSubKey( pFile, m_pFavoritesData );
	ReplaceSavedSubKey( pFile, m_pHistoryData );

	pFile->SaveToFile( g_pFullFileSystem, SERVERBROWSER_FILE, "CONFIG" );
	pFile->deleteThis();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CServerContextMenu *CServerBrowserDialog::GetContextMenu(vgui::Panel *pPanel)
{
	// create a drop down for this object's states
	if (m_pContextMenu)
		delete m_pContextMenu;
	m_pContextMenu = new CServerContextMenu(this);
	m_pContextMenu->SetAutoDelete( false );

    if (!pPanel)
    {
	    m_pContextMenu->SetParent(this);
    }
    else
    {
        m_pContextMenu->SetParent(pPanel);
    }

	m_pContextMenu->SetVisible(false);
	return m_pContextMenu;
}

//-----------------------------------------------------------------------------
// Purpose: begins the process of joining a server from a game list
//			the game info dialog it opens will also update the game list
//-----------------------------------------------------------------------------
CDialogGameInfo *CServerBrowserDialog::JoinGame(IGameList *gameList, newgameserver_t *pServer)
{
	// open the game info dialog, then mark it to attempt to connect right away
	CDialogGameInfo *gameDialog = OpenGameInfoDialog(gameList, pServer);

	// set the dialog name to be the server name
	gameDialog->Connect();

	return gameDialog;
}

//-----------------------------------------------------------------------------
// Purpose: joins a game by a specified IP, not attached to any game list
//-----------------------------------------------------------------------------
CDialogGameInfo *CServerBrowserDialog::JoinGame(int serverIP, int serverPort, const char *pszConnectCode)
{
	// open the game info dialog, then mark it to attempt to connect right away
	CDialogGameInfo *gameDialog = OpenGameInfoDialog( serverIP, serverPort, pszConnectCode );

	// set the dialog name to be the server name
	gameDialog->Connect();

	return gameDialog;
}

//-----------------------------------------------------------------------------
// Purpose: opens a game info dialog from a game list
//-----------------------------------------------------------------------------
CDialogGameInfo *CServerBrowserDialog::OpenGameInfoDialog( IGameList *gameList, newgameserver_t *pServer )
{
	CDialogGameInfo *gameDialog = new CDialogGameInfo( NULL, pServer->m_NetAdr.GetIPHostByteOrder(), pServer->m_NetAdr.GetPort(), gameList->GetConnectCode() );
	gameDialog->SetParent(GetVParent());
	gameDialog->AddActionSignalTarget(this);
	gameDialog->Run( pServer->m_szServerName );
	int i = m_GameInfoDialogs.AddToTail();
	m_GameInfoDialogs[i] = gameDialog;
	return gameDialog;
}

//-----------------------------------------------------------------------------
// Purpose: opens a game info dialog by a specified IP, not attached to any game list
//-----------------------------------------------------------------------------
CDialogGameInfo *CServerBrowserDialog::OpenGameInfoDialog( int serverIP, uint16 port, const char *pszConnectCode )
{
	CDialogGameInfo *gameDialog = new CDialogGameInfo(NULL, serverIP, port, pszConnectCode);
	gameDialog->AddActionSignalTarget(this);
	gameDialog->SetParent(GetVParent());
	gameDialog->Run("");
	int i = m_GameInfoDialogs.AddToTail();
	m_GameInfoDialogs[i] = gameDialog;
	return gameDialog;
}

//-----------------------------------------------------------------------------
// Purpose: closes all the game info dialogs
//-----------------------------------------------------------------------------
void CServerBrowserDialog::CloseAllGameInfoDialogs()
{
	for (int i = 0; i < m_GameInfoDialogs.Count(); i++)
	{
		vgui::Panel *dlg = m_GameInfoDialogs[i];
		if (dlg)
		{
			vgui::ivgui()->PostMessage(dlg->GetVPanel(), new KeyValues("Close"), NULL);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: accessor to the filter save data
//-----------------------------------------------------------------------------
KeyValues *CServerBrowserDialog::GetFilterSaveData(const char *filterSet)
{
	return m_pFilterData->FindKey(filterSet, true);
}

//-----------------------------------------------------------------------------
// Purpose: gets the name of the mod directory we're restricted to accessing, NULL if none
//-----------------------------------------------------------------------------
const char *CServerBrowserDialog::GetActiveModName()
{
	return m_szModDir[0] ? m_szModDir : NULL;
}


//-----------------------------------------------------------------------------
// Purpose: gets the name of the mod directory we're restricted to accessing, NULL if none
//-----------------------------------------------------------------------------
const char *CServerBrowserDialog::GetActiveGameName()
{
	return m_szGameName[0] ? m_szGameName : NULL;
}

//-----------------------------------------------------------------------------
// Purpose: return the app id to limit game queries to, set by Source/HL1 engines (NOT by filter settings, that is per page)
//-----------------------------------------------------------------------------
CGameID &CServerBrowserDialog::GetActiveAppID()
{
	// !TEST! Un comment this to force a particular AppID
	//m_iLimitAppID = CGameID( 440 );
	return m_iLimitAppID;
}


//-----------------------------------------------------------------------------
// Purpose: receives a specified game is active, so no other game types can be displayed in server list
//-----------------------------------------------------------------------------
void CServerBrowserDialog::OnActiveGameName( KeyValues *pKV )
{
	Q_strncpy(m_szModDir, pKV->GetString( "name" ), sizeof(m_szModDir));
	Q_strncpy(m_szGameName, pKV->GetString( "game" ), sizeof(m_szGameName));
	m_iLimitAppID = CGameID( pKV->GetUint64( "appid", 0 ) );	
	// reload filter settings (since they are no forced to be game specific)
	ReloadFilterSettings();
}

//-----------------------------------------------------------------------------
// Purpose: resets all pages filter settings
//-----------------------------------------------------------------------------
void CServerBrowserDialog::ReloadFilterSettings()
{
	m_pInternetGames->LoadFilterSettings();
	//m_pSpectateGames->LoadFilterSettings();
	if ( m_pFavorites )
		m_pFavorites->LoadFilterSettings();
	if ( m_pLanGames )
		m_pLanGames->LoadFilterSettings();
	if ( m_pHistory )
		m_pHistory->LoadFilterSettings();
}

//-----------------------------------------------------------------------------
// Purpose: Adds server to the history, saves as currently connected server
//-----------------------------------------------------------------------------
void CServerBrowserDialog::OnConnectToGame( KeyValues *pMessageValues )
{
	int ip = pMessageValues->GetInt( "ip" );
	int connectionPort = pMessageValues->GetInt( "connectionport" );
	int queryPort = pMessageValues->GetInt( "queryport" );

	// v34 servers don't put "QueryPort" into the startup string table: the game port answers queries
	if ( !queryPort )
		queryPort = connectionPort;

	if ( !ip || !connectionPort )
		return;

	uint32 unIP = htonl( ip );

	memset( &m_CurrentConnection, 0, sizeof(gameserveritem_t) );
	m_CurrentConnection.m_NetAdr.SetIP( unIP );
	m_CurrentConnection.m_NetAdr.SetQueryPort( queryPort );
	m_CurrentConnection.m_NetAdr.SetConnectionPort( (unsigned short)connectionPort );

	m_CurrentServerAdr.SetIPAndPort( unIP, (unsigned short)connectionPort );

	// remember the server in the history tab (not our own local game)
	if ( m_pHistory && !m_CurrentServerAdr.IsLocalhost() )
	{
		AddServerToHistory( m_CurrentServerAdr, FindServerName( m_CurrentServerAdr ) );
	}

	// tell the game info dialogs, so they can cancel if we have connected
	// to a server they were auto-retrying
	for (int i = 0; i < m_GameInfoDialogs.Count(); i++)
	{
		vgui::Panel *dlg = m_GameInfoDialogs[i];
		if (dlg)
		{
			KeyValues *kv = new KeyValues("ConnectedToGame", "ip", unIP, "connectionport", connectionPort);
			kv->SetInt( "queryport", queryPort );
			vgui::ivgui()->PostMessage(dlg->GetVPanel(), kv, NULL);
		}
	}

	m_bCurrentlyConnected = true;

	// forward to favorites
	if ( m_pFavorites )
		m_pFavorites->OnConnectToGame();

	// Now we want to track which tabs have the quick list button checked
	int iQuickListBitField = 0;
	if ( m_pLanGames && m_pLanGames->IsQuickListButtonChecked() )
	{
		iQuickListBitField |= ( 1 << 1 );
	}
	/*if ( m_pSpectateGames && m_pSpectateGames->IsQuickListButtonChecked() )
	{
		iQuickListBitField |= ( 1 << 2 );
	}*/
	if ( m_pHistory && m_pHistory->IsQuickListButtonChecked() )
	{
		iQuickListBitField |= ( 1 << 3 );
	}
	if ( m_pFavorites && m_pFavorites->IsQuickListButtonChecked() )
	{
		iQuickListBitField |= ( 1 << 4 );
	}
	if ( m_pInternetGames && m_pInternetGames->IsQuickListButtonChecked() )
	{
		iQuickListBitField |= ( 1 << 5 );
	}

	// Set the value so that the client.dll can use it for gamestats
	sb_quick_list_bit_field.SetValue( iQuickListBitField );

	// TF2 wants to close this dialog when the player connects to a game
	if ( GameSupportsReplay() )
	{
		ConVarRef sb_close_browser_on_connect( "sb_close_browser_on_connect" );
		if ( sb_close_browser_on_connect.IsValid() )
		{
			if ( sb_close_browser_on_connect.GetBool() == true )
			{
				OnClose();
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Clears currently connected server
//-----------------------------------------------------------------------------
void CServerBrowserDialog::OnDisconnectFromGame( void )
{
	m_bCurrentlyConnected = false;
	memset( &m_CurrentConnection, 0, sizeof(gameserveritem_t) );
	m_CurrentServerAdr.Clear();

	// forward to favorites
	if ( m_pFavorites )
		m_pFavorites->OnDisconnectFromGame();
}

//-----------------------------------------------------------------------------
// Purpose: Called when start start loading, so we can cease server browser activity
//-----------------------------------------------------------------------------
void CServerBrowserDialog::OnLoadingStarted( void )
{
	m_pInternetGames->OnLoadingStarted();
	//m_pSpectateGames->OnLoadingStarted();
	if ( m_pFavorites )
		m_pFavorites->OnLoadingStarted();
	if ( m_pLanGames )
		m_pLanGames->OnLoadingStarted();
	if ( m_pHistory )
		m_pHistory->OnLoadingStarted();
}

//-----------------------------------------------------------------------------
// Purpose: Passes build mode activation down into the pages
//-----------------------------------------------------------------------------
void CServerBrowserDialog::ActivateBuildMode()
{
	// no subpanel, no build mode
	EditablePanel *panel = dynamic_cast<EditablePanel *>(m_pTabPanel->GetActivePage());
	if (!panel)
		return;

	panel->ActivateBuildMode();
}

//-----------------------------------------------------------------------------
// Purpose: gets the default position and size on the screen to appear the first time
//-----------------------------------------------------------------------------
bool CServerBrowserDialog::GetDefaultScreenPosition(int &x, int &y, int &wide, int &tall)
{
	int wx, wy, ww, wt;
	surface()->GetWorkspaceBounds( wx, wy, ww, wt );
	x = wx + (int)(ww * 0.05);
	y = wy + (int)(wt * 0.4);
	wide = (int)(ww * 0.5);
	tall = (int)(wt * 0.55);
	return true;
}

void CServerBrowserDialog::OnKeyCodePressed( vgui::KeyCode code )
{
	// Handle close here, CBasePanel parent doesn't support "DialogClosing" command
	ButtonCode_t nButtonCode = GetBaseButtonCode( code );

	if ( nButtonCode == KEY_XBUTTON_B || nButtonCode == STEAMCONTROLLER_B )
	{
		OnCommand( "Close" );
		return;
	}
	else if ( nButtonCode == KEY_XBUTTON_A || nButtonCode == STEAMCONTROLLER_A )
	{
		//OnOK( false );
		//return;
	}
	else if ( nButtonCode == KEY_XBUTTON_UP || 
			  nButtonCode == KEY_XSTICK1_UP ||
			  nButtonCode == KEY_XSTICK2_UP || 
			  nButtonCode == STEAMCONTROLLER_DPAD_UP ||
			  nButtonCode == KEY_UP ||
			  nButtonCode == KEY_XBUTTON_DOWN || 
			  nButtonCode == KEY_XSTICK1_DOWN ||
			  nButtonCode == KEY_XSTICK2_DOWN || 
			  nButtonCode == STEAMCONTROLLER_DPAD_DOWN ||
			  nButtonCode == KEY_DOWN )
	{
		CBaseGamesPage *pGamesPage = dynamic_cast< CBaseGamesPage* >( m_pTabPanel->GetActivePage() );
		if ( pGamesPage )
		{
			ListPanel *pListPanel = dynamic_cast< ListPanel * >( pGamesPage->GetActiveList() );
			if ( pListPanel )
			{
				if ( pListPanel->GetSelectedItem( 0 ) == -1 )
				{
					pListPanel->SetSingleSelectedItem( pListPanel->GetItemIDFromRow( 0 ) );
					pListPanel->RequestFocus();
					return;
				}
				else if ( !pListPanel->HasFocus() )
				{
					pListPanel->RequestFocus();
					return;
				}
			}
		}
	}

	BaseClass::OnKeyCodePressed( code );
}
