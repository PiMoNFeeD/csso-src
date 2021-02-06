//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//


#if defined( WIN32 ) && !defined( _X360 )
#include <windows.h> // SRC only!!
#endif

#include "ModOptionsSubCrosshair.h"
#include <stdio.h>

#include <vgui_controls/Button.h>
#include <vgui_controls/QueryBox.h>
#include <vgui_controls/CheckButton.h>
#include "tier1/KeyValues.h"
#include <vgui_controls/Label.h>
#include <vgui/ISystem.h>
#include <vgui/ISurface.h>
#include <vgui/Cursor.h>
#include <vgui_controls/RadioButton.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/FileOpenDialog.h>
#include <vgui_controls/MessageBox.h>
#include <vgui/IVGui.h>
#include <vgui/ILocalize.h>
#include <vgui/IPanel.h>
#include <vgui_controls/MessageBox.h>

#include "CvarTextEntry.h"
#include "CvarToggleCheckButton.h"
#include "cvarslider.h"
#include "LabeledCommandComboBox.h"
#include "filesystem.h"
#include "EngineInterface.h"
#include "BitmapImagePanel.h"
#include "tier1/utlbuffer.h"
#include "ModInfo.h"
#include "tier1/convar.h"
#include "tier0/icommandline.h"

#ifdef WIN32
#include <io.h>
#endif

#if defined( _X360 )
#include "xbox/xbox_win32stubs.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

using namespace vgui;

struct ColorItem_t
{
	const char	*name;
	int			r, g, b;
};

static ColorItem_t s_crosshairColors[] = 
{
	{ "#Valve_Green",	50,		250,	50 },
	{ "#Valve_Red",		250,	50,		50 },
	{ "#Valve_Blue",	50,		50,		250 },
	{ "#Valve_Yellow",	250,	250,	50 },
	{ "#Valve_Ltblue",	50,		250,	250 },
};

static const char* crosshairBackgroundImages[] =
{
	"crosshair/de_dust2",
	"crosshair/de_mirage",
	"crosshair/de_aztec",
	"crosshair/cs_office",
};


//-----------------------------------------------------------------------------
class CrosshairImagePanelCS : public CrosshairImagePanelBase
{
	DECLARE_CLASS_SIMPLE( CrosshairImagePanelCS, CrosshairImagePanelBase );

public:
	CrosshairImagePanelCS( Panel *parent, const char *name, CModOptionsSubCrosshair* pOptionsPanel );
	virtual void ResetData();
	virtual void ApplyChanges();

protected:
	MESSAGE_FUNC_PARAMS( OnSliderMoved, "SliderMoved", data );
	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );
	MESSAGE_FUNC( OnCheckButtonChecked, "CheckButtonChecked" );

	virtual void Paint();
	void DrawCrosshairRect( int r, int g, int b, int a, int x0, int y0, int x1, int y1, bool bAdditive );
	void UpdateCrosshair();

private:
	CModOptionsSubCrosshair	*m_pOptionsPanel;
	CLabeledCommandComboBox	*m_pCrosshairStyle;
	CCvarSlider				*m_pCrosshairAlpha;
	CCvarToggleCheckButton	*m_pCrosshairUseAlpha;
	CCvarSlider				*m_pCrosshairGap;
	CCvarToggleCheckButton	*m_pCrosshairGapUseWeaponValue;
	CCvarSlider				*m_pCrosshairSize;
	CCvarSlider				*m_pCrosshairThickness;
	CCvarToggleCheckButton	*m_pCrosshairDot;
	CCvarSlider				*m_pCrosshairColorR;
	CCvarSlider				*m_pCrosshairColorG;
	CCvarSlider				*m_pCrosshairColorB;
	CCvarToggleCheckButton	*m_pCrosshairDrawOutline;
	CCvarSlider				*m_pCrosshairOutlineThickness;
	CCvarToggleCheckButton	*m_pCrosshairT;
	int m_iCrosshairTextureID;
};

//-----------------------------------------------------------------------------
CrosshairImagePanelCS::CrosshairImagePanelCS( Panel *parent, const char *name, CModOptionsSubCrosshair* pOptionsPanel ) : CrosshairImagePanelBase( parent, name )
{
	m_pOptionsPanel = pOptionsPanel;
	m_pCrosshairStyle = new CLabeledCommandComboBox( m_pOptionsPanel, "CrosshairStyle" );
	m_pCrosshairAlpha = new CCvarSlider( m_pOptionsPanel, "CrosshairAlpha", "#GameUI_Crosshair_Alpha", 0.0f, 255.0f, "cl_crosshairalpha" );
	m_pCrosshairUseAlpha = new CCvarToggleCheckButton( m_pOptionsPanel, "CrosshairUseAlpha", "#GameUI_Crosshair_UseAlpha", "cl_crosshairusealpha" );
	m_pCrosshairGap = new CCvarSlider( m_pOptionsPanel, "CrosshairGap", "#GameUI_Crosshair_Gap", -5.0f, 5.0f, "cl_crosshairgap" );
	m_pCrosshairGapUseWeaponValue = new CCvarToggleCheckButton( m_pOptionsPanel, "CrosshairGapUseWeaponValue", "#GameUI_Crosshair_Gap_UseWeaponValue", "cl_crosshairgap_useweaponvalue" );
	m_pCrosshairSize = new CCvarSlider( m_pOptionsPanel, "CrosshairSize", "#GameUI_Crosshair_Size", 0.0f, 10.0f, "cl_crosshairsize" );
	m_pCrosshairThickness = new CCvarSlider( m_pOptionsPanel, "CrosshairThickness", "#GameUI_Crosshair_Thickness", 0.1f, 6.0f, "cl_crosshairthickness" );
	m_pCrosshairDot = new CCvarToggleCheckButton( m_pOptionsPanel, "CrosshairDot", "#GameUI_CrosshairDot", "cl_crosshairdot" );
	m_pCrosshairColorR = new CCvarSlider( m_pOptionsPanel, "CrosshairColorR", "#GameUI_Crosshair_Color_R", 0.0f, 255.0f, "cl_crosshaircolor_r" );
	m_pCrosshairColorG = new CCvarSlider( m_pOptionsPanel, "CrosshairColorG", "#GameUI_Crosshair_Color_G", 0.0f, 255.0f, "cl_crosshaircolor_g" );
	m_pCrosshairColorB = new CCvarSlider( m_pOptionsPanel, "CrosshairColorB", "#GameUI_Crosshair_Color_B", 0.0f, 255.0f, "cl_crosshaircolor_b" );
	m_pCrosshairDrawOutline = new CCvarToggleCheckButton( m_pOptionsPanel, "CrosshairDrawOutline", "#GameUI_Crosshair_DrawOutline", "cl_crosshair_drawoutline" );
	m_pCrosshairOutlineThickness = new CCvarSlider( m_pOptionsPanel, "CrosshairOutlineThickness", "#GameUI_Crosshair_OutlineThickness", 0.0f, 3.0f, "cl_crosshair_outlinethickness" );
	m_pCrosshairT = new CCvarToggleCheckButton( m_pOptionsPanel, "CrosshairT", "#GameUI_Crosshair_T", "cl_crosshair_t" );

	//m_pCrosshairStyle->AddItem( "#GameUI_Crosshair_Style_0", "cl_crosshairstyle 0" );
	//m_pCrosshairStyle->AddItem( "#GameUI_Crosshair_Style_1", "cl_crosshairstyle 1" );
	m_pCrosshairStyle->AddItem( "#GameUI_Crosshair_Style_2", "cl_crosshairstyle 2" );
	m_pCrosshairStyle->AddItem( "#GameUI_Crosshair_Style_3", "cl_crosshairstyle 3" );
	m_pCrosshairStyle->AddItem( "#GameUI_Crosshair_Style_4", "cl_crosshairstyle 4" );
	//m_pCrosshairStyle->AddItem( "#GameUI_Crosshair_Style_5", "cl_crosshairstyle 5" ); // deprecated in CSGO

	m_pCrosshairStyle->AddActionSignalTarget( this );
	m_pCrosshairAlpha->AddActionSignalTarget( this );
	m_pCrosshairUseAlpha->AddActionSignalTarget( this );
	m_pCrosshairGap->AddActionSignalTarget( this );
	m_pCrosshairGapUseWeaponValue->AddActionSignalTarget( this );
	m_pCrosshairSize->AddActionSignalTarget( this );
	m_pCrosshairThickness->AddActionSignalTarget( this );
	m_pCrosshairDot->AddActionSignalTarget( this );
	m_pCrosshairColorR->AddActionSignalTarget( this );
	m_pCrosshairColorG->AddActionSignalTarget( this );
	m_pCrosshairColorB->AddActionSignalTarget( this );
	m_pCrosshairDrawOutline->AddActionSignalTarget( this );
	m_pCrosshairOutlineThickness->AddActionSignalTarget( this );
	m_pCrosshairT->AddActionSignalTarget( this );

	m_iCrosshairTextureID = vgui::surface()->CreateNewTextureID();
	vgui::surface()->DrawSetTextureFile( m_iCrosshairTextureID, "vgui/white_additive" , true, false);

	ResetData();
}

//-----------------------------------------------------------------------------
void CrosshairImagePanelCS::DrawCrosshairRect( int r, int g, int b, int a, int x0, int y0, int x1, int y1, bool bAdditive )
{
	if ( m_pCrosshairDrawOutline->IsSelected() )
	{
		float flThick = m_pCrosshairOutlineThickness->GetSliderValue();
		vgui::surface()->DrawSetColor( 0, 0, 0, a );
		vgui::surface()->DrawFilledRect( x0-flThick, y0-flThick, x1+flThick, y1+flThick );
	}

	vgui::surface()->DrawSetColor( r, g, b, a );

	if ( bAdditive )
	{
		vgui::surface()->DrawTexturedRect( x0, y0, x1, y1 );
	}
	else
	{
		// Alpha-blended crosshair
		vgui::surface()->DrawFilledRect( x0, y0, x1, y1 );
	}
}

//-----------------------------------------------------------------------------
void CrosshairImagePanelCS::Paint()
{
	int screenWide, screenTall;
	surface()->GetScreenSize( screenWide, screenTall );;

	BaseClass::Paint();

	int wide, tall;
	GetSize( wide, tall );

	bool bAdditive = !m_pCrosshairUseAlpha->IsSelected();

	int a = 200;
	if ( !bAdditive )
		a = m_pCrosshairAlpha->GetSliderValue();

	int r, g, b;
	r = m_pCrosshairColorR->GetSliderValue();
	g = m_pCrosshairColorG->GetSliderValue();
	b = m_pCrosshairColorB->GetSliderValue();

	vgui::surface()->DrawSetColor( r, g, b, a );

	if ( bAdditive )
	{
		vgui::surface()->DrawSetTexture( m_iCrosshairTextureID );
	}

	int centerX = wide / 2;
	int centerY = tall / 2;

	int iBarSize = RoundFloatToInt(m_pCrosshairSize->GetSliderValue() * screenTall / 480.0f);
	int iBarThickness = max(1, RoundFloatToInt(m_pCrosshairThickness->GetSliderValue() * (float)screenTall / 480.0f));

	int iBarGap = m_pCrosshairGap->GetSliderValue() + 4;

	// draw horizontal crosshair lines
	int iInnerLeft = centerX - iBarGap - iBarThickness / 2;
	int iInnerRight = iInnerLeft + 2 * iBarGap + iBarThickness;
	int iOuterLeft = iInnerLeft - iBarSize;
	int iOuterRight = iInnerRight + iBarSize;
	int y0 = centerY - iBarThickness / 2;
	int y1 = y0 + iBarThickness;
	DrawCrosshairRect( r, g, b, a, iOuterLeft, y0, iInnerLeft, y1, bAdditive );
	DrawCrosshairRect( r, g, b, a, iInnerRight, y0, iOuterRight, y1, bAdditive );

	// draw vertical crosshair lines
	int iInnerTop = centerY - iBarGap - iBarThickness / 2;
	int iInnerBottom = iInnerTop + 2 * iBarGap + iBarThickness;
	int iOuterTop = iInnerTop - iBarSize;
	int iOuterBottom = iInnerBottom + iBarSize;
	int x0 = centerX - iBarThickness / 2;
	int x1 = x0 + iBarThickness;
	if ( !m_pCrosshairT->IsSelected() )
		DrawCrosshairRect( r, g, b, a, x0, iOuterTop, x1, iInnerTop, bAdditive );
	DrawCrosshairRect( r, g, b, a, x0, iInnerBottom, x1, iOuterBottom, bAdditive );

	// draw dot
	if ( m_pCrosshairDot->IsSelected() )
	{
		x0 = centerX - iBarThickness / 2;
		x1 = x0 + iBarThickness;
		y0 = centerY - iBarThickness / 2;
		y1 = y0 + iBarThickness;
		DrawCrosshairRect( r, g, b, a, x0, y0, x1, y1, bAdditive );
	}
}


//-----------------------------------------------------------------------------
// Purpose: takes the settings from the crosshair settings combo boxes and sliders
//          and apply it to the crosshair illustrations.
//-----------------------------------------------------------------------------
void CrosshairImagePanelCS::UpdateCrosshair()
{
}


void CrosshairImagePanelCS::OnSliderMoved(KeyValues *data)
{
	m_pOptionsPanel->OnControlModified();

	UpdateCrosshair();
}


//-----------------------------------------------------------------------------
// Purpose: Called whenever color combo changes
//-----------------------------------------------------------------------------
void CrosshairImagePanelCS::OnTextChanged(vgui::Panel *panel)
{
	m_pOptionsPanel->OnControlModified();
	UpdateCrosshair();
}

void CrosshairImagePanelCS::OnCheckButtonChecked()
{
	m_pCrosshairAlpha->SetEnabled(m_pCrosshairUseAlpha->IsSelected());
	m_pCrosshairOutlineThickness->SetEnabled(m_pCrosshairDrawOutline->IsSelected());
	m_pOptionsPanel->OnControlModified();
	UpdateCrosshair();
}

void CrosshairImagePanelCS::ResetData()
{
	ConVarRef cl_crosshairstyle( "cl_crosshairstyle" );
	m_pCrosshairStyle->SetInitialItem( Clamp( cl_crosshairstyle.GetInt() - 2, 0, 2 ) ); // remove Clamp if unlocking other styles

	m_pCrosshairAlpha->Reset();
	m_pCrosshairUseAlpha->Reset();
	m_pCrosshairGap->Reset();
	m_pCrosshairGapUseWeaponValue->Reset();
	m_pCrosshairSize->Reset();
	m_pCrosshairThickness->Reset();
	m_pCrosshairDot->Reset();
	m_pCrosshairColorR->Reset();
	m_pCrosshairColorG->Reset();
	m_pCrosshairColorB->Reset();
	m_pCrosshairDrawOutline->Reset();
	m_pCrosshairOutlineThickness->Reset();
	m_pCrosshairT->Reset();

	SetImage( crosshairBackgroundImages[RandomInt(0, ARRAYSIZE(crosshairBackgroundImages) - 1)] ); // bruh

	UpdateCrosshair();
}

void CrosshairImagePanelCS::ApplyChanges()
{
	m_pCrosshairStyle->ApplyChanges();
	m_pCrosshairAlpha->ApplyChanges();
	m_pCrosshairUseAlpha->ApplyChanges();
	m_pCrosshairGap->ApplyChanges();
	m_pCrosshairGapUseWeaponValue->ApplyChanges();
	m_pCrosshairSize->ApplyChanges();
	m_pCrosshairThickness->ApplyChanges();
	m_pCrosshairDot->ApplyChanges();
	m_pCrosshairColorR->ApplyChanges();
	m_pCrosshairColorG->ApplyChanges();
	m_pCrosshairColorB->ApplyChanges();
	m_pCrosshairDrawOutline->ApplyChanges();
	m_pCrosshairOutlineThickness->ApplyChanges();
	m_pCrosshairT->ApplyChanges();
}

//-----------------------------------------------------------------------------
// Purpose: Basic help dialog
//-----------------------------------------------------------------------------
CModOptionsSubCrosshair::CModOptionsSubCrosshair(vgui::Panel *parent) : vgui::PropertyPage(parent, "ModOptionsSubCrosshair") 
{
	Button *cancel = new Button( this, "Cancel", "#GameUI_Cancel" );
	cancel->SetCommand( "Close" );

	Button *ok = new Button( this, "OK", "#GameUI_OK" );
	ok->SetCommand( "Ok" );

	Button *apply = new Button( this, "Apply", "#GameUI_Apply" );
	apply->SetCommand( "Apply" );

	m_pCrosshairImage = new CrosshairImagePanelCS( this, "CrosshairImage", this );

	//=========

	LoadControlSettings("Resource/ModOptionsSubCrosshair.res");

	// this is necessary because some of the game .res files don't have visiblity flags set up correctly for their controls
	if ( m_pCrosshairImage )
		m_pCrosshairImage->UpdateVisibility();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CModOptionsSubCrosshair::~CModOptionsSubCrosshair()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CModOptionsSubCrosshair::OnControlModified()
{
	PostMessage(GetParent(), new KeyValues("ApplyButtonEnable"));
	InvalidateLayout();
}

#define DIB_HEADER_MARKER   ((WORD) ('M' << 8) | 'B')
#define SUIT_HUE_START 192
#define SUIT_HUE_END 223
#define PLATE_HUE_START 160
#define PLATE_HUE_END 191

#ifdef POSIX 
typedef struct tagRGBQUAD { 
  uint8 rgbBlue;
  uint8 rgbGreen;
  uint8 rgbRed;
  uint8 rgbReserved;
} RGBQUAD;
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static void PaletteHueReplace( RGBQUAD *palSrc, int newHue, int Start, int end )
{
	int i;
	float r, b, g;
	float maxcol, mincol;
	float hue, val, sat;

	hue = (float)(newHue * (360.0 / 255));

	for (i = Start; i <= end; i++)
	{
		b = palSrc[ i ].rgbBlue;
		g = palSrc[ i ].rgbGreen;
		r = palSrc[ i ].rgbRed;
		
		maxcol = max( max( r, g ), b ) / 255.0f;
		mincol = min( min( r, g ), b ) / 255.0f;
		
		val = maxcol;
		sat = (maxcol - mincol) / maxcol;

		mincol = val * (1.0f - sat);

		if (hue <= 120)
		{
			b = mincol;
			if (hue < 60)
			{
				r = val;
				g = mincol + hue * (val - mincol)/(120 - hue);
			}
			else
			{
				g = val;
				r = mincol + (120 - hue)*(val-mincol)/hue;
			}
		}
		else if (hue <= 240)
		{
			r = mincol;
			if (hue < 180)
			{
				g = val;
				b = mincol + (hue - 120)*(val-mincol)/(240 - hue);
			}
			else
			{
				b = val;
				g = mincol + (240 - hue)*(val-mincol)/(hue - 120);
			}
		}
		else
		{
			g = mincol;
			if (hue < 300)
			{
				b = val;
				r = mincol + (hue - 240)*(val-mincol)/(360 - hue);
			}
			else
			{
				r = val;
				b = mincol + (360 - hue)*(val-mincol)/(hue - 240);
			}
		}

		palSrc[ i ].rgbBlue = (unsigned char)(b * 255);
		palSrc[ i ].rgbGreen = (unsigned char)(g * 255);
		palSrc[ i ].rgbRed = (unsigned char)(r * 255);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CModOptionsSubCrosshair::OnResetData()
{
	if ( m_pCrosshairImage )
		m_pCrosshairImage->ResetData();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CModOptionsSubCrosshair::OnApplyChanges()
{
	if ( m_pCrosshairImage != NULL )
		m_pCrosshairImage->ApplyChanges();
}

