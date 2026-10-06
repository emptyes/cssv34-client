//========= cssv34-client: extra options page ================================//
//
// Purpose: "Extra" tab in Options (radar customization, client additions)
//
//=============================================================================//

#include "OptionsSubExtra.h"
#include "CvarToggleCheckButton.h"
#include "cvarslider.h"
#include "EngineInterface.h"

#include <KeyValues.h>
#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/ScrollBar.h>
#include "tier1/convar.h"
#include <stdio.h>

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

using namespace vgui;

struct ExtraSliderDef_t
{
	const char *caption;
	const char *cvar;
	float flMin;
	float flMax;
	const char *fmt;
};

static const ExtraSliderDef_t s_Sliders[] =
{
	{ EXTRA_STR_SCALE, "cl_radar_scale",      0.25f, 4.0f,   "%.2f" },
	{ EXTRA_STR_SIZE,  "cl_radar_size",       0.5f,  2.5f,   "%.2f" },
	{ EXTRA_STR_ICON,  "cl_radar_icon_scale", 0.3f,  3.0f,   "%.2f" },
	{ EXTRA_STR_ALPHA, "cl_radaralpha",       0.0f,  255.0f, "%.0f" },
};

//-----------------------------------------------------------------------------
// Layout helpers. Everything is measured from the real fonts at layout time,
// so the page works for any resolution / scheme font size / dialog size.
//-----------------------------------------------------------------------------
static int ExtraFontTall( HFont font, int fallback = 14 )
{
	if ( font == INVALID_FONT )
		return fallback;
	int t = surface()->GetFontTall( font );
	return t > 0 ? t : fallback;
}

static int ExtraTextWide( HFont font, const wchar_t *text )
{
	if ( font == INVALID_FONT || !text || !text[0] )
		return 0;
	int w = 0, t = 0;
	surface()->GetTextSize( font, text, w, t );
	return w;
}

// Greedy word wrap: inserts '\n' so that every line fits into maxWide pixels.
// Returns the number of lines.
static int ExtraWrapText( HFont font, const wchar_t *src, int maxWide, wchar_t *out, int outChars )
{
	out[0] = 0;
	if ( !src || !src[0] )
		return 1;

	if ( font == INVALID_FONT || maxWide <= 0 )
	{
		Q_wcsncpy( out, src, outChars * (int)sizeof( wchar_t ) );
		return 1;
	}

	int outLen = 0;
	int lineStart = 0;
	int lines = 1;
	const wchar_t *p = src;

	wchar_t measure[512];

	while ( *p )
	{
		while ( *p == L' ' )
			p++;
		if ( !*p )
			break;

		const wchar_t *wordStart = p;
		while ( *p && *p != L' ' )
			p++;
		int wordLen = (int)( p - wordStart );

		bool lineEmpty = ( outLen == lineStart );
		int needed = wordLen + ( lineEmpty ? 0 : 1 );
		if ( outLen + needed + 2 >= outChars )
			break;

		if ( !lineEmpty )
		{
			// measure "current line + space + word"
			int curLen = outLen - lineStart;
			bool bFits = false;
			if ( curLen + 1 + wordLen < (int)ARRAYSIZE( measure ) )
			{
				memcpy( measure, out + lineStart, curLen * sizeof( wchar_t ) );
				measure[curLen] = L' ';
				memcpy( measure + curLen + 1, wordStart, wordLen * sizeof( wchar_t ) );
				measure[curLen + 1 + wordLen] = 0;
				bFits = ExtraTextWide( font, measure ) <= maxWide;
			}

			if ( bFits )
			{
				out[outLen++] = L' ';
			}
			else
			{
				out[outLen++] = L'\n';
				lineStart = outLen;
				lines++;
			}
		}

		memcpy( out + outLen, wordStart, wordLen * sizeof( wchar_t ) );
		outLen += wordLen;
		out[outLen] = 0;
	}

	out[outLen] = 0;
	return lines;
}

// Sets wrapped text on a label (only if it changed, to avoid layout loops).
// Returns the height in pixels needed to show all lines.
static int ExtraApplyWrapped( Label *pLabel, const wchar_t *src, int maxWide )
{
	HFont font = pLabel->GetFont();
	wchar_t wrapped[512];
	int lines = ExtraWrapText( font, src, maxWide, wrapped, ARRAYSIZE( wrapped ) );

	wchar_t current[512];
	pLabel->GetText( current, sizeof( current ) );
	if ( wcscmp( current, wrapped ) )
		pLabel->SetText( wrapped );

	return lines * ExtraFontTall( font );
}

COptionsSubExtra::COptionsSubExtra( vgui::Panel *parent ) : PropertyPage( parent, NULL )
{
	m_hTickFont = INVALID_FONT;

	m_pRadarHeader = new Label( this, "RadarHeader", EXTRA_STR_RADAR );
	m_pRadarLocked = new CCvarToggleCheckButton( this, "RadarLocked", EXTRA_STR_LOCKED, "cl_radar_locked" );

	g_pVGuiLocalize->ConvertANSIToUnicode( EXTRA_STR_RADAR, m_wszRadarHeader, sizeof( m_wszRadarHeader ) );
	g_pVGuiLocalize->ConvertANSIToUnicode( EXTRA_STR_LOCKED, m_wszRadarLocked, sizeof( m_wszRadarLocked ) );

	for ( int i = 0; i < NUM_SLIDERS; i++ )
	{
		char name[32];
		Q_snprintf( name, sizeof( name ), "ExtraCaption%d", i );
		m_pSliderCaption[i] = new Label( this, name, s_Sliders[i].caption );
		Q_snprintf( name, sizeof( name ), "ExtraSlider%d", i );
		m_pSlider[i] = new CCvarSlider( this, name, s_Sliders[i].caption, s_Sliders[i].flMin, s_Sliders[i].flMax, s_Sliders[i].cvar, true );
		Q_snprintf( name, sizeof( name ), "ExtraValue%d", i );
		m_pSliderValue[i] = new Label( this, name, "" );
		m_pSliderValue[i]->SetContentAlignment( Label::a_west );

		g_pVGuiLocalize->ConvertANSIToUnicode( s_Sliders[i].caption, m_wszSliderCaption[i], sizeof( m_wszSliderCaption[i] ) );
	}

	m_pScoreboardHeader = new Label( this, "ScoreboardHeader", EXTRA_STR_SB );
	m_pShowClientMod = new CCvarToggleCheckButton( this, "ShowClientMod", EXTRA_STR_CM, "cl_scoreboard_show_clientmod" );

	g_pVGuiLocalize->ConvertANSIToUnicode( EXTRA_STR_SB, m_wszScoreboardHeader, sizeof( m_wszScoreboardHeader ) );
	g_pVGuiLocalize->ConvertANSIToUnicode( EXTRA_STR_CM, m_wszShowClientMod, sizeof( m_wszShowClientMod ) );

	// Vertical scrollbar, shown only if the content doesn't fit into the page
	m_pScrollBar = new ScrollBar( this, "ExtraScrollBar", true );
	m_pScrollBar->SetVisible( false );
	m_pScrollBar->AddActionSignalTarget( this );
	m_iContentTall = 0;

	UpdateValueLabels();
}

COptionsSubExtra::~COptionsSubExtra()
{
}

void COptionsSubExtra::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	// CCvarSlider (vgui::Slider) draws its min/max captions with this font
	m_hTickFont = pScheme->GetFont( "DefaultVerySmall", IsProportional() );
	InvalidateLayout();
}

int COptionsSubExtra::LayoutCheckButton( CCvarToggleCheckButton *pCheck, const wchar_t *text, int x, int y, int wide )
{
	// width of the check box image + its inset + gap before the text
	int boxWide = 20, boxTall = 16;
	IImage *pBox = pCheck->GetImageAtIndex( 0 );
	if ( pBox )
	{
		pBox->GetContentSize( boxWide, boxTall );
		boxWide += 6 /* CheckButton::CHECK_INSET */ + 6;
	}

	int textTall = ExtraApplyWrapped( pCheck, text, wide - boxWide - 6 );
	int tall = MAX( textTall, boxTall ) + 6;
	pCheck->SetBounds( x, y, wide, tall );
	return tall;
}

int COptionsSubExtra::LayoutContent( int x, int yOffset, int wide )
{
	const int fontTall = ExtraFontTall( m_pRadarHeader->GetFont() );
	const int sectionGap = MAX( 10, fontTall );
	const int rowGap = MAX( 4, fontTall / 3 );

	// Slider: track at y=8, nob 8px, ticks + 4px, then min/max captions
	int trackY = IsProportional() ? scheme()->GetProportionalScaledValue( 8 ) : 8;
	const int sliderTall = MAX( 24, trackY + 8 + 4 + ExtraFontTall( m_hTickFont, 10 ) + 2 );
	const int trackCenter = trackY + 2;

	// Width of the value column: widest possible value text
	HFont valueFont = m_pSliderValue[0]->GetFont();
	int valueWide = 0;
	for ( int i = 0; i < NUM_SLIDERS; i++ )
	{
		char buf[32];
		wchar_t wbuf[32];
		Q_snprintf( buf, sizeof( buf ), s_Sliders[i].fmt, s_Sliders[i].flMax );
		g_pVGuiLocalize->ConvertANSIToUnicode( buf, wbuf, sizeof( wbuf ) );
		valueWide = MAX( valueWide, ExtraTextWide( valueFont, wbuf ) );
		Q_snprintf( buf, sizeof( buf ), s_Sliders[i].fmt, s_Sliders[i].flMin );
		g_pVGuiLocalize->ConvertANSIToUnicode( buf, wbuf, sizeof( wbuf ) );
		valueWide = MAX( valueWide, ExtraTextWide( valueFont, wbuf ) );
	}
	valueWide = MAX( valueWide + 8, 32 );
	const int valueGap = 8;

	// Caption and slider in one row if all captions fit into ~45% of the width,
	// otherwise caption above the slider (wrapped to the full width).
	int captionWide = wide * 45 / 100;
	int inlineSliderWide = wide - captionWide - valueGap - valueWide - 8;
	bool bInline = inlineSliderWide >= 100;
	for ( int i = 0; bInline && i < NUM_SLIDERS; i++ )
	{
		if ( ExtraTextWide( m_pSliderCaption[i]->GetFont(), m_wszSliderCaption[i] ) > captionWide - 6 )
			bInline = false;
	}

	int y = MAX( 8, fontTall / 2 );

	// --- Radar ---
	int h = ExtraApplyWrapped( m_pRadarHeader, m_wszRadarHeader, wide );
	m_pRadarHeader->SetBounds( x, y - yOffset, wide, h + 2 );
	y += h + 2 + rowGap;

	y += LayoutCheckButton( m_pRadarLocked, m_wszRadarLocked, x, y - yOffset, wide ) + rowGap;

	for ( int i = 0; i < NUM_SLIDERS; i++ )
	{
		if ( bInline )
		{
			int capTall = ExtraApplyWrapped( m_pSliderCaption[i], m_wszSliderCaption[i], captionWide - 6 ) + 2;
			int capY = MAX( 0, trackCenter - capTall / 2 );
			int sx = x + captionWide;
			m_pSliderCaption[i]->SetBounds( x, y + capY - yOffset, captionWide, capTall );
			m_pSlider[i]->SetBounds( sx, y - yOffset, inlineSliderWide, sliderTall );
			m_pSliderValue[i]->SetBounds( sx + inlineSliderWide + valueGap, y + MAX( 0, trackCenter - ( fontTall + 2 ) / 2 ) - yOffset, valueWide, fontTall + 2 );
			y += MAX( sliderTall, capY + capTall ) + rowGap;
		}
		else
		{
			int capTall = ExtraApplyWrapped( m_pSliderCaption[i], m_wszSliderCaption[i], wide ) + 2;
			m_pSliderCaption[i]->SetBounds( x, y - yOffset, wide, capTall );
			y += capTall;
			int sw = MAX( 40, wide - valueGap - valueWide );
			m_pSlider[i]->SetBounds( x, y - yOffset, sw, sliderTall );
			m_pSliderValue[i]->SetBounds( x + sw + valueGap, y + MAX( 0, trackCenter - ( fontTall + 2 ) / 2 ) - yOffset, valueWide, fontTall + 2 );
			y += sliderTall + rowGap;
		}
	}

	// --- Scoreboard ---
	y += sectionGap - rowGap;
	h = ExtraApplyWrapped( m_pScoreboardHeader, m_wszScoreboardHeader, wide );
	m_pScoreboardHeader->SetBounds( x, y - yOffset, wide, h + 2 );
	y += h + 2 + rowGap;

	y += LayoutCheckButton( m_pShowClientMod, m_wszShowClientMod, x, y - yOffset, wide );

	y += MAX( 8, fontTall / 2 );
	return y;
}

void COptionsSubExtra::PerformLayout()
{
	BaseClass::PerformLayout();

	int wide, tall;
	GetSize( wide, tall );
	if ( wide <= 0 || tall <= 0 )
		return;

	const int margin = MIN( MAX( wide / 24, 8 ), 24 );
	const int scrollWide = IsProportional() ? scheme()->GetProportionalScaledValue( 18 ) : 18;

	// First try without a scrollbar
	int contentWide = wide - margin * 2;
	m_iContentTall = LayoutContent( margin, 0, contentWide );

	if ( m_iContentTall <= tall )
	{
		if ( m_pScrollBar->IsVisible() )
		{
			m_pScrollBar->SetValue( 0 );
			m_pScrollBar->SetVisible( false );
		}
		return;
	}

	// Content doesn't fit: reserve space for the scrollbar and lay out again
	contentWide = wide - margin * 2 - scrollWide;
	m_iContentTall = LayoutContent( margin, 0, contentWide );

	m_pScrollBar->SetVisible( true );
	m_pScrollBar->SetEnabled( true );
	m_pScrollBar->SetBounds( wide - scrollWide - 2, 2, scrollWide, tall - 4 );
	m_pScrollBar->SetRange( 0, m_iContentTall );
	m_pScrollBar->SetRangeWindow( tall );
	m_pScrollBar->SetButtonPressedScrollValue( MAX( 16, ExtraFontTall( m_pRadarHeader->GetFont() ) * 2 ) );
	m_pScrollBar->InvalidateLayout();

	int offset = m_pScrollBar->GetValue();
	if ( offset > 0 )
		LayoutContent( margin, offset, contentWide );
}

void COptionsSubExtra::OnScrollBarMoved( int position )
{
	InvalidateLayout();
	Repaint();
}

void COptionsSubExtra::OnMouseWheeled( int delta )
{
	if ( m_pScrollBar->IsVisible() )
	{
		int step = MAX( 16, ExtraFontTall( m_pRadarHeader->GetFont() ) * 2 );
		m_pScrollBar->SetValue( m_pScrollBar->GetValue() - delta * step );
		return;
	}
	BaseClass::OnMouseWheeled( delta );
}

void COptionsSubExtra::UpdateValueLabels()
{
	for ( int i = 0; i < NUM_SLIDERS; i++ )
	{
		char buf[32];
		Q_snprintf( buf, sizeof( buf ), s_Sliders[i].fmt, m_pSlider[i]->GetSliderValue() );
		m_pSliderValue[i]->SetText( buf );
	}
}

void COptionsSubExtra::OnResetData()
{
	m_pRadarLocked->Reset();
	for ( int i = 0; i < NUM_SLIDERS; i++ )
		m_pSlider[i]->Reset();
	m_pShowClientMod->Reset();
	UpdateValueLabels();
}

void COptionsSubExtra::OnApplyChanges()
{
	m_pRadarLocked->ApplyChanges();
	for ( int i = 0; i < NUM_SLIDERS; i++ )
		m_pSlider[i]->ApplyChanges();
	m_pShowClientMod->ApplyChanges();
}

void COptionsSubExtra::OnControlModified( Panel *panel )
{
	PostActionSignal( new KeyValues( "ApplyButtonEnable" ) );
	UpdateValueLabels();
}
