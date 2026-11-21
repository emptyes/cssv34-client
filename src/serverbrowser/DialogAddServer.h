//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef DIALOGADDSERVER_H
#define DIALOGADDSERVER_H
#ifdef _WIN32
#pragma once
#endif

class CAddServerGameList;
class IGameList;

//-----------------------------------------------------------------------------
// Purpose: Dialog which lets the user add a server by IP address
//-----------------------------------------------------------------------------
class CDialogAddServer : public vgui::Frame, public IServerPingResponse
{
	DECLARE_CLASS_SIMPLE( CDialogAddServer, vgui::Frame );
	friend class CAddServerGameList;

public:
	CDialogAddServer(vgui::Panel *parent, IGameList *gameList);
	~CDialogAddServer();

	// IServerPingResponse: answers of every pinged server arrive here, only ours are shown
	virtual void ServerResponded( newgameserver_t &server );
	void ServerFailedToRespond();
	MESSAGE_FUNC( OnTestServersTimeout, "TestServersTimeout" );

	void ApplySchemeSettings( vgui::IScheme *pScheme );

	MESSAGE_FUNC( OnItemSelected, "ItemSelected" );
private:
	virtual void OnCommand(const char *command);

	void OnOK();

	void TestServers();
	MESSAGE_FUNC( OnTextChanged, "TextChanged" );

	virtual void FinishAddServer( newgameserver_t &pServer );
	virtual bool AllowInvalidIPs( void ) { return false; }

protected:
	IGameList *m_pGameList;
	
	vgui::Button *m_pTestServersButton;
	vgui::Button *m_pAddServerButton;
	vgui::Button *m_pAddSelectedServerButton;
	
	vgui::PropertySheet *m_pTabPanel;
	vgui::TextEntry *m_pTextEntry;
	vgui::ListPanel *m_pDiscoveredGames;
	int m_OriginalHeight;
	CUtlVector<newgameserver_t> m_Servers;
	CUtlVector<HServerQuery> m_Queries;
	CUtlVector<netadr_t> m_TestAddresses;
};

#endif // DIALOGADDSERVER_H
