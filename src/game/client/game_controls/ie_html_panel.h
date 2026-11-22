//========= cssv34-client: HTML panel without the Steam HTML surface ==========//
//
// Purpose: VGUI panel that shows a web page rendered by the Windows WebBrowser
//          control (see ie_browser_win32.h). Used for HTML MOTDs when Steam's
//          HTML surface isn't available. On other platforms IsAvailable() is
//          false and the panel stays empty.
//
//=============================================================================//

#ifndef IE_HTML_PANEL_H
#define IE_HTML_PANEL_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Panel.h>
#include "tier1/utlvector.h"

class CIEBrowser;

class CIEHTMLPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CIEHTMLPanel, vgui::Panel );

public:
	CIEHTMLPanel( vgui::Panel *pParent, const char *pszName );
	virtual ~CIEHTMLPanel();

	static bool IsAvailable();

	// creates the browser on first use; false if it can't be created
	bool OpenURL( const char *pszURL );
	// shows HTML text (an MOTD sent as text, not as a URL); same rights as a web page
	bool OpenHTML( const char *pszHTML );

	// destroys the browser (stops scripts, frees memory); OpenURL creates it again
	void CloseBrowser();
	bool HasBrowser() const { return m_pBrowser != NULL; }

protected:
	virtual void Paint();
	virtual void OnThink();
	virtual void OnSizeChanged( int newWide, int newTall );
	virtual void OnCursorMoved( int x, int y );
	virtual void OnMousePressed( vgui::MouseCode code );
	virtual void OnMouseReleased( vgui::MouseCode code );
	virtual void OnMouseWheeled( int delta );

private:
	void GetCursorLocal( int &x, int &y );
	bool CreateBrowser();

	CIEBrowser				*m_pBrowser;
	bool					m_bBrowserFailed;
	int						m_nTextureID;
	int						m_nTexWide, m_nTexTall;	// power of two, as the surface allocates it
	CUtlVector<unsigned char> m_Pixels;
	int						m_nFrameWide, m_nFrameTall;
	bool					m_bHasFrame;
	float					m_flNextRender;
	int						m_nLastHTMLState;
};

#endif // IE_HTML_PANEL_H
