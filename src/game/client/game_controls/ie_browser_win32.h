//========= cssv34-client: offscreen Internet Explorer browser ================//
//
// Purpose: Hosts the Windows WebBrowser control (Trident) in a hidden window and
//          renders pages into an RGBA buffer, so HTML MOTDs can be drawn inside
//          the game without the Steam HTML surface. Windows only.
//
//          Plain Win32/COM code without engine headers. Everything runs on the
//          main thread (the browser needs an STA and the thread's message pump).
//
//=============================================================================//

#ifndef IE_BROWSER_WIN32_H
#define IE_BROWSER_WIN32_H
#ifdef _WIN32
#pragma once
#endif

class CIEBrowserHost;

class CIEBrowser
{
public:
	CIEBrowser();
	~CIEBrowser();

	// creates the browser; false if the WebBrowser control is not available
	bool Init( int nWide, int nTall );
	void Shutdown();
	bool IsValid() const { return m_pHost != 0; }

	// UTF-8 URL (http://, https://, about:blank)
	void Navigate( const char *pszURL );
	// shows HTML text (an MOTD sent as text, not as a URL) in about:blank
	void NavigateHTML( const char *pszHTML );
	// HTML text: 0 none, 1 waiting for about:blank, 2 written, < 0 failed
	// (-1 no HTML document, -2 no memory, -3 write failed, -4 text not convertible)
	int GetHTMLState() const;
	int GetHTMLWrites() const;		// diagnostics: times the text was written
	int GetHTMLBodyChars() const;	// diagnostics: size of the page body, -1 unknown
	void SetSize( int nWide, int nTall );

	// renders the current view into pRGBA (nWide * nTall * 4 bytes, opaque RGBA)
	bool Render( unsigned char *pRGBA, int nWide, int nTall );

	// page coordinates in pixels of the browser view
	void MouseMove( int x, int y );
	void MouseButton( int x, int y, bool bDown );
	void Scroll( int nPixels );

private:
	CIEBrowserHost *m_pHost;
	bool m_bHTMLConvertFailed;
};

#endif // IE_BROWSER_WIN32_H
