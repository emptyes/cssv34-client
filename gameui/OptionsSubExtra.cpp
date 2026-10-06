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
#include <vgui_controls/Label.h>
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

COptionsSubExtra::COptionsSubExtra( vgui::Panel *parent ) : PropertyPage( parent, NULL )
{
	m_pRadarHeader = new Label( this, "RadarHeader", EXTRA_STR_RADAR );
	m_pRadarLocked = new CCvarToggleCheckButton( this, "RadarLocked", EXTRA_STR_LOCKED, "cl_radar_locked" );

	for ( int i = 0; i < NUM_SLIDERS; i++ )
	{
		char name[32];
		Q_snprintf( name, sizeof( name ), "ExtraCaption%d", i );
		m_pSliderCaption[i] = new Label( this, name, s_Sliders[i].caption );
		Q_snprintf( name, sizeof( name ), "ExtraSlider%d", i );
		m_pSlider[i] = new CCvarSlider( this, name, s_Sliders[i].caption, s_Sliders[i].flMin, s_Sliders[i].flMax, s_Sliders[i].cvar, true );
		Q_snprintf( name, sizeof( name ), "ExtraValue%d", i );
		m_pSliderValue[i] = new Label( this, name, "" );
	}

	m_pScoreboardHeader = new Label( this, "ScoreboardHeader", EXTRA_STR_SB );
	m_pShowClientMod = new CCvarToggleCheckButton( this, "ShowClientMod", EXTRA_STR_CM, "cl_scoreboard_show_clientmod" );

	UpdateValueLabels();
}

COptionsSubExtra::~COptionsSubExtra()
{
}

void COptionsSubExtra::PerformLayout()
{
	BaseClass::PerformLayout();

	const int x = 24;
	const int lineH = 24;
	int y = 20;

	m_pRadarHeader->SetBounds( x, y, 300, 20 );
	y += lineH;
	m_pRadarLocked->SetBounds( x, y, 420, 24 );
	y += lineH + 8;

	for ( int i = 0; i < NUM_SLIDERS; i++ )
	{
		m_pSliderCaption[i]->SetBounds( x, y, 300, 20 );
		y += 20;
		m_pSlider[i]->SetBounds( x, y, 320, 24 );
		m_pSliderValue[i]->SetBounds( x + 330, y, 60, 24 );
		y += lineH + 10;
	}

	y += 6;
	m_pScoreboardHeader->SetBounds( x, y, 300, 20 );
	y += lineH;
	m_pShowClientMod->SetBounds( x, y, 420, 24 );
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
