//========= cssv34-client: HTML panel without the Steam HTML surface ==========//
//
// Purpose: see ie_html_panel.h
//
//=============================================================================//

#include "cbase.h"
#include "ie_html_panel.h"
#include "ie_browser_win32.h"

#include <vgui/ISurface.h>
#include <vgui/IInput.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

// the page is re-rendered at this rate while the panel is visible (animations, loading)
static const float IE_HTML_RENDER_INTERVAL = 0.1f;
static const int IE_HTML_MAX_SIZE = 2048;

CIEHTMLPanel::CIEHTMLPanel( Panel *pParent, const char *pszName ) : BaseClass( pParent, pszName )
{
	m_pBrowser = NULL;
	m_bBrowserFailed = false;
	m_nTextureID = -1;
	m_nTexWide = m_nTexTall = 0;
	m_nFrameWide = m_nFrameTall = 0;
	m_bHasFrame = false;
	m_flNextRender = 0.0f;

	SetPaintBackgroundEnabled( false );
	SetMouseInputEnabled( true );
	SetKeyBoardInputEnabled( false );
}

CIEHTMLPanel::~CIEHTMLPanel()
{
	CloseBrowser();

	if ( m_nTextureID != -1 && surface() )
	{
		surface()->DestroyTextureID( m_nTextureID );
		m_nTextureID = -1;
	}
}

bool CIEHTMLPanel::IsAvailable()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

bool CIEHTMLPanel::CreateBrowser()
{
#ifdef _WIN32
	if ( m_pBrowser )
		return true;

	// don't retry every time the MOTD opens if the control is missing on this system
	if ( m_bBrowserFailed )
		return false;

	int wide, tall;
	GetSize( wide, tall );

	m_pBrowser = new CIEBrowser();
	if ( !m_pBrowser->Init( clamp( wide, 1, IE_HTML_MAX_SIZE ), clamp( tall, 1, IE_HTML_MAX_SIZE ) ) )
	{
		Warning( "HTML MOTD: the Windows WebBrowser control is not available\n" );
		delete m_pBrowser;
		m_pBrowser = NULL;
		m_bBrowserFailed = true;
		return false;
	}
	return true;
#else
	return false;
#endif
}

bool CIEHTMLPanel::OpenURL( const char *pszURL )
{
	if ( !pszURL || !pszURL[0] || !CreateBrowser() )
		return false;

	m_bHasFrame = false;
	m_flNextRender = 0.0f;
	m_pBrowser->Navigate( pszURL );
	return true;
}

bool CIEHTMLPanel::OpenHTML( const char *pszHTML )
{
	if ( !pszHTML || !pszHTML[0] || !CreateBrowser() )
		return false;

	m_bHasFrame = false;
	m_flNextRender = 0.0f;
	m_pBrowser->NavigateHTML( pszHTML );
	return true;
}

void CIEHTMLPanel::CloseBrowser()
{
	if ( m_pBrowser )
	{
		delete m_pBrowser;
		m_pBrowser = NULL;
	}
	m_bHasFrame = false;
}

void CIEHTMLPanel::OnSizeChanged( int newWide, int newTall )
{
	BaseClass::OnSizeChanged( newWide, newTall );

	if ( m_pBrowser )
		m_pBrowser->SetSize( clamp( newWide, 1, IE_HTML_MAX_SIZE ), clamp( newTall, 1, IE_HTML_MAX_SIZE ) );
	m_flNextRender = 0.0f;
}

void CIEHTMLPanel::OnThink()
{
	BaseClass::OnThink();

	if ( !m_pBrowser || Plat_FloatTime() < m_flNextRender )
		return;
	m_flNextRender = Plat_FloatTime() + IE_HTML_RENDER_INTERVAL;

	int wide, tall;
	GetSize( wide, tall );
	wide = clamp( wide, 0, IE_HTML_MAX_SIZE );
	tall = clamp( tall, 0, IE_HTML_MAX_SIZE );
	if ( wide <= 0 || tall <= 0 )
		return;

	m_Pixels.SetCount( wide * tall * 4 );
	if ( !m_pBrowser->Render( m_Pixels.Base(), wide, tall ) )
		return;

	// The surface allocates a procedural texture once, rounded up to a power of two, and
	// never resizes it: create a new one when the panel no longer fits (resolution change),
	// and draw only the part that holds the page (see Paint).
	int texWide = SmallestPowerOfTwoGreaterOrEqual( wide );
	int texTall = SmallestPowerOfTwoGreaterOrEqual( tall );
	if ( m_nTextureID != -1 && ( texWide != m_nTexWide || texTall != m_nTexTall ) )
	{
		surface()->DestroyTextureID( m_nTextureID );
		m_nTextureID = -1;
	}
	if ( m_nTextureID == -1 )
	{
		m_nTextureID = surface()->CreateNewTextureID( true );
		m_nTexWide = texWide;
		m_nTexTall = texTall;
	}

	surface()->DrawSetTextureRGBA( m_nTextureID, m_Pixels.Base(), wide, tall, 0, true );
	m_nFrameWide = wide;
	m_nFrameTall = tall;
	m_bHasFrame = true;
}

void CIEHTMLPanel::Paint()
{
	if ( !m_bHasFrame || m_nTextureID == -1 )
		return;

	surface()->DrawSetColor( 255, 255, 255, 255 );
	surface()->DrawSetTexture( m_nTextureID );
	surface()->DrawTexturedSubRect( 0, 0, m_nFrameWide, m_nFrameTall, 0.0f, 0.0f,
		(float)m_nFrameWide / m_nTexWide, (float)m_nFrameTall / m_nTexTall );
}

void CIEHTMLPanel::GetCursorLocal( int &x, int &y )
{
	input()->GetCursorPos( x, y );
	ScreenToLocal( x, y );
}

void CIEHTMLPanel::OnCursorMoved( int x, int y )
{
	if ( m_pBrowser )
		m_pBrowser->MouseMove( x, y );
}

void CIEHTMLPanel::OnMousePressed( MouseCode code )
{
	if ( !m_pBrowser || code != MOUSE_LEFT )
		return;

	int x, y;
	GetCursorLocal( x, y );
	m_pBrowser->MouseButton( x, y, true );
	m_flNextRender = 0.0f;
}

void CIEHTMLPanel::OnMouseReleased( MouseCode code )
{
	if ( !m_pBrowser || code != MOUSE_LEFT )
		return;

	int x, y;
	GetCursorLocal( x, y );
	m_pBrowser->MouseButton( x, y, false );
	m_flNextRender = 0.0f;
}

void CIEHTMLPanel::OnMouseWheeled( int delta )
{
	if ( !m_pBrowser )
		return;

	m_pBrowser->Scroll( -delta * 60 );
	m_flNextRender = 0.0f;
}
