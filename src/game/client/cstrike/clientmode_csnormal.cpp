//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//
#include "cbase.h"
#include "hud.h"
#include "clientmode_csnormal.h"
#include "cdll_client_int.h"
#include "iinput.h"
#include "vgui/ISurface.h"
#include "vgui/IPanel.h"
#include <vgui_controls/AnimationController.h>
#include "ivmodemanager.h"
#include "buymenu.h"
#include "filesystem.h"
#include "vgui/IVGui.h"
#include "hud_basechat.h"
#include "view_shared.h"
#include "view.h"
#include "ivrenderview.h"
#include "cstrikeclassmenu.h"
#include "cstrikebuymenu.h"
#include "c_te_legacytempents.h"
#include "tempent.h"
#include "model_types.h"
#include "iefx.h"
#include "dlight.h"
#include <imapoverview.h>
#include "c_playerresource.h"
#include "c_soundscape.h"
#include <KeyValues.h>
#include "text_message.h"
#include "panelmetaclassmgr.h"
#include "vguicenterprint.h"
#include "physpropclientside.h"
#include "c_weapon__stubs.h"
#include <engine/IEngineSound.h>
#include "c_cs_hostage.h"
#include "buy_presets/buy_presets.h"
#include "bitbuf.h"
#include "usermessages.h"
#include "prediction.h"
#include "datacache/imdlcache.h"
#include "cs_shareddefs.h"
#include "cs_loadout.h"
//=============================================================================
// HPE_BEGIN:
// [tj] Needed to retrieve achievement text
// [menglish] Need access to message macros
//=============================================================================
 
#include "achievementmgr.h"
#include "hud_macros.h"
#include "c_plantedc4.h"
#include "tier1/fmtstr.h"
#include "history_resource.h"
#include "cs_client_gamestats.h"

// [tj] We need to forward declare this, since the definition is all inside the implementation file 
class CHudHintDisplay;
 
//=============================================================================
// HPE_END
//=============================================================================


void __MsgFunc_MatchEndConditions( bf_read &msg );

class CHudChat;

ConVar default_fov( "default_fov", "90", FCVAR_CHEAT );

IClientMode *g_pClientMode = NULL;

// This is a temporary entity used to render the player's model while drawing the class selection menu.
CHandle<C_BaseAnimating> g_ClassImagePlayer;	// player
CHandle<C_BaseAnimating> g_ClassImageGloves;	// gloves
CHandle<C_BaseAnimating> g_ClassImageWeapon;	// weapon

// This is a temporary entity used to render the player's model while drawing the buy menu.
CHandle<C_BaseAnimating> g_BuyMenuImagePlayer;	// player
CHandle<C_BaseAnimating> g_BuyMenuImageGloves;	// gloves
CHandle<C_BaseAnimating> g_BuyMenuImageWeapon;	// weapon

STUB_WEAPON_CLASS( cycler_weapon,	WeaponCycler,	C_BaseCombatWeapon );
STUB_WEAPON_CLASS( weapon_cubemap,	WeaponCubemap,	C_BaseCombatWeapon );

//-----------------------------------------------------------------------------
// HACK: the detail sway convars are archive, and default to 0.  Existing CS:S players thus have no detail
// prop sway.  We'll force them to DoD's default values for now.  What we really need in the long run is
// a system to apply changes to archived convars' defaults to existing players.
extern ConVar cl_detail_max_sway;
extern ConVar cl_detail_avoid_radius;
extern ConVar cl_detail_avoid_force;
extern ConVar cl_detail_avoid_recover_speed;
extern ConVar v_viewmodel_fov;

//-----------------------------------------------------------------------------
ConVar cl_autobuy(
	"cl_autobuy",
	"",
	FCVAR_USERINFO,
	"The order in which autobuy will attempt to purchase items" );

//-----------------------------------------------------------------------------
ConVar cl_rebuy(
	"cl_rebuy",
	"",
	FCVAR_USERINFO,
	"The order in which rebuy will attempt to repurchase items" );

//-----------------------------------------------------------------------------
void SetBuyData( const ConVar &buyVar, const char *filename )
{
	// if we already have autobuy data, don't bother re-parsing the text file
	if ( *buyVar.GetString() )
		return;

	// First, look for a mapcycle file in the cfg directory, which is preferred
	char szRecommendedName[ 256 ];
	char szResolvedName[ 256 ];
	V_sprintf_safe( szRecommendedName, "cfg/%s", filename );
	V_strcpy_safe( szResolvedName, szRecommendedName );
	if ( filesystem->FileExists( szResolvedName, "GAME" ) )
	{
		Msg( "Loading '%s'.\n", szResolvedName );
	}
	else
	{
		// Check the root
		V_strcpy_safe( szResolvedName, filename );
		if ( filesystem->FileExists( szResolvedName, "GAME" ) )
		{
			Msg( "Loading '%s'  ('%s' was not found.)\n", szResolvedName, szRecommendedName );
		}
		else
		{

			// Try cfg/xxx_default.txt
			V_strcpy_safe( szResolvedName, szRecommendedName );
			char *dotTxt = V_stristr( szResolvedName, ".txt" );
			Assert( dotTxt );
			if ( dotTxt )
			{
				V_strcpy( dotTxt, "_default.txt" );
			}
			if ( !filesystem->FileExists( szResolvedName, "GAME" ) )
			{
				Warning( "Not loading buy data.  Neither '%s' nor %s were found.\n", szResolvedName, szRecommendedName );
				return;
			}
			Msg( "Loading '%s'\n", szResolvedName );
		}
	}

	CUtlBuffer buf;
	if ( !filesystem->ReadFile( szResolvedName, "GAME", buf ) )
	{
		// WAT
		Warning( "Failed to load '%s'.\n", szResolvedName );
		return;
	}
	buf.PutChar('\0');

	char token[256];
	char buystring[256];
	V_sprintf_safe( buystring, "setinfo %s \"", buyVar.GetName() );

	const char *pfile = engine->ParseFile( (const char *)buf.Base(), token, sizeof(token) );

	bool first = true;

	while (pfile != NULL)
	{
		if (first)
		{
			first = false;
		}
		else
		{
			Q_strncat(buystring, " ", sizeof(buystring), COPY_ALL_CHARACTERS);
		}

		Q_strncat(buystring, token, sizeof(buystring), COPY_ALL_CHARACTERS);

		pfile = engine->ParseFile( pfile, token, sizeof(token) );
	}

	Q_strncat(buystring, "\"", sizeof(buystring), COPY_ALL_CHARACTERS);

	engine->ClientCmd(buystring);
}

void MsgFunc_KillCam(bf_read &msg) 
{
	C_CSPlayer *pPlayer = ToCSPlayer( C_BasePlayer::GetLocalPlayer() );

	if ( !pPlayer )
		return;

	int newMode = msg.ReadByte();

	if ( newMode != g_nKillCamMode )
	{
#if !defined( NO_ENTITY_PREDICTION )
		if ( g_nKillCamMode == OBS_MODE_NONE )
		{
			// kill cam is switch on, turn off prediction
			g_bForceCLPredictOff = true;
		}
		else if ( newMode == OBS_MODE_NONE )
		{
			// kill cam is switched off, restore old prediction setting is we switch back to normal mode
			g_bForceCLPredictOff = false;
		}
#endif
		g_nKillCamMode = newMode;
	}

	g_nKillCamTarget1	= msg.ReadByte();
	g_nKillCamTarget2	= msg.ReadByte();
}

// --------------------------------------------------------------------------------- //
// CCSModeManager.
// --------------------------------------------------------------------------------- //

class CCSModeManager : public IVModeManager
{
public:
	virtual void	Init();
	virtual void	SwitchMode( bool commander, bool force ) {}
	virtual void	LevelInit( const char *newmap );
	virtual void	LevelShutdown( void );
	virtual void	ActivateMouse( bool isactive ) {}
};

static CCSModeManager g_ModeManager;
IVModeManager *modemanager = ( IVModeManager * )&g_ModeManager;

// --------------------------------------------------------------------------------- //
// CCSModeManager implementation.
// --------------------------------------------------------------------------------- //

#define SCREEN_FILE		"scripts/vgui_screens.txt"

void CCSModeManager::Init()
{
	g_pClientMode = GetClientModeNormal();
	
	PanelMetaClassMgr()->LoadMetaClassDefinitionFile( SCREEN_FILE );
}

void CCSModeManager::LevelInit( const char *newmap )
{
	g_pClientMode->LevelInit( newmap );

	SetBuyData( cl_autobuy, "autobuy.txt" );
	SetBuyData( cl_rebuy, "rebuy.txt" );

#if !defined( NO_ENTITY_PREDICTION )
	if ( g_nKillCamMode > OBS_MODE_NONE )
	{
		g_bForceCLPredictOff = false;
	}
#endif

	g_nKillCamMode		= OBS_MODE_NONE;
	g_nKillCamTarget1	= 0;
	g_nKillCamTarget2	= 0;

	// HACK: the detail sway convars are archive, and default to 0.  Existing CS:S players thus have no detail
	// prop sway.  We'll force them to DoD's default values for now.
	if ( !cl_detail_max_sway.GetFloat() &&
		!cl_detail_avoid_radius.GetFloat() &&
		!cl_detail_avoid_force.GetFloat() &&
		!cl_detail_avoid_recover_speed.GetFloat() )
	{
		cl_detail_max_sway.SetValue( "5" );
		cl_detail_avoid_radius.SetValue( "64" );
		cl_detail_avoid_force.SetValue( "0.4" );
		cl_detail_avoid_recover_speed.SetValue( "0.25" );
	}
}

void CCSModeManager::LevelShutdown( void )
{
	g_pClientMode->LevelShutdown();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ClientModeCSNormal::ClientModeCSNormal()
{
	HOOK_MESSAGE( MatchEndConditions );
}

void ClientModeCSNormal::Init()
{
	BaseClass::Init();

	ListenForGameEvent( "round_end" );
	ListenForGameEvent( "round_start" );
	ListenForGameEvent( "player_team" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "bomb_planted" );
	ListenForGameEvent( "bomb_exploded" );
	ListenForGameEvent( "bomb_defused" );
	ListenForGameEvent( "hostage_follows" );
	ListenForGameEvent( "hostage_killed" );
	ListenForGameEvent( "hostage_hurt" );
	ListenForGameEvent( "round_freeze_end" );
	ListenForGameEvent( "round_time_warning" );
	ListenForGameEvent( "round_mvp" );
	ListenForGameEvent( "bot_takeover" );

	usermessages->HookMessage( "KillCam", MsgFunc_KillCam );

	//=============================================================================
	// HPE_BEGIN:
	// [tj] Add the shared HUD elements to the render groups responsible for hiding 
	//		conflicting UI
	//=============================================================================
	CHudElement* hintBox = (CHudElement*)GET_HUDELEMENT( CHudHintDisplay );
	if (hintBox)
	{
		hintBox->RegisterForRenderGroup("hide_for_scoreboard");
		hintBox->RegisterForRenderGroup("hide_for_round_panel");
	}


	CHudElement* historyResource = (CHudElement*)GET_HUDELEMENT( CHudHistoryResource );
	if (historyResource)
	{
		historyResource->RegisterForRenderGroup("hide_for_scoreboard");		
	}
	//=============================================================================
	// HPE_END
	//=============================================================================

	m_fDelayedCTWinTime = -1.0f;
}

void ClientModeCSNormal::InitViewport()
{
	BaseClass::InitViewport();

	m_pViewport = new CounterStrikeViewport();
	m_pViewport->Start( gameuifuncs, gameeventmanager );
}


void ClientModeCSNormal::Update()
{
	BaseClass::Update();

	// Override the hud's visibility if this is a logo (like E3 demo) map.
	if ( CSGameRules() && CSGameRules()->IsLogoMap() )
		m_pViewport->SetVisible( false );

	if ( (m_fDelayedCTWinTime > 0.0f) && (gpGlobals->curtime >= m_fDelayedCTWinTime) )
	{
		CLocalPlayerFilter filter;
		C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "Event.CTWin");

		m_fDelayedCTWinTime = -1.0f;
	}
}


/*
void ClientModeCSNormal::UpdateSpectatorMode( void )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	if ( !pPlayer )
		return;

	IMapOverview * overviewmap = m_pViewport->GetMapOverviewInterface();

	if ( !overviewmap )
		return;

	overviewmap->SetTime( gpGlobals->curtime );

	int obs_mode = pPlayer->GetObserverMode();

	if ( obs_mode < OBS_MODE_IN_EYE )
		return;

	Vector worldpos = pPlayer->GetLocalOrigin();
	QAngle angles; engine->GetViewAngles( angles );

	C_BaseEntity *target = pPlayer->GetObserverTarget();

	if ( target && (obs_mode == OBS_MODE_IN_EYE || obs_mode == OBS_MODE_CHASE) )
	{
		worldpos = target->GetAbsOrigin();

		if ( obs_mode == OBS_MODE_IN_EYE )
		{
			angles = target->GetAbsAngles();
		}
	}

	Vector2D mappos = overviewmap->WorldToMap( worldpos );

	overviewmap->SetCenter( mappos );
	overviewmap->SetAngle( angles.y );	
	
	for ( int i = 1; i<= MAX_PLAYERS; i++)
	{
		C_BaseEntity *ent = ClientEntityList().GetEnt( i );

		if ( !ent || !ent->IsPlayer() )
			continue;

		C_BasePlayer *p = ToBasePlayer( ent );

		// update position of active players in our PVS
		Vector position = p->GetAbsOrigin();
		QAngle angle = p->GetAbsAngles();

		if ( p->IsDormant() )
		{
			// if player is not in PVS, use PlayerResources data
			position = g_PR->GetPosition( i );
			angles[1] = g_PR->GetViewAngle( i );
		}
		
		overviewmap->SetPlayerPositions( i-1, position, angles );
	}
} */

//-----------------------------------------------------------------------------
// Purpose: We've received a keypress from the engine. Return 1 if the engine is allowed to handle it.
//-----------------------------------------------------------------------------
int	ClientModeCSNormal::KeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
{
	// don't process input in LogoMaps
	if( CSGameRules() && CSGameRules()->IsLogoMap() )
		return 1;
	
	return BaseClass::KeyInput( down, keynum, pszCurrentBinding );
}



IClientMode *GetClientModeNormal()
{
	static ClientModeCSNormal g_ClientModeNormal;
	return &g_ClientModeNormal;
}


ClientModeCSNormal* GetClientModeCSNormal()
{
	Assert( dynamic_cast< ClientModeCSNormal* >( GetClientModeNormal() ) );

	return static_cast< ClientModeCSNormal* >( GetClientModeNormal() );
}

#if IRONSIGHT
#ifdef DEBUG
	ConVar ironsight_scoped_viewmodel_fov( "ironsight_scoped_viewmodel_fov", "54", FCVAR_CHEAT, "The fov of the viewmodel when ironsighted" );
#else
	#define IRONSIGHT_SCOPED_FOV 54.0f
#endif
#endif

float ClientModeCSNormal::GetViewModelFOV( void )
{
#if IRONSIGHT
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		CWeaponCSBase *pIronSightWeapon = (CWeaponCSBase*)pPlayer->GetActiveWeapon();
		if ( pIronSightWeapon )
		{
			CIronSightController* pIronSightController = pIronSightWeapon->GetIronSightController();
			if ( pIronSightController && pIronSightController->IsInIronSight() )
			{
				return FLerp( v_viewmodel_fov.GetFloat(),	
					#ifdef DEBUG
						ironsight_scoped_viewmodel_fov.GetFloat(),
					#else
						IRONSIGHT_SCOPED_FOV,
					#endif
				pIronSightController->GetIronSightAmount() );
			}
		}
	}
#endif
	return v_viewmodel_fov.GetFloat();
}

int ClientModeCSNormal::GetDeathMessageStartHeight( void )
{
	return m_pViewport->GetDeathMessageStartHeight();
}

void ClientModeCSNormal::FireGameEvent( IGameEvent *event )
{
	CBaseHudChat *pHudChat = (CBaseHudChat *)GET_HUDELEMENT( CHudChat );
	C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();
	CLocalPlayerFilter filter;
	
	if ( !pLocalPlayer || !pHudChat )
		return;

	const char *eventname = event->GetName();

	if ( !eventname || !eventname[0] )
		return;

	if ( Q_strcmp( "round_start", eventname ) == 0 )
	{
		// recreate all client side physics props
		C_PhysPropClientside::RecreateAll();

		// remove hostage ragdolls
		for ( int i=0; i<g_HostageRagdolls.Count(); ++i )
		{
			// double-check that the EHANDLE is still valid
			if ( g_HostageRagdolls[i] )
			{
				g_HostageRagdolls[i]->Release();
			}
		}
		g_HostageRagdolls.RemoveAll();

		// Just tell engine to clear decals
		engine->ClientCmd( "r_cleardecals\n" );

		//stop any looping sounds
		enginesound->StopAllSounds( true );

		Soundscape_OnStopAllSounds();	// Tell the soundscape system.
	}
	else if ( Q_strcmp( "round_end", eventname ) == 0 )
	{
		int winningTeam = event->GetInt("winner");
		int reason = event->GetInt("reason");

		if ( reason != Game_Commencing )
		{
			// if spectating play music for team being spectated at that moment
			C_BasePlayer* pTeamPlayer = pLocalPlayer;
			if ( pLocalPlayer->GetTeamNumber() == TEAM_SPECTATOR || pLocalPlayer->IsHLTV() )
			{
				pTeamPlayer = UTIL_PlayerByIndex( GetLocalPlayerIndex() );
			}
			if ( winningTeam == pTeamPlayer->GetTeamNumber() )
			{
				PlayMusicSelection( filter, CSMUSIC_WONROUND );
			}
			else
			{
				PlayMusicSelection( filter, CSMUSIC_LOSTROUND );
			}
		}

		// play endround announcer sound
		if ( winningTeam == TEAM_CT )
		{
			if ( reason == Bomb_Defused )
			{
				C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "Event.BombDefused" );
				m_fDelayedCTWinTime = gpGlobals->curtime + C_BaseEntity::GetSoundDuration( "Event.BombDefused", NULL ) + 0.3;
			}
			else
			{
				C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "Event.CTWin");
			}
		}
		else if ( winningTeam == TEAM_TERRORIST )
		{
			C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "Event.TERWin");
		}
		else
		{
			C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "Event.RoundDraw");
		}
		
		// [pfreese] Only show centerprint message for game commencing; the rest of 
		// these messages are handled by the end-of-round panel.
		// [Forrest] Show all centerprint messages if the end-of-round panel is disabled.
		static ConVarRef sv_nowinpanel( "sv_nowinpanel" );
		static ConVarRef cl_nowinpanel( "cl_nowinpanel" );
		if ( reason == Game_Commencing || sv_nowinpanel.GetBool() || cl_nowinpanel.GetBool() )
		{
			internalCenterPrint->Print( hudtextmessage->LookupString( event->GetString("message") ) );

			// we are starting a new round; clear the current match stats
			g_CSClientGameStats.ResetMatchStats();
		}
	}
	else if ( Q_strcmp( "player_team", eventname ) == 0 )
	{
		CBaseHudChat *pHudChat = (CBaseHudChat *)GET_HUDELEMENT( CHudChat );
		C_BasePlayer *pPlayer = USERID2PLAYER( event->GetInt("userid") );
		
		if ( !pPlayer )
			return;

		bool bDisconnected = event->GetBool("disconnect");

		if ( bDisconnected )
			return;

		int iTeam = event->GetInt("team");

		if ( pPlayer->IsLocalPlayer() )
		{
			// that's me
			pPlayer->TeamChange( iTeam );
		}

		if ( iTeam == TEAM_SPECTATOR )
			pHudChat->Printf( CHAT_FILTER_NONE, hudtextmessage->LookupString( "#Game_join_spectators" ), pPlayer->GetPlayerName() );
		else if ( iTeam == TEAM_TERRORIST )
			pHudChat->Printf( CHAT_FILTER_NONE, hudtextmessage->LookupString( "#Game_join_terrorist" ), pPlayer->GetPlayerName() );
		else if ( iTeam == TEAM_CT )
			pHudChat->Printf( CHAT_FILTER_NONE, hudtextmessage->LookupString( "#Game_join_ct" ), pPlayer->GetPlayerName() );
	}
	else if ( Q_strcmp( "bomb_planted", eventname ) == 0 )
	{
		//C_BasePlayer *pPlayer = USERID2PLAYER( event->GetInt("userid") );

		// show centerprint message
		internalCenterPrint->Print( "#Cstrike_TitlesTXT_Bomb_Planted" );

		PlayMusicSelection( filter, CSMUSIC_BOMB );

		// play sound
		 C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "Event.BombPlanted")  ;
	}
	else if ( Q_strcmp( "bomb_defused", eventname ) == 0 )
	{
		// C_BasePlayer *pPlayer = USERID2PLAYER( event->GetInt("userid") );
	}

	// [menglish] Tell the client side bomb that the bomb has exploding here creating the explosion particle effect	 
	else if ( Q_strcmp( "bomb_exploded", eventname ) == 0 )
	{
		if ( g_PlantedC4s.Count() > 0 )
		{
			// bomb is planted
			C_PlantedC4 *pC4 = g_PlantedC4s[0];
			pC4->Explode();
		}
	}
	else if ( Q_strcmp( "hostage_follows", eventname ) == 0 )
	{
		internalCenterPrint->Print( "#Cstrike_TitlesTXT_Hostage_Being_Taken" );

		bool roundWasAlreadyWon = ( CSGameRules()->m_iRoundWinStatus != WINNER_NONE );
		if ( !roundWasAlreadyWon )
		{
			PlayMusicSelection( filter, CSMUSIC_HOSTAGE );
		}
	}
	else if ( Q_strcmp( "hostage_killed", eventname ) == 0 )
	{
		// play sound for spectators and CTs
		if ( pLocalPlayer->IsObserver() || (pLocalPlayer->GetTeamNumber() == TEAM_CT) )
		{
			C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "Event.HostageKilled")  ;
		}

		// Show warning to killer
		if ( pLocalPlayer->GetUserID() == event->GetInt("userid") )
		{
			internalCenterPrint->Print( "#Cstrike_TitlesTXT_Killed_Hostage" );
		}
	}
	else if ( Q_strcmp( "hostage_hurt", eventname ) == 0 )
	{
		// Let the loacl player know he harmed a hostage
		if ( pLocalPlayer->GetUserID() == event->GetInt("userid") )
		{
			internalCenterPrint->Print( "#Cstrike_TitlesTXT_Injured_Hostage" );
		}
	}
	else if ( Q_strcmp( "player_death", eventname ) == 0 )
	{
		C_BasePlayer *pPlayer = USERID2PLAYER( event->GetInt("userid") );

		C_CSPlayer* csPlayer = ToCSPlayer(pPlayer);
		if (csPlayer)
		{
			csPlayer->ClearSoundEvents();
		}

		if ( pPlayer == C_BasePlayer::GetLocalPlayer() )
		{
			// we just died, hide any buy panels
			gViewPortInterface->ShowPanel( PANEL_BUY, false );
			gViewPortInterface->ShowPanel( PANEL_BUY_CT, false );
			gViewPortInterface->ShowPanel( PANEL_BUY_TER, false );
			gViewPortInterface->ShowPanel( PANEL_BUY_EQUIP_CT, false );
			gViewPortInterface->ShowPanel( PANEL_BUY_EQUIP_TER, false );
		}
	}
	else if ( Q_strcmp( "player_changename", eventname ) == 0 )
	{
		return; // server sends a colorized text string for this
	}

    // [tj] We handle this here instead of in the base class 
    //      The reason is that we don't use string tables to localize.
    //      Instead, we use the steam localization mechanism.

    // [dwenger] Remove dependency on stats system for name of achievement.
    else if ( Q_strcmp( "achievement_earned", eventname ) == 0 )
    {
        CBaseHudChat *hudChat = (CBaseHudChat *)GET_HUDELEMENT( CHudChat );
        int iPlayerIndex = event->GetInt( "player" );
        C_BasePlayer *pPlayer = UTIL_PlayerByIndex( iPlayerIndex );
        int iAchievement = event->GetInt( "achievement" );

        if ( !hudChat || !pPlayer )
            return;


        CAchievementMgr *pAchievementMgr = dynamic_cast<CAchievementMgr *>( engine->GetAchievementMgr() );
        if ( !pAchievementMgr )
            return;

        IAchievement *pAchievement = pAchievementMgr->GetAchievementByID( iAchievement );
        if ( pAchievement )
        {
            if ( !pPlayer->IsDormant() && pPlayer->ShouldAnnounceAchievement() )
            {
                pPlayer->SetNextAchievementAnnounceTime( gpGlobals->curtime + ACHIEVEMENT_ANNOUNCEMENT_MIN_TIME );

                //Do something for the player - Actually we should probably do this client-side when the achievement is first earned.
                if (pPlayer->IsLocalPlayer()) 
                {
                }
                pPlayer->OnAchievementAchieved( iAchievement );
            }

            if ( g_PR )
            {
                wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
                g_pVGuiLocalize->ConvertANSIToUnicode( g_PR->GetPlayerName( iPlayerIndex ), wszPlayerName, sizeof( wszPlayerName ) );

                wchar_t achievementName[1024];
                const wchar_t* constAchievementName = &achievementName[0];

                constAchievementName = ACHIEVEMENT_LOCALIZED_NAME( pAchievement );

                if (constAchievementName)
                {
                    wchar_t wszLocalizedString[128];
                    g_pVGuiLocalize->ConstructString( wszLocalizedString, sizeof( wszLocalizedString ), g_pVGuiLocalize->Find( "#Achievement_Earned" ), 2, wszPlayerName, constAchievementName/*wszAchievementString*/ );

                    char szLocalized[128];
                    g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalizedString, szLocalized, sizeof( szLocalized ) );

                    hudChat->ChatPrintf( iPlayerIndex, CHAT_FILTER_ACHIEVEMENT, "%s", szLocalized );

					/*
                    if (pPlayer->IsLocalPlayer()) 
                    {
                        char achievementDescription[1024];
                        const char* constAchievementDescription = &achievementDescription[0];

                        constAchievementDescription = pUserStats->GetAchievementDisplayAttribute( pAchievement->GetName(), "desc" );  
                        hudChat->ChatPrintf( iPlayerIndex, CHAT_FILTER_ACHIEVEMENT, "(%s)", constAchievementDescription );
                    }
					*/
                }
            }
        }
    }
	else if ( V_strcmp( "round_freeze_end", eventname ) == 0 )
	{
		int nObsMode = pLocalPlayer->GetObserverMode();
		if ( nObsMode == OBS_MODE_FIXED || nObsMode == OBS_MODE_ROAMING )
		{
			C_CSPlayer* pCSLocalPlayer = ToCSPlayer( pLocalPlayer );
			if ( pCSLocalPlayer->GetCurrentMusic() == CSMUSIC_START )
			{
				CLocalPlayerFilter filter;
				PlayMusicSelection( filter, CSMUSIC_ACTION );
				pCSLocalPlayer->SetCurrentMusic( CSMUSIC_ACTION );
			}
		}
	}
	else if ( V_strcmp( "round_time_warning", eventname ) == 0 )
	{
		if ( !CSGameRules()->m_bBombPlanted )
		{
			PlayMusicSelection( filter, CSMUSIC_ROUNDTEN );
		}
	}
	else if ( V_strcmp( "round_mvp", eventname ) == 0 )
	{
		PlayMusicSelection( filter, CSMUSIC_MVP );
	}
	else if ( V_strcmp( "bot_takeover", eventname ) == 0 )
	{
		C_BasePlayer* pBot = UTIL_PlayerByUserId( event->GetInt( "botid" ) );
		if ( pBot && pLocalPlayer && pLocalPlayer->GetUserID() == event->GetInt( "userid" ) )
		{
			wchar_t wszLocalized[100];
			wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
			g_pVGuiLocalize->ConvertANSIToUnicode( pBot->GetPlayerName(), wszPlayerName, sizeof( wszPlayerName ) );
			g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#Cstrike_TitlesTXT_Hint_Bot_Takeover" ), 1, wszPlayerName );

			internalCenterPrint->Print( wszLocalized );
		}
	}

	else
	{
		BaseClass::FireGameEvent( event );
	}
}


bool ShouldRecreateImageEntity( C_BaseAnimating *pEnt, const char *pNewModelName )
{
	if ( !pNewModelName || !pNewModelName[0] )
		return false;

	if ( !pEnt )
		return true;

	const model_t *pModel = pEnt->GetModel();

	if ( !pModel )
		return true;

	const char *pName = modelinfo->GetModelName( pModel );
	if ( !pName )
		return true;

	// reload only if names are different
	return( Q_stricmp( pName, pNewModelName ) != 0 );
}


void UpdateClassImageEntity( 
	const char *pModelName,
	int x, int y, int width, int height )
{
	C_CSPlayer *pLocalPlayer = C_CSPlayer::GetLocalCSPlayer();
	
	if ( !pLocalPlayer )
		return;

	MDLCACHE_CRITICAL_SECTION();

	const char* pWeaponName = "models/weapons/w_rif_ak47.mdl";
	const char* pWeaponSequence = "t_loadout_rifle02_idle";
	int iTeamNumber = TEAM_UNASSIGNED;

	if ( Q_strncmp( V_UnqualifiedFileName(pModelName), "ctm_", 4 ) == 0 )
	{
		// give CTs a m4
		pWeaponName = "models/weapons/w_rif_m4a4.mdl";
		pWeaponSequence = "ct_loadout_rifle_idle_handrepo_m4";
		iTeamNumber = TEAM_CT;
	}
	else if ( Q_strncmp( V_UnqualifiedFileName( pModelName ), "tm_", 3 ) == 0 )
		iTeamNumber = TEAM_TERRORIST;

	bool m_bSilenced = false;
	/*if ( pLocalPlayer->IsAlive() && pLocalPlayer->GetActiveWeapon() )
	{
		C_WeaponCSBase *pWeapon = dynamic_cast< C_WeaponCSBase * >( pLocalPlayer->Weapon_GetSlot( WEAPON_SLOT_RIFLE ) );

		// set player's primary weapon for ui model
		if ( pWeapon )
		{
			m_bSilenced = pWeapon->IsSilenced() ? true : false;
			pWeaponName = pWeapon->GetCSWpnData().szWorldModel;
			pWeaponSequence = VarArgs( "UI_Idle_%s", pWeapon->GetCSWpnData().m_szUIAnimExtension );
		}
		else
		{
			// no primary weapon? ok, use the secondary weapon
			pWeapon = dynamic_cast< C_WeaponCSBase * >( pLocalPlayer->Weapon_GetSlot( WEAPON_SLOT_PISTOL ) );
			if ( pWeapon )
			{
				m_bSilenced = pWeapon->IsSilenced() ? true : false;
				pWeaponName = pWeapon->GetCSWpnData().szWorldModel;
				pWeaponSequence = VarArgs( "UI_Idle_%s", pWeapon->GetCSWpnData().m_szUIAnimExtension );
			}
			else
			{
				// no pistol as well? ok, lets try active weapon then...
				pWeapon = pLocalPlayer->GetActiveCSWeapon();
				if ( pWeapon )
				{
					m_bSilenced = pWeapon->IsSilenced() ? true : false;
					pWeaponName = pWeapon->GetCSWpnData().szWorldModel;
					pWeaponSequence = VarArgs( "UI_Idle_%s", pWeapon->GetCSWpnData().m_szUIAnimExtension );
				}
			}
		}
	}*/

	C_BaseAnimating *pPlayerModel = g_ClassImagePlayer.Get();

	bool bCreateGloves = false;

	// Does the entity even exist yet?
	bool recreatePlayer = ShouldRecreateImageEntity( pPlayerModel, pModelName );
	if ( recreatePlayer )
	{
		if ( pPlayerModel )
			pPlayerModel->Remove();

		pPlayerModel = new C_BaseAnimating;
		pPlayerModel->InitializeAsClientEntity( pModelName, RENDER_GROUP_OPAQUE_ENTITY );
		pPlayerModel->AddEffects( EF_NODRAW ); // don't let the renderer draw the model normally
		pPlayerModel->m_flAnimTime = gpGlobals->curtime;

		g_ClassImagePlayer = pPlayerModel;
	}

	if ( pPlayerModel && pPlayerModel->DoesModelSupportGloves() )
	{
		if ( CSLoadout()->HasGlovesSet( pLocalPlayer, iTeamNumber ) )
			bCreateGloves = true;
	}

	C_BaseAnimating *pWeaponModel = g_ClassImageWeapon.Get();

	// Does the entity even exist yet?
	if ( recreatePlayer || ShouldRecreateImageEntity( pWeaponModel, pWeaponName ) )
	{
		if ( pWeaponModel )
			pWeaponModel->Remove();

		pWeaponModel = new C_BaseAnimating;
		pWeaponModel->InitializeAsClientEntity( pWeaponName, RENDER_GROUP_OPAQUE_ENTITY );
		pWeaponModel->AddEffects( EF_NODRAW ); // don't let the renderer draw the model normally
		pWeaponModel->FollowEntity( pPlayerModel ); // attach to player model
		pWeaponModel->m_flAnimTime = gpGlobals->curtime;

		int silencerBodygroup = pWeaponModel->FindBodygroupByName( "silencer" );
		if ( silencerBodygroup > -1 )
			pWeaponModel->SetBodygroup( silencerBodygroup, m_bSilenced ? 0 : 1 );
		g_ClassImageWeapon = pWeaponModel;
	}

	C_BaseAnimating *pGlovesModel = g_ClassImageGloves.Get();

	if ( bCreateGloves )
	{
		const char* pGlovesName = GetGlovesInfo( CSLoadout()->GetGlovesForPlayer( pLocalPlayer, iTeamNumber ) )->szWorldModel;

		// Does the entity even exist yet?
		if ( recreatePlayer || ShouldRecreateImageEntity( pGlovesModel, pGlovesName ) )
		{
			if ( pGlovesModel )
				pGlovesModel->Remove();

			pGlovesModel = new C_BaseAnimating;
			pGlovesModel->InitializeAsClientEntity( pGlovesName, RENDER_GROUP_OPAQUE_ENTITY );
			pGlovesModel->AddEffects( EF_NODRAW ); // don't let the renderer draw the model normally
			pGlovesModel->FollowEntity( pPlayerModel ); // attach to player model
			pGlovesModel->m_flAnimTime = gpGlobals->curtime;

			g_ClassImageGloves = pGlovesModel;
		}

		pPlayerModel->SetBodygroup( pPlayerModel->FindBodygroupByName( "gloves" ), 1 );
	}
	else if ( pGlovesModel )
		pGlovesModel->Remove();

	Vector origin = pLocalPlayer->EyePosition();
	Vector lightOrigin = origin;

	// find a spot inside the world for the dlight's origin, or it won't illuminate the model
	Vector testPos( origin.x - 100, origin.y, origin.z + 100 );
	trace_t tr;
	UTIL_TraceLine( origin, testPos, MASK_OPAQUE, pLocalPlayer, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction == 1.0f )
	{
		lightOrigin = tr.endpos;
	}
	else
	{
		// Now move the model away so we get the correct illumination
		lightOrigin = tr.endpos + Vector( 1, 0, -1 );	// pull out from the solid
		Vector start = lightOrigin;
		Vector end = lightOrigin + Vector( 100, 0, -100 );
		UTIL_TraceLine( start, end, MASK_OPAQUE, pLocalPlayer, COLLISION_GROUP_NONE, &tr );
		origin = tr.endpos;
	}

	// move player model in front of our view
	pPlayerModel->SetAbsOrigin( origin );
	pPlayerModel->SetAbsAngles( QAngle( 5, 180, 0 ) );

	// now set the sequence for this player model
	pPlayerModel->SetSequence( pPlayerModel->LookupSequence( pWeaponSequence ) );

	pPlayerModel->FrameAdvance( gpGlobals->frametime );

	// Now draw it.
	CViewSetup view;
	view.x = x;
	view.y = y;
	view.width = width;
	view.height = height;

	view.m_bOrtho = false;
	view.fov = 35;

	view.origin = origin + Vector( -150, 0, 40 );

	view.angles.Init();
	view.zNear = VIEW_NEARZ;
	view.zFar = 1000;

	Frustum dummyFrustum;
	render->Push3DView( view, 0, NULL, dummyFrustum );

	// [mhansen] We don't want to light the model in the world. We want it to 
	// always be lit normal like even if you are standing in a dark (or green) area
	// in the world.
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetLightingOrigin( vec3_origin );
	pRenderContext->SetAmbientLight( 0.6, 0.6, 0.6 );

	static Vector white[6] = 
	{
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
	};

	g_pStudioRender->SetAmbientLightColors( white );
	g_pStudioRender->SetLocalLights( 0, NULL );

	modelrender->SuppressEngineLighting( true );
	float color[3] = { 1.0f, 1.0f, 1.0f };
	render->SetColorModulation( color );
	render->SetBlend( 1.0f );
	pPlayerModel->DrawModel( STUDIO_RENDER );
	if ( pWeaponModel )
	{
		pWeaponModel->DrawModel( STUDIO_RENDER );
	}
	if ( pGlovesModel )
	{
		pGlovesModel->DrawModel( STUDIO_RENDER );
	}

	modelrender->SuppressEngineLighting( false );

	render->PopView( dummyFrustum );
}

// universal function for both CCSBuyMenuPlayerImagePanel
// (player image with active weapon) and CCSBuyMenuImagePanel
// (player image with selected weapon in buy menu)
void UpdateBuyMenuImageEntity(
	const char *pWeaponName,
	int x, int y, int width, int height,
	int viewX, int viewY, int viewZ )
{
	C_CSPlayer *pLocalPlayer = C_CSPlayer::GetLocalCSPlayer();
	
	if ( !pLocalPlayer || !pLocalPlayer->IsAlive() )
		return;

	MDLCACHE_CRITICAL_SECTION();

	const char* pWeaponModel = NULL;
	const char* pAnimName = NULL;
	const char* pAnimNameT = NULL;

	bool m_bSilenced = true;

	// check if its CCSBuyMenuPlayerImagePanel
	if ( pWeaponName == NULL )
	{
		if ( pLocalPlayer->IsAlive() && pLocalPlayer->GetActiveWeapon() )
		{
			C_WeaponCSBase *pWeapon = dynamic_cast< C_WeaponCSBase * >( pLocalPlayer->Weapon_GetSlot( WEAPON_SLOT_RIFLE ) );

			// set player's primary weapon for ui model
			if ( pWeapon )
			{
				m_bSilenced = pWeapon->IsSilenced() ? true : false;
				pWeaponModel = pWeapon->GetCSWpnData().szWorldModel;
				pAnimName = pWeapon->GetCSWpnData().m_szBuyMenuAnim;
				pAnimNameT = pWeapon->GetCSWpnData().m_szBuyMenuAnimT;
			}
			else
			{
				// no primary weapon? ok, use the secondary weapon
				pWeapon = dynamic_cast< C_WeaponCSBase * >( pLocalPlayer->Weapon_GetSlot( WEAPON_SLOT_PISTOL ) );
				if ( pWeapon )
				{
					m_bSilenced = pWeapon->IsSilenced() ? true : false;
					pWeaponModel = pWeapon->GetCSWpnData().szWorldModel;
					pAnimName = pWeapon->GetCSWpnData().m_szBuyMenuAnim;
					pAnimNameT = pWeapon->GetCSWpnData().m_szBuyMenuAnimT;
				}
				else
				{
					// no pistol as well? ok, lets try active weapon then...
					pWeapon = pLocalPlayer->GetActiveCSWeapon();
					if ( pWeapon )
					{
						m_bSilenced = pWeapon->IsSilenced() ? true : false;
						pWeaponModel = pWeapon->GetCSWpnData().szWorldModel;
						pAnimName = pWeapon->GetCSWpnData().m_szBuyMenuAnim;
						pAnimNameT = pWeapon->GetCSWpnData().m_szBuyMenuAnimT;
					}
				}
			}
		}
	}
	else
	{
		// special cases for items: they don't have a weaponscript
		if ( V_strcmp( pWeaponName, "item_defuser" ) == 0 )
		{
			pWeaponModel = "models/weapons/w_defuser.mdl";
			pAnimName = "t_buymenu_defuser";
			pAnimNameT = pAnimName;
		}
		else if ( V_strcmp( pWeaponName, "item_kevlar" ) == 0 )
		{
			pWeaponModel = "models/weapons/w_eq_armor.mdl";
			pAnimName = "t_buymenu_armor_helmet";
			pAnimNameT = pAnimName;
		}
		else if ( V_strcmp( pWeaponName, "item_assaultsuit" ) == 0 )
		{
			pWeaponModel = "models/weapons/w_eq_armor_helmet.mdl";
			pAnimName = "t_buymenu_armor_helmet";
			pAnimNameT = pAnimName;
		}
		else
		{
			WEAPON_FILE_INFO_HANDLE	hWpnInfo = LookupWeaponInfoSlot( pWeaponName );
			if ( hWpnInfo == GetInvalidWeaponInfoHandle() )
			{
				AssertMsg( false, "Failed to load a weapon script for weapon %s", pWeaponModel );
				return;
			}

			CCSWeaponInfo *pWeaponInfo = dynamic_cast<CCSWeaponInfo*>(GetFileWeaponInfoFromHandle( hWpnInfo ));
			if ( pWeaponInfo )
			{
				pWeaponModel = pWeaponInfo->szWorldModel;
				pAnimName = pWeaponInfo->m_szBuyMenuAnim;
				pAnimNameT = pWeaponInfo->m_szBuyMenuAnimT;
			}
			else
			{
				AssertMsg( false, "Failed to load a weapon script for weapon %s", pWeaponModel );
				return;
			}
		}
	}

	C_BaseAnimating *pPlayerModel = g_BuyMenuImagePlayer.Get();

	bool bCreateGloves = false;

	// Does the entity even exist yet?
	bool recreatePlayer = ShouldRecreateImageEntity( pPlayerModel, modelinfo->GetModelName( pLocalPlayer->GetModel() ) );
	if ( recreatePlayer )
	{
		if ( pPlayerModel )
			pPlayerModel->Remove();

		pPlayerModel = new C_BaseAnimating;
		pPlayerModel->InitializeAsClientEntity( modelinfo->GetModelName( pLocalPlayer->GetModel() ), RENDER_GROUP_OPAQUE_ENTITY );
		pPlayerModel->AddEffects( EF_NODRAW ); // don't let the renderer draw the model normally
		pPlayerModel->m_flAnimTime = gpGlobals->curtime;

		g_BuyMenuImagePlayer = pPlayerModel;
	}

	if ( pPlayerModel && pPlayerModel->DoesModelSupportGloves() )
	{
		if ( CSLoadout()->HasGlovesSet( pLocalPlayer, pLocalPlayer->GetTeamNumber() ) )
			bCreateGloves = true;
	}

	C_BaseAnimating *pWeaponAnimating = g_BuyMenuImageWeapon.Get();

	// Does the entity even exist yet?
	if ( recreatePlayer || ShouldRecreateImageEntity( pWeaponAnimating, pWeaponModel ) )
	{
		if ( pWeaponAnimating )
			pWeaponAnimating->Remove();

		pWeaponAnimating = new C_BaseAnimating;
		pWeaponAnimating->InitializeAsClientEntity( pWeaponModel, RENDER_GROUP_OPAQUE_ENTITY );
		pWeaponAnimating->AddEffects( EF_NODRAW ); // don't let the renderer draw the model normally
		pWeaponAnimating->FollowEntity( pPlayerModel ); // attach to player model
		pWeaponAnimating->m_flAnimTime = gpGlobals->curtime;

		int silencerBodygroup = pWeaponAnimating->FindBodygroupByName( "silencer" );
		if ( silencerBodygroup > -1 )
			pWeaponAnimating->SetBodygroup( silencerBodygroup, m_bSilenced ? 0 : 1 );
		g_BuyMenuImageWeapon = pWeaponAnimating;
	}

	C_BaseAnimating *pGlovesModel = g_BuyMenuImageGloves.Get();

	if ( bCreateGloves )
	{
		const char* pGlovesName = GetGlovesInfo( CSLoadout()->GetGlovesForPlayer( pLocalPlayer, pLocalPlayer->GetTeamNumber() ) )->szWorldModel;

		// Does the entity even exist yet?
		if ( recreatePlayer || ShouldRecreateImageEntity( pGlovesModel, pGlovesName ) )
		{
			if ( pGlovesModel )
				pGlovesModel->Remove();

			pGlovesModel = new C_BaseAnimating;
			pGlovesModel->InitializeAsClientEntity( pGlovesName, RENDER_GROUP_OPAQUE_ENTITY );
			pGlovesModel->AddEffects( EF_NODRAW ); // don't let the renderer draw the model normally
			pGlovesModel->FollowEntity( pPlayerModel ); // attach to player model
			pGlovesModel->m_flAnimTime = gpGlobals->curtime;

			g_BuyMenuImageGloves = pGlovesModel;
		}

		pPlayerModel->SetBodygroup( pPlayerModel->FindBodygroupByName( "gloves" ), 1 );
	}
	else if ( pGlovesModel )
		pGlovesModel->Remove();

	Vector origin = pLocalPlayer->EyePosition();
	Vector lightOrigin = origin;

	// find a spot inside the world for the dlight's origin, or it won't illuminate the model
	Vector testPos( origin.x - 100, origin.y, origin.z + 100 );
	trace_t tr;
	UTIL_TraceLine( origin, testPos, MASK_OPAQUE, pLocalPlayer, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction == 1.0f )
	{
		lightOrigin = tr.endpos;
	}
	else
	{
		// Now move the model away so we get the correct illumination
		lightOrigin = tr.endpos + Vector( 1, 0, -1 );	// pull out from the solid
		Vector start = lightOrigin;
		Vector end = lightOrigin + Vector( 100, 0, -100 );
		UTIL_TraceLine( start, end, MASK_OPAQUE, pLocalPlayer, COLLISION_GROUP_NONE, &tr );
		origin = tr.endpos;
	}

	// move player model in front of our view
	pPlayerModel->SetAbsOrigin( origin );
	pPlayerModel->SetAbsAngles( QAngle( 0, 180, 0 ) );

	// now set the sequence for this player model
	if ( pLocalPlayer->GetTeamNumber() == TEAM_TERRORIST && pAnimNameT != NULL )
		pPlayerModel->SetSequence( pPlayerModel->LookupSequence( pAnimNameT ) );
	else if ( pAnimName != NULL )
		pPlayerModel->SetSequence( pPlayerModel->LookupSequence( pAnimName ) );

	pPlayerModel->FrameAdvance( gpGlobals->frametime );

	// Now draw it.
	CViewSetup view;
	view.x = x;
	view.y = y;
	view.width = width;
	view.height = height;

	view.m_bOrtho = false;
	view.fov = 42; // previously was 32

	view.origin = origin + Vector( viewX, viewY, viewZ );
	//view.origin = origin + Vector( -70, -0, 56 ); -- old values for old animations

	view.angles.Init();
	view.zNear = VIEW_NEARZ;
	view.zFar = 1000;

	Frustum dummyFrustum;
	render->Push3DView( view, 0, NULL, dummyFrustum );

	// [mhansen] We don't want to light the model in the world. We want it to 
	// always be lit normal like even if you are standing in a dark (or green) area
	// in the world.
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetLightingOrigin( vec3_origin );
	pRenderContext->SetAmbientLight( 0.6, 0.6, 0.6 );

	static Vector white[6] = 
	{
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
		Vector( 0.6, 0.6, 0.6 ),
	};

	g_pStudioRender->SetAmbientLightColors( white );
	g_pStudioRender->SetLocalLights( 0, NULL );

	modelrender->SuppressEngineLighting( true );
	float color[3] = { 1.0f, 1.0f, 1.0f };
	render->SetColorModulation( color );
	render->SetBlend( 1.0f );
	pPlayerModel->DrawModel( STUDIO_RENDER );

	if ( pWeaponAnimating )
	{
		pWeaponAnimating->DrawModel( STUDIO_RENDER );
	}
	if ( pGlovesModel )
	{
		pGlovesModel->DrawModel( STUDIO_RENDER );
	}

	modelrender->SuppressEngineLighting( false );

	render->PopView( dummyFrustum );
}

bool WillPanelBeVisible( vgui::VPANEL hPanel )
{
	while ( hPanel )
	{
		if ( !vgui::ipanel()->IsVisible( hPanel ) )
			return false;

		hPanel = vgui::ipanel()->GetParent( hPanel );
	}
	return true;
}

extern ConVar loadout_slot_fiveseven_weapon;
extern ConVar loadout_slot_hkp2000_weapon;
extern ConVar loadout_slot_m4_weapon;
extern ConVar loadout_slot_mp7_weapon_ct;
extern ConVar loadout_slot_mp7_weapon_t;
extern ConVar loadout_slot_tec9_weapon;
extern ConVar loadout_slot_deagle_weapon_ct;
extern ConVar loadout_slot_deagle_weapon_t;
void ClientModeCSNormal::PostRenderVGui()
{
	// If the team menu is up, then we will render the model of the character that is currently selected.
	for ( int i=0; i < g_ClassImagePanels.Count(); i++ )
	{
		CCSClassImagePanel *pPanel = g_ClassImagePanels[i];
		if ( WillPanelBeVisible( pPanel->GetVPanel() ) )
		{
			// Ok, we have a visible class image panel.
			int x, y, w, h;
			pPanel->GetBounds( x, y, w, h );
			pPanel->LocalToScreen( x, y );

			// Allow for the border.
			x += 3;
			y += 5;
			w -= 2;
			h -= 10;

			UpdateClassImageEntity( pPanel->m_ModelName, x, y, w, h );
			return;
		}
	}

	// If the team menu is up, then we will render the model of the character that is currently selected.
	for ( int i=0; i < g_BuyMenuPlayerImagePanels.Count(); i++ )
	{
		CCSBuyMenuPlayerImagePanel *pPanel = g_BuyMenuPlayerImagePanels[i];
		if ( WillPanelBeVisible( pPanel->GetVPanel() ) )
		{
			// Ok, we have a visible class image panel.
			int x, y, w, h;
			pPanel->GetBounds( x, y, w, h );

			// Allow for the border.
			x += 3;
			y += 5;
			w -= 2;
			h -= 10;

			UpdateBuyMenuImageEntity( NULL, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
			return;
		}
	}

	// If the buy menu is up, then we will render the model of the weapon that is currently selected.
	for ( int i=0; i < g_BuyMenuImagePanels.Count(); i++ )
	{
		CCSBuyMenuImagePanel *pPanel = g_BuyMenuImagePanels[i];
		if ( WillPanelBeVisible( pPanel->GetVPanel() ) )
		{
			// Ok, we have a visible class image panel.
			int x, y, w, h;
			pPanel->GetBounds( x, y, w, h );
			pPanel->LocalToScreen( x, y );

			// Allow for the border.
			x += 3;
			y += 5;
			w -= 2;
			h -= 10;

			const char *szWeaponName = NULL;
			if ( V_strcmp( pPanel->m_WeaponName, "fiveseven_cz75" ) == 0 )
			{
				szWeaponName = !loadout_slot_fiveseven_weapon.GetBool() ? "weapon_fiveseven" : "weapon_cz75";
				UpdateBuyMenuImageEntity( szWeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
			else if ( V_strcmp( pPanel->m_WeaponName, "hkp2000_usp" ) == 0 )
			{
				szWeaponName = !loadout_slot_hkp2000_weapon.GetBool() ? "weapon_hkp2000" : "weapon_usp_silencer";
				UpdateBuyMenuImageEntity( szWeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
			else if ( V_strcmp( pPanel->m_WeaponName, "m4a4_m4a1" ) == 0 )
			{
				szWeaponName = !loadout_slot_m4_weapon.GetBool() ? "weapon_m4a4" : "weapon_m4a1_silencer";
				UpdateBuyMenuImageEntity( szWeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
			else if ( V_strcmp( pPanel->m_WeaponName, "mp7_mp5sd_ct" ) == 0 )
			{
				szWeaponName = !loadout_slot_mp7_weapon_ct.GetBool() ? "weapon_mp7" : "weapon_mp5sd";
				UpdateBuyMenuImageEntity( szWeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
			else if ( V_strcmp( pPanel->m_WeaponName, "mp7_mp5sd_t" ) == 0 )
			{
				szWeaponName = !loadout_slot_mp7_weapon_t.GetBool() ? "weapon_mp7" : "weapon_mp5sd";
				UpdateBuyMenuImageEntity( szWeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
			else if ( V_strcmp( pPanel->m_WeaponName, "tec9_cz75" ) == 0 )
			{
				szWeaponName = !loadout_slot_tec9_weapon.GetBool() ? "weapon_tec9" : "weapon_cz75";
				UpdateBuyMenuImageEntity( szWeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
			else if ( V_strcmp( pPanel->m_WeaponName, "deagle_revolver_ct" ) == 0 )
			{
				szWeaponName = !loadout_slot_deagle_weapon_ct.GetBool() ? "weapon_deagle" : "weapon_revolver";
				UpdateBuyMenuImageEntity( szWeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
			else if ( V_strcmp( pPanel->m_WeaponName, "deagle_revolver_t" ) == 0 )
			{
				szWeaponName = !loadout_slot_deagle_weapon_t.GetBool() ? "weapon_deagle" : "weapon_revolver";
				UpdateBuyMenuImageEntity( szWeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
			else
			{
				UpdateBuyMenuImageEntity( pPanel->m_WeaponName, x, y, w, h, pPanel->m_ViewXPos, pPanel->m_ViewYPos, pPanel->m_ViewZPos );
				return;
			}
		}
	}
}

bool ClientModeCSNormal::ShouldDrawViewModel( void )
{
	C_CSPlayer *pPlayer = C_CSPlayer::GetLocalCSPlayer();
	
	if( pPlayer && pPlayer->GetFOV() != CSGameRules()->DefaultFOV() )
	{
		CWeaponCSBase *pWpn = pPlayer->GetActiveCSWeapon();

		if( pWpn && pWpn->HideViewModelWhenZoomed() )
		{
			return false;
		}
	}

	return BaseClass::ShouldDrawViewModel();
}


bool ClientModeCSNormal::CanRecordDemo( char *errorMsg, int length ) const
{
	C_CSPlayer *player = C_CSPlayer::GetLocalCSPlayer();
	if ( !player )
	{
		return true;
	}

	if ( !player->IsAlive() )
	{
		return true;
	}

	// don't start recording while flashed, as it would remove the flash
	if ( player->m_flFlashBangTime > gpGlobals->curtime )
	{
		Q_strncpy( errorMsg, "Cannot record demos while blind.", length );
		return false;
	}

	// don't start recording while smoke grenades are spewing smoke, as the existing smoke would be destroyed
	C_BaseEntityIterator it;
	C_BaseEntity *ent;
	while ( (ent = it.Next()) != NULL )
	{
		if ( Q_strcmp( ent->GetClassname(), "class C_ParticleSmokeGrenade" ) == 0 )
		{
			Q_strncpy( errorMsg, "Cannot record demos while a smoke grenade is active.", length );
			return false;
		}
	}

	return true;
}

//=============================================================================
// HPE_BEGIN:
// [menglish] Save server information shown to the client in a persistent place
//=============================================================================
 
void ClientModeCSNormal::SetServerName(wchar_t* name)
{
	V_wcsncpy(m_pServerName, name, sizeof( m_pServerName ) );
}

void ClientModeCSNormal::SetMapName(wchar_t* name)
{
	V_wcsncpy(m_pMapName, name, sizeof( m_pMapName ) );
}

//=============================================================================
// HPE_END
//=============================================================================

// Receive the PlayerIgnited user message and send out a clientside event for achievements to hook.
void __MsgFunc_MatchEndConditions( bf_read &msg )
{
	int iFragLimit = (int) msg.ReadLong();
	int iMaxRounds = (int) msg.ReadLong();
	int iWinRounds = (int) msg.ReadLong();
	int iTimeLimit = (int) msg.ReadLong();

	IGameEvent *event = gameeventmanager->CreateEvent( "match_end_conditions" );
	if ( event )
	{
		event->SetInt( "frags", iFragLimit );
		event->SetInt( "max_rounds", iMaxRounds );
		event->SetInt( "win_rounds", iWinRounds );
		event->SetInt( "time", iTimeLimit );
		gameeventmanager->FireEventClientSide( event );
	}
}
