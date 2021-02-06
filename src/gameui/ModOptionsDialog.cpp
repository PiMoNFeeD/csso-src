//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "BasePanel.h"
#include "ModOptionsDialog.h"

#include "vgui_controls/Button.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/PropertySheet.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/QueryBox.h"

#include "vgui/ILocalize.h"
#include "vgui/ISurface.h"
#include "vgui/ISystem.h"
#include "vgui/IVGui.h"

#include "KeyValues.h"
#include "ModOptionsSubGameplay.h"
#include "ModOptionsSubCrosshair.h"
#include "ModOptionsSubLoadout.h"
#include "ModOptionsSubKnives.h"
#include "ModOptionsSubAgents.h"
#include "ModOptionsSubGloves.h"
#include "ModInfo.h"

using namespace vgui;

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

//-----------------------------------------------------------------------------
// Purpose: Basic help dialog
//-----------------------------------------------------------------------------
CModOptionsDialog::CModOptionsDialog(vgui::Panel *parent) : PropertyDialog(parent, "OptionsDialog")
{
	SetDeleteSelfOnClose(true);
	SetBounds(0, 0, 512, 406);
	SetSizeable( false );

	SetTitle("#GameUI_Mod_Options", true);

	AddPage(new CModOptionsSubGameplay(this), "#GameUI_Gameplay");
	AddPage(new CModOptionsSubCrosshair(this), "#GameUI_Crosshair");
	AddPage(new CModOptionsSubLoadout(this), "#GameUI_Loadout");
	AddPage(new CModOptionsSubKnives(this), "#GameUI_Knives");
	AddPage(new CModOptionsSubAgents(this), "#GameUI_Agents");
	AddPage( new CModOptionsSubGloves( this ), "#GameUI_Gloves" );

	SetApplyButtonVisible(true);
	GetPropertySheet()->SetTabWidth(84);
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CModOptionsDialog::~CModOptionsDialog()
{
}

//-----------------------------------------------------------------------------
// Purpose: Brings the dialog to the fore
//-----------------------------------------------------------------------------
void CModOptionsDialog::Activate()
{
	BaseClass::Activate();
	EnableApplyButton(false);
}

void CModOptionsDialog::OnKeyCodePressed( KeyCode code )
{
	switch ( GetBaseButtonCode( code ) )
	{
	case KEY_XBUTTON_B:
		OnCommand( "Cancel" );
		return;
	}

	BaseClass::OnKeyCodePressed( code );
}

//-----------------------------------------------------------------------------
// Purpose: Opens the dialog
//-----------------------------------------------------------------------------
void CModOptionsDialog::Run()
{
	SetTitle("#GameUI_Mod_Options", true);
	Activate();
}

//-----------------------------------------------------------------------------
// Purpose: Called when the GameUI is hidden
//-----------------------------------------------------------------------------
void CModOptionsDialog::OnGameUIHidden()
{
	// tell our children about it
	for ( int i = 0 ; i < GetChildCount() ; i++ )
	{
		Panel *pChild = GetChild( i );
		if ( pChild )
		{
			PostMessage( pChild, new KeyValues( "GameUIHidden" ) );
		}
	}
}
