//========= cssv34-client: offscreen Internet Explorer browser ================//
//
// Purpose: see ie_browser_win32.h. Source 2007 (and so the original v34 client)
//          drew HTML MOTDs the same way: the WebBrowser ActiveX control in a
//          hidden window, IViewObject::Draw into a bitmap, the bitmap uploaded
//          as a texture. This is a new, x64 clean implementation of that idea.
//
//=============================================================================//

#include "ie_browser_win32.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <oleidl.h>
#include <exdisp.h>
#include <exdispid.h>
#include <mshtml.h>
#include <mshtmhst.h>
#include <mshtmdid.h>
#include <shellapi.h>
#include <wchar.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "oleaut32.lib" )
#pragma comment( lib, "uuid.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "advapi32.lib" )
#endif

#ifndef DISPID_NEWWINDOW3
#define DISPID_NEWWINDOW3 273
#endif

static const wchar_t *HOST_WINDOW_CLASS = L"cssv34_ie_host";

//-----------------------------------------------------------------------------
// The WebBrowser control emulates IE7 unless told otherwise; ask for IE11 standards
// mode for this executable (per-user, the same key every app embedding IE uses).
//-----------------------------------------------------------------------------
static void SetBrowserEmulation()
{
	static bool s_bDone = false;
	if ( s_bDone )
		return;
	s_bDone = true;

	wchar_t path[MAX_PATH];
	DWORD len = GetModuleFileNameW( NULL, path, MAX_PATH );
	if ( !len || len >= MAX_PATH )
		return;

	const wchar_t *exe = wcsrchr( path, L'\\' );
	exe = exe ? exe + 1 : path;

	HKEY key;
	if ( RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Internet Explorer\\Main\\FeatureControl\\FEATURE_BROWSER_EMULATION",
			0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL ) == ERROR_SUCCESS )
	{
		DWORD value = 11001; // IE11 edge mode
		RegSetValueExW( key, exe, 0, REG_DWORD, (const BYTE *)&value, sizeof( value ) );
		RegCloseKey( key );
	}
}

static bool RegisterHostWindowClass()
{
	static bool s_bRegistered = false;
	if ( s_bRegistered )
		return true;

	WNDCLASSW wc;
	memset( &wc, 0, sizeof( wc ) );
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW( NULL );
	wc.lpszClassName = HOST_WINDOW_CLASS;
	if ( !RegisterClassW( &wc ) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS )
		return false;

	s_bRegistered = true;
	return true;
}

//-----------------------------------------------------------------------------
// OLE container for the WebBrowser control. One object implements every site
// interface; its lifetime is reference counted (the owner holds one reference).
//-----------------------------------------------------------------------------
class CIEBrowserHost :
	public IOleClientSite,
	public IOleInPlaceSite,
	public IOleInPlaceFrame,
	public IDocHostUIHandler,
	public IDocHostShowUI,
	public IDispatch
{
public:
	CIEBrowserHost();

	bool Create( int nWide, int nTall );
	void Destroy();

	void Navigate( const char *pszURL );
	void SetSize( int nWide, int nTall );
	bool Render( unsigned char *pRGBA, int nWide, int nTall );
	void PostMouse( UINT msg, WPARAM wParam, int x, int y );
	void Scroll( int nPixels );

	// IUnknown
	STDMETHODIMP QueryInterface( REFIID riid, void **ppv );
	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement( &m_cRef ); }
	STDMETHODIMP_(ULONG) Release()
	{
		LONG c = InterlockedDecrement( &m_cRef );
		if ( c == 0 )
			delete this;
		return c;
	}

	// IOleClientSite
	STDMETHODIMP SaveObject() { return E_NOTIMPL; }
	STDMETHODIMP GetMoniker( DWORD, DWORD, IMoniker **ppmk ) { if ( ppmk ) *ppmk = NULL; return E_NOTIMPL; }
	STDMETHODIMP GetContainer( IOleContainer **ppContainer ) { if ( ppContainer ) *ppContainer = NULL; return E_NOINTERFACE; }
	STDMETHODIMP ShowObject() { return S_OK; }
	STDMETHODIMP OnShowWindow( BOOL ) { return S_OK; }
	STDMETHODIMP RequestNewObjectLayout() { return E_NOTIMPL; }

	// IOleWindow (shared by IOleInPlaceSite and IOleInPlaceFrame)
	STDMETHODIMP GetWindow( HWND *phwnd ) { if ( !phwnd ) return E_POINTER; *phwnd = m_hWnd; return S_OK; }
	STDMETHODIMP ContextSensitiveHelp( BOOL ) { return E_NOTIMPL; }

	// IOleInPlaceSite
	STDMETHODIMP CanInPlaceActivate() { return S_OK; }
	STDMETHODIMP OnInPlaceActivate() { return S_OK; }
	STDMETHODIMP OnUIActivate() { return S_OK; }
	STDMETHODIMP GetWindowContext( IOleInPlaceFrame **ppFrame, IOleInPlaceUIWindow **ppDoc, LPRECT lprcPosRect, LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO lpFrameInfo );
	STDMETHODIMP Scroll( SIZE ) { return E_NOTIMPL; }
	STDMETHODIMP OnUIDeactivate( BOOL ) { return S_OK; }
	STDMETHODIMP OnInPlaceDeactivate() { return S_OK; }
	STDMETHODIMP DiscardUndoState() { return E_NOTIMPL; }
	STDMETHODIMP DeactivateAndUndo() { return E_NOTIMPL; }
	STDMETHODIMP OnPosRectChange( LPCRECT ) { return S_OK; }

	// IOleInPlaceUIWindow / IOleInPlaceFrame
	STDMETHODIMP GetBorder( LPRECT ) { return INPLACE_E_NOTOOLSPACE; }
	STDMETHODIMP RequestBorderSpace( LPCBORDERWIDTHS ) { return INPLACE_E_NOTOOLSPACE; }
	STDMETHODIMP SetBorderSpace( LPCBORDERWIDTHS ) { return S_OK; }
	STDMETHODIMP SetActiveObject( IOleInPlaceActiveObject *, LPCOLESTR ) { return S_OK; }
	STDMETHODIMP InsertMenus( HMENU, LPOLEMENUGROUPWIDTHS ) { return E_NOTIMPL; }
	STDMETHODIMP SetMenu( HMENU, HOLEMENU, HWND ) { return S_OK; }
	STDMETHODIMP RemoveMenus( HMENU ) { return E_NOTIMPL; }
	STDMETHODIMP SetStatusText( LPCOLESTR ) { return S_OK; }
	STDMETHODIMP EnableModeless( BOOL ) { return S_OK; } // also IDocHostUIHandler::EnableModeless
	STDMETHODIMP TranslateAccelerator( LPMSG, WORD ) { return S_FALSE; }

	// IDocHostUIHandler
	STDMETHODIMP ShowContextMenu( DWORD, POINT *, IUnknown *, IDispatch * ) { return S_OK; } // no context menu
	STDMETHODIMP GetHostInfo( DOCHOSTUIINFO *pInfo );
	STDMETHODIMP ShowUI( DWORD, IOleInPlaceActiveObject *, IOleCommandTarget *, IOleInPlaceFrame *, IOleInPlaceUIWindow * ) { return S_OK; }
	STDMETHODIMP HideUI() { return S_OK; }
	STDMETHODIMP UpdateUI() { return S_OK; }
	STDMETHODIMP OnDocWindowActivate( BOOL ) { return S_OK; }
	STDMETHODIMP OnFrameWindowActivate( BOOL ) { return S_OK; }
	STDMETHODIMP ResizeBorder( LPCRECT, IOleInPlaceUIWindow *, BOOL ) { return S_OK; }
	STDMETHODIMP TranslateAccelerator( LPMSG, const GUID *, DWORD ) { return S_FALSE; }
	STDMETHODIMP GetOptionKeyPath( LPOLESTR *pchKey, DWORD ) { if ( pchKey ) *pchKey = NULL; return S_FALSE; }
	STDMETHODIMP GetDropTarget( IDropTarget *, IDropTarget **ppDropTarget ) { if ( ppDropTarget ) *ppDropTarget = NULL; return E_NOTIMPL; }
	STDMETHODIMP GetExternal( IDispatch **ppDispatch ) { if ( ppDispatch ) *ppDispatch = NULL; return S_FALSE; }
	STDMETHODIMP TranslateUrl( DWORD, LPWSTR, LPWSTR *ppchURLOut ) { if ( ppchURLOut ) *ppchURLOut = NULL; return S_FALSE; }
	STDMETHODIMP FilterDataObject( IDataObject *, IDataObject **ppDORet ) { if ( ppDORet ) *ppDORet = NULL; return S_FALSE; }

	// IDocHostShowUI: swallow alert()/confirm() boxes, they would pop up over the game
	STDMETHODIMP ShowMessage( HWND, LPOLESTR, LPOLESTR, DWORD, LPOLESTR, DWORD, LRESULT *plResult ) { if ( plResult ) *plResult = IDCANCEL; return S_OK; }
	STDMETHODIMP ShowHelp( HWND, LPOLESTR, UINT, DWORD, POINT, IDispatch * ) { return S_OK; }

	// IDispatch: DWebBrowserEvents2 sink and ambient properties of the site
	STDMETHODIMP GetTypeInfoCount( UINT *pctinfo ) { if ( pctinfo ) *pctinfo = 0; return S_OK; }
	STDMETHODIMP GetTypeInfo( UINT, LCID, ITypeInfo **ppTInfo ) { if ( ppTInfo ) *ppTInfo = NULL; return E_NOTIMPL; }
	STDMETHODIMP GetIDsOfNames( REFIID, LPOLESTR *, UINT, LCID, DISPID * ) { return E_NOTIMPL; }
	STDMETHODIMP Invoke( DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr );

private:
	virtual ~CIEBrowserHost() {}

	HWND FindServerWindow();
	void ReleaseBitmap();

	LONG				m_cRef;
	HWND				m_hWnd;
	int					m_nWide, m_nTall;
	bool				m_bOleInitialized;

	IOleObject			*m_pOleObject;
	IOleInPlaceObject	*m_pInPlaceObject;
	IViewObject			*m_pViewObject;
	IWebBrowser2		*m_pBrowser;
	IConnectionPoint	*m_pConnectionPoint;
	DWORD				m_dwAdviseCookie;

	HDC					m_hMemDC;
	HBITMAP				m_hBitmap;
	HGDIOBJ				m_hOldBitmap;
	void				*m_pBits;
	int					m_nBitmapWide, m_nBitmapTall;
};

CIEBrowserHost::CIEBrowserHost()
{
	m_cRef = 1;
	m_hWnd = NULL;
	m_nWide = m_nTall = 0;
	m_bOleInitialized = false;
	m_pOleObject = NULL;
	m_pInPlaceObject = NULL;
	m_pViewObject = NULL;
	m_pBrowser = NULL;
	m_pConnectionPoint = NULL;
	m_dwAdviseCookie = 0;
	m_hMemDC = NULL;
	m_hBitmap = NULL;
	m_hOldBitmap = NULL;
	m_pBits = NULL;
	m_nBitmapWide = m_nBitmapTall = 0;
}

STDMETHODIMP CIEBrowserHost::QueryInterface( REFIID riid, void **ppv )
{
	if ( !ppv )
		return E_POINTER;

	*ppv = NULL;
	if ( riid == IID_IUnknown || riid == IID_IOleClientSite )
		*ppv = static_cast<IOleClientSite *>( this );
	else if ( riid == IID_IOleWindow || riid == IID_IOleInPlaceSite )
		*ppv = static_cast<IOleInPlaceSite *>( this );
	else if ( riid == IID_IOleInPlaceUIWindow || riid == IID_IOleInPlaceFrame )
		*ppv = static_cast<IOleInPlaceFrame *>( this );
	else if ( riid == IID_IDocHostUIHandler )
		*ppv = static_cast<IDocHostUIHandler *>( this );
	else if ( riid == IID_IDocHostShowUI )
		*ppv = static_cast<IDocHostShowUI *>( this );
	else if ( riid == IID_IDispatch || riid == DIID_DWebBrowserEvents2 )
		*ppv = static_cast<IDispatch *>( this );
	else
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

STDMETHODIMP CIEBrowserHost::GetWindowContext( IOleInPlaceFrame **ppFrame, IOleInPlaceUIWindow **ppDoc, LPRECT lprcPosRect, LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO lpFrameInfo )
{
	if ( ppFrame )
	{
		*ppFrame = static_cast<IOleInPlaceFrame *>( this );
		AddRef();
	}
	if ( ppDoc )
		*ppDoc = NULL;

	RECT rc = { 0, 0, m_nWide, m_nTall };
	if ( lprcPosRect )
		*lprcPosRect = rc;
	if ( lprcClipRect )
		*lprcClipRect = rc;

	if ( lpFrameInfo )
	{
		lpFrameInfo->fMDIApp = FALSE;
		lpFrameInfo->hwndFrame = m_hWnd;
		lpFrameInfo->haccel = NULL;
		lpFrameInfo->cAccelEntries = 0;
	}
	return S_OK;
}

STDMETHODIMP CIEBrowserHost::GetHostInfo( DOCHOSTUIINFO *pInfo )
{
	if ( !pInfo )
		return E_POINTER;

	pInfo->cbSize = sizeof( DOCHOSTUIINFO );
	pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_DISABLE_HELP_MENU | DOCHOSTUIFLAG_THEME;
	pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
	return S_OK;
}

STDMETHODIMP CIEBrowserHost::Invoke( DISPID dispIdMember, REFIID, LCID, WORD, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *, UINT * )
{
	switch ( dispIdMember )
	{
	case DISPID_AMBIENT_DLCONTROL:
		// images yes; no background sounds or videos from server pages, no ActiveX
		if ( pVarResult )
		{
			VariantInit( pVarResult );
			pVarResult->vt = VT_I4;
			pVarResult->lVal = DLCTL_DLIMAGES | DLCTL_SILENT | DLCTL_NO_DLACTIVEXCTLS | DLCTL_NO_RUNACTIVEXCTLS;
		}
		return S_OK;

	case DISPID_NEWWINDOW3:
		// a link wants a new window: open it in the system browser instead
		// args (reversed): bstrUrl, bstrUrlContext, dwFlags, Cancel, ppDisp
		if ( pDispParams && pDispParams->cArgs >= 5 )
		{
			VARIANT &url = pDispParams->rgvarg[0];
			VARIANT &cancel = pDispParams->rgvarg[3];
			if ( cancel.vt == ( VT_BYREF | VT_BOOL ) && cancel.pboolVal )
				*cancel.pboolVal = VARIANT_TRUE;

			if ( url.vt == VT_BSTR && url.bstrVal &&
				( !_wcsnicmp( url.bstrVal, L"http://", 7 ) || !_wcsnicmp( url.bstrVal, L"https://", 8 ) ) )
			{
				ShellExecuteW( NULL, L"open", url.bstrVal, NULL, NULL, SW_SHOWNORMAL );
			}
		}
		return S_OK;

	default:
		return DISP_E_MEMBERNOTFOUND;
	}
}

bool CIEBrowserHost::Create( int nWide, int nTall )
{
	m_nWide = nWide > 0 ? nWide : 1;
	m_nTall = nTall > 0 ? nTall : 1;

	// in-place activation needs an STA; S_FALSE = already initialized on this thread
	HRESULT hr = OleInitialize( NULL );
	if ( FAILED( hr ) )
		return false;
	m_bOleInitialized = true;

	SetBrowserEmulation();

	if ( !RegisterHostWindowClass() )
		return false;

	// never shown: the control only needs a parent window to activate in
	m_hWnd = CreateWindowExW( WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, HOST_WINDOW_CLASS, L"", WS_POPUP,
		-32000, -32000, m_nWide, m_nTall, NULL, NULL, GetModuleHandleW( NULL ), NULL );
	if ( !m_hWnd )
		return false;

	hr = CoCreateInstance( CLSID_WebBrowser, NULL, CLSCTX_INPROC_SERVER, IID_IOleObject, (void **)&m_pOleObject );
	if ( FAILED( hr ) || !m_pOleObject )
		return false;

	if ( FAILED( m_pOleObject->SetClientSite( static_cast<IOleClientSite *>( this ) ) ) )
		return false;

	RECT rc = { 0, 0, m_nWide, m_nTall };
	hr = m_pOleObject->DoVerb( OLEIVERB_INPLACEACTIVATE, NULL, static_cast<IOleClientSite *>( this ), 0, m_hWnd, &rc );
	if ( FAILED( hr ) )
		return false;

	if ( FAILED( m_pOleObject->QueryInterface( IID_IWebBrowser2, (void **)&m_pBrowser ) ) || !m_pBrowser ||
		FAILED( m_pOleObject->QueryInterface( IID_IViewObject, (void **)&m_pViewObject ) ) || !m_pViewObject ||
		FAILED( m_pOleObject->QueryInterface( IID_IOleInPlaceObject, (void **)&m_pInPlaceObject ) ) || !m_pInPlaceObject )
	{
		return false;
	}

	IConnectionPointContainer *pContainer = NULL;
	if ( SUCCEEDED( m_pOleObject->QueryInterface( IID_IConnectionPointContainer, (void **)&pContainer ) ) && pContainer )
	{
		if ( SUCCEEDED( pContainer->FindConnectionPoint( DIID_DWebBrowserEvents2, &m_pConnectionPoint ) ) && m_pConnectionPoint )
		{
			if ( FAILED( m_pConnectionPoint->Advise( static_cast<IDispatch *>( this ), &m_dwAdviseCookie ) ) )
				m_dwAdviseCookie = 0;
		}
		pContainer->Release();
	}

	// no script error dialogs
	m_pBrowser->put_Silent( VARIANT_TRUE );
	m_pBrowser->put_RegisterAsDropTarget( VARIANT_FALSE );

	SetSize( m_nWide, m_nTall );
	return true;
}

void CIEBrowserHost::Destroy()
{
	if ( m_pConnectionPoint )
	{
		if ( m_dwAdviseCookie )
			m_pConnectionPoint->Unadvise( m_dwAdviseCookie );
		m_pConnectionPoint->Release();
		m_pConnectionPoint = NULL;
		m_dwAdviseCookie = 0;
	}

	if ( m_pBrowser )
	{
		m_pBrowser->Stop();
		m_pBrowser->Release();
		m_pBrowser = NULL;
	}

	if ( m_pViewObject )
	{
		m_pViewObject->Release();
		m_pViewObject = NULL;
	}

	if ( m_pInPlaceObject )
	{
		m_pInPlaceObject->InPlaceDeactivate();
		m_pInPlaceObject->Release();
		m_pInPlaceObject = NULL;
	}

	if ( m_pOleObject )
	{
		m_pOleObject->Close( OLECLOSE_NOSAVE );
		m_pOleObject->SetClientSite( NULL );
		m_pOleObject->Release();
		m_pOleObject = NULL;
	}

	ReleaseBitmap();

	if ( m_hWnd )
	{
		DestroyWindow( m_hWnd );
		m_hWnd = NULL;
	}

	if ( m_bOleInitialized )
	{
		OleUninitialize();
		m_bOleInitialized = false;
	}
}

void CIEBrowserHost::Navigate( const char *pszURL )
{
	if ( !m_pBrowser || !pszURL || !pszURL[0] )
		return;

	int nChars = MultiByteToWideChar( CP_UTF8, 0, pszURL, -1, NULL, 0 );
	if ( nChars <= 0 )
		return;

	BSTR bstrURL = SysAllocStringLen( NULL, nChars - 1 );
	if ( !bstrURL )
		return;
	MultiByteToWideChar( CP_UTF8, 0, pszURL, -1, bstrURL, nChars );

	VARIANT url, empty;
	VariantInit( &url );
	VariantInit( &empty );
	url.vt = VT_BSTR;
	url.bstrVal = bstrURL;

	m_pBrowser->Navigate2( &url, &empty, &empty, &empty, &empty );
	VariantClear( &url );
}

void CIEBrowserHost::SetSize( int nWide, int nTall )
{
	m_nWide = nWide > 0 ? nWide : 1;
	m_nTall = nTall > 0 ? nTall : 1;

	if ( m_hWnd )
		SetWindowPos( m_hWnd, NULL, 0, 0, m_nWide, m_nTall, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );

	RECT rc = { 0, 0, m_nWide, m_nTall };
	if ( m_pInPlaceObject )
		m_pInPlaceObject->SetObjectRects( &rc, &rc );

	if ( m_pBrowser )
	{
		m_pBrowser->put_Width( m_nWide );
		m_pBrowser->put_Height( m_nTall );
	}
}

void CIEBrowserHost::ReleaseBitmap()
{
	if ( m_hMemDC && m_hOldBitmap )
		SelectObject( m_hMemDC, m_hOldBitmap );
	m_hOldBitmap = NULL;

	if ( m_hBitmap )
	{
		DeleteObject( m_hBitmap );
		m_hBitmap = NULL;
	}

	if ( m_hMemDC )
	{
		DeleteDC( m_hMemDC );
		m_hMemDC = NULL;
	}

	m_pBits = NULL;
	m_nBitmapWide = m_nBitmapTall = 0;
}

bool CIEBrowserHost::Render( unsigned char *pRGBA, int nWide, int nTall )
{
	if ( !m_pViewObject || !pRGBA || nWide <= 0 || nTall <= 0 )
		return false;

	if ( nWide != m_nWide || nTall != m_nTall )
		SetSize( nWide, nTall );

	if ( !m_hBitmap || m_nBitmapWide != nWide || m_nBitmapTall != nTall )
	{
		ReleaseBitmap();

		m_hMemDC = CreateCompatibleDC( NULL );
		if ( !m_hMemDC )
			return false;

		BITMAPINFO bi;
		memset( &bi, 0, sizeof( bi ) );
		bi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
		bi.bmiHeader.biWidth = nWide;
		bi.bmiHeader.biHeight = -nTall; // top-down
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		m_hBitmap = CreateDIBSection( m_hMemDC, &bi, DIB_RGB_COLORS, &m_pBits, NULL, 0 );
		if ( !m_hBitmap || !m_pBits )
		{
			ReleaseBitmap();
			return false;
		}

		m_hOldBitmap = SelectObject( m_hMemDC, m_hBitmap );
		m_nBitmapWide = nWide;
		m_nBitmapTall = nTall;
	}

	RECT rcFill = { 0, 0, nWide, nTall };
	FillRect( m_hMemDC, &rcFill, (HBRUSH)GetStockObject( WHITE_BRUSH ) );

	RECTL rc = { 0, 0, nWide, nTall };
	HRESULT hr = m_pViewObject->Draw( DVASPECT_CONTENT, -1, NULL, NULL, NULL, m_hMemDC, &rc, NULL, NULL, 0 );
	GdiFlush();
	if ( FAILED( hr ) )
		return false;

	// BGRX -> RGBA, opaque
	const unsigned char *src = (const unsigned char *)m_pBits;
	const int nPixels = nWide * nTall;
	for ( int i = 0; i < nPixels; i++, src += 4, pRGBA += 4 )
	{
		pRGBA[0] = src[2];
		pRGBA[1] = src[1];
		pRGBA[2] = src[0];
		pRGBA[3] = 255;
	}
	return true;
}

static BOOL CALLBACK FindServerWindowProc( HWND hwnd, LPARAM lParam )
{
	wchar_t name[64];
	if ( GetClassNameW( hwnd, name, 64 ) && !wcscmp( name, L"Internet Explorer_Server" ) )
	{
		*(HWND *)lParam = hwnd;
		return FALSE;
	}
	return TRUE;
}

// the window that actually handles input, created by the control inside our host window
HWND CIEBrowserHost::FindServerWindow()
{
	HWND hServer = NULL;
	if ( m_hWnd )
		EnumChildWindows( m_hWnd, FindServerWindowProc, (LPARAM)&hServer );
	return hServer;
}

void CIEBrowserHost::PostMouse( UINT msg, WPARAM wParam, int x, int y )
{
	HWND hServer = FindServerWindow();
	if ( hServer )
		PostMessageW( hServer, msg, wParam, MAKELPARAM( x, y ) );
}

void CIEBrowserHost::Scroll( int nPixels )
{
	if ( !m_pBrowser )
		return;

	IDispatch *pDocDisp = NULL;
	if ( FAILED( m_pBrowser->get_Document( &pDocDisp ) ) || !pDocDisp )
		return;

	IHTMLDocument2 *pDoc = NULL;
	if ( SUCCEEDED( pDocDisp->QueryInterface( IID_IHTMLDocument2, (void **)&pDoc ) ) && pDoc )
	{
		IHTMLWindow2 *pWindow = NULL;
		if ( SUCCEEDED( pDoc->get_parentWindow( &pWindow ) ) && pWindow )
		{
			pWindow->scrollBy( 0, nPixels );
			pWindow->Release();
		}
		pDoc->Release();
	}
	pDocDisp->Release();
}

//-----------------------------------------------------------------------------
// CIEBrowser
//-----------------------------------------------------------------------------
CIEBrowser::CIEBrowser() : m_pHost( 0 )
{
}

CIEBrowser::~CIEBrowser()
{
	Shutdown();
}

bool CIEBrowser::Init( int nWide, int nTall )
{
	Shutdown();

	CIEBrowserHost *pHost = new CIEBrowserHost();
	if ( !pHost->Create( nWide, nTall ) )
	{
		pHost->Destroy();
		pHost->Release();
		return false;
	}

	m_pHost = pHost;
	return true;
}

void CIEBrowser::Shutdown()
{
	if ( !m_pHost )
		return;

	m_pHost->Destroy();
	m_pHost->Release(); // deleted once the control has dropped its references too
	m_pHost = 0;
}

void CIEBrowser::Navigate( const char *pszURL )
{
	if ( m_pHost )
		m_pHost->Navigate( pszURL );
}

void CIEBrowser::SetSize( int nWide, int nTall )
{
	if ( m_pHost )
		m_pHost->SetSize( nWide, nTall );
}

bool CIEBrowser::Render( unsigned char *pRGBA, int nWide, int nTall )
{
	return m_pHost ? m_pHost->Render( pRGBA, nWide, nTall ) : false;
}

void CIEBrowser::MouseMove( int x, int y )
{
	if ( m_pHost )
		m_pHost->PostMouse( WM_MOUSEMOVE, 0, x, y );
}

void CIEBrowser::MouseButton( int x, int y, bool bDown )
{
	if ( m_pHost )
		m_pHost->PostMouse( bDown ? WM_LBUTTONDOWN : WM_LBUTTONUP, bDown ? MK_LBUTTON : 0, x, y );
}

void CIEBrowser::Scroll( int nPixels )
{
	if ( m_pHost )
		m_pHost->Scroll( nPixels );
}

#else // !_WIN32

// no WebBrowser control: the browser can't be created, callers fall back to text
CIEBrowser::CIEBrowser() : m_pHost( 0 ) {}
CIEBrowser::~CIEBrowser() {}
bool CIEBrowser::Init( int, int ) { return false; }
void CIEBrowser::Shutdown() {}
void CIEBrowser::Navigate( const char * ) {}
void CIEBrowser::SetSize( int, int ) {}
bool CIEBrowser::Render( unsigned char *, int, int ) { return false; }
void CIEBrowser::MouseMove( int, int ) {}
void CIEBrowser::MouseButton( int, int, bool ) {}
void CIEBrowser::Scroll( int ) {}

#endif // _WIN32
