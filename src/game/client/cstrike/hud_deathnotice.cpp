//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Draws CSPort's death notices
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "c_playerresource.h"
#include "iclientmode.h"
#include <vgui_controls/Controls.h>
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <KeyValues.h>
#include "c_baseplayer.h"
#include "c_team.h"

#include "cs_shareddefs.h"
#include "clientmode_csnormal.h"
#include "c_cs_player.h"
#include "c_cs_playerresource.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar hud_deathnotice_time( "hud_deathnotice_time", "6", 0 );

// Player entries in a death notice
struct DeathNoticePlayer
{
	char		szName[MAX_PLAYER_NAME_LENGTH];
	char		szClan[MAX_CLAN_TAG_LENGTH];
	int			iEntIndex;
	Color		color;
};

// Contents of each entry in our list of death notices
struct DeathNoticeItem 
{
	DeathNoticePlayer	Killer;
	DeathNoticePlayer   Victim;
	DeathNoticePlayer   Assister;
	CHudTexture *iconDeath;
	bool		bSuicide;
	float		flDisplayTime;
	bool		bHeadshot;
	bool		bNoScope;
	bool		bBlind;
	bool		bPenetrated;
	bool		bThruSmoke;
	bool		bDomination;
	bool		bRevenge;
	bool		bAssisted;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CHudDeathNotice : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudDeathNotice, vgui::Panel );
public:
	CHudDeathNotice( const char *pElementName );

	void Init( void );
	void VidInit( void );
	virtual bool ShouldDraw( void );
	virtual void Paint( void );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme );

	void RetireExpiredDeathNotices( void );
	
	void FireGameEvent( IGameEvent *event );

protected:
	int SetupHudImageId( const char* fname );

private:

	CPanelAnimationVarAliasType( float, m_flLineHeight, "LineHeight", "15", "proportional_float" );

	CPanelAnimationVar( float, m_flMaxDeathNotices, "MaxDeathNotices", "4" );

	CPanelAnimationVar( bool, m_bRightJustify, "RightJustify", "1" );

	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "HudNumbersTimer" );

	CPanelAnimationVar( Color, m_clrCTText, "CTTextColor", "CTTextColor" );
	CPanelAnimationVar( Color, m_clrTerroristText, "TerroristTextColor", "TerroristTextColor" );

	// Texture for skull symbol
	CHudTexture		*m_iconD_skull; 
	CHudTexture		*m_iconD_headshot;
	CHudTexture		*m_iconD_dominated;
	CHudTexture		*m_iconD_revenge;
	CHudTexture		*m_iconD_noscope; 
	CHudTexture		*m_iconD_blind;
	CHudTexture		*m_iconD_penetrated;
	CHudTexture		*m_iconD_thrusmoke;

	Color			m_teamColors[TEAM_MAXCOUNT];

	CUtlVector<DeathNoticeItem> m_DeathNotices;
};

using namespace vgui;

DECLARE_HUDELEMENT( CHudDeathNotice );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudDeathNotice::CHudDeathNotice( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "HudDeathNotice" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_iconD_headshot = NULL;
	m_iconD_skull = NULL;
	m_iconD_dominated = NULL;
	m_iconD_revenge = NULL;
	m_iconD_noscope = NULL;
	m_iconD_blind = NULL;
	m_iconD_penetrated = NULL;
	m_iconD_thrusmoke = NULL;

	SetHiddenBits( HIDEHUD_MISCSTATUS );
}


/**
 * Helper function to get an image id and set 
 */
int CHudDeathNotice::SetupHudImageId( const char* fname )
{
	int imageId = surface()->CreateNewTextureID();
	surface()->DrawSetTextureFile( imageId, fname, true, false );
	return imageId;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false );

	// make team color lookups easier
	memset(m_teamColors, 0, sizeof(m_teamColors));
	m_teamColors[TEAM_CT] = m_clrCTText;
	m_teamColors[TEAM_TERRORIST] = m_clrTerroristText;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::Init( void )
{
	ListenForGameEvent( "player_death" );	
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::VidInit( void )
{
	m_iconD_skull = gHUD.GetIcon( "d_skull_cs" );
	m_iconD_headshot = gHUD.GetIcon( "d_headshot" );
	m_iconD_dominated = gHUD.GetIcon( "d_dominated" );
	m_iconD_revenge = gHUD.GetIcon( "d_revenge" );
	m_iconD_noscope = gHUD.GetIcon( "d_noscope" );
	m_iconD_blind = gHUD.GetIcon( "d_blind" );
	m_iconD_penetrated = gHUD.GetIcon( "d_penetrated" );
	m_iconD_thrusmoke = gHUD.GetIcon( "d_thrusmoke" );
	m_DeathNotices.Purge();
}

//-----------------------------------------------------------------------------
// Purpose: Draw if we've got at least one death notice in the queue
//-----------------------------------------------------------------------------
bool CHudDeathNotice::ShouldDraw( void )
{
	C_CSPlayer *pPlayer = C_CSPlayer::GetLocalCSPlayer();

	if ( !pPlayer )
		return false;

	// don't show death notices when flashed
	if ( pPlayer->IsAlive() && pPlayer->m_flFlashBangTime >= gpGlobals->curtime )
	{
		float flAlpha = pPlayer->m_flFlashMaxAlpha * (pPlayer->m_flFlashBangTime - gpGlobals->curtime) / pPlayer->m_flFlashDuration;
		if ( flAlpha > 128.0f ) // 0..255
		{
			return false;
		}
	}

	return ( CHudElement::ShouldDraw() && ( m_DeathNotices.Count() ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::Paint()
{
	if ( !m_iconD_headshot || !m_iconD_skull || !m_iconD_dominated || !m_iconD_revenge || !m_iconD_noscope || !m_iconD_blind || !m_iconD_penetrated || !m_iconD_thrusmoke )
		return;

	int yStart = GetClientModeCSNormal()->GetDeathMessageStartHeight();

	surface()->DrawSetTextFont( m_hTextFont );
	surface()->DrawSetTextColor( m_clrCTText );

	int iconDominationWide = surface()->GetCharacterWidth( m_iconD_dominated->hFont, m_iconD_dominated->cCharacterInFont );
	int iconDominationTall = surface()->GetFontTall( m_iconD_dominated->hFont );
	int iconRevengeWide = surface()->GetCharacterWidth( m_iconD_revenge->hFont, m_iconD_revenge->cCharacterInFont );
	int iconRevengeTall = surface()->GetFontTall( m_iconD_revenge->hFont );

	int iconHeadshotWide;
	int iconHeadshotTall;

	if( m_iconD_headshot->bRenderUsingFont )
	{
		iconHeadshotWide = surface()->GetCharacterWidth( m_iconD_headshot->hFont, m_iconD_headshot->cCharacterInFont );
		iconHeadshotTall = surface()->GetFontTall( m_iconD_headshot->hFont );
	}
	else
	{
		float scale = ( (float)ScreenHeight() / 480.0f );	//scale based on 640x480
		iconHeadshotWide = (int)( scale * (float)m_iconD_headshot->Width() );
		iconHeadshotTall = (int)( scale * (float)m_iconD_headshot->Height() );
	}

	int iconNoScopeWide = surface()->GetCharacterWidth( m_iconD_noscope->hFont, m_iconD_noscope->cCharacterInFont );
	int iconNoScopeTall = surface()->GetFontTall( m_iconD_noscope->hFont );

	int iconBlindWide = surface()->GetCharacterWidth( m_iconD_blind->hFont, m_iconD_blind->cCharacterInFont );
	int iconBlindTall = surface()->GetFontTall( m_iconD_blind->hFont );

	int iconPenetrateWide = surface()->GetCharacterWidth( m_iconD_penetrated->hFont, m_iconD_penetrated->cCharacterInFont );
	int iconPenetrateTall = surface()->GetFontTall( m_iconD_penetrated->hFont );

	int iconThruSmokeWide = surface()->GetCharacterWidth( m_iconD_thrusmoke->hFont, m_iconD_thrusmoke->cCharacterInFont );
	int iconThruSmokeTall = surface()->GetFontTall( m_iconD_thrusmoke->hFont );

	int iCount = m_DeathNotices.Count();
	for ( int i = 0; i < iCount; i++ )
	{
		CHudTexture *icon = m_DeathNotices[i].iconDeath;
		if ( !icon )
			continue;

		wchar_t victim[ 256 ];
		wchar_t killer[ 256 ];
		wchar_t assister[ 256 ];
		wchar_t victimclan[ 256 ];
		wchar_t killerclan[ 256 ];
		wchar_t assisterclan[ 256 ];

		wchar_t assistplussign[4];
		g_pVGuiLocalize->ConvertANSIToUnicode( " +", assistplussign, sizeof( assistplussign ) );

		g_pVGuiLocalize->ConvertANSIToUnicode( m_DeathNotices[i].Victim.szName, victim, sizeof( victim ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( m_DeathNotices[i].Killer.szName, killer, sizeof( killer ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( m_DeathNotices[i].Assister.szName, assister, sizeof( assister ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( m_DeathNotices[i].Victim.szClan, victimclan, sizeof( victimclan ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( m_DeathNotices[i].Killer.szClan, killerclan, sizeof( killerclan ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( m_DeathNotices[i].Assister.szClan, assisterclan, sizeof( assisterclan ) );

		// Get the local position for this notice
		int victimNameLen = UTIL_ComputeStringWidth( m_hTextFont, victim );
		int victimClanLen = UTIL_ComputeStringWidth( m_hTextFont, victimclan );
		int y = yStart + (m_flLineHeight * i);

		int iconWide;
		int iconTall;

		if( icon->bRenderUsingFont )
		{
			iconWide = surface()->GetCharacterWidth( icon->hFont, icon->cCharacterInFont );
			iconTall = surface()->GetFontTall( icon->hFont );
		}
		else
		{
			float scale = ( (float)ScreenHeight() / 480.0f );	//scale based on 640x480
			iconWide = (int)( scale * (float)icon->Width() );
			iconTall = (int)( scale * (float)icon->Height() );
		}

		int x = 0;
		if ( m_bRightJustify )
		{
			x =	GetWide();
			x -= victimNameLen;
			x -= victimClanLen;
			x -= iconWide;

			if ( m_DeathNotices[i].bBlind )
				x -= iconBlindWide;

			if ( m_DeathNotices[i].bNoScope )
				x -= iconNoScopeWide;

			if ( m_DeathNotices[i].bPenetrated )
				x -= iconPenetrateWide;

			if ( m_DeathNotices[i].bThruSmoke )
				x -= iconThruSmokeWide;

			if ( m_DeathNotices[i].bHeadshot )
				x -= iconHeadshotWide;

			if ( m_DeathNotices[i].bAssisted )
			{
				x -= UTIL_ComputeStringWidth( m_hTextFont, assistplussign );
				x -= UTIL_ComputeStringWidth( m_hTextFont, assister );
				x -= UTIL_ComputeStringWidth( m_hTextFont, assisterclan );
			}
			
			//if ( !m_DeathNotices[i].bSuicide )
			{
				x -= UTIL_ComputeStringWidth( m_hTextFont, killer );
				x -= UTIL_ComputeStringWidth( m_hTextFont, killerclan );
			}
			
			if (m_DeathNotices[i].bDomination)
			{				
				x -= iconDominationWide;
			}
			if (m_DeathNotices[i].bRevenge)
			{				
				x -= iconRevengeWide;
			}
		}

		Color iconColor( 255, 80, 0, 255 );

		if (m_DeathNotices[i].bDomination)
		{
			m_iconD_dominated->DrawSelf( x, y, iconDominationWide, iconDominationTall, iconColor );
			x += iconDominationWide;
		}
		if (m_DeathNotices[i].bRevenge)
		{
			m_iconD_revenge->DrawSelf( x, y, iconRevengeWide, iconRevengeTall, iconColor );
			x += iconRevengeWide;
		}
		
		// Only draw killers name if it wasn't a suicide
		//if ( !m_DeathNotices[i].bSuicide )
		{
			if ( m_DeathNotices[i].bBlind )
			{
				m_iconD_blind->DrawSelf( x, y, iconBlindWide, iconBlindTall, iconColor );
				x += iconBlindWide;
			}

			// Draw killer's clan
			surface()->DrawSetTextColor( m_DeathNotices[i].Killer.color );
			surface()->DrawSetTextPos( x, y );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( killerclan );
			surface()->DrawGetTextPos( x, y );

			// Draw killer's name
			surface()->DrawSetTextColor( m_DeathNotices[i].Killer.color );
			surface()->DrawSetTextPos( x, y );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( killer );
			surface()->DrawGetTextPos( x, y );
		}
		
		if ( m_DeathNotices[i].bAssisted )
		{
			// Draw the plus sign in between killer and assister name
			surface()->DrawSetTextColor( Color( 255, 128, 0, 255 ) );
			//surface()->DrawSetTextColor( iconColor );
			surface()->DrawSetTextPos( x, y );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( assistplussign );
			surface()->DrawGetTextPos( x, y );

			// Draw assister's clan
			surface()->DrawSetTextColor( m_DeathNotices[i].Assister.color );
			surface()->DrawSetTextPos( x, y );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( assisterclan );
			surface()->DrawGetTextPos( x, y );

			// Draw assister's name
			surface()->DrawSetTextColor( m_DeathNotices[i].Assister.color );
			surface()->DrawSetTextPos( x, y );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( assister );
			surface()->DrawGetTextPos( x, y );
		}

		// Draw death weapon
		//If we're using a font char, this will ignore iconTall and iconWide
		icon->DrawSelf( x, y, iconWide, iconTall, iconColor );
		x += iconWide;

		if( m_DeathNotices[i].bNoScope )
		{
			m_iconD_noscope->DrawSelf( x, y, iconNoScopeWide, iconNoScopeTall, iconColor );
			x += iconNoScopeWide;
		}

		if( m_DeathNotices[i].bThruSmoke )
		{
			m_iconD_thrusmoke->DrawSelf( x, y, iconThruSmokeWide, iconThruSmokeTall, iconColor );
			x += iconThruSmokeWide;
		}

		if( m_DeathNotices[i].bPenetrated )
		{
			m_iconD_penetrated->DrawSelf( x, y, iconPenetrateWide, iconPenetrateTall, iconColor );
			x += iconPenetrateWide;
		}

		if( m_DeathNotices[i].bHeadshot )
		{
			m_iconD_headshot->DrawSelf( x, y, iconHeadshotWide, iconHeadshotTall, iconColor );
			x += iconHeadshotWide;
		}

		// Draw victims clan
		surface()->DrawSetTextColor( m_DeathNotices[i].Victim.color );
		surface()->DrawSetTextPos( x, y );
		surface()->DrawSetTextFont( m_hTextFont );	//reset the font, draw icon can change it
		surface()->DrawUnicodeString( victimclan );
		surface()->DrawGetTextPos( x, y );

		// Draw victims name
		surface()->DrawSetTextColor( m_DeathNotices[i].Victim.color );
		surface()->DrawSetTextPos( x, y );
		surface()->DrawSetTextFont( m_hTextFont );	//reset the font, draw icon can change it
		surface()->DrawUnicodeString( victim );
	}

	// Now retire any death notices that have expired
	RetireExpiredDeathNotices();
}

//-----------------------------------------------------------------------------
// Purpose: This message handler may be better off elsewhere
//-----------------------------------------------------------------------------
void CHudDeathNotice::RetireExpiredDeathNotices( void )
{
	// Loop backwards because we might remove one
	int iSize = m_DeathNotices.Size();
	for ( int i = iSize-1; i >= 0; i-- )
	{
		if ( m_DeathNotices[i].flDisplayTime < gpGlobals->curtime )
		{
			m_DeathNotices.Remove(i);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Server's told us that someone's died
//-----------------------------------------------------------------------------
void CHudDeathNotice::FireGameEvent( IGameEvent *event )
{
	if (!g_PR)
		return;

	C_CS_PlayerResource *cs_PR = dynamic_cast<C_CS_PlayerResource *>( g_PR );
	if ( !cs_PR )
		return;

	if ( hud_deathnotice_time.GetFloat() == 0 )
		return;

	// the event should be "player_death"
	
	int iKiller = engine->GetPlayerForUserID( event->GetInt("attacker") );
	int iAssister = engine->GetPlayerForUserID( event->GetInt("assister") );
	int iVictim = engine->GetPlayerForUserID( event->GetInt("userid") );
	const char *killedwith = event->GetString( "weapon" );
	bool headshot = event->GetInt( "headshot" ) > 0;
	bool noscope = event->GetInt( "noscope" ) > 0;
	bool blind = event->GetInt( "blind" ) > 0;
	bool penetrated = event->GetInt( "penetrated" ) > 0;

	C_CSPlayer* pKiller = ToCSPlayer( ClientEntityList().GetBaseEntity( iKiller ) );
	C_CSPlayer* pVictim = ToCSPlayer( ClientEntityList().GetBaseEntity( iVictim ) );
	C_CSPlayer* pAssister = ToCSPlayer( ClientEntityList().GetBaseEntity( iAssister ) );

#if CS_CONTROLLABLE_BOTS_ENABLED
	if ( pKiller && pKiller->IsControllingBot() )
		iKiller = pKiller->GetControlledBotIndex();

	if ( pVictim && pVictim->IsControllingBot() )
		iVictim = pVictim->GetControlledBotIndex();

	if ( pAssister && pAssister->IsControllingBot() )
		iAssister = pAssister->GetControlledBotIndex();
#endif

	if ( !iKiller )
	{
		// PiMoN: assuming that we're suicided, so the killer equals a victim
		// probably won't cause any problems like that but if it will, then
		// its and easy-fix by just adding a check for suicide ( !iKiller || iKiller == iVictim );
		iKiller = iVictim;
	}

	bool thrusmoke = false;
	if ( pKiller && pVictim )
		thrusmoke = LineGoesThroughSmoke( pKiller->GetAbsOrigin(), pVictim->GetAbsOrigin(), 1.0f );

	// no thrusmoke icon for grenades
	if ( thrusmoke )
	{
		char pWeaponName[64];
		V_sprintf_safe( pWeaponName, "weapon_%s", killedwith );
		WEAPON_FILE_INFO_HANDLE	hWpnInfo = LookupWeaponInfoSlot( pWeaponName );
		if ( hWpnInfo != GetInvalidWeaponInfoHandle() )
		{
			CCSWeaponInfo *pWeaponInfo = dynamic_cast<CCSWeaponInfo*>(GetFileWeaponInfoFromHandle( hWpnInfo ));
			if ( pWeaponInfo )
				thrusmoke = (pWeaponInfo->m_WeaponType != WEAPONTYPE_GRENADE);
		}
		// no thrusmoke icon for inferno as well
		else if ( !V_strcmp( killedwith, "inferno" ) )
			thrusmoke = false;
	}

	char fullkilledwith[128];
	if ( killedwith && *killedwith )
	{
		Q_snprintf( fullkilledwith, sizeof(fullkilledwith), "d_%s", killedwith );
	}
	else
	{
		fullkilledwith[0] = 0;
	}

	// Do we have too many death messages in the queue?
	if ( m_DeathNotices.Count() > 0 &&
		m_DeathNotices.Count() >= (int)m_flMaxDeathNotices )
	{
		// Remove the oldest one in the queue, which will always be the first
		m_DeathNotices.Remove(0);
	}
	// if our attacker is the same as our assiter, it means a bot attacked the victim and a player took over that bot
	if ( iAssister == iKiller )
		iAssister = 0;

	// if our attacker is the same as our assiter, it means a bot attacked the victim and a player took over that bot
	if ( iAssister == iKiller )
		iAssister = 0;

	// Get the names of the players
	const char *killer_name = iKiller > 0 ? g_PR->GetPlayerName( iKiller ) : NULL;
	const char *victim_name = iVictim > 0 ? g_PR->GetPlayerName( iVictim ) : NULL;
	const char *assister_name = iAssister > 0 ? g_PR->GetPlayerName( iAssister ) : NULL;

	if ( !killer_name )
		killer_name = "";
	if ( !victim_name )
		victim_name = "";
	if ( !assister_name )
		assister_name = "";

	// Get the clan tags of the players
	const char *killer_clan = iKiller > 0 ? cs_PR->GetClanTag( iKiller ) : NULL;
	const char *victim_clan = iVictim > 0 ? cs_PR->GetClanTag( iVictim ) : NULL;
	const char *assister_clan = iAssister > 0 ? cs_PR->GetClanTag( iAssister ) : NULL;

	if ( !killer_clan )
		killer_clan = "";
	if ( !victim_clan )
		victim_clan = "";
	if ( !assister_clan )
		assister_clan = "";

	// Make a new death notice
	DeathNoticeItem deathMsg;
	deathMsg.Killer.iEntIndex = iKiller;
	deathMsg.Victim.iEntIndex = iVictim;
	deathMsg.Assister.iEntIndex = iAssister;
	deathMsg.Killer.color = iKiller > 0 ? m_teamColors[g_PR->GetTeam(iKiller)] : COLOR_WHITE;
	deathMsg.Victim.color = iVictim > 0 ? m_teamColors[g_PR->GetTeam(iVictim)] : COLOR_WHITE;
	deathMsg.Assister.color = iAssister > 0 ? m_teamColors[g_PR->GetTeam(iAssister)] : COLOR_WHITE;
	Q_snprintf( deathMsg.Killer.szClan, sizeof( deathMsg.Killer.szClan ), "%s ", killer_clan );
	Q_snprintf( deathMsg.Victim.szClan, sizeof( deathMsg.Victim.szClan ), "%s ", victim_clan );
	Q_snprintf( deathMsg.Assister.szClan, sizeof( deathMsg.Assister.szClan ), "%s ", assister_clan );
	Q_strncpy( deathMsg.Killer.szName, killer_name, MAX_PLAYER_NAME_LENGTH );
	Q_strncpy( deathMsg.Victim.szName, victim_name, MAX_PLAYER_NAME_LENGTH );
	Q_strncpy( deathMsg.Assister.szName, assister_name, MAX_PLAYER_NAME_LENGTH );
	deathMsg.flDisplayTime = gpGlobals->curtime + hud_deathnotice_time.GetFloat();
	deathMsg.bSuicide = ( !iKiller || iKiller == iVictim );
	deathMsg.bHeadshot = headshot;
	deathMsg.bNoScope = noscope;
	deathMsg.bBlind = blind;
	deathMsg.bPenetrated = penetrated;
	deathMsg.bThruSmoke = thrusmoke;
	deathMsg.bDomination = event->GetInt( "dominated" ) > 0 || (pKiller != NULL && pKiller->IsPlayerDominated( iVictim ));
	deathMsg.bRevenge = event->GetInt( "revenge" ) > 0;
	deathMsg.bAssisted = iAssister > 0;

	// Try and find the death identifier in the icon list
	deathMsg.iconDeath = gHUD.GetIcon( fullkilledwith );

	if ( !deathMsg.iconDeath )
	{
		// Can't find it, so use the default skull & crossbones icon
		deathMsg.iconDeath = m_iconD_skull;
	}

	// Add it to our list of death notices
	m_DeathNotices.AddToTail( deathMsg );

	char sDeathMsg[512];

	// Record the death notice in the console
	if ( deathMsg.bSuicide )
	{
		if ( !strcmp( fullkilledwith, "d_planted_c4" ) )
		{
			Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s was a bit too close to the c4.\n", deathMsg.Victim.szName );
		}
		else if ( !strcmp( fullkilledwith, "d_worldspawn" ) )
		{
			Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s died.\n", deathMsg.Victim.szName );
		}
		else	//d_world
		{
			Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s suicided.\n", deathMsg.Victim.szName );
		}
	}
	else
	{
		Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s killed %s", deathMsg.Killer.szName, deathMsg.Victim.szName );

		if ( fullkilledwith && *fullkilledwith && (*fullkilledwith > 13 ) )
		{
			Q_strncat( sDeathMsg, VarArgs( " with %s.\n", fullkilledwith+2 ), sizeof( sDeathMsg ), COPY_ALL_CHARACTERS );
		}
	}

	Msg( "%s", sDeathMsg );
}



