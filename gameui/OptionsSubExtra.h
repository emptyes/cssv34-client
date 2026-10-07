//========= cssv34-client: extra options page ================================//
//
// Purpose: "Extra" tab in Options. Radar customization and other client
//          additions. New settings for future features go here.
//
//=============================================================================//

#ifndef OPTIONS_SUB_EXTRA_H
#define OPTIONS_SUB_EXTRA_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/PropertyPage.h>
#include <vgui/VGUI.h>

class CCvarToggleCheckButton;
class CCvarSlider;

namespace vgui { class CheckButton; }

namespace vgui
{
	class Label;
	class ScrollBar;
	class IScheme;
}

// UTF-8 strings (escaped so the source file is encoding-independent)
#define EXTRA_STR_TAB "\xD0\x94\xD0\xBE\xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBD\xD0\xB8\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C\xD0\xBD\xD0\xBE"
#define EXTRA_STR_RADAR "\xD0\xA0\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x80"
#define EXTRA_STR_LOCKED "\xD0\xA1\xD1\x82\xD0\xB0\xD1\x82\xD0\xB8\xD1\x87\xD0\xBD\xD1\x8B\xD0\xB9 \xD1\x80\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x80"
#define EXTRA_STR_SCALE "\xD0\x9E\xD0\xB1\xD0\xB7\xD0\xBE\xD1\x80 (\xD1\x81\xD0\xBA\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xBA\xD0\xBE \xD0\xBA\xD0\xB0\xD1\x80\xD1\x82\xD1\x8B \xD0\xB2\xD0\xB8\xD0\xB4\xD0\xBD\xD0\xBE)"
#define EXTRA_STR_SIZE "\xD0\xA0\xD0\xB0\xD0\xB7\xD0\xBC\xD0\xB5\xD1\x80 \xD1\x80\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x80\xD0\xB0 \xD0\xBD\xD0\xB0 \xD1\x8D\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB5"
#define EXTRA_STR_ICON "\xD0\xA0\xD0\xB0\xD0\xB7\xD0\xBC\xD0\xB5\xD1\x80 \xD0\xB8\xD0\xBA\xD0\xBE\xD0\xBD\xD0\xBE\xD0\xBA"
#define EXTRA_STR_ALPHA "\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB7\xD1\x80\xD0\xB0\xD1\x87\xD0\xBD\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C \xD1\x84\xD0\xBE\xD0\xBD\xD0\xB0"
#define EXTRA_STR_SB "\xD0\xA2\xD0\xB0\xD0\xB1\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0 \xD1\x81\xD1\x87\xD1\x91\xD1\x82\xD0\xB0 (TAB)"
#define EXTRA_STR_CM "\xD0\x9F\xD0\xBE\xD0\xBA\xD0\xB0\xD0\xB7\xD1\x8B\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C CM \xD1\x83 \xD0\xB8\xD0\xB3\xD1\x80\xD0\xBE\xD0\xBA\xD0\xBE\xD0\xB2 \xD1\x81 ClientMod"

#define EXTRA_STR_PERF "\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB8\xD0\xB7\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C\xD0\xBD\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C"
#define EXTRA_STR_THREADED "\xD0\x9C\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xBF\xD0\xBE\xD1\x82\xD0\xBE\xD1\x87\xD0\xBD\xD0\xB0\xD1\x8F \xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82\xD0\xBA\xD0\xB0 \xD0\xB0\xD0\xBD\xD0\xB8\xD0\xBC\xD0\xB0\xD1\x86\xD0\xB8\xD0\xB9 \xD0\xB8 \xD0\xBE\xD0\xB1\xD1\x8A\xD0\xB5\xD0\xBA\xD1\x82\xD0\xBE\xD0\xB2 (\xD0\xB1\xD0\xBE\xD0\xBB\xD1\x8C\xD1\x88\xD0\xB5 FPS \xD0\xBD\xD0\xB0 \xD0\xBC\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\xD1\x8F\xD0\xB4\xD0\xB5\xD1\x80\xD0\xBD\xD1\x8B\xD1\x85 \xD0\xBF\xD1\x80\xD0\xBE\xD1\x86\xD0\xB5\xD1\x81\xD1\x81\xD0\xBE\xD1\x80\xD0\xB0\xD1\x85)"

class COptionsSubExtra : public vgui::PropertyPage
{
	DECLARE_CLASS_SIMPLE( COptionsSubExtra, vgui::PropertyPage );

public:
	COptionsSubExtra( vgui::Panel *parent );
	~COptionsSubExtra();

	virtual void OnResetData();
	virtual void OnApplyChanges();

protected:
	virtual void PerformLayout();
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void OnMouseWheeled( int delta );

	MESSAGE_FUNC_INT( OnScrollBarMoved, "ScrollBarSliderMoved", position );

	MESSAGE_FUNC_PTR( OnControlModified, "ControlModified", panel );
	MESSAGE_FUNC_PTR( OnCheckButtonChecked, "CheckButtonChecked", panel )
	{
		OnControlModified( panel );
	}

private:
	enum { NUM_SLIDERS = 4 };

	void UpdateValueLabels();

	// Lays out all controls; returns full content height. yOffset = scroll position.
	int LayoutContent( int x, int yOffset, int wide );
	int LayoutCheckButton( vgui::CheckButton *pCheck, const wchar_t *text, int x, int y, int wide );

	vgui::Label					*m_pRadarHeader;
	CCvarToggleCheckButton		*m_pRadarLocked;
	vgui::Label					*m_pSliderCaption[NUM_SLIDERS];
	CCvarSlider					*m_pSlider[NUM_SLIDERS];
	vgui::Label					*m_pSliderValue[NUM_SLIDERS];

	vgui::Label					*m_pScoreboardHeader;
	CCvarToggleCheckButton		*m_pShowClientMod;

	vgui::Label					*m_pPerfHeader;
	vgui::CheckButton			*m_pThreaded;	// cl_threaded_bone_setup + r_threaded_renderables
	bool						m_bThreadedStart;

	vgui::ScrollBar				*m_pScrollBar;
	int							m_iContentTall;
	vgui::HFont					m_hTickFont;

	// original (unwrapped) texts, re-wrapped on every layout
	wchar_t						m_wszRadarHeader[64];
	wchar_t						m_wszRadarLocked[256];
	wchar_t						m_wszSliderCaption[NUM_SLIDERS][128];
	wchar_t						m_wszScoreboardHeader[64];
	wchar_t						m_wszShowClientMod[256];
	wchar_t						m_wszPerfHeader[64];
	wchar_t						m_wszThreaded[256];
};

#endif // OPTIONS_SUB_EXTRA_H
