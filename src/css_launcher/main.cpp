//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game launcher with launch options, drawn in the 2004 Steam style.
//			Settings are stored in css_launcher.ini next to the executable.
//
//=============================================================================//

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource.h"

#pragma comment( lib, "kernel32.lib" )
#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )

#define LAUNCHER_TITLE			"Counter-Strike: Source"
#define LAUNCHER_CLASS			"CSSLauncherWindow"
#define LAUNCHER_INI			"css_launcher.ini"
#define LAUNCHER_SECTION		"Launcher"
#define DEFAULT_GAME_EXE		"hl2_launcher.exe"
#define FALLBACK_GAME_EXE		"hl2.exe"
#define DEFAULT_GAME_DIR		"cstrike"

#define CLIENT_WIDE				440
#define CLIENT_TALL				432
#define MARGIN					14
#define LABEL_WIDE				116
#define CONTROL_X				( MARGIN + LABEL_WIDE )
#define MAX_VIDEO_MODES			128

// Steam (2004) colors
#define CLR_UI_BACKGROUND		RGB( 76, 88, 68 )
#define CLR_UI_FIELD				RGB( 62, 70, 55 )
#define CLR_UI_TEXT				RGB( 216, 222, 211 )
#define CLR_UI_TEXT_DIM			RGB( 160, 170, 149 )
#define CLR_UI_TITLE				RGB( 196, 181, 80 )
#define CLR_UI_BORDER_LIGHT		RGB( 136, 145, 128 )
#define CLR_UI_BORDER_DARK		RGB( 41, 46, 36 )
#define CLR_UI_SELECTION			RGB( 149, 136, 49 )

enum
{
	IDC_WINDOWMODE = 1000,
	IDC_RESOLUTION,
	IDC_NOVID,
	IDC_CONSOLE,
	IDC_HIGHPRIORITY,
	IDC_CLOSEONLAUNCH,
	IDC_RMLUI,
	IDC_CONNECT,
	IDC_EXTRAARGS,
	IDC_CMDLINE,
	IDC_LABEL,
};

enum WindowMode_t
{
	WINDOWMODE_DEFAULT = 0,
	WINDOWMODE_FULLSCREEN,
	WINDOWMODE_WINDOWED,
	WINDOWMODE_NOBORDER,

	WINDOWMODE_COUNT
};

static const char *s_pWindowModeNames[WINDOWMODE_COUNT] =
{
	"Game default",
	"Fullscreen",
	"Windowed",
	"Windowed (no border)",
};

struct LauncherSettings_t
{
	int		nWindowMode;
	int		nWidth;					// 0 = use game settings
	int		nHeight;
	bool	bNoVid;
	bool	bConsole;
	bool	bHighPriority;
	bool	bCloseOnLaunch;
	bool	bRmlUi;
	char	szConnect[128];
	char	szExtraArgs[512];
	char	szGameExe[MAX_PATH];
	char	szGameDir[64];
};

struct VideoMode_t
{
	int		nWidth;
	int		nHeight;
};

struct CheckBoxDesc_t
{
	int			nID;
	const char	*pText;
	int			x, y, wide;
};

static HINSTANCE			s_hInstance;
static HWND					s_hWnd;
static HFONT				s_hFont;
static HFONT				s_hTitleFont;
static HFONT				s_hSectionFont;
static HBRUSH				s_hBackgroundBrush;
static HBRUSH				s_hFieldBrush;
static char					s_szBaseDir[MAX_PATH];
static char					s_szIniFile[MAX_PATH];
static LauncherSettings_t	s_Settings;
static VideoMode_t			s_VideoModes[MAX_VIDEO_MODES];
static int					s_nVideoModes;
static bool					s_bUpdatingControls;

static const CheckBoxDesc_t s_CheckBoxes[] =
{
	{ IDC_NOVID,			"Skip intro video",						MARGIN,			166,	200 },
	{ IDC_CONSOLE,			"Developer console",					MARGIN + 212,	166,	200 },
	{ IDC_HIGHPRIORITY,		"High process priority",				MARGIN,			188,	200 },
	{ IDC_CLOSEONLAUNCH,	"Close launcher after start",			MARGIN + 212,	188,	200 },
	{ IDC_RMLUI,			"Enable RmlUi (experimental)",			MARGIN,			210,	300 },
};

//-----------------------------------------------------------------------------
// Purpose: strncpy that always terminates
//-----------------------------------------------------------------------------
static void SafeStrCopy( char *pDest, const char *pSrc, int nDestSize )
{
	if ( nDestSize <= 0 )
		return;

	strncpy( pDest, pSrc, nDestSize - 1 );
	pDest[nDestSize - 1] = 0;
}

static void SafeStrCat( char *pDest, const char *pSrc, int nDestSize )
{
	int nLen = (int)strlen( pDest );
	if ( nLen < nDestSize - 1 )
	{
		SafeStrCopy( pDest + nLen, pSrc, nDestSize - nLen );
	}
}

//-----------------------------------------------------------------------------
// Purpose: removes leading and trailing whitespace in place
//-----------------------------------------------------------------------------
static void TrimString( char *pString )
{
	char *pStart = pString;
	while ( *pStart == ' ' || *pStart == '\t' || *pStart == '\r' || *pStart == '\n' )
		pStart++;

	if ( pStart != pString )
		memmove( pString, pStart, strlen( pStart ) + 1 );

	int nLen = (int)strlen( pString );
	while ( nLen > 0 && ( pString[nLen - 1] == ' ' || pString[nLen - 1] == '\t' || pString[nLen - 1] == '\r' || pString[nLen - 1] == '\n' ) )
	{
		pString[--nLen] = 0;
	}
}

static bool FileExists( const char *pPath )
{
	DWORD dwAttributes = GetFileAttributesA( pPath );
	return ( dwAttributes != INVALID_FILE_ATTRIBUTES ) && !( dwAttributes & FILE_ATTRIBUTE_DIRECTORY );
}

static void MakeFullPath( const char *pFileName, char *pOut, int nOutSize )
{
	_snprintf( pOut, nOutSize, "%s\\%s", s_szBaseDir, pFileName );
	pOut[nOutSize - 1] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: directory of the launcher executable, without trailing slash
//-----------------------------------------------------------------------------
static void InitBaseDir()
{
	GetModuleFileNameA( NULL, s_szBaseDir, sizeof( s_szBaseDir ) );
	s_szBaseDir[sizeof( s_szBaseDir ) - 1] = 0;

	char *pSlash = strrchr( s_szBaseDir, '\\' );
	if ( pSlash )
		*pSlash = 0;

	MakeFullPath( LAUNCHER_INI, s_szIniFile, sizeof( s_szIniFile ) );
}

//-----------------------------------------------------------------------------
// Purpose: settings
//-----------------------------------------------------------------------------
static int ReadIniInt( const char *pKey, int nDefault )
{
	return (int)GetPrivateProfileIntA( LAUNCHER_SECTION, pKey, nDefault, s_szIniFile );
}

static void ReadIniString( const char *pKey, const char *pDefault, char *pOut, int nOutSize )
{
	GetPrivateProfileStringA( LAUNCHER_SECTION, pKey, pDefault, pOut, nOutSize, s_szIniFile );
	pOut[nOutSize - 1] = 0;
	TrimString( pOut );
}

static void WriteIniInt( const char *pKey, int nValue )
{
	char szValue[32];
	_snprintf( szValue, sizeof( szValue ), "%d", nValue );
	szValue[sizeof( szValue ) - 1] = 0;
	WritePrivateProfileStringA( LAUNCHER_SECTION, pKey, szValue, s_szIniFile );
}

static void WriteIniString( const char *pKey, const char *pValue )
{
	// quote the value so leading/trailing spaces and quotes inside survive
	char szValue[1024];
	_snprintf( szValue, sizeof( szValue ), "\"%s\"", pValue );
	szValue[sizeof( szValue ) - 1] = 0;
	WritePrivateProfileStringA( LAUNCHER_SECTION, pKey, szValue, s_szIniFile );
}

static void LoadSettings()
{
	LauncherSettings_t &s = s_Settings;

	s.nWindowMode = ReadIniInt( "WindowMode", WINDOWMODE_DEFAULT );
	if ( s.nWindowMode < 0 || s.nWindowMode >= WINDOWMODE_COUNT )
		s.nWindowMode = WINDOWMODE_DEFAULT;

	s.nWidth = ReadIniInt( "Width", 0 );
	s.nHeight = ReadIniInt( "Height", 0 );
	if ( s.nWidth < 320 || s.nHeight < 240 )
	{
		s.nWidth = 0;
		s.nHeight = 0;
	}

	s.bNoVid = ReadIniInt( "NoVid", 1 ) != 0;
	s.bConsole = ReadIniInt( "Console", 0 ) != 0;
	s.bHighPriority = ReadIniInt( "HighPriority", 0 ) != 0;
	s.bCloseOnLaunch = ReadIniInt( "CloseOnLaunch", 1 ) != 0;
	s.bRmlUi = ReadIniInt( "RmlUi", 0 ) != 0;

	ReadIniString( "Connect", "", s.szConnect, sizeof( s.szConnect ) );
	ReadIniString( "ExtraArgs", "", s.szExtraArgs, sizeof( s.szExtraArgs ) );
	ReadIniString( "GameExe", "", s.szGameExe, sizeof( s.szGameExe ) );
	ReadIniString( "GameDir", DEFAULT_GAME_DIR, s.szGameDir, sizeof( s.szGameDir ) );

	if ( !s.szGameDir[0] )
		SafeStrCopy( s.szGameDir, DEFAULT_GAME_DIR, sizeof( s.szGameDir ) );
}

static void SaveSettings()
{
	const LauncherSettings_t &s = s_Settings;

	WriteIniInt( "WindowMode", s.nWindowMode );
	WriteIniInt( "Width", s.nWidth );
	WriteIniInt( "Height", s.nHeight );
	WriteIniInt( "NoVid", s.bNoVid ? 1 : 0 );
	WriteIniInt( "Console", s.bConsole ? 1 : 0 );
	WriteIniInt( "HighPriority", s.bHighPriority ? 1 : 0 );
	WriteIniInt( "CloseOnLaunch", s.bCloseOnLaunch ? 1 : 0 );
	WriteIniInt( "RmlUi", s.bRmlUi ? 1 : 0 );
	WriteIniString( "Connect", s.szConnect );
	WriteIniString( "ExtraArgs", s.szExtraArgs );

	// only written when set by hand, so a default install keeps auto-detection
	if ( s.szGameExe[0] )
		WriteIniString( "GameExe", s.szGameExe );

	WriteIniString( "GameDir", s.szGameDir );
}

//-----------------------------------------------------------------------------
// Purpose: finds the game executable, returns false if there is none
//-----------------------------------------------------------------------------
static bool GetGameExePath( char *pOut, int nOutSize )
{
	if ( s_Settings.szGameExe[0] )
	{
		// absolute path or relative to the launcher
		if ( strchr( s_Settings.szGameExe, ':' ) || ( s_Settings.szGameExe[0] == '\\' && s_Settings.szGameExe[1] == '\\' ) )
			SafeStrCopy( pOut, s_Settings.szGameExe, nOutSize );
		else
			MakeFullPath( s_Settings.szGameExe, pOut, nOutSize );

		return FileExists( pOut );
	}

	MakeFullPath( DEFAULT_GAME_EXE, pOut, nOutSize );
	if ( FileExists( pOut ) )
		return true;

	MakeFullPath( FALLBACK_GAME_EXE, pOut, nOutSize );
	if ( FileExists( pOut ) )
		return true;

	// report the expected name
	MakeFullPath( DEFAULT_GAME_EXE, pOut, nOutSize );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: game arguments (without the executable)
//-----------------------------------------------------------------------------
static void BuildGameArgs( char *pOut, int nOutSize )
{
	const LauncherSettings_t &s = s_Settings;
	char szTemp[256];

	pOut[0] = 0;

	_snprintf( szTemp, sizeof( szTemp ), "-game %s", s.szGameDir );
	szTemp[sizeof( szTemp ) - 1] = 0;
	SafeStrCat( pOut, szTemp, nOutSize );

	switch ( s.nWindowMode )
	{
	case WINDOWMODE_FULLSCREEN:
		SafeStrCat( pOut, " -fullscreen", nOutSize );
		break;
	case WINDOWMODE_WINDOWED:
		SafeStrCat( pOut, " -windowed", nOutSize );
		break;
	case WINDOWMODE_NOBORDER:
		SafeStrCat( pOut, " -windowed -noborder", nOutSize );
		break;
	default:
		break;
	}

	if ( s.nWidth > 0 && s.nHeight > 0 )
	{
		_snprintf( szTemp, sizeof( szTemp ), " -w %d -h %d", s.nWidth, s.nHeight );
		szTemp[sizeof( szTemp ) - 1] = 0;
		SafeStrCat( pOut, szTemp, nOutSize );
	}

	if ( s.bNoVid )
		SafeStrCat( pOut, " -novid", nOutSize );

	if ( s.bConsole )
		SafeStrCat( pOut, " -console", nOutSize );

	if ( s.bRmlUi )
		SafeStrCat( pOut, " -rmlui", nOutSize );

	if ( s.szExtraArgs[0] )
	{
		SafeStrCat( pOut, " ", nOutSize );
		SafeStrCat( pOut, s.szExtraArgs, nOutSize );
	}

	// console commands go last
	if ( s.szConnect[0] )
	{
		_snprintf( szTemp, sizeof( szTemp ), " +connect %s", s.szConnect );
		szTemp[sizeof( szTemp ) - 1] = 0;
		SafeStrCat( pOut, szTemp, nOutSize );
	}
}

//-----------------------------------------------------------------------------
// Purpose: starts the game, returns true on success
//-----------------------------------------------------------------------------
static bool LaunchGame()
{
	char szExePath[MAX_PATH];
	if ( !GetGameExePath( szExePath, sizeof( szExePath ) ) )
	{
		char szMessage[MAX_PATH + 128];
		_snprintf( szMessage, sizeof( szMessage ), "Could not find the game executable:\n%s\n\nPlace the launcher in the game folder.", szExePath );
		szMessage[sizeof( szMessage ) - 1] = 0;
		MessageBoxA( s_hWnd, szMessage, LAUNCHER_TITLE, MB_OK | MB_ICONERROR );
		return false;
	}

	char szArgs[1024];
	BuildGameArgs( szArgs, sizeof( szArgs ) );

	// CreateProcess wants a writable command line that starts with the program name
	char szCmdLine[MAX_PATH + 1024 + 4];
	_snprintf( szCmdLine, sizeof( szCmdLine ), "\"%s\" %s", szExePath, szArgs );
	szCmdLine[sizeof( szCmdLine ) - 1] = 0;

	char szWorkDir[MAX_PATH];
	SafeStrCopy( szWorkDir, szExePath, sizeof( szWorkDir ) );
	char *pSlash = strrchr( szWorkDir, '\\' );
	if ( pSlash )
		*pSlash = 0;

	STARTUPINFOA si;
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );

	PROCESS_INFORMATION pi;
	memset( &pi, 0, sizeof( pi ) );

	DWORD dwFlags = s_Settings.bHighPriority ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
	if ( !CreateProcessA( szExePath, szCmdLine, NULL, NULL, FALSE, dwFlags, NULL, szWorkDir, &si, &pi ) )
	{
		char szMessage[MAX_PATH + 128];
		_snprintf( szMessage, sizeof( szMessage ), "Failed to start the game (error %lu):\n%s", GetLastError(), szExePath );
		szMessage[sizeof( szMessage ) - 1] = 0;
		MessageBoxA( s_hWnd, szMessage, LAUNCHER_TITLE, MB_OK | MB_ICONERROR );
		return false;
	}

	CloseHandle( pi.hThread );
	CloseHandle( pi.hProcess );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: video modes of the primary display, sorted, without duplicates
//-----------------------------------------------------------------------------
static void AddVideoMode( int nWidth, int nHeight )
{
	for ( int i = 0; i < s_nVideoModes; i++ )
	{
		if ( s_VideoModes[i].nWidth == nWidth && s_VideoModes[i].nHeight == nHeight )
			return;
	}

	if ( s_nVideoModes >= MAX_VIDEO_MODES )
		return;

	// insertion sort, smallest first
	int nInsert = s_nVideoModes;
	while ( nInsert > 0 )
	{
		const VideoMode_t &prev = s_VideoModes[nInsert - 1];
		if ( prev.nWidth < nWidth || ( prev.nWidth == nWidth && prev.nHeight < nHeight ) )
			break;

		s_VideoModes[nInsert] = prev;
		nInsert--;
	}

	s_VideoModes[nInsert].nWidth = nWidth;
	s_VideoModes[nInsert].nHeight = nHeight;
	s_nVideoModes++;
}

static void InitVideoModes()
{
	s_nVideoModes = 0;

	DEVMODEA mode;
	memset( &mode, 0, sizeof( mode ) );
	mode.dmSize = sizeof( mode );

	for ( DWORD i = 0; EnumDisplaySettingsA( NULL, i, &mode ); i++ )
	{
		if ( mode.dmBitsPerPel < 16 || mode.dmPelsWidth < 640 || mode.dmPelsHeight < 480 )
			continue;

		AddVideoMode( (int)mode.dmPelsWidth, (int)mode.dmPelsHeight );
	}

	// keep a saved resolution selectable even if the display doesn't list it
	if ( s_Settings.nWidth > 0 && s_Settings.nHeight > 0 )
	{
		AddVideoMode( s_Settings.nWidth, s_Settings.nHeight );
	}
}

//-----------------------------------------------------------------------------
// Purpose: controls <-> settings
//-----------------------------------------------------------------------------
static bool IsChecked( int nID )
{
	return GetWindowLongPtrA( GetDlgItem( s_hWnd, nID ), GWLP_USERDATA ) != 0;
}

static void SetChecked( int nID, bool bChecked )
{
	HWND hControl = GetDlgItem( s_hWnd, nID );
	SetWindowLongPtrA( hControl, GWLP_USERDATA, bChecked ? 1 : 0 );
	InvalidateRect( hControl, NULL, TRUE );
}

static void UpdateCommandLinePreview()
{
	char szExePath[MAX_PATH];
	GetGameExePath( szExePath, sizeof( szExePath ) );

	const char *pExeName = strrchr( szExePath, '\\' );
	pExeName = pExeName ? pExeName + 1 : szExePath;

	char szArgs[1024];
	BuildGameArgs( szArgs, sizeof( szArgs ) );

	char szPreview[MAX_PATH + 1024 + 4];
	_snprintf( szPreview, sizeof( szPreview ), "%s %s", pExeName, szArgs );
	szPreview[sizeof( szPreview ) - 1] = 0;

	SetDlgItemTextA( s_hWnd, IDC_CMDLINE, szPreview );
}

static void ReadControls()
{
	LauncherSettings_t &s = s_Settings;

	int nMode = (int)SendDlgItemMessageA( s_hWnd, IDC_WINDOWMODE, CB_GETCURSEL, 0, 0 );
	s.nWindowMode = ( nMode >= 0 && nMode < WINDOWMODE_COUNT ) ? nMode : WINDOWMODE_DEFAULT;

	s.nWidth = 0;
	s.nHeight = 0;
	int nRes = (int)SendDlgItemMessageA( s_hWnd, IDC_RESOLUTION, CB_GETCURSEL, 0, 0 );
	if ( nRes > 0 )
	{
		int nModeIndex = (int)SendDlgItemMessageA( s_hWnd, IDC_RESOLUTION, CB_GETITEMDATA, nRes, 0 ) - 1;
		if ( nModeIndex >= 0 && nModeIndex < s_nVideoModes )
		{
			s.nWidth = s_VideoModes[nModeIndex].nWidth;
			s.nHeight = s_VideoModes[nModeIndex].nHeight;
		}
	}

	s.bNoVid = IsChecked( IDC_NOVID );
	s.bConsole = IsChecked( IDC_CONSOLE );
	s.bHighPriority = IsChecked( IDC_HIGHPRIORITY );
	s.bCloseOnLaunch = IsChecked( IDC_CLOSEONLAUNCH );
	s.bRmlUi = IsChecked( IDC_RMLUI );

	GetDlgItemTextA( s_hWnd, IDC_CONNECT, s.szConnect, sizeof( s.szConnect ) );
	TrimString( s.szConnect );
	GetDlgItemTextA( s_hWnd, IDC_EXTRAARGS, s.szExtraArgs, sizeof( s.szExtraArgs ) );
	TrimString( s.szExtraArgs );
}

static void WriteControls()
{
	const LauncherSettings_t &s = s_Settings;

	s_bUpdatingControls = true;

	SendDlgItemMessageA( s_hWnd, IDC_WINDOWMODE, CB_SETCURSEL, s.nWindowMode, 0 );

	int nSelected = 0;
	int nCount = (int)SendDlgItemMessageA( s_hWnd, IDC_RESOLUTION, CB_GETCOUNT, 0, 0 );
	for ( int i = 1; i < nCount; i++ )
	{
		int nModeIndex = (int)SendDlgItemMessageA( s_hWnd, IDC_RESOLUTION, CB_GETITEMDATA, i, 0 ) - 1;
		if ( nModeIndex >= 0 && nModeIndex < s_nVideoModes &&
			s_VideoModes[nModeIndex].nWidth == s.nWidth && s_VideoModes[nModeIndex].nHeight == s.nHeight )
		{
			nSelected = i;
			break;
		}
	}
	SendDlgItemMessageA( s_hWnd, IDC_RESOLUTION, CB_SETCURSEL, nSelected, 0 );

	SetChecked( IDC_NOVID, s.bNoVid );
	SetChecked( IDC_CONSOLE, s.bConsole );
	SetChecked( IDC_HIGHPRIORITY, s.bHighPriority );
	SetChecked( IDC_CLOSEONLAUNCH, s.bCloseOnLaunch );
	SetChecked( IDC_RMLUI, s.bRmlUi );

	SetDlgItemTextA( s_hWnd, IDC_CONNECT, s.szConnect );
	SetDlgItemTextA( s_hWnd, IDC_EXTRAARGS, s.szExtraArgs );

	s_bUpdatingControls = false;

	UpdateCommandLinePreview();
}

static void OnSettingsChanged()
{
	if ( s_bUpdatingControls )
		return;

	ReadControls();
	UpdateCommandLinePreview();
}

//-----------------------------------------------------------------------------
// Purpose: control creation
//-----------------------------------------------------------------------------
static HWND CreateControl( DWORD dwExStyle, const char *pClass, const char *pText, DWORD dwStyle, int x, int y, int wide, int tall, int nID )
{
	HWND hControl = CreateWindowExA( dwExStyle, pClass, pText, WS_CHILD | WS_VISIBLE | dwStyle,
		x, y, wide, tall, s_hWnd, (HMENU)(INT_PTR)nID, s_hInstance, NULL );

	SendMessageA( hControl, WM_SETFONT, (WPARAM)s_hFont, FALSE );
	return hControl;
}

static void CreateLabel( const char *pText, int y )
{
	CreateControl( 0, "STATIC", pText, SS_LEFT, MARGIN, y + 4, LABEL_WIDE - 8, 16, IDC_LABEL );
}

static void CreateControls()
{
	// video
	CreateLabel( "Display mode", 70 );
	HWND hCombo = CreateControl( 0, "COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
		CONTROL_X, 70, 200, 200, IDC_WINDOWMODE );
	for ( int i = 0; i < WINDOWMODE_COUNT; i++ )
	{
		SendMessageA( hCombo, CB_ADDSTRING, 0, (LPARAM)s_pWindowModeNames[i] );
	}

	CreateLabel( "Resolution", 98 );
	hCombo = CreateControl( 0, "COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
		CONTROL_X, 98, 200, 300, IDC_RESOLUTION );
	SendMessageA( hCombo, CB_ADDSTRING, 0, (LPARAM)"Game default" );
	for ( int i = s_nVideoModes - 1; i >= 0; i-- )
	{
		char szMode[32];
		_snprintf( szMode, sizeof( szMode ), "%d x %d", s_VideoModes[i].nWidth, s_VideoModes[i].nHeight );
		szMode[sizeof( szMode ) - 1] = 0;

		int nItem = (int)SendMessageA( hCombo, CB_ADDSTRING, 0, (LPARAM)szMode );
		SendMessageA( hCombo, CB_SETITEMDATA, nItem, i + 1 );
	}

	// options
	for ( int i = 0; i < (int)( sizeof( s_CheckBoxes ) / sizeof( s_CheckBoxes[0] ) ); i++ )
	{
		const CheckBoxDesc_t &desc = s_CheckBoxes[i];
		CreateControl( 0, "BUTTON", desc.pText, WS_TABSTOP | BS_OWNERDRAW, desc.x, desc.y, desc.wide, 18, desc.nID );
	}

	// advanced
	CreateLabel( "Connect to server", 260 );
	CreateControl( 0, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
		CONTROL_X, 260, CLIENT_WIDE - CONTROL_X - MARGIN, 20, IDC_CONNECT );
	SendDlgItemMessageA( s_hWnd, IDC_CONNECT, EM_LIMITTEXT, sizeof( s_Settings.szConnect ) - 1, 0 );

	CreateLabel( "Extra parameters", 288 );
	CreateControl( 0, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
		CONTROL_X, 288, CLIENT_WIDE - CONTROL_X - MARGIN, 20, IDC_EXTRAARGS );
	SendDlgItemMessageA( s_hWnd, IDC_EXTRAARGS, EM_LIMITTEXT, sizeof( s_Settings.szExtraArgs ) - 1, 0 );

	CreateControl( 0, "STATIC", "Command line", SS_LEFT, MARGIN, 318, 200, 16, IDC_LABEL );
	CreateControl( 0, "EDIT", "", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
		MARGIN, 336, CLIENT_WIDE - MARGIN * 2, 42, IDC_CMDLINE );

	// buttons
	CreateControl( 0, "BUTTON", "Play", WS_TABSTOP | BS_OWNERDRAW, CLIENT_WIDE - MARGIN - 84 * 2 - 8, CLIENT_TALL - MARGIN - 24, 84, 24, IDOK );
	CreateControl( 0, "BUTTON", "Exit", WS_TABSTOP | BS_OWNERDRAW, CLIENT_WIDE - MARGIN - 84, CLIENT_TALL - MARGIN - 24, 84, 24, IDCANCEL );
}

//-----------------------------------------------------------------------------
// Purpose: drawing
//-----------------------------------------------------------------------------
static void FillSolidRect( HDC hDC, const RECT &rect, COLORREF color )
{
	SetBkColor( hDC, color );
	ExtTextOutA( hDC, 0, 0, ETO_OPAQUE, &rect, NULL, 0, NULL );
}

// 1px frame, raised (light top-left) or sunken (dark top-left)
static void DrawFrame( HDC hDC, const RECT &rect, bool bSunken )
{
	COLORREF topLeft = bSunken ? CLR_UI_BORDER_DARK : CLR_UI_BORDER_LIGHT;
	COLORREF bottomRight = bSunken ? CLR_UI_BORDER_LIGHT : CLR_UI_BORDER_DARK;

	RECT edge;
	SetRect( &edge, rect.left, rect.top, rect.right, rect.top + 1 );
	FillSolidRect( hDC, edge, topLeft );
	SetRect( &edge, rect.left, rect.top, rect.left + 1, rect.bottom );
	FillSolidRect( hDC, edge, topLeft );
	SetRect( &edge, rect.left, rect.bottom - 1, rect.right, rect.bottom );
	FillSolidRect( hDC, edge, bottomRight );
	SetRect( &edge, rect.right - 1, rect.top, rect.right, rect.bottom );
	FillSolidRect( hDC, edge, bottomRight );
}

static void DrawSection( HDC hDC, const char *pText, int y )
{
	SelectObject( hDC, s_hSectionFont );
	SetTextColor( hDC, CLR_UI_TITLE );
	SetBkMode( hDC, TRANSPARENT );
	TextOutA( hDC, MARGIN, y, pText, (int)strlen( pText ) );

	SIZE size;
	GetTextExtentPoint32A( hDC, pText, (int)strlen( pText ), &size );

	// etched line after the caption
	RECT line;
	int nLineY = y + size.cy / 2;
	SetRect( &line, MARGIN + size.cx + 8, nLineY, CLIENT_WIDE - MARGIN, nLineY + 1 );
	FillSolidRect( hDC, line, CLR_UI_BORDER_DARK );
	OffsetRect( &line, 0, 1 );
	FillSolidRect( hDC, line, CLR_UI_BORDER_LIGHT );
}

static void OnPaint()
{
	PAINTSTRUCT ps;
	HDC hDC = BeginPaint( s_hWnd, &ps );

	SelectObject( hDC, s_hTitleFont );
	SetTextColor( hDC, CLR_UI_TEXT );
	SetBkMode( hDC, TRANSPARENT );
	TextOutA( hDC, MARGIN, 12, LAUNCHER_TITLE, (int)strlen( LAUNCHER_TITLE ) );

	DrawSection( hDC, "Video", 46 );
	DrawSection( hDC, "Options", 142 );
	DrawSection( hDC, "Advanced", 236 );

	EndPaint( s_hWnd, &ps );
}

static void DrawButton( const DRAWITEMSTRUCT *pDIS )
{
	HDC hDC = pDIS->hDC;
	RECT rect = pDIS->rcItem;
	bool bPressed = ( pDIS->itemState & ODS_SELECTED ) != 0;
	bool bFocused = ( pDIS->itemState & ODS_FOCUS ) != 0;

	FillSolidRect( hDC, rect, CLR_UI_BACKGROUND );

	// default button gets an outer dark border, like in Steam
	if ( pDIS->CtlID == IDOK || bFocused )
	{
		RECT outer = rect;
		FrameRect( hDC, &outer, (HBRUSH)GetStockObject( BLACK_BRUSH ) );
		InflateRect( &rect, -1, -1 );
	}

	DrawFrame( hDC, rect, bPressed );

	char szText[64];
	GetWindowTextA( pDIS->hwndItem, szText, sizeof( szText ) );

	RECT textRect = rect;
	if ( bPressed )
		OffsetRect( &textRect, 1, 1 );

	SelectObject( hDC, s_hFont );
	SetBkMode( hDC, TRANSPARENT );
	SetTextColor( hDC, CLR_UI_TEXT );
	DrawTextA( hDC, szText, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE );

	if ( bFocused )
	{
		RECT focus = rect;
		InflateRect( &focus, -3, -3 );
		SetTextColor( hDC, CLR_UI_TEXT );
		SetBkColor( hDC, CLR_UI_BACKGROUND );
		DrawFocusRect( hDC, &focus );
	}
}

static void DrawCheckBox( const DRAWITEMSTRUCT *pDIS )
{
	HDC hDC = pDIS->hDC;
	RECT rect = pDIS->rcItem;
	bool bChecked = GetWindowLongPtrA( pDIS->hwndItem, GWLP_USERDATA ) != 0;

	FillSolidRect( hDC, rect, CLR_UI_BACKGROUND );

	// box
	const int nBox = 12;
	RECT box;
	box.left = rect.left;
	box.top = rect.top + ( ( rect.bottom - rect.top ) - nBox ) / 2;
	box.right = box.left + nBox;
	box.bottom = box.top + nBox;
	FillSolidRect( hDC, box, CLR_UI_FIELD );
	DrawFrame( hDC, box, true );

	if ( bChecked )
	{
		HPEN hPen = CreatePen( PS_SOLID, 2, CLR_UI_TITLE );
		HGDIOBJ hOldPen = SelectObject( hDC, hPen );
		MoveToEx( hDC, box.left + 3, box.top + 6, NULL );
		LineTo( hDC, box.left + 5, box.top + 8 );
		LineTo( hDC, box.left + 9, box.top + 3 );
		SelectObject( hDC, hOldPen );
		DeleteObject( hPen );
	}

	char szText[128];
	GetWindowTextA( pDIS->hwndItem, szText, sizeof( szText ) );

	RECT textRect = rect;
	textRect.left = box.right + 6;

	SelectObject( hDC, s_hFont );
	SetBkMode( hDC, TRANSPARENT );
	SetTextColor( hDC, CLR_UI_TEXT );

	RECT calcRect = textRect;
	DrawTextA( hDC, szText, -1, &calcRect, DT_LEFT | DT_SINGLELINE | DT_CALCRECT );
	DrawTextA( hDC, szText, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

	if ( pDIS->itemState & ODS_FOCUS )
	{
		int nTextTall = calcRect.bottom - calcRect.top;
		RECT focus;
		focus.left = textRect.left - 2;
		focus.right = textRect.left + ( calcRect.right - calcRect.left ) + 2;
		focus.top = rect.top + ( ( rect.bottom - rect.top ) - nTextTall ) / 2 - 1;
		focus.bottom = focus.top + nTextTall + 2;
		SetTextColor( hDC, CLR_UI_TEXT );
		SetBkColor( hDC, CLR_UI_BACKGROUND );
		DrawFocusRect( hDC, &focus );
	}
}

static void DrawComboItem( const DRAWITEMSTRUCT *pDIS )
{
	HDC hDC = pDIS->hDC;
	RECT rect = pDIS->rcItem;
	bool bSelected = ( pDIS->itemState & ODS_SELECTED ) != 0;

	FillSolidRect( hDC, rect, bSelected ? CLR_UI_SELECTION : CLR_UI_FIELD );

	if ( (int)pDIS->itemID < 0 )
		return;

	char szText[128];
	szText[0] = 0;
	int nLen = (int)SendMessageA( pDIS->hwndItem, CB_GETLBTEXTLEN, pDIS->itemID, 0 );
	if ( nLen >= 0 && nLen < (int)sizeof( szText ) )
	{
		SendMessageA( pDIS->hwndItem, CB_GETLBTEXT, pDIS->itemID, (LPARAM)szText );
	}

	RECT textRect = rect;
	textRect.left += 4;

	SelectObject( hDC, s_hFont );
	SetBkMode( hDC, TRANSPARENT );
	SetTextColor( hDC, bSelected ? RGB( 255, 255, 255 ) : CLR_UI_TEXT );
	DrawTextA( hDC, szText, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
}

static void OnDrawItem( const DRAWITEMSTRUCT *pDIS )
{
	if ( pDIS->CtlType == ODT_COMBOBOX )
	{
		DrawComboItem( pDIS );
	}
	else if ( pDIS->CtlType == ODT_BUTTON )
	{
		if ( pDIS->CtlID == IDOK || pDIS->CtlID == IDCANCEL )
			DrawButton( pDIS );
		else
			DrawCheckBox( pDIS );
	}
}

//-----------------------------------------------------------------------------
// Purpose: window procedure
//-----------------------------------------------------------------------------
static LRESULT CALLBACK LauncherWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch ( uMsg )
	{
	case WM_CREATE:
		s_hWnd = hWnd;
		CreateControls();
		WriteControls();
		return 0;

	case WM_PAINT:
		OnPaint();
		return 0;

	case WM_ERASEBKGND:
		{
			RECT rect;
			GetClientRect( hWnd, &rect );
			FillRect( (HDC)wParam, &rect, s_hBackgroundBrush );
		}
		return 1;

	case WM_CTLCOLORSTATIC:
		{
			HDC hDC = (HDC)wParam;
			if ( GetDlgCtrlID( (HWND)lParam ) == IDC_CMDLINE )
			{
				// read-only edit
				SetTextColor( hDC, CLR_UI_TEXT_DIM );
				SetBkColor( hDC, CLR_UI_FIELD );
				return (LRESULT)s_hFieldBrush;
			}

			SetTextColor( hDC, CLR_UI_TEXT );
			SetBkMode( hDC, TRANSPARENT );
			return (LRESULT)s_hBackgroundBrush;
		}

	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
		{
			HDC hDC = (HDC)wParam;
			SetTextColor( hDC, CLR_UI_TEXT );
			SetBkColor( hDC, CLR_UI_FIELD );
			return (LRESULT)s_hFieldBrush;
		}

	case WM_MEASUREITEM:
		{
			MEASUREITEMSTRUCT *pMIS = (MEASUREITEMSTRUCT *)lParam;
			if ( pMIS->CtlType == ODT_COMBOBOX )
			{
				pMIS->itemHeight = 16;
				return TRUE;
			}
		}
		break;

	case WM_DRAWITEM:
		OnDrawItem( (const DRAWITEMSTRUCT *)lParam );
		return TRUE;

	case WM_COMMAND:
		{
			int nID = LOWORD( wParam );
			int nCode = HIWORD( wParam );

			switch ( nID )
			{
			case IDOK:
				ReadControls();
				SaveSettings();
				if ( LaunchGame() && s_Settings.bCloseOnLaunch )
				{
					DestroyWindow( hWnd );
				}
				return 0;

			case IDCANCEL:
				DestroyWindow( hWnd );
				return 0;

			case IDC_WINDOWMODE:
			case IDC_RESOLUTION:
				if ( nCode == CBN_SELCHANGE )
					OnSettingsChanged();
				return 0;

			case IDC_CONNECT:
			case IDC_EXTRAARGS:
				if ( nCode == EN_CHANGE )
					OnSettingsChanged();
				return 0;

			case IDC_NOVID:
			case IDC_CONSOLE:
			case IDC_HIGHPRIORITY:
			case IDC_CLOSEONLAUNCH:
			case IDC_RMLUI:
				if ( nCode == BN_CLICKED || nCode == BN_DOUBLECLICKED )
				{
					SetChecked( nID, !IsChecked( nID ) );
					OnSettingsChanged();
				}
				return 0;
			}
		}
		break;

	case WM_CLOSE:
		DestroyWindow( hWnd );
		return 0;

	case WM_DESTROY:
		ReadControls();
		SaveSettings();
		PostQuitMessage( 0 );
		return 0;
	}

	return DefWindowProcA( hWnd, uMsg, wParam, lParam );
}

static HFONT CreateUIFont( const char *pFace, int nPointSize, int nWeight )
{
	HDC hDC = GetDC( NULL );
	int nHeight = -MulDiv( nPointSize, GetDeviceCaps( hDC, LOGPIXELSY ), 72 );
	ReleaseDC( NULL, hDC );

	return CreateFontA( nHeight, 0, 0, 0, nWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, pFace );
}

//-----------------------------------------------------------------------------
// Purpose: entry point
//-----------------------------------------------------------------------------
int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	s_hInstance = hInstance;

	InitBaseDir();
	LoadSettings();
	InitVideoModes();

	s_hFont = CreateUIFont( "Tahoma", 8, FW_NORMAL );
	s_hSectionFont = CreateUIFont( "Tahoma", 8, FW_BOLD );
	s_hTitleFont = CreateUIFont( "Verdana", 14, FW_BOLD );
	s_hBackgroundBrush = CreateSolidBrush( CLR_UI_BACKGROUND );
	s_hFieldBrush = CreateSolidBrush( CLR_UI_FIELD );

	HICON hIcon = LoadIconA( hInstance, MAKEINTRESOURCEA( IDI_LAUNCHER ) );

	WNDCLASSA wc;
	memset( &wc, 0, sizeof( wc ) );
	wc.lpfnWndProc = LauncherWndProc;
	wc.hInstance = hInstance;
	wc.hIcon = hIcon;
	wc.hCursor = LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground = s_hBackgroundBrush;
	wc.lpszClassName = LAUNCHER_CLASS;
	if ( !RegisterClassA( &wc ) )
		return 1;

	DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
	RECT rect;
	SetRect( &rect, 0, 0, CLIENT_WIDE, CLIENT_TALL );
	AdjustWindowRect( &rect, dwStyle, FALSE );

	int nWide = rect.right - rect.left;
	int nTall = rect.bottom - rect.top;
	int x = ( GetSystemMetrics( SM_CXSCREEN ) - nWide ) / 2;
	int y = ( GetSystemMetrics( SM_CYSCREEN ) - nTall ) / 2;

	HWND hWnd = CreateWindowExA( 0, LAUNCHER_CLASS, LAUNCHER_TITLE, dwStyle,
		x, y, nWide, nTall, NULL, NULL, hInstance, NULL );
	if ( !hWnd )
		return 1;

	ShowWindow( hWnd, nCmdShow );
	UpdateWindow( hWnd );
	SetFocus( GetDlgItem( hWnd, IDOK ) );

	MSG msg;
	while ( GetMessageA( &msg, NULL, 0, 0 ) > 0 )
	{
		// tab navigation, Enter = Play, Escape = Exit
		if ( IsDialogMessageA( hWnd, &msg ) )
			continue;

		TranslateMessage( &msg );
		DispatchMessageA( &msg );
	}

	DeleteObject( s_hFont );
	DeleteObject( s_hSectionFont );
	DeleteObject( s_hTitleFont );
	DeleteObject( s_hBackgroundBrush );
	DeleteObject( s_hFieldBrush );

	return (int)msg.wParam;
}
