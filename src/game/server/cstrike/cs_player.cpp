//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Player for HL1.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "cs_player.h"
#include "cs_gamerules.h"
#include "trains.h"
#include "vcollide_parse.h"
#include "in_buttons.h"
#include "igamemovement.h"
#include "ai_hull.h"
#include "ndebugoverlay.h"
#include "weapon_csbase.h"
#include "decals.h"
#include "cs_ammodef.h"
#include "IEffects.h"
#include "cs_client.h"
#include "client.h"
#include "cs_shareddefs.h"
#include "shake.h"
#include "team.h"
#include "weapon_c4.h"
#include "weapon_parse.h"
#include "weapon_knife.h"
#include "movehelper_server.h"
#include "tier0/vprof.h"
#include "te_effect_dispatch.h"
#include "vphysics/player_controller.h"
#include "weapon_hegrenade.h"
#include "weapon_flashbang.h"
#include "weapon_csbasegun.h"
#include "weapon_smokegrenade.h"
#include <KeyValues.h>
#include "engine/IEngineSound.h"
#include "bot.h"
#include "studio.h"
#include <coordsize.h>
#include "predicted_viewmodel.h"
#include "baseviewmodel_shared.h"
#include "props_shared.h"
#include "tier0/icommandline.h"
#include "info_camera_link.h"
#include "hintmessage.h"
#include "obstacle_pushaway.h"
#include "movevars_shared.h"
#include "death_pose.h"
#include "basecsgrenade_projectile.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "CRagdollMagnet.h"
#include "datacache/imdlcache.h"
#include "npcevent.h"
#include "cs_gamestats.h"
#include "gamestats.h"
#include "holiday_gift.h"
#include "../../shared/cstrike/cs_achievement_constants.h"
#include "weapon_decoy.h"
#include "molotov_projectile.h"
#include "cs_loadout.h"
#include "item_healthshot.h"

//=============================================================================
// HPE_BEGIN
//=============================================================================

// [dwenger] Needed for global hostage list
#include "cs_simple_hostage.h"

// [dwenger] Needed for weapon type used tracking
#include "../../shared/cstrike/cs_weapon_parse.h"

#define REPORT_PLAYER_DAMAGE 0

//=============================================================================
// HPE_END
//=============================================================================

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#pragma optimize( "", off )

#pragma warning( disable : 4355 )

// Minimum interval between rate-limited commands that players can run.
#define CS_COMMAND_MAX_RATE 0.3

const float CycleLatchInterval = 0.2f;

#define CS_PUSHAWAY_THINK_CONTEXT	"CSPushawayThink"

ConVar cs_ShowStateTransitions( "cs_ShowStateTransitions", "-2", FCVAR_CHEAT, "cs_ShowStateTransitions <ent index or -1 for all>. Show player state transitions." );
ConVar sv_max_usercmd_future_ticks( "sv_max_usercmd_future_ticks", "8", 0, "Prevents clients from running usercmds too far in the future. Prevents speed hacks." );
ConVar sv_motd_unload_on_dismissal( "sv_motd_unload_on_dismissal", "0", 0, "If enabled, the MOTD contents will be unloaded when the player closes the MOTD." );

//=============================================================================
// HPE_BEGIN:
// [Forrest] Allow MVP to be turned off for a server
// [Forrest] Allow freezecam to be turned off for a server
// [Forrest] Allow win panel to be turned off for a server
//=============================================================================
static void SvNoMVPChangeCallback( IConVar *pConVar, const char *pOldValue, float flOldValue )
{
	ConVarRef var( pConVar );
	if ( var.IsValid() && var.GetBool() )
	{
		// Clear the MVPs of all players when MVP is turned off.
		for ( int i = 1; i <= MAX_PLAYERS; i++ )
		{
			CCSPlayer *pPlayer = ToCSPlayer( UTIL_PlayerByIndex( i ) );

			if ( pPlayer )
			{
				pPlayer->SetNumMVPs( 0 );
			}
		}
	}
}
ConVar sv_nomvp( "sv_nomvp", "0", 0, "Disable MVP awards.", SvNoMVPChangeCallback );
ConVar sv_disablefreezecam( "sv_disablefreezecam", "0", FCVAR_REPLICATED, "Turn on/off freezecam on server" );
ConVar sv_nowinpanel( "sv_nowinpanel", "0", FCVAR_REPLICATED, "Turn on/off win panel on server" );
//=============================================================================
// HPE_END
//=============================================================================


// ConVar bot_mimic( "bot_mimic", "0", FCVAR_CHEAT );
ConVar bot_freeze( "bot_freeze", "0", FCVAR_CHEAT );
ConVar bot_crouch( "bot_crouch", "0", FCVAR_CHEAT );
ConVar bot_mimic_yaw_offset( "bot_mimic_yaw_offset", "180", FCVAR_CHEAT );

ConVar sv_legacy_grenade_damage( "sv_legacy_grenade_damage", "0", FCVAR_REPLICATED, "Enable to replicate grenade damage behavior of the original Counter-Strike Source game." );

extern ConVar mp_autokick;
extern ConVar mp_holiday_nogifts;
extern ConVar sv_turbophysics;
extern ConVar mp_anyone_can_pickup_c4;

// [menglish] Added in convars for freeze cam time length
extern ConVar spec_freeze_time;
extern ConVar spec_freeze_time_lock;
extern ConVar spec_freeze_traveltime;

extern ConVar ammo_hegrenade_max;
extern ConVar ammo_flashbang_max;
extern ConVar ammo_smokegrenade_max;
extern ConVar ammo_decoy_max;
extern ConVar ammo_molotov_max;

extern ConVar mp_buy_anywhere;

extern ConVar mp_respawn_immunitytime;

ConVar mp_drop_knife_enable( "mp_drop_knife_enable", "0", 0, "Allows players to drop knives." );

ConVar mp_deathcam_skippable( "mp_deathcam_skippable", "1", FCVAR_REPLICATED, "Determines whether a player can early-out of the deathcam." );

#define THROWGRENADE_COUNTER_BITS 3


EHANDLE g_pLastCTSpawn;
EHANDLE g_pLastTerroristSpawn;

void TE_RadioIcon( IRecipientFilter& filter, float delay, CBaseEntity *pPlayer );


// -------------------------------------------------------------------------------- //
// Classes
// -------------------------------------------------------------------------------- //

class CPhysicsPlayerCallback : public IPhysicsPlayerControllerEvent
{
public:
	int ShouldMoveTo( IPhysicsObject *pObject, const Vector &position )
	{
		CCSPlayer *pPlayer = (CCSPlayer *)pObject->GetGameData();
		if ( pPlayer )
		{
			if ( pPlayer->TouchedPhysics() )
			{
				return 0;
			}
		}
		return 1;
	}
};

static CPhysicsPlayerCallback playerCallback;


// -------------------------------------------------------------------------------- //
// Ragdoll entities.
// -------------------------------------------------------------------------------- //

class CCSRagdoll : public CBaseAnimatingOverlay
{
public:
	DECLARE_CLASS( CCSRagdoll, CBaseAnimatingOverlay );
	DECLARE_SERVERCLASS();

	// Transmit ragdolls to everyone.
	virtual int UpdateTransmitState()
	{
		return SetTransmitState( FL_EDICT_ALWAYS );
	}

	void Init( void )
	{
		SetSolid( SOLID_BBOX );
		SetMoveType( MOVETYPE_STEP );
		SetFriction( 1.0f );
		SetCollisionBounds( VEC_DUCK_HULL_MIN, VEC_DUCK_HULL_MAX );
		m_takedamage = DAMAGE_NO;
		SetCollisionGroup( COLLISION_GROUP_DEBRIS );
		SetAbsOrigin( m_hPlayer->GetAbsOrigin() );
		SetAbsVelocity( m_hPlayer->GetAbsVelocity() );
		AddSolidFlags( FSOLID_NOT_SOLID );
		ChangeTeam( m_hPlayer->GetTeamNumber() );
		UseClientSideAnimation();
	}

public:
	// In case the client has the player entity, we transmit the player index.
	// In case the client doesn't have it, we transmit the player's model index, origin, and angles
	// so they can create a ragdoll in the right place.
	CNetworkHandle( CBaseEntity, m_hPlayer );	// networked entity handle
	CNetworkVector( m_vecRagdollVelocity );
	CNetworkVector( m_vecRagdollOrigin );
	CNetworkVar(int, m_iDeathPose );
	CNetworkVar(int, m_iDeathFrame );
	CNetworkVar(float, m_flDeathYaw );
};

LINK_ENTITY_TO_CLASS( cs_ragdoll, CCSRagdoll );

IMPLEMENT_SERVERCLASS_ST_NOBASE( CCSRagdoll, DT_CSRagdoll )
	SendPropVector	(SENDINFO(m_vecOrigin), -1,  SPROP_COORD|SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_Origin ),
	SendPropVector( SENDINFO(m_vecRagdollOrigin), -1,  SPROP_COORD ),
	SendPropEHandle( SENDINFO( m_hPlayer ) ),
	SendPropModelIndex( SENDINFO( m_nModelIndex ) ),
	SendPropInt		( SENDINFO(m_nForceBone), 8, 0 ),
	SendPropVector	( SENDINFO(m_vecForce), -1, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_vecRagdollVelocity ) ),
	SendPropInt( SENDINFO( m_iDeathPose ), ANIMATION_SEQUENCE_BITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iDeathFrame ), 5 ),
	SendPropInt( SENDINFO(m_iTeamNum), TEAMNUM_NUM_BITS, 0),
	SendPropInt( SENDINFO( m_bClientSideAnimation ), 1, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flDeathYaw ), 0, SPROP_NOSCALE ),
END_SEND_TABLE()


// -------------------------------------------------------------------------------- //
// Player animation event. Sent to the client when a player fires, jumps, reloads, etc..
// -------------------------------------------------------------------------------- //

class CTEPlayerAnimEvent : public CBaseTempEntity
{
public:
	DECLARE_CLASS( CTEPlayerAnimEvent, CBaseTempEntity );
	DECLARE_SERVERCLASS();

					CTEPlayerAnimEvent( const char *name ) : CBaseTempEntity( name )
					{
					}

	CNetworkHandle( CBasePlayer, m_hPlayer );
	CNetworkVar( int, m_iEvent );
	CNetworkVar( int, m_nData );
};

IMPLEMENT_SERVERCLASS_ST_NOBASE( CTEPlayerAnimEvent, DT_TEPlayerAnimEvent )
	SendPropEHandle( SENDINFO( m_hPlayer ) ),
	SendPropInt( SENDINFO( m_iEvent ), Q_log2( PLAYERANIMEVENT_COUNT ) + 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nData ), 32 )
END_SEND_TABLE()

static CTEPlayerAnimEvent g_TEPlayerAnimEvent( "PlayerAnimEvent" );

void TE_PlayerAnimEvent( CBasePlayer *pPlayer, PlayerAnimEvent_t event, int nData )
{
	CPVSFilter filter( (const Vector&)pPlayer->EyePosition() );

	g_TEPlayerAnimEvent.m_hPlayer = pPlayer;
	g_TEPlayerAnimEvent.m_iEvent = event;
	g_TEPlayerAnimEvent.m_nData = nData;
	g_TEPlayerAnimEvent.Create( filter, 0 );
}


//-----------------------------------------------------------------------------
// Purpose: Filters updates to a variable so that only non-local players see
// the changes.  This is so we can send a low-res origin to non-local players
// while sending a hi-res one to the local player.
// Input  : *pVarData -
//			*pOut -
//			objectID -
//-----------------------------------------------------------------------------

void* SendProxy_SendNonLocalDataTable( const SendProp *pProp, const void *pStruct, const void *pVarData, CSendProxyRecipients *pRecipients, int objectID )
{
	pRecipients->SetAllRecipients();
	pRecipients->ClearRecipient( objectID - 1 );
	return ( void * )pVarData;
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER( SendProxy_SendNonLocalDataTable );


// -------------------------------------------------------------------------------- //
// Tables.
// -------------------------------------------------------------------------------- //

LINK_ENTITY_TO_CLASS( player, CCSPlayer );
PRECACHE_REGISTER(player);

BEGIN_SEND_TABLE_NOBASE( CCSPlayer, DT_CSLocalPlayerExclusive )
	SendPropFloat( SENDINFO( m_flStamina ), 14, 0, 0, 1400  ),
	SendPropInt( SENDINFO( m_iDirection ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iShotsFired ), 8, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flVelocityModifier ), 8, 0, 0, 1  ),

	SendPropBool( SENDINFO( m_bDuckOverride ) ),

	// send a hi-res origin to the local player for use in prediction
	SendPropVector	(SENDINFO(m_vecOrigin), -1,  SPROP_NOSCALE|SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_Origin ),

	//=============================================================================
	// HPE_BEGIN:
	// [tj]Set up the send table for per-client domination data
	//=============================================================================
 
	SendPropArray3( SENDINFO_ARRAY3( m_bPlayerDominated ), SendPropBool( SENDINFO_ARRAY( m_bPlayerDominated ) ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_bPlayerDominatingMe ), SendPropBool( SENDINFO_ARRAY( m_bPlayerDominatingMe ) ) ),
 
	//=============================================================================
	// HPE_END
	//=============================================================================

	SendPropBool( SENDINFO( m_bIsLookingAtWeapon ) ),
	SendPropBool( SENDINFO( m_bIsHoldingLookAtWeapon ) ),

	SendPropFloat( SENDINFO( m_flLowerBodyYawTarget ), 8, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_bStrafing ) ),

	SendPropFloat( SENDINFO( m_flThirdpersonRecoil ), 8, SPROP_NOSCALE ),

END_SEND_TABLE()


BEGIN_SEND_TABLE_NOBASE( CCSPlayer, DT_CSNonLocalPlayerExclusive )
	// send a lo-res origin to other players
	SendPropVector	(SENDINFO(m_vecOrigin), -1,  SPROP_COORD|SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_Origin ),
END_SEND_TABLE()


IMPLEMENT_SERVERCLASS_ST( CCSPlayer, DT_CSPlayer )
	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	SendPropExclude( "DT_BaseAnimating", "m_flPlaybackRate" ),
	SendPropExclude( "DT_BaseAnimating", "m_nSequence" ),
	SendPropExclude( "DT_BaseAnimating", "m_nNewSequenceParity" ),
	SendPropExclude( "DT_BaseAnimating", "m_nResetEventsParity" ),
	SendPropExclude( "DT_BaseAnimating", "m_nMuzzleFlashParity" ),
	SendPropExclude( "DT_BaseEntity", "m_angRotation" ),
	SendPropExclude( "DT_BaseAnimatingOverlay", "overlay_vars" ),

	// cs_playeranimstate and clientside animation takes care of these on the client
	SendPropExclude( "DT_ServerAnimationData" , "m_flCycle" ),
	SendPropExclude( "DT_AnimTimeMustBeFirst" , "m_flAnimTime" ),

	// We need to send a hi-res origin to the local player to avoid prediction errors sliding along walls
	SendPropExclude( "DT_BaseEntity", "m_vecOrigin" ),

	// Data that only gets sent to the local player.
	SendPropDataTable( "cslocaldata", 0, &REFERENCE_SEND_TABLE(DT_CSLocalPlayerExclusive), SendProxy_SendLocalDataTable ),
	SendPropDataTable( "csnonlocaldata", 0, &REFERENCE_SEND_TABLE(DT_CSNonLocalPlayerExclusive), SendProxy_SendNonLocalDataTable ),

	SendPropInt( SENDINFO( m_iThrowGrenadeCounter ), THROWGRENADE_COUNTER_BITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iAddonBits ), NUM_ADDON_BITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iPrimaryAddon ), 8, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iSecondaryAddon ), 8, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iKnifeAddon ), 8, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iPlayerState ), Q_log2( NUM_PLAYER_STATES )+1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iAccount ), 16, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bInBombZone ) ),
	SendPropInt( SENDINFO( m_bInBuyZone ), 1, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bKilledByTaser ) ),
	SendPropInt( SENDINFO( m_iMoveState ), 0, SPROP_CHANGES_OFTEN ),
	SendPropInt( SENDINFO( m_iClass ), Q_log2( CS_NUM_CLASSES )+1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_ArmorValue ), 8 ),
	SendPropAngle( SENDINFO_VECTORELEM(m_angEyeAngles, 0), 11, SPROP_CHANGES_OFTEN ),
	SendPropAngle( SENDINFO_VECTORELEM(m_angEyeAngles, 1), 11, SPROP_CHANGES_OFTEN ),
	SendPropBool( SENDINFO( m_bHasDefuser ) ),
	SendPropBool( SENDINFO( m_bNightVisionOn ) ),	//send as int so we can use a RecvProxy on the client
	SendPropBool( SENDINFO( m_bHasNightVision ) ),
	SendPropBool( SENDINFO( m_bIsGrabbingHostage ) ),
	SendPropEHandle( SENDINFO( m_hCarriedHostage ) ),
	SendPropEHandle( SENDINFO( m_hCarriedHostageProp ) ),
	SendPropBool( SENDINFO( m_bIsWalking ) ),
	SendPropBool( SENDINFO( m_bHasMovedSinceSpawn ) ),
	SendPropFloat( SENDINFO( m_flGroundAccelLinearFracLastTime ), 0, SPROP_CHANGES_OFTEN ),

	//=============================================================================
	// HPE_BEGIN:
	// [dwenger] Added for fun-fact support
	//=============================================================================

	//SendPropBool( SENDINFO( m_bPickedUpDefuser ) ),
	//SendPropBool( SENDINFO( m_bDefusedWithPickedUpKit) ),

	//=============================================================================
	// HPE_END
	//=============================================================================

	SendPropBool( SENDINFO( m_bInHostageRescueZone ) ),
	SendPropBool( SENDINFO( m_bIsDefusing ) ),

	SendPropBool( SENDINFO( m_bResumeZoom ) ),
	SendPropBool( SENDINFO( m_bHasMovedSinceSpawn ) ),
	SendPropFloat( SENDINFO( m_fImmuneToDamageTime ) ),
	SendPropBool( SENDINFO( m_bImmunity ) ),
	SendPropInt( SENDINFO( m_iLastZoom ), 8, SPROP_UNSIGNED ),

#ifdef CS_SHIELD_ENABLED
	SendPropBool( SENDINFO( m_bHasShield ) ),
	SendPropBool( SENDINFO( m_bShieldDrawn ) ),
#endif
	SendPropBool( SENDINFO( m_bHasHelmet ) ),
	SendPropFloat	(SENDINFO(m_flFlashDuration), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO(m_flFlashMaxAlpha), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_iProgressBarDuration ), 4, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flProgressBarStartTime ), 0, SPROP_NOSCALE ),
	SendPropEHandle( SENDINFO( m_hRagdoll ) ),
	SendPropInt( SENDINFO( m_cycleLatch ), 4, SPROP_UNSIGNED ),

#if CS_CONTROLLABLE_BOTS_ENABLED
	SendPropBool( SENDINFO( m_bIsControllingBot ) ),
	SendPropBool( SENDINFO( m_bHasControlledBotThisRound ) ),
	SendPropBool( SENDINFO( m_bCanControlObservedBot ) ),
	SendPropInt( SENDINFO( m_iControlledBotEntIndex ) ),
#endif

	SendPropBool( SENDINFO( m_bNeedToChangeGloves ) ),
	SendPropInt( SENDINFO( m_iLoadoutSlotGlovesCT ) ),
	SendPropInt( SENDINFO( m_iLoadoutSlotGlovesT ) ),


END_SEND_TABLE()


BEGIN_DATADESC( CCSPlayer )

	DEFINE_INPUTFUNC( FIELD_VOID, "OnRescueZoneTouch", RescueZoneTouch ),
	DEFINE_THINKFUNC( PushawayThink )

END_DATADESC()


// has to be included after above macros
#include "cs_bot.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"



// -------------------------------------------------------------------------------- //

void cc_CreatePredictionError_f( const CCommand &args )
{
	float distance = 32;

	if ( args.ArgC() >= 2 )
	{
		distance = atof(args[1]);
	}

	CBaseEntity *pEnt = CBaseEntity::Instance( 1 );
	pEnt->SetAbsOrigin( pEnt->GetAbsOrigin() + Vector( distance, 0, 0 ) );
}

ConCommand cc_CreatePredictionError( "CreatePredictionError", cc_CreatePredictionError_f, "Create a prediction error", FCVAR_CHEAT );


// -------------------------------------------------------------------------------- //
// CCSPlayer implementation.
// -------------------------------------------------------------------------------- //

CCSPlayer::CCSPlayer()
{
	m_PlayerAnimState = CreatePlayerAnimState( this, this, LEGANIM_9WAY, true );
	m_PlayerAnimStateCSGO = CreateCSGOPlayerAnimstate( this );

	UseClientSideAnimation();

	m_iLastWeaponFireUsercmd = 0;
	m_iAddonBits = 0;
	m_bEscaped = false;
	m_iAccount = 0;

	m_bIsVIP = false;
	m_iClass = (int)CS_CLASS_NONE;
	m_iSkin = 0;
	m_angEyeAngles.Init();

	m_flThirdpersonRecoil = 0;

	SetViewOffset( VEC_VIEW_SCALED( this ) );

	m_pCurStateInfo = NULL;	// no state yet
	m_iThrowGrenadeCounter = 0;

	m_lifeState = LIFE_DEAD; // Start "dead".
	m_bInBombZone = false;
	m_bInBuyZone = false;
	m_bInHostageRescueZone = false;
	m_flDeathTime = 0.0f;
	m_iHostagesKilled = 0;
	iRadioMenu = -1;
	m_bTeamChanged = false;
	m_iShotsFired = 0;
	m_iDirection = 0;
	m_receivesMoneyNextRound = true;
	m_bIsBeingGivenItem = false;
	m_isVIP = false;

	m_bJustKilledTeammate = false;
	m_bPunishedForTK = false;
	m_iTeamKills = 0;
	m_flLastMovement = gpGlobals->curtime;
	m_iNextTimeCheck = 0;

	m_szNewName[0] = 0;
	m_szClanTag[0] = 0;

	for ( int i=0; i<NAME_CHANGE_HISTORY_SIZE; i++ )
	{
		m_flNameChangeHistory[i] = -NAME_CHANGE_HISTORY_INTERVAL;
	}

	m_iIgnoreGlobalChat = 0;
	m_bIgnoreRadio = false;

	m_pHintMessageQueue = new CHintMessageQueue(this);
	m_iDisplayHistoryBits = 0;
	m_bShowHints = true;
	m_flNextMouseoverUpdate = gpGlobals->curtime;

	m_lastDamageHealth = 0;
	m_lastDamageArmor = 0;

	m_applyDeafnessTime = 0.0f;

	m_cycleLatch = 0;
	m_cycleLatchTimer.Invalidate();

	m_iShouldHaveCash = 0;

	m_lastNavArea = NULL;

	// [menglish] Init achievement variables
	m_NumEnemiesKilledThisRound = 0;
	m_NumEnemiesAtRoundStart = 0;
	m_KillingSpreeStartTime = -1;
	m_firstKillBlindStartTime = -1;
	m_killsWhileBlind = 0;
	m_bSurvivedHeadshotDueToHelmet = false;
	m_pGooseChaseDistractingPlayer = NULL;
	m_gooseChaseStep = GC_NONE;
	m_defuseDefenseStep = DD_NONE;
	m_lastRoundResult = Invalid_Round_End_Reason;
	m_bMadeFootstepNoise = false;
	m_bombPickupTime = -1;
	m_bMadePurchseThisRound = false;
	m_roundsWonWithoutPurchase = 0;
	m_iDeathFlags = 0;
	m_lastFlashBangAttacker = NULL;
	m_iMVPs = 0;
	m_bKilledDefuser = false;
	m_bKilledRescuer = false;
	m_maxGrenadeKills = 0;
	m_grenadeDamageTakenThisRound = 0;
	m_flGotHostageTalkTimer = 0;
	m_flDefusingTalkTimer = 0;
	m_flC4PlantTalkTimer = 0;

	m_vLastHitLocationObjectSpace = Vector(0,0,0);

	m_wasNotKilledNaturally = false;

	m_bHasMovedSinceSpawn = false;

	m_fImmuneToDamageTime = 0.0f;
	m_bImmunity = false;

	m_bHasMovedSinceSpawn = false;

	m_fNextMolotovDamageSoundTime = 0.0f;

#if CS_CONTROLLABLE_BOTS_ENABLED
	m_bIsControllingBot = false;
	m_bCanControlObservedBot = false;
	m_iControlledBotEntIndex = -1;
#endif

	m_iMoveState = MOVESTATE_IDLE;

	m_storedSpawnPosition = vec3_origin;
	m_storedSpawnAngle.Init();

	m_nPreferredGrenadeDrop = 0;

	m_bNeedToChangeAgent = true;
	m_bNeedToChangeGloves = true;
}


CCSPlayer::~CCSPlayer()
{
	delete m_pHintMessageQueue;
	m_pHintMessageQueue = NULL;

	// delete the records of damage taken and given
	ResetDamageCounters();
	
	if ( m_PlayerAnimState )
		m_PlayerAnimState->Release();

	if ( m_PlayerAnimStateCSGO )
		m_PlayerAnimStateCSGO->Release();
}


CCSPlayer *CCSPlayer::CreatePlayer( const char *className, edict_t *ed )
{
	CCSPlayer::s_PlayerEdict = ed;
	return (CCSPlayer*)CreateEntityByName( className );
}

void CCSPlayer::Precache()
{
	Vector mins( -14, -30, -10 );
	Vector maxs( 14, 30, 80 );

	int i;
	for ( i=0; i<CTST6PlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( CTST6PlayerModels[i] ) )
		{
			PrecacheModel( CTST6PlayerModels[i] );
			engine->ForceModelBounds( CTST6PlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<CTGSG9PlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( CTGSG9PlayerModels[i] ) )
		{
			PrecacheModel( CTGSG9PlayerModels[i] );
			engine->ForceModelBounds( CTGSG9PlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<CTSASPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( CTSASPlayerModels[i] ) )
		{
			PrecacheModel( CTSASPlayerModels[i] );
			engine->ForceModelBounds( CTSASPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<CTGIGNPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( CTGIGNPlayerModels[i] ) )
		{
			PrecacheModel( CTGIGNPlayerModels[i] );
			engine->ForceModelBounds( CTGIGNPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<CTFBIPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( CTFBIPlayerModels[i] ) )
		{
			PrecacheModel( CTFBIPlayerModels[i] );
			engine->ForceModelBounds( CTFBIPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<CTIDFPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( CTIDFPlayerModels[i] ) )
		{
			PrecacheModel( CTIDFPlayerModels[i] );
			engine->ForceModelBounds( CTIDFPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<CTSWATPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( CTSWATPlayerModels[i] ) )
		{
			PrecacheModel( CTSWATPlayerModels[i] );
			engine->ForceModelBounds( CTSWATPlayerModels[i], mins, maxs );
		}
	}

	for ( i=0; i<TPhoenixPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( TPhoenixPlayerModels[i] ) )
		{
			PrecacheModel( TPhoenixPlayerModels[i] );
			engine->ForceModelBounds( TPhoenixPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<TLeetPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( TLeetPlayerModels[i] ) )
		{
			PrecacheModel( TLeetPlayerModels[i] );
			engine->ForceModelBounds( TLeetPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<TSeparatistPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( TSeparatistPlayerModels[i] ) )
		{
			PrecacheModel( TSeparatistPlayerModels[i] );
			engine->ForceModelBounds( TSeparatistPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<TBalkanPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( TBalkanPlayerModels[i] ) )
		{
			PrecacheModel( TBalkanPlayerModels[i] );
			engine->ForceModelBounds( TBalkanPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<TProfessionalPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( TProfessionalPlayerModels[i] ) )
		{
			PrecacheModel( TProfessionalPlayerModels[i] );
			engine->ForceModelBounds( TProfessionalPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<TAnarchistPlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( TAnarchistPlayerModels[i] ) )
		{
			PrecacheModel( TAnarchistPlayerModels[i] );
			engine->ForceModelBounds( TAnarchistPlayerModels[i], mins, maxs );
		}
	}
	for ( i=0; i<TPiratePlayerModels.Count(); ++i )
	{
		if ( !engine->IsModelPrecached( TPiratePlayerModels[i] ) )
		{
			PrecacheModel( TPiratePlayerModels[i] );
			engine->ForceModelBounds( TPiratePlayerModels[i], mins, maxs );
		}
	}

	for ( i=0; i<MAX_AGENTS_CT+1; ++i )
	{
		if ( !engine->IsModelPrecached( GetCSAgentInfoCT( i )->m_szModel ) )
		{
			PrecacheModel( GetCSAgentInfoCT( i )->m_szModel );
		}
	}
	for ( i=0; i<MAX_AGENTS_T+1; ++i )
	{
		if ( !engine->IsModelPrecached( GetCSAgentInfoT( i )->m_szModel ) )
		{
			PrecacheModel( GetCSAgentInfoT( i )->m_szModel );
		}
	}

	for ( i=0; i<ARRAYSIZE( s_playerViewmodelArmConfigs ); ++i )
	{
		if ( !engine->IsModelPrecached( s_playerViewmodelArmConfigs[i].szAssociatedGloveModel ) )
			PrecacheModel( s_playerViewmodelArmConfigs[i].szAssociatedGloveModel );

		if ( !engine->IsModelPrecached( s_playerViewmodelArmConfigs[i].szAssociatedSleeveModelGloveOverride ) )
			PrecacheModel( s_playerViewmodelArmConfigs[i].szAssociatedSleeveModelGloveOverride );

		if ( !engine->IsModelPrecached( s_playerViewmodelArmConfigs[i].szAssociatedSleeveModel ) )
			PrecacheModel( s_playerViewmodelArmConfigs[i].szAssociatedSleeveModel );
	}

	for ( i=0; i<MAX_GLOVES+1; ++i)
	{
		if ( !engine->IsModelPrecached( GetGlovesInfo( i )->szViewModel ) )
			PrecacheModel( GetGlovesInfo( i )->szViewModel );
		if ( !engine->IsModelPrecached( GetGlovesInfo( i )->szWorldModel ) )
			PrecacheModel( GetGlovesInfo( i )->szWorldModel );
	}

#ifdef CS_SHIELD_ENABLED
	PrecacheModel( SHIELD_VIEW_MODEL );
#endif

	PrecacheScriptSound( "Player.DeathHeadShot" );
	PrecacheScriptSound( "Player.Death" );
	PrecacheScriptSound( "Player.DeathFem" );
	PrecacheScriptSound( "Player.PickupWeapon" );
	PrecacheScriptSound( "Player.PickupWeaponSilent" );
	PrecacheScriptSound( "Player.DamageHelmet" );
	PrecacheScriptSound( "Player.DamageHeadShot" );
	PrecacheScriptSound( "Flesh.BulletImpact" );
	PrecacheScriptSound( "Player.DamageKevlar" );
	PrecacheScriptSound( "Player.NightVisionOff" );
	PrecacheScriptSound( "Player.NightVisionOn" );
	PrecacheScriptSound( "Player.FlashlightOn" );
	PrecacheScriptSound( "Player.FlashlightOff" );
	PrecacheScriptSound( "HealthShot.Success" );

	// CS Bot sounds
	PrecacheScriptSound( "Bot.StuckSound" );
	PrecacheScriptSound( "Bot.StuckStart" );
	PrecacheScriptSound( "Bot.FellOff" );

	UTIL_PrecacheOther( "item_kevlar" );
	UTIL_PrecacheOther( "item_assaultsuit" );
	UTIL_PrecacheOther( "item_defuser" );

	PrecacheModel ( "sprites/glow01.vmt" );
	PrecacheModel ( "models/items/cs_gift.mdl" );

	BaseClass::Precache();
}


//-----------------------------------------------------------------------------
// Purpose: Allow pre-frame adjustments on the player
//-----------------------------------------------------------------------------
ConVar sv_runcmds( "sv_runcmds", "1" );
void CCSPlayer::PlayerRunCommand( CUserCmd *ucmd, IMoveHelper *moveHelper )
{
	VPROF( "CCSPlayer::PlayerRunCommand" );

	if ( !sv_runcmds.GetInt() )
		return;

	// don't run commands in the future
	if ( !IsEngineThreaded() &&
		( ucmd->tick_count > (gpGlobals->tickcount + sv_max_usercmd_future_ticks.GetInt()) ) )
	{
		DevMsg( "Client cmd out of sync (delta %i).\n", ucmd->tick_count - gpGlobals->tickcount );
		return;
	}

	// If they use a negative bot_mimic value, then don't process their usercmds, but have
	// bots process them instead (so they can stay still and have the bot move around).
	CUserCmd tempCmd;
	if ( -bot_mimic.GetInt() == entindex() )
	{
		tempCmd = *ucmd;
		ucmd = &tempCmd;

		ucmd->forwardmove = ucmd->sidemove = ucmd->upmove = 0;
		ucmd->buttons = 0;
		ucmd->impulse = 0;
	}

	if ( IsBot() && bot_crouch.GetInt() )
		ucmd->buttons |= IN_DUCK;

	if ( IsLookingAtWeapon() )
	{
		if ( (ucmd->buttons & (IN_ATTACK | IN_ATTACK2 | IN_RELOAD)) != 0 /*|| ucmd->forwardmove || ucmd->sidemove || ucmd->upmove*/ )
		{
			StopLookingAtWeapon();

			if ( (ucmd->buttons & IN_ATTACK2) != 0 && (ucmd->buttons & (IN_ATTACK | IN_RELOAD)) == 0 )
			{
				CWeaponCSBase *pWeapon = GetActiveCSWeapon();
				if ( pWeapon && pWeapon->GetWeaponType() == WEAPONTYPE_SNIPER_RIFLE )
				{
					// Force the animation back to idle since changing zoom has no specific animation
					CBaseViewModel *pViewModel = GetViewModel();
					if ( pViewModel )
					{
						int nSequence = pViewModel->LookupSequence( "idle" );

						if ( nSequence != ACTIVITY_NOT_AVAILABLE )
						{
							pViewModel->ForceCycle( 0 );
							pViewModel->ResetSequence( nSequence );
						}
					}
				}
			}
		}
	}

	BaseClass::PlayerRunCommand( ucmd, moveHelper );
}


bool CCSPlayer::RunMimicCommand( CUserCmd& cmd )
{
	if ( !IsBot() )
		return false;

	int iMimic = abs( bot_mimic.GetInt() );
	if ( iMimic > gpGlobals->maxClients )
		return false;

	CBasePlayer *pPlayer = UTIL_PlayerByIndex( iMimic );
	if ( !pPlayer )
		return false;

	if ( !pPlayer->GetLastUserCommand() )
		return false;

	cmd = *pPlayer->GetLastUserCommand();
	cmd.viewangles[YAW] += bot_mimic_yaw_offset.GetFloat();

	pl.fixangle = FIXANGLE_NONE;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Simulates a single frame of movement for a player
//-----------------------------------------------------------------------------
void CCSPlayer::RunPlayerMove( const QAngle& viewangles, float forwardmove, float sidemove, float upmove, unsigned short buttons, byte impulse, float frametime )
{
	CUserCmd cmd;

	// Store off the globals.. they're gonna get whacked
	float flOldFrametime = gpGlobals->frametime;
	float flOldCurtime = gpGlobals->curtime;

	float flTimeBase = gpGlobals->curtime + gpGlobals->frametime - frametime;
	this->SetTimeBase( flTimeBase );

	CUserCmd lastUserCmd = *GetLastUserCommand();
	Q_memset( &cmd, 0, sizeof( cmd ) );

	if ( !RunMimicCommand( cmd ) )
	{
		cmd.forwardmove = forwardmove;
		cmd.sidemove = sidemove;
		cmd.upmove = upmove;
		cmd.buttons = buttons;
		cmd.impulse = impulse;

		VectorCopy( viewangles, cmd.viewangles );
		cmd.random_seed = random->RandomInt( 0, 0x7fffffff );
	}

	MoveHelperServer()->SetHost( this );
	PlayerRunCommand( &cmd, MoveHelperServer() );

	// save off the last good usercmd
	if ( -bot_mimic.GetInt() == entindex() )
	{
		CUserCmd lastCmd = *GetLastUserCommand();
		lastCmd.command_number = cmd.command_number;
		lastCmd.tick_count = cmd.tick_count;
		SetLastUserCommand( lastCmd );
	}
	else
	{
		SetLastUserCommand( cmd );
	}

	// Clear out any fixangle that has been set
	pl.fixangle = FIXANGLE_NONE;

	// Restore the globals..
	gpGlobals->frametime = flOldFrametime;
	gpGlobals->curtime = flOldCurtime;

	MoveHelperServer()->SetHost( NULL );
}


void CCSPlayer::InitialSpawn( void )
{
	BaseClass::InitialSpawn();

	// we're going to give the bots money here instead of FinishClientPutInServer()
	// because of the bots' timing for purchasing weapons/items.
	if ( IsBot() )
	{
		m_iAccount = CSGameRules()->GetStartMoney();
	}
	/*
	if ( !engine->IsDedicatedServer() && TheNavMesh->IsOutOfDate() && this == UTIL_GetListenServerHost() )
	{
		ClientPrint( this, HUD_PRINTCENTER, "The Navigation Mesh was built using a different version of this map." );
	}*/

	State_Enter( STATE_WELCOME );

	//=============================================================================
	// HPE_BEGIN:
	// [tj] We reset the stats at the beginning of the map (including domination tracking)
	//=============================================================================
	 
	CCS_GameStats.ResetPlayerStats(this);
	RemoveNemesisRelationships();
	 
	//=============================================================================
	// HPE_END
	//=============================================================================
	
}

void CCSPlayer::SetModelFromClass( void )
{
	if ( HasAgentSet( GetTeamNumber() ) && !IsBotOrControllingBot() )
	{
		if ( GetTeamNumber() == TEAM_CT )
		{
			SetModel( GetCSAgentInfoCT( GetAgentID( GetTeamNumber() ) )->m_szModel );
			return;
		}
		else if ( GetTeamNumber() == TEAM_TERRORIST )
		{
			SetModel( GetCSAgentInfoT( GetAgentID( GetTeamNumber() ) )->m_szModel );
			return;
		}
	}

	if ( GetTeamNumber() == TEAM_TERRORIST )
	{
		int index = m_iClass - FIRST_T_CLASS;
		if ( index < 0 || index >= LAST_T_CLASS )
		{
			index = RandomInt( 0, LAST_T_CLASS - 1 );
			m_iClass = index + FIRST_T_CLASS; // clean up players who selected a higher class than we support yet
		}
		
		switch ( m_iClass )
		{
			case CS_CLASS_PHOENIX_CONNNECTION:
			{
				SetModel( TPhoenixPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_L337_KREW:
			{
				SetModel( TLeetPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_SEPARATIST:
			{
				SetModel( TSeparatistPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_BALKAN:
			{
				SetModel( TBalkanPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_PROFESSIONAL:
			{
				SetModel( TProfessionalPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_ANARCHIST:
			{
				SetModel( TAnarchistPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_PIRATE:
			{
				SetModel( TPiratePlayerModels[m_iSkin] );
				break;
			}
			default:
			{
				Assert( false ); // we shouldn't be here
				break;
			}
		}
	}
	else if ( GetTeamNumber() == TEAM_CT )
	{
		int index = m_iClass - FIRST_CT_CLASS;
		if ( index < 0 || index >= (LAST_CT_CLASS - FIRST_CT_CLASS + 1) )
		{
			index = RandomInt( 0, LAST_CT_CLASS - FIRST_CT_CLASS );
			m_iClass = index + FIRST_CT_CLASS; // clean up players who selected a higher class than we support yet
		}

		switch ( m_iClass )
		{
			case CS_CLASS_SEAL_TEAM_6:
			{
				SetModel( CTST6PlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_GSG_9:
			{
				SetModel( CTGSG9PlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_SAS:
			{
				SetModel( CTSASPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_GIGN:
			{
				SetModel( CTGIGNPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_FBI:
			{
				SetModel( CTFBIPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_IDF:
			{
				SetModel( CTIDFPlayerModels[m_iSkin] );
				break;
			}
			case CS_CLASS_SWAT:
			{
				SetModel( CTSWATPlayerModels[m_iSkin] );
				break;
			}
			default:
			{
				Assert( false ); // we shouldn't be here
				break;
			}
		}
	}
	else
	{
		// todo: can we actually get here?
		Assert( false ); // we shouldn't be here
		//SetModel( CTST6PlayerModels[0] );
	}
}

void CCSPlayer::SetCSSpawnLocation( Vector position, QAngle angle )
{
	m_storedSpawnPosition = position;
	m_storedSpawnAngle = angle;
}

void CCSPlayer::Spawn()
{
	m_iLoadoutSlotKnifeWeaponCT = atoi( engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "loadout_slot_knife_weapon_ct" ) );
	m_iLoadoutSlotKnifeWeaponT = atoi( engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "loadout_slot_knife_weapon_t" ) );
	
	if ( m_bNeedToChangeAgent )
	{
		m_iLoadoutSlotAgentCT = atoi( engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "loadout_slot_agent_ct" ) );
		m_iLoadoutSlotAgentT = atoi( engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "loadout_slot_agent_t" ) );
		m_bNeedToChangeAgent = false;
	}
	if ( m_bNeedToChangeGloves )
	{
		m_iLoadoutSlotGlovesCT = atoi( engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "loadout_slot_gloves_ct" ) );
		m_iLoadoutSlotGlovesT = atoi( engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "loadout_slot_gloves_t" ) );
		m_bNeedToChangeGloves = false;
	}

	m_RateLimitLastCommandTimes.Purge();

	// Get rid of the progress bar...
	SetProgressBarTime( 0 );

	// CreateViewModel( 1 );

	// we need to do that because player can change their agent but the class won't
	// change and it won't change arms as well
	if ( HasAgentSet( GetTeamNumber() ) && !IsBotOrControllingBot() )
	{
		if ( GetTeamNumber() == TEAM_CT )
			m_iClass = GetCSAgentInfoCT( GetAgentID( GetTeamNumber() ) )->m_iClass;
		if ( GetTeamNumber() == TEAM_TERRORIST )
			m_iClass = GetCSAgentInfoT( GetAgentID( GetTeamNumber() ) )->m_iClass;
	}
	// PiMoN: placing it here since the server can change the varriable mid-game
	else if ( CSGameRules()->UseMapFactionsForThisPlayer(this) && CSGameRules()->GetMapFactionsForThisPlayer(this) > -1 )
	{
		m_iClass = CSGameRules()->GetMapFactionsForThisPlayer(this);
	}

	// Set their player model.
	SetModelFromClass();

	BaseClass::Spawn();

	//=============================================================================
	// HPE_BEGIN:
	// [pfreese] Clear the last known nav area (used to be done by CBasePlayer)
	//=============================================================================
	
	m_lastNavArea = NULL;
	
	//=============================================================================
	// HPE_END
	//=============================================================================

	AddFlag(FL_ONGROUND); // set the player on the ground at the start of the round.

	// Override what CBasePlayer set for the view offset.
	SetViewOffset( VEC_VIEW_SCALED( this ) );

	//
	// Our player movement speed is set once here. This will override the cl_xxxx
	// cvars unless they are set to be lower than this.
	//
	SetMaxSpeed( CS_PLAYER_SPEED_RUN );

	SetFOV( this, 0 );

	m_bIsDefusing = false;
	m_bIsGrabbingHostage = false;

	m_bIsWalking = false;

	// [dwenger] Reset hostage-related variables
	m_bIsRescuing = false;
	m_bInjuredAHostage = false;
	m_iNumFollowers = 0;
	
	// [tj] Reset this flag if the player is not in observer mode (as happens when a player spawns late)
	if (m_iPlayerState != STATE_OBSERVER_MODE)
	{
		m_wasNotKilledNaturally = false;
	}

	m_iShotsFired = 0;
	m_iDirection = 0;

	if ( m_pHintMessageQueue )
	{
		m_pHintMessageQueue->Reset();
	}
	m_iDisplayHistoryBits &= ~DHM_ROUND_CLEAR;

	// Special-case here. A bunch of things happen in CBasePlayer::Spawn(), and we really want the
	// player states to control these things, so give whatever player state we're in a chance
	// to reinitialize itself.
	State_Transition( m_iPlayerState );

	ClearFlashbangScreenFade();

	m_flVelocityModifier = 1.0f;
	m_flGroundAccelLinearFracLastTime = 0.0f;

	ResetStamina();

	m_flLastRadarUpdateTime = 0.0f;

	m_fNextMolotovDamageSoundTime = 0.0f;

	m_iNumSpawns++;
	/*
	if ( !engine->IsDedicatedServer() && CSGameRules()->m_iTotalRoundsPlayed < 2 && TheNavMesh->IsOutOfDate() && this == UTIL_GetListenServerHost() )
	{
		ClientPrint( this, HUD_PRINTCENTER, "The Navigation Mesh was built using a different version of this map." );
	}
	*/
	m_bTeamChanged	= false;
	m_iOldTeam = TEAM_UNASSIGNED;

	m_bHasMovedSinceSpawn = false;

	m_iRadioMessages = 60;
	m_flRadioTime = gpGlobals->curtime;

	if ( m_hRagdoll )
	{
		UTIL_Remove( m_hRagdoll );
	}

	m_hRagdoll = NULL;

	// did we change our name while we were dead?
	if ( m_szNewName[0] != 0 )
	{
		ChangeName( m_szNewName );
		m_szNewName[0] = 0;
	}

	if ( m_bIsVIP )
	{
		HintMessage( "#Hint_you_are_the_vip", true, true );
	}

	m_bIsInAutoBuy = false;
	m_bIsInRebuy = false;
	m_bAutoReload = false;

	SetContextThink( &CCSPlayer::PushawayThink, gpGlobals->curtime + PUSHAWAY_THINK_INTERVAL, CS_PUSHAWAY_THINK_CONTEXT );

	if ( GetActiveWeapon() && !IsObserver() )
	{
		GetActiveWeapon()->Deploy();
		m_flNextAttack = gpGlobals->curtime; // Allow reloads to finish, since we're playing the deploy anim instead.  This mimics goldsrc behavior, anyway.
	}

	m_applyDeafnessTime = 0.0f;

	m_cycleLatch = 0;
	if ( !m_bUseNewAnimstate )
		m_cycleLatchTimer.Start( RandomFloat( 0.0f, CycleLatchInterval ) );

	StockPlayerAmmo();

	// Calculate timeout for immunity
	float flImmuneTime = mp_respawn_immunitytime.GetFloat();

	if ( flImmuneTime > 0 || CSGameRules()->IsWarmupPeriod() )
	{
		if ( CSGameRules()->IsWarmupPeriod() )
		{
			flImmuneTime = 3;
		}

		m_fImmuneToDamageTime = gpGlobals->curtime + flImmuneTime;
		m_bImmunity = true;
	}
	else
	{
		m_fImmuneToDamageTime = 0.0f;
		m_bImmunity = false;
	}

	m_bKilledByTaser = false;

	StopLookingAtWeapon();
	m_bIsHoldingLookAtWeapon = false;

	m_bDuckOverride = false;

	// If we're constantly respawning then reset damage stats on spawn. Otherwise this'll happen on roundrespawn after damage is reported.
	if ( IsAbleToInstantRespawn() )
	{
		ResetDamageCounters();
		RemoveSelfFromOthersDamageCounters();
	}

	// clear out and carried hostage stuff
	RemoveCarriedHostage();

	if ( m_bUseNewAnimstate && m_PlayerAnimStateCSGO )
	{
		m_PlayerAnimStateCSGO->Reset();
		m_PlayerAnimStateCSGO->Update( EyeAngles()[YAW], EyeAngles()[PITCH], true );
		DoAnimationEvent( PLAYERANIMEVENT_DEPLOY ); // re-deploy default weapon when spawning
	}
	
	if ( GetTeamNumber() == TEAM_CT )
		m_bIsFemale = (HasAgentSet( TEAM_CT )) ? (GetCSAgentInfoCT( GetAgentID( TEAM_CT ) )->m_bIsFemale) : false;
	else
		m_bIsFemale = (HasAgentSet( TEAM_TERRORIST )) ? (GetCSAgentInfoT( GetAgentID( TEAM_TERRORIST ) )->m_bIsFemale) : false;
}

void CCSPlayer::ShowViewPortPanel( const char * name, bool bShow, KeyValues *data )
{
	if ( CSGameRules()->IsLogoMap() )
		return;

	if ( CommandLine()->FindParm("-makedevshots") )
		return;

	BaseClass::ShowViewPortPanel( name, bShow, data );
}

void CCSPlayer::ClearFlashbangScreenFade( void )
{
	if( IsBlind() )
	{
		color32 clr = { 0, 0, 0, 0 };
		UTIL_ScreenFade( this, clr, 0.01, 0.0, FFADE_OUT | FFADE_PURGE );

		m_flFlashDuration = 0.0f;
		m_flFlashMaxAlpha = 255.0f;
	}

	// clear blind time (after screen fades are canceled)
	m_blindUntilTime = 0.0f;
	m_blindStartTime = 0.0f;
}

void CCSPlayer::GiveDefaultItems()
{
	if ( State_Get() != STATE_ACTIVE )
		return;

#if CS_CONTROLLABLE_BOTS_ENABLED
	if ( m_bIsControllingBot )
		return;
#endif

	// Always give the player the knife.
	CBaseCombatWeapon *pistol = Weapon_GetSlot( WEAPON_SLOT_PISTOL );

	if ( GetTeamNumber() == TEAM_CT )
	{
		if ( m_iLoadoutSlotKnifeWeaponCT == 0 || IsBotOrControllingBot() )
			GiveNamedItem( "weapon_knife" );
		else
			GiveNamedItem( KnivesEntities[m_iLoadoutSlotKnifeWeaponCT + 1] );

		if ( !pistol )
		{
			if ( IsBot() )
				GiveNamedItem( "weapon_hkp2000" );
			else
			{
				char weapon[32];
				Q_snprintf( weapon, sizeof( weapon ), "weapon_%s", CSLoadout()->GetWeaponFromSlot( this, SLOT_HKP2000 ) );
				GiveNamedItem( weapon );
			}
			m_bUsingDefaultPistol = true;
		}
	}
	else if ( GetTeamNumber() == TEAM_TERRORIST )
	{
		if ( m_iLoadoutSlotKnifeWeaponT == 0 || IsBotOrControllingBot() )
			GiveNamedItem( "weapon_knife_t" );
		else
			GiveNamedItem( KnivesEntities[m_iLoadoutSlotKnifeWeaponT + 1] );

		if ( !pistol )
		{
			GiveNamedItem( "weapon_glock" );
			m_bUsingDefaultPistol = true;
		}
	}

	if ( Weapon_GetSlot( WEAPON_SLOT_PISTOL ) )
	{
		Weapon_GetSlot( WEAPON_SLOT_PISTOL )->GiveReserveAmmo( AMMO_POSITION_PRIMARY, 250 );
	}

	if ( Weapon_GetSlot( WEAPON_SLOT_RIFLE ) )
	{
		Weapon_GetSlot( WEAPON_SLOT_RIFLE )->GiveReserveAmmo( AMMO_POSITION_PRIMARY, 250 );
	}

	if ( CSGameRules()->IsArmorFree() )
		GiveNamedItem( "item_assaultsuit" );
}

void CCSPlayer::SetClanTag( const char *pTag )
{
	if ( pTag )
	{
		Q_strncpy( m_szClanTag, pTag, sizeof( m_szClanTag ) );
	}
}
void CCSPlayer::CreateRagdollEntity()
{
	// If we already have a ragdoll, don't make another one.
	CCSRagdoll *pRagdoll = dynamic_cast< CCSRagdoll* >( m_hRagdoll.Get() );

	if ( !pRagdoll )
	{
		// create a new one
		pRagdoll = dynamic_cast< CCSRagdoll* >( CreateEntityByName( "cs_ragdoll" ) );
	}

	if ( pRagdoll )
	{
		pRagdoll->m_hPlayer = this;
		pRagdoll->m_vecRagdollOrigin = GetAbsOrigin();
		pRagdoll->m_vecRagdollVelocity = GetAbsVelocity();
		pRagdoll->m_nModelIndex = m_nModelIndex;
		pRagdoll->m_nForceBone = m_nForceBone;
		pRagdoll->m_vecForce = m_vecTotalBulletForce;
		pRagdoll->m_iDeathPose = m_iDeathPose;
		pRagdoll->m_iDeathFrame = m_iDeathFrame;
		pRagdoll->m_flDeathYaw = m_flDeathYaw;
		pRagdoll->Init();
	}

	// ragdolls will be removed on round restart automatically
	m_hRagdoll = pRagdoll;
}

int CCSPlayer::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	if ( m_bImmunity )
	{
		// No damage if immune
		return 0;
	}

	// set damage type sustained
	m_bitsDamageType |= info.GetDamageType();

	if ( !CBaseCombatCharacter::OnTakeDamage_Alive( info ) )
		return 0;

	// don't apply damage forces in CS

	// fire global game event

	IGameEvent * event = gameeventmanager->CreateEvent( "player_hurt" );

	if ( event )
	{
		event->SetInt("userid", GetUserID() );
		event->SetInt("health", MAX(0, m_iHealth) );
		event->SetInt("armor", MAX(0, ArmorValue()) );

		event->SetInt( "dmg_health", m_lastDamageHealth );
		event->SetInt( "dmg_armor", m_lastDamageArmor );

		if ( info.GetDamageType() & DMG_BLAST )
		{
			event->SetInt( "hitgroup", HITGROUP_GENERIC );
		}
		else
		{
			event->SetInt( "hitgroup", m_LastHitGroup );
		}

		CBaseEntity * attacker = info.GetAttacker();
		const char *weaponName = "";

		if ( attacker->IsPlayer() )
		{
			CBasePlayer *player = ToBasePlayer( attacker );
			event->SetInt("attacker", player->GetUserID() ); // hurt by other player

			CBaseEntity *pInflictor = info.GetInflictor();
			if ( pInflictor )
			{
				if ( pInflictor == player )
				{
					// If the inflictor is the killer,  then it must be their current weapon doing the damage
					if ( player->GetActiveWeapon() )
					{
						weaponName = player->GetActiveWeapon()->GetClassname();
					}
				}
				else
				{
					weaponName = STRING( pInflictor->m_iClassname );  // it's just that easy
				}
			}
		}
		else
		{
			event->SetInt("attacker", 0 ); // hurt by "world"
		}

		if ( strncmp( weaponName, "weapon_", 7 ) == 0 )
		{
			weaponName += 7;
		}
		else if( strncmp( weaponName, "hegrenade", 9 ) == 0 )	//"hegrenade_projectile"
		{
			//=============================================================================
			// HPE_BEGIN:
			// [tj] Handle grenade-surviving achievement
			//=============================================================================
			if (info.GetAttacker()->GetTeamNumber() != GetTeamNumber())
			{
				m_grenadeDamageTakenThisRound += info.GetDamage();
			}
			//=============================================================================
			// HPE_END
			//=============================================================================

			weaponName = "hegrenade";
		}
		else if( strncmp( weaponName, "flashbang", 9 ) == 0 )	//"flashbang_projectile"
		{
			weaponName = "flashbang";
		}
		else if( strncmp( weaponName, "smokegrenade", 12 ) == 0 )	//"smokegrenade_projectile"
		{
			weaponName = "smokegrenade";
		}
		else if( strncmp( weaponName, "decoy", 5 ) == 0 )	//"decoy_projectile"
		{
			weaponName = "decoy";
		}
		else if( strncmp( weaponName, "molotov", 7 ) == 0 )	//"decoy_projectile"
		{
			CMolotovProjectile *pMolotovProjectile = dynamic_cast<CMolotovProjectile*>(info.GetInflictor());
			if ( pMolotovProjectile )
			{
				if ( pMolotovProjectile->IsIncGrenade() )
					weaponName = "incgrenade";
				else
					weaponName = "molotov";
			}
		}

		event->SetString( "weapon", weaponName );
		event->SetInt( "priority", 5 );

		gameeventmanager->FireEvent( event );
	}

	return 1;
}

// [dwenger] Supports fun-fact
// Returns the % of the enemies this player killed in the round
int CCSPlayer::GetPercentageOfEnemyTeamKilled()
{
	if ( m_NumEnemiesAtRoundStart > 0 )
	{
		return (int)( ( (float)m_NumEnemiesKilledThisRound / (float)m_NumEnemiesAtRoundStart ) * 100.0f );
	}

	return 0;
}

void CCSPlayer::Event_Killed( const CTakeDamageInfo &info )
{
	// [pfreese] Process on-death achievements
	ProcessPlayerDeathAchievements(ToCSPlayer(info.GetAttacker()), this, info);

	SetArmorValue( 0 );

	// [tj] Added a parameter so we know if it was death that caused the drop
	// [menglish] Keep track of what the player has dropped for the freeze panel callouts
	CBaseEntity* pAttacker = info.GetAttacker();
	bool friendlyFire = pAttacker && pAttacker->GetTeamNumber() == GetTeamNumber();

	CCSPlayer* pAttackerPlayer = ToCSPlayer( info.GetAttacker() );
	if ( pAttackerPlayer )
	{
		CWeaponCSBase* pAttackerWeapon = dynamic_cast< CWeaponCSBase * >( pAttackerPlayer->GetActiveCSWeapon() );	// this can be NULL if the kill is by HE/molly/impact/etc. (inflictor is non-NULL and points to grenade then)

		// killed by a taser?
		if ( pAttackerWeapon && pAttackerWeapon->IsA( WEAPON_TASER ) )
		{
			m_bKilledByTaser = true;
		}
	}

	//Only count the drop if it was not friendly fire
	DropWeapons(true, !friendlyFire);
	

	// Just in case the progress bar is on screen, kill it.
	SetProgressBarTime( 0 );

	m_bIsDefusing = false;
	m_bIsGrabbingHostage = false;

	m_bHasNightVision = false;
	m_bNightVisionOn = false;

	// [dwenger] Added for fun-fact support

	m_bPickedUpDefuser = false;
	m_bDefusedWithPickedUpKit = false;

	m_nPreferredGrenadeDrop = 0;

	m_bHasHelmet = false;

	m_flFlashDuration = 0.0f;

	FlashlightTurnOff();

	// show killer in death cam mode
	if( IsValidObserverTarget( info.GetAttacker() ) )
	{
		SetObserverTarget( info.GetAttacker() );
	}
	else
	{
		ResetObserverMode();
	}

	//update damage info with our accumulated physics force
	CTakeDamageInfo subinfo = info;

	// HACK[pfreese]: scale impulse up for visual effect
	const float kImpulseBonusScale = 2.0f;
	subinfo.SetDamageForce( m_vecTotalBulletForce * kImpulseBonusScale);

	//Adrian: Select a death pose to extrapolate the ragdoll's velocity.
	SelectDeathPose( info );

	// See if there's a ragdoll magnet that should influence our force.
	CRagdollMagnet *pMagnet = CRagdollMagnet::FindBestMagnet( this );
	if( pMagnet )
	{
		m_vecTotalBulletForce += pMagnet->GetForceVector( this );
	}

	// Note: since we're dead, it won't draw us on the client, but we don't set EF_NODRAW
	// because we still want to transmit to the clients in our PVS.
	CreateRagdollEntity();

	// Special code to drop holiday gifts for the holiday achievement
	if ( ( mp_holiday_nogifts.GetBool() == false ) && UTIL_IsHolidayActive( 3 /*kHoliday_Christmas*/ ) )
	{
		if ( RandomInt( 0, 100 ) < 20 )
		{
			CHolidayGift::Create( WorldSpaceCenter(), GetAbsAngles(), EyeAngles(), GetAbsVelocity(), this );
		}
	}

	State_Transition( STATE_DEATH_ANIM );	// Transition into the dying state.
	BaseClass::Event_Killed( subinfo );

	// [pfreese] If this kill ended the round, award the MVP to someone on the
	// winning team.
	// TODO - move this code somewhere else more MVP related
	bool roundWasAlreadyWon = (CSGameRules()->m_iRoundWinStatus != WINNER_NONE);
	bool roundIsWonNow = CSGameRules()->CheckWinConditions();

	if ( !roundWasAlreadyWon && roundIsWonNow )
	{
		CCSPlayer* pMVP = NULL;
		int maxKills = 0;
		int maxDamage = 0;

		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CCSPlayer* pPlayer = ToCSPlayer( UTIL_PlayerByIndex( i ) );
			if ( pPlayer )
			{
				// only consider players on the winning team
				if ( pPlayer->GetTeamNumber() != CSGameRules()->m_iRoundWinStatus )
					continue;

				int nKills = CCS_GameStats.FindPlayerStats( pPlayer ).statsCurrentRound[CSSTAT_KILLS];
				int nDamage = CCS_GameStats.FindPlayerStats( pPlayer ).statsCurrentRound[CSSTAT_DAMAGE];

				if ( nKills > maxKills || ( nKills == maxKills && nDamage > maxDamage ) )
				{
					pMVP = pPlayer;
					maxKills = nKills;
					maxDamage = nDamage;
				}
			}
		}

		if ( pMVP )
		{
			pMVP->IncrementNumMVPs( CSMVP_ELIMINATION );
		}
	}

	OutputDamageGiven();
	OutputDamageTaken();
	ResetDamageCounters();

	if ( m_bPunishedForTK )
	{
		m_bPunishedForTK = false;
		HintMessage( "#Hint_cannot_play_because_tk", true, true );
	}

	if ( !(m_iDisplayHistoryBits & DHF_SPEC_DUCK) )
	{
		m_iDisplayHistoryBits |= DHF_SPEC_DUCK;
		HintMessage( "#Spec_Duck", true, true );
	}

#if CS_CONTROLLABLE_BOTS_ENABLED
	if ( IsControllingBot() )	// Should this be here, or at the top?
	{
		ReleaseControlOfBot();
	}
#endif
}

// [menglish, tj] Update and check any one-off achievements based on the kill
// Notify that I've killed some other entity. (called from Victim's Event_Killed).
void CCSPlayer::Event_KilledOther( CBaseEntity *pVictim, const CTakeDamageInfo &info )
{
	BaseClass::Event_KilledOther(pVictim, info);
}


void CCSPlayer::DeathSound( const CTakeDamageInfo &info )
{
	if( m_LastHitGroup == HITGROUP_HEAD )
	{
		EmitSound( "Player.DeathHeadShot" );
	}
	else
	{
		if ( m_bIsFemale )
		{
			EmitSound( "Player.DeathFem" );
			return;
		}

		EmitSound( "Player.Death" );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCSPlayer::InitVCollision( const Vector &vecAbsOrigin, const Vector &vecAbsVelocity )
{
	BaseClass::InitVCollision( vecAbsOrigin, vecAbsVelocity );

	if ( sv_turbophysics.GetBool() )
		return;

	// Setup the HL2 specific callback.
	GetPhysicsController()->SetEventHandler( &playerCallback );
}

void CCSPlayer::VPhysicsShadowUpdate( IPhysicsObject *pPhysics )
{
	if ( !CanMove() )
		return;

	BaseClass::VPhysicsShadowUpdate( pPhysics );
}

bool CCSPlayer::HasShield() const
{
#ifdef CS_SHIELD_ENABLED
	return m_bHasShield;
#else
	return false;
#endif
}


bool CCSPlayer::IsShieldDrawn() const
{
#ifdef CS_SHIELD_ENABLED
	return m_bShieldDrawn;
#else
	return false;
#endif
}


void CCSPlayer::CheatImpulseCommands( int iImpulse )
{
	switch( iImpulse )
	{
		case 101:
		{
			if ( sv_cheats->GetBool() )
			{
				extern int gEvilImpulse101;
				gEvilImpulse101 = true;

				AddAccount( mp_maxmoney.GetInt() );

				for ( int i = 0; i < MAX_WEAPONS; ++i )
				{
					CBaseCombatWeapon *pWeapon = GetWeapon( i );
					if ( pWeapon )
					{
						pWeapon->GiveReserveAmmo( AMMO_POSITION_PRIMARY, 999 );
						pWeapon->GiveReserveAmmo( AMMO_POSITION_SECONDARY, 999 );
					}
				}

				gEvilImpulse101 = false;
			}
		}
		break;

		default:
		{
			BaseClass::CheatImpulseCommands( iImpulse );
		}
	}
}

void CCSPlayer::SetupVisibility( CBaseEntity *pViewEntity, unsigned char *pvs, int pvssize )
{
	BaseClass::SetupVisibility( pViewEntity, pvs, pvssize );

	int area = pViewEntity ? pViewEntity->NetworkProp()->AreaNum() : NetworkProp()->AreaNum();
	PointCameraSetupVisibility( this, area, pvs, pvssize );
}

CBaseEntity* CCSPlayer::FindNextObserverTarget( bool bReverse )
{
	CBaseEntity* pTarget = BaseClass::FindNextObserverTarget( bReverse );

	// [jason] If we have no valid targets left (eg. last teammate dies in competitive mode )
	//	then try to place the camera near any planted bomb 
	/*if ( !pTarget )
	{		
		if ( g_PlantedC4s.Count() > 0 )
		{
			// Immediately change the observer target, so we can handle SetObserverMode appropriately
			SetObserverTarget( g_PlantedC4s[0] );

			// [mbooth] free roaming spectator is useful for testing
			if ( GetObserverMode() != OBS_MODE_ROAMING )
			{
				// Allow the camera to pivot
				SetObserverMode( OBS_MODE_CHASE );
			}

			return g_PlantedC4s[0];
		}
	}*/

	return pTarget;
}

void CCSPlayer::UpdateAddonBits()
{
	int iNewBits = 0;

	//it's ok to show the active weapon as a holstered weapon if it's not yet visible (still deploying)
	CBaseCombatWeapon *pActiveWeapon = GetActiveWeapon();
	bool bActiveWeaponIsVisible = true;
	if ( pActiveWeapon && pActiveWeapon->GetWeaponWorldModel() )
	{
		bActiveWeaponIsVisible = !pActiveWeapon->GetWeaponWorldModel()->IsEffectActive( EF_NODRAW );
	}

	int nFlashbang = GetAmmoCount( GetAmmoDef()->Index( AMMO_TYPE_FLASHBANG ) );
	if ( dynamic_cast< CFlashbang* >( GetActiveWeapon() ) && bActiveWeaponIsVisible )
	{
		--nFlashbang;
	}

	if ( nFlashbang >= 1 )
		iNewBits |= ADDON_FLASHBANG_1;

	if ( nFlashbang >= 2 )
		iNewBits |= ADDON_FLASHBANG_2;

	if ( GetAmmoCount( GetAmmoDef()->Index( AMMO_TYPE_HEGRENADE ) ) &&
		( !dynamic_cast< CHEGrenade* >( GetActiveWeapon() ) || !bActiveWeaponIsVisible ) )
	{
		iNewBits |= ADDON_HE_GRENADE;
	}

	if ( GetAmmoCount( GetAmmoDef()->Index( AMMO_TYPE_SMOKEGRENADE ) ) &&
		( !dynamic_cast< CSmokeGrenade* >( GetActiveWeapon() ) || !bActiveWeaponIsVisible ) )
	{
		iNewBits |= ADDON_SMOKE_GRENADE;
	}

	if ( GetAmmoCount( GetAmmoDef()->Index( AMMO_TYPE_DECOY ) ) &&
		( !dynamic_cast< CDecoyGrenade* >( GetActiveWeapon() ) || !bActiveWeaponIsVisible ) )
	{
		iNewBits |= ADDON_DECOY;
	}
	
	if ( HasC4() && ( !dynamic_cast< CC4* >( GetActiveWeapon() ) || !bActiveWeaponIsVisible ) )
		iNewBits |= ADDON_C4;

	if ( HasDefuser() )
		iNewBits |= ADDON_DEFUSEKIT;

	CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >(Weapon_GetSlot( WEAPON_SLOT_RIFLE ));
	if ( weapon && ( weapon != GetActiveWeapon() || !bActiveWeaponIsVisible ) )
	{
		iNewBits |= ADDON_PRIMARY;
		m_iPrimaryAddon = weapon->GetWeaponID();
	}
	else
	{
		m_iPrimaryAddon = WEAPON_NONE;
	}

	weapon = dynamic_cast< CWeaponCSBase * >(Weapon_GetSlot( WEAPON_SLOT_PISTOL ));
	if ( weapon && ( weapon != GetActiveWeapon() || !bActiveWeaponIsVisible ) )
	{
		iNewBits |= ADDON_PISTOL;
		if ( weapon->GetWeaponID() == WEAPON_ELITE )
		{
			iNewBits |= ADDON_PISTOL2;
		}
		m_iSecondaryAddon = weapon->GetWeaponID();
	}
	else if ( weapon && weapon->GetWeaponID() == WEAPON_ELITE )
	{
		// The active weapon is weapon_elite.  Set ADDON_PISTOL2 without ADDON_PISTOL, so we know
		// to display the empty holster.
		iNewBits |= ADDON_PISTOL2;
		m_iSecondaryAddon = weapon->GetWeaponID();
	}
	else
	{
		m_iSecondaryAddon = WEAPON_NONE;
	}

	weapon = dynamic_cast< CWeaponCSBase * >(Weapon_GetSlot( WEAPON_SLOT_KNIFE ));
	if ( weapon && (weapon != GetActiveWeapon() || !bActiveWeaponIsVisible) )
	{
		iNewBits |= ADDON_KNIFE;
		m_iKnifeAddon = weapon->GetWeaponID();
	}
	else
	{
		m_iKnifeAddon = WEAPON_NONE;
	}

	m_iAddonBits = iNewBits;
}

void CCSPlayer::UpdateRadar()
{
	// update once a second
	if ( (m_flLastRadarUpdateTime + 1.0) > gpGlobals->curtime )
		return;

	m_flLastRadarUpdateTime = gpGlobals->curtime;

	// update positions of all players outside of my PVS
	CBitVec< ABSOLUTE_PLAYER_LIMIT > playerbits;
	engine->Message_DetermineMulticastRecipients( false, EyePosition(), playerbits );

	CSingleUserRecipientFilter user( this );
	UserMessageBegin( user, "UpdateRadar" );

	for ( int i=0; i < MAX_PLAYERS; i++ )
	{
		CCSPlayer *pPlayer = ToCSPlayer( UTIL_PlayerByIndex( i+1 ) );

		if ( !pPlayer )
			continue; // nothing there

		bool bSameTeam = pPlayer->GetTeamNumber() == GetTeamNumber();

		if ( playerbits.Get(i) && bSameTeam == true )
			continue; // this player is in my PVS and not in my team, don't update radar pos

		if ( pPlayer == this )
			continue;

		if ( !pPlayer->IsAlive() || pPlayer->IsObserver() || !pPlayer->IsConnected() )
			continue; // don't update specattors or dead players

		WRITE_BYTE( i+1 ); // player index as entity
		WRITE_SBITLONG( pPlayer->GetAbsOrigin().x/4, COORD_INTEGER_BITS-1 );
		WRITE_SBITLONG( pPlayer->GetAbsOrigin().y/4, COORD_INTEGER_BITS-1 );
		WRITE_SBITLONG( pPlayer->GetAbsOrigin().z/4, COORD_INTEGER_BITS-1 );
		WRITE_SBITLONG(  AngleNormalize( pPlayer->GetAbsAngles().y ), 9 );
	}

	WRITE_BYTE( 0 ); // end marker

	MessageEnd();
}

void CCSPlayer::UpdateMouseoverHints()
{
	if ( IsBlind() || IsObserver() )
		return;

	Vector forward, up;
	EyeVectors( &forward, NULL, &up );

	trace_t tr;
	// Search for objects in a sphere (tests for entities that are not solid, yet still useable)
	Vector searchStart = EyePosition();
	Vector searchEnd = searchStart + forward * 2048;

	int useableContents = MASK_NPCSOLID_BRUSHONLY | MASK_VISIBLE_AND_NPCS;

	UTIL_TraceLine( searchStart, searchEnd, useableContents, this, COLLISION_GROUP_NONE, &tr );

	if ( tr.fraction != 1.0f )
	{
		if (tr.DidHitNonWorldEntity() && tr.m_pEnt)
		{
			CBaseEntity *pObject = tr.m_pEnt;
			switch ( pObject->Classify() )
			{
			case CLASS_PLAYER:
				{
					const float grenadeBloat = 1.2f; // Be conservative in estimating what a player can distinguish
					if ( !TheBots->IsLineBlockedBySmoke( EyePosition(), pObject->EyePosition(), grenadeBloat ) )
					{
						if ( g_pGameRules->PlayerRelationship( this, pObject ) == GR_TEAMMATE )
						{
							if ( !(m_iDisplayHistoryBits & DHF_FRIEND_SEEN) )
							{
								m_iDisplayHistoryBits |= DHF_FRIEND_SEEN;
								HintMessage( "#Hint_spotted_a_friend", true );
							}
						}
						else
						{
							if ( !(m_iDisplayHistoryBits & DHF_ENEMY_SEEN) )
							{
								m_iDisplayHistoryBits |= DHF_ENEMY_SEEN;
								HintMessage( "#Hint_spotted_an_enemy", true );
							}
						}
					}
				}
				break;
			case CLASS_PLAYER_ALLY:
				switch ( GetTeamNumber() )
				{
				case TEAM_CT:
					if ( !(m_iDisplayHistoryBits & DHF_HOSTAGE_SEEN_FAR) && tr.fraction > 0.1f )
					{
						m_iDisplayHistoryBits |= DHF_HOSTAGE_SEEN_FAR;
						HintMessage( "#Hint_rescue_the_hostages", true );
					}
					else if ( !(m_iDisplayHistoryBits & DHF_HOSTAGE_SEEN_NEAR) && tr.fraction <= 0.1f )
					{
						m_iDisplayHistoryBits |= DHF_HOSTAGE_SEEN_FAR;
						m_iDisplayHistoryBits |= DHF_HOSTAGE_SEEN_NEAR;
						HintMessage( "#Hint_press_use_so_hostage_will_follow", false );
					}
					break;
				case TEAM_TERRORIST:
					if ( !(m_iDisplayHistoryBits & DHF_HOSTAGE_SEEN_FAR) )
					{
						m_iDisplayHistoryBits |= DHF_HOSTAGE_SEEN_FAR;
						HintMessage( "#Hint_prevent_hostage_rescue", true );
					}
					break;
				}
				break;
			}
		}
	}
}

void CCSPlayer::PostThink()
{
	BaseClass::PostThink();

	if ( IsLookingAtWeapon() )
	{
		if ( gpGlobals->curtime >= m_flLookWeaponEndTime )
			StopLookingAtWeapon();
	}

	UpdateAddonBits();

	UpdateRadar();

	if ( !(m_iDisplayHistoryBits & DHF_ROUND_STARTED) && CanPlayerBuy(false) )
	{
		HintMessage( "#Hint_press_buy_to_purchase", false );
		m_iDisplayHistoryBits |= DHF_ROUND_STARTED;
	}
	if ( m_flNextMouseoverUpdate < gpGlobals->curtime )
	{
		m_flNextMouseoverUpdate = gpGlobals->curtime + 0.2f;
		if ( m_bShowHints )
		{
			UpdateMouseoverHints();
		}
	}
	if ( GetActiveWeapon() && !(m_iDisplayHistoryBits & DHF_AMMO_EXHAUSTED) )
	{
		CBaseCombatWeapon *pWeapon = GetActiveWeapon();
		CWeaponCSBase* pCSWeapon = dynamic_cast<CWeaponCSBase*>(pWeapon);
		if ( !pWeapon->HasAnyAmmo() && !(pWeapon->GetWpnData().iFlags & ITEM_FLAG_EXHAUSTIBLE) && pCSWeapon->GetWeaponID() != WEAPON_TASER )
		{
			m_iDisplayHistoryBits |= DHF_AMMO_EXHAUSTED;
			HintMessage( "#Hint_out_of_ammo", false );
		}
	}

	QAngle angles = GetLocalAngles();
	angles[PITCH] = 0;
	SetLocalAngles( angles );

	// Store the eye angles pitch so the client can compute its animation state correctly.
	m_angEyeAngles = EyeAngles();

	m_flThirdpersonRecoil = GetPunchAngle()[PITCH];

	if ( m_bUseNewAnimstate )
		m_PlayerAnimStateCSGO->Update( m_angEyeAngles[YAW], m_angEyeAngles[PITCH] );
	else
		m_PlayerAnimState->Update( m_angEyeAngles[YAW], m_angEyeAngles[PITCH] );

	// check if we need to apply a deafness DSP effect.
	if ((m_applyDeafnessTime != 0.0f) && (m_applyDeafnessTime <= gpGlobals->curtime))
	{
		ApplyDeafnessEffect();
	}

	if ( IsPlayerUnderwater() && GetWaterLevel() < 3 )
	{
		StopSound( "Player.AmbientUnderWater" );
		SetPlayerUnderwater( false );
	}
	
	if( !m_bUseNewAnimstate && IsAlive() && m_cycleLatchTimer.IsElapsed() )
	{
		m_cycleLatchTimer.Start( CycleLatchInterval );

		// Cycle is a float from 0 to 1.  We don't need to transmit a whole float for that.  Compress it in to a small fixed point
		m_cycleLatch.GetForModify() = 16 * GetCycle();// 4 point fixed
	}

	// if player is not blind, set flash duration to default
	if ( m_flFlashDuration > 0.000001f && !IsBlind() )
	{
		m_flFlashDuration = 0.0f;
	}
}


void CCSPlayer::PushawayThink()
{
	// Push physics props out of our way.
	PerformObstaclePushaway( this );
	SetNextThink( gpGlobals->curtime + PUSHAWAY_THINK_INTERVAL, CS_PUSHAWAY_THINK_CONTEXT );
}

void CCSPlayer::SetModel( const char *szModelName )
{
	m_bUseNewAnimstate = ( Q_stristr( szModelName, "custom_player" ) != 0 );

	if ( m_bUseNewAnimstate )
	{
		if ( m_bUseNewAnimstate && m_PlayerAnimStateCSGO )
			m_PlayerAnimStateCSGO->Reset();

	}

	BaseClass::SetModel( szModelName );
}


//-----------------------------------------------------------------------------
// Purpose: Returns whether or not we can switch to the given weapon.
// Input  : pWeapon -
//-----------------------------------------------------------------------------
bool CCSPlayer::Weapon_CanSwitchTo( CBaseCombatWeapon *pWeapon )
{
	if ( !pWeapon->CanDeploy() )
		return false;

	if ( GetActiveWeapon() )
	{
		if ( !GetActiveWeapon()->CanHolster() )
			return false;
	}

	return true;
}

bool CCSPlayer::ShouldDoLargeFlinch( int nHitGroup, CBaseEntity *pAttacker )
{
	if ( FBitSet( GetFlags(), FL_DUCKING ) )
		return false;

	if ( nHitGroup == HITGROUP_LEFTLEG )
		return false;

	if ( nHitGroup == HITGROUP_RIGHTLEG )
		return false;

	return true;
}

bool CCSPlayer::IsArmored( int nHitGroup )
{
	bool bApplyArmor = false;

	if ( ArmorValue() > 0 )
	{
		switch ( nHitGroup )
		{
		case HITGROUP_GENERIC:
		case HITGROUP_CHEST:
		case HITGROUP_STOMACH:
		case HITGROUP_LEFTARM:
		case HITGROUP_RIGHTARM:
			bApplyArmor = true;
			break;
		case HITGROUP_HEAD:
			if ( m_bHasHelmet )
			{
				bApplyArmor = true;
			}
			break;
		default:
			break;
		}
	}

	return bApplyArmor;
}

void CCSPlayer::Pain( bool bHasArmour, int nDmgTypeBits )
{
	if ( (nDmgTypeBits & DMG_BURN) )
	{
		if ( m_fNextMolotovDamageSoundTime <= gpGlobals->curtime )
		{
			if ( bHasArmour == false )
			{
				EmitSound( "Player.BurnDamage" );
			}
			else
			{
				EmitSound( "Player.BurnDamageKevlar" );
			}
			m_fNextMolotovDamageSoundTime = gpGlobals->curtime + 1.0;
		}
		return;
	}

	if ( nDmgTypeBits & DMG_CLUB )
	{
		if ( bHasArmour == false )
		{
			EmitSound( "Flesh.BulletImpact" );
		}
		else
		{
			EmitSound( "Player.DamageKevlar" );
		}
		return;
	}

	switch (m_LastHitGroup)
	{
		case HITGROUP_HEAD:
			if (m_bHasHelmet)  // He's wearing a helmet
			{
				EmitSound( "Player.DamageHelmet" );
			}
			else  // He's not wearing a helmet
			{
				EmitSound( "Player.DamageHeadShot" );
			}
			break;
		default:
			if ( bHasArmour == false )
			{
				EmitSound( "Flesh.BulletImpact" );
			}
			else
			{
				EmitSound( "Player.DamageKevlar" );
			}
			break;
	}
}

class CBombShieldTraceEnum : public IEntityEnumerator
{
public:
	CBombShieldTraceEnum( Ray_t *pRay ) : m_pRay(pRay), m_bHitBombBlocker(false)
	{
	}

	virtual bool EnumEntity( IHandleEntity *pHandleEntity )
	{
		Assert( pHandleEntity );

		trace_t tr;
		enginetrace->ClipRayToEntity( *m_pRay, MASK_ALL, pHandleEntity, &tr );

		if (( tr.fraction < 1.0f ) || (tr.startsolid) || (tr.allsolid))
		{
			if ( !V_strcmp( tr.surface.name, "TOOLS/TOOLSBLOCKBOMB" ) )
			{
				m_bHitBombBlocker = true;
				return false;
			}
		}

		return true;
	}

	bool HitBombBlocker( void ) { return m_bHitBombBlocker; }

private:
	Ray_t	*m_pRay;
	bool m_bHitBombBlocker;
};

int CCSPlayer::OnTakeDamage( const CTakeDamageInfo &inputInfo )
{
	if ( m_bImmunity )
	{
		// No damage if immune
		return 0;
	}

	CTakeDamageInfo info = inputInfo;

	CBaseEntity *pInflictor = info.GetInflictor();

	if ( info.GetDamage() < 1 )
		return 0;

	if ( !pInflictor )
		return 0;

	if ( GetMoveType() == MOVETYPE_NOCLIP || GetMoveType() == MOVETYPE_OBSERVER )
		return 0;

	//if this is C4 bomb damage, make sure it didn't pass through any bomb blockers to reach this player.
	CPlantedC4 *pInflictorC4 = dynamic_cast< CPlantedC4 * >( pInflictor );
	if ( pInflictorC4 )
	{
		Ray_t ray;
		ray.Init( pInflictorC4->GetAbsOrigin(), GetAbsOrigin() );

		CBombShieldTraceEnum bombShieldTrace( &ray );
		enginetrace->EnumerateEntities( ray, true, &bombShieldTrace );

		if ( bombShieldTrace.HitBombBlocker() )
		{
			return 0;
		}
	}

	// Because explosions and fire damage don't do raytracing, but rather lookup entities in volume,
	// we need to set damage hitgroup to generic to make sure previous bullet damage hitgroup is not
	// carried over. Only bullets do raytracing so force it here!
	if ( ( info.GetDamageType() & DMG_BULLET ) == 0 )
		m_LastHitGroup = HITGROUP_GENERIC;

	const float flArmorBonus = 0.5f;
	float flArmorRatio = 0.5f;
	float flDamage = info.GetDamage();

	bool bFriendlyFire = CSGameRules()->IsFriendlyFireOn();

	//=============================================================================
	// HPE_BEGIN:
	// [tj] Added properties for goose chase achievement
	//=============================================================================
 
	CSGameRules()->PlayerTookDamage(this, inputInfo);

	bool bDamageIsFromFire = !!( info.GetDamageType() & DMG_BURN ); //  check the damage type [mtw]

	//Check "Goose Chase" achievement
	CCSPlayer *pAttacker = ToCSPlayer(info.GetAttacker());
	if (m_bIsDefusing && m_gooseChaseStep == GC_NONE && pAttacker && pAttacker->GetTeamNumber() != GetTeamNumber() )
	{

		//Count enemies
		int livingEnemies = 0;
		CTeam *pAttackerTeam = GetGlobalTeam( pAttacker->GetTeamNumber() );
		for ( int iPlayer=0; iPlayer < pAttackerTeam->GetNumPlayers(); iPlayer++ )
		{
			CCSPlayer *pPlayer = ToCSPlayer( pAttackerTeam->GetPlayer( iPlayer ) );
			Assert( pPlayer );
			if ( !pPlayer )
				continue;

			Assert( pPlayer->GetTeamNumber() == pAttackerTeam->GetTeamNumber() );
			
			if ( pPlayer->m_lifeState == LIFE_ALIVE )
			{
				livingEnemies++;
			}
		}

		//Must be last enemy alive;
		if (livingEnemies == 1)
		{
			m_gooseChaseStep = GC_SHOT_DURING_DEFUSE;
			m_pGooseChaseDistractingPlayer = pAttacker;
		}
	}
 
	//=============================================================================
	// HPE_END
	//=============================================================================


	// warn about team attacks
	if ( bFriendlyFire && pInflictor->GetTeamNumber() == GetTeamNumber() && pInflictor != this && info.GetAttacker() != this && !bDamageIsFromFire )
	{
		CCSPlayer *pCSAttacker = ToCSPlayer( pInflictor );
		if ( !pCSAttacker )
			pCSAttacker = ToCSPlayer( info.GetAttacker() );

		if ( pCSAttacker )
		{
			if ( !(pCSAttacker->m_iDisplayHistoryBits & DHF_FRIEND_INJURED) )
			{
				pCSAttacker->HintMessage( "#Hint_try_not_to_injure_teammates", false );
				pCSAttacker->m_iDisplayHistoryBits |= DHF_FRIEND_INJURED;
			}

			if ( (pCSAttacker->m_flLastAttackedTeammate + 0.6f) < gpGlobals->curtime )
			{
				pCSAttacker->m_flLastAttackedTeammate = gpGlobals->curtime;

				// tell the rest of this player's team
				Msg( "%s attacked a teammate\n", pCSAttacker->GetPlayerName() );
				for ( int i=1; i<=gpGlobals->maxClients; ++i )
				{
					CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
					if ( pPlayer && pPlayer->GetTeamNumber() == GetTeamNumber()	)
					{
						ClientPrint( pPlayer, HUD_PRINTTALK, "#Game_teammate_attack", pCSAttacker->GetPlayerName() );
					}
				}
			}
		}
	}

	if ( bFriendlyFire ||
		pInflictor->GetTeamNumber() != GetTeamNumber() ||
		pInflictor == this ||
		info.GetAttacker() == this )
	{
		if ( bFriendlyFire && (info.GetDamageType() & DMG_BLAST) == 0 )
		{
			if ( pInflictor->GetTeamNumber() == GetTeamNumber() )
			{
				if ( CSGameRules()->IsWarmupPeriod() )
					flDamage = 0; // no friendlyfire in warmup
				else
					flDamage *= 0.35; // bullets hurt teammates less
			}
		}

		// special case for molotovs
		if ( ( info.GetDamageType() & DMG_BURN ) == 0 )
		{
			if ( ShouldDoLargeFlinch( m_LastHitGroup, info.GetAttacker() ) )
			{
				if ( GetAbsVelocity().Length() < 300 )
				{
					m_flVelocityModifier = 0.65;
				}
			}
			else
			{
				m_flVelocityModifier = 0.5;
			}
		}

//=============================================================================
// HPE_BEGIN:
//=============================================================================
		// [menglish] Store whether or not the knife did this damage as knives do bullet damage,
		// so we need to specifically check the weapon here
		bool bKnifeDamage = false;
		CCSPlayer *pPlayer = ToCSPlayer( info.GetAttacker() );

		if ( pPlayer )
		{

			// [paquin. forest] if  this is blast damage, and we haven't opted out with a cvar,
			// we need to get the armor ratio out of the inflictor

			if ( (info.GetDamageType() & DMG_BLAST) && !sv_legacy_grenade_damage.GetBool() )
			{

				// [paquin] if we know this is a grenade, use it's armor ratio, otherwise
				// use the he grenade armor ratio

				CBaseCSGrenadeProjectile *pGrenade = dynamic_cast< CBaseCSGrenadeProjectile * >( pInflictor );
				CCSWeaponInfo* pWeaponInfo;

				if ( pGrenade && pGrenade->m_pWeaponInfo )
				{
					pWeaponInfo = pGrenade->m_pWeaponInfo;
				}
				else
				{
					pWeaponInfo = GetWeaponInfo( WEAPON_HEGRENADE );
				}

				if ( pWeaponInfo )
				{
					flArmorRatio *= pWeaponInfo->m_flArmorRatio;
				}
			}
			else
			{
				CWeaponCSBase *pWeapon = pPlayer->GetActiveCSWeapon();

				if ( pWeapon )
				{
					flArmorRatio *= pWeapon->GetCSWpnData().m_flArmorRatio;
					//Knives do bullet damage, so we need to specifically check the weapon here
					bKnifeDamage = pWeapon->GetCSWpnData().m_WeaponType == WEAPONTYPE_KNIFE;

					if (  info.GetDamageType() & DMG_BULLET && !bKnifeDamage && pPlayer->GetTeam() != GetTeam() )
					{
						CCS_GameStats.Event_ShotHit( pPlayer, info );
					}
				}
			}
		}
 
//=============================================================================
// HPE_END
//=============================================================================

		// keep track of amount of damage last sustained
		m_lastDamageAmount = flDamage;

		// Deal with Armour
		if ( ArmorValue() && !( info.GetDamageType() & (DMG_FALL | DMG_DROWN)) && IsArmored( m_LastHitGroup ) )
		{
			float fDamageToHealth = flDamage * flArmorRatio;
			float fDamageToArmor = (flDamage - fDamageToHealth) * flArmorBonus;

			int armorValue = ArmorValue();

			// Does this use more armor than we have?
			if (fDamageToArmor > armorValue )
			{
				fDamageToHealth = flDamage - armorValue / flArmorBonus;
				fDamageToArmor = armorValue;
				armorValue = 0;
			}
			else
			{
				if ( fDamageToArmor < 0 )
					 fDamageToArmor = 1;

				armorValue -= fDamageToArmor;
			}
			m_lastDamageArmor = (int)fDamageToArmor;
			SetArmorValue(armorValue);

			//=============================================================================
			// HPE_BEGIN:
			// [tj] Handle headshot-surviving achievement
			//=============================================================================
			if (m_LastHitGroup == HITGROUP_HEAD && flDamage > m_iHealth && fDamageToHealth < m_iHealth)
			{
				m_bSurvivedHeadshotDueToHelmet = true;
			}			
			//=============================================================================
			// HPE_END			
			//=============================================================================

			flDamage = fDamageToHealth;

			info.SetDamage( flDamage );

			if ( ArmorValue() <= 0.0)
				m_bHasHelmet = false;

			if( !(info.GetDamageType() & DMG_FALL ) && !(info.GetDamageType() & DMG_BURN ) && !(info.GetDamageType() & DMG_BLAST ) )
				Pain( true /*has armor*/, info.GetDamageType() );
		}
		else
		{
			m_lastDamageArmor = 0;
			if( !(info.GetDamageType() & DMG_FALL) )
				Pain( false /*no armor*/, info.GetDamageType() );
		}

		// round damage to integer
		m_lastDamageHealth = (int)flDamage;
		info.SetDamage( m_lastDamageHealth );

		if ( info.GetDamage() <= 0 )
			return 0;

		CSingleUserRecipientFilter user( this );
		user.MakeReliable();
		UserMessageBegin( user, "Damage" );
			WRITE_BYTE( (int)info.GetDamage() );
			WRITE_VEC3COORD( info.GetInflictor()->WorldSpaceCenter() );
//=============================================================================
// HPE_BEGIN:
// [menglish] Send the info about where the player was hit
//=============================================================================
			if ( !( info.GetDamageType() & DMG_BULLET ) || bKnifeDamage )
			{
				WRITE_LONG( -1 );
			}
			else
			{
				WRITE_LONG( m_LastHitBox );
			}
			WRITE_VEC3COORD( m_vLastHitLocationObjectSpace );
			 
//=============================================================================
// HPE_END
//=============================================================================
		MessageEnd();

		// Do special explosion damage effect
		if ( info.GetDamageType() & DMG_BLAST )
		{
			OnDamagedByExplosion( info );
		}

// [menglish] Achievement award for kill stealing i.e. killing an enemy who was very damaged from other players
// [Forrest] Moved this check before RecordDamageTaken so that the damage currently being dealt by this player
//           won't disqualify them from getting the achievement.

		if(m_iHealth - info.GetDamage() <= 0 && m_iHealth <= AchievementConsts::KillLowDamage_MaxHealthLeft)
		{
			bool onlyDamage = true;
			CCSPlayer *pAttacker = ToCSPlayer(info.GetAttacker());
			if(pAttacker && pAttacker->GetTeamNumber() != GetTeamNumber())
			{
				//Verify that the killer has not done damage to this player beforehand
				FOR_EACH_LL( m_DamageList, i )
				{
					if ( m_DamageList[i]->GetPlayerRecipientPtr() == this && m_DamageList[i]->GetPlayerDamagerPtr() == pAttacker )
					{
						onlyDamage = false;
						break;
					}
				}
				if(onlyDamage)
				{
					pAttacker->AwardAchievement(CSKillLowDamage);
				}
			}
		}

//=============================================================================
// HPE_END
//=============================================================================

#if REPORT_PLAYER_DAMAGE
		// damage output spew
		char dmgtype[64];
		CTakeDamageInfo::DebugGetDamageTypeString( info.GetDamageType(), dmgtype, sizeof(dmgtype) );

		if ( info.GetDamageType() & DMG_HEADSHOT )
			Q_strncat(dmgtype, "HEADSHOT", sizeof(dmgtype));

		char outputString[256];
		Q_snprintf( outputString, sizeof(outputString), "%f: Player %s incoming %f damage from %s, type %s; applied %d health and %d armor\n", 
			gpGlobals->curtime, GetPlayerName(),
			inputInfo.GetDamage(), info.GetInflictor()->GetDebugName(), dmgtype,
			m_lastDamageHealth, m_lastDamageArmor);

		Msg(outputString);
#endif

		// this is the actual damage applied to the player and not the raw damage that was output from the weapon
		int nHealthRemoved = (GetHealth() < info.GetDamage()) ? GetHealth() : info.GetDamage();

		if ( pPlayer )
		{		
			// Record for the shooter
			pAttacker->RecordDamage( pAttacker, this, info.GetDamage(), nHealthRemoved );

			// And for the victim (don't double-record if it is the same person)
			if ( pAttacker != this )
			{
				RecordDamage( pAttacker, this, info.GetDamage(), nHealthRemoved );
			}
		}
		else
		{
			RecordDamage( NULL, this, info.GetDamage(), nHealthRemoved ); //damaged by a null player - likely the world
		}

		m_vecTotalBulletForce += info.GetDamageForce();

		gamestats->Event_PlayerDamage( this, info );

		return CBaseCombatCharacter::OnTakeDamage( info );
	}
	else
	{
		return 0;
	}
}


//MIKETODO: this probably should let the shield model catch the trace attacks.
bool CCSPlayer::IsHittingShield( const Vector &vecDirection, trace_t *ptr )
{
	if ( HasShield() == false )
		 return false;

	if ( IsShieldDrawn() == false )
		 return false;

	float		flDot;
	Vector		vForward;
	Vector2D	vec2LOS = vecDirection.AsVector2D();
	AngleVectors( GetLocalAngles(), &vForward );

	Vector2DNormalize( vForward.AsVector2D() );
	Vector2DNormalize( vec2LOS );

	flDot = DotProduct2D ( vec2LOS , vForward.AsVector2D() );

	if ( flDot < -0.87f )
		 return true;

	return false;
}

void CCSPlayer::ClearImmunity( void )
{
	// Fired a shot so no longer immune
	m_bImmunity = false;
	m_fImmuneToDamageTime = 0.0f;
}

void CCSPlayer::TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator )
{
	bool bShouldBleed = true;
	bool bShouldSpark = false;
	bool bHitShield = IsHittingShield( vecDir, ptr );

	CBasePlayer *pAttacker = (CBasePlayer*)ToBasePlayer( info.GetAttacker() );

	// show blood for firendly fire only if FF is on
	if ( pAttacker && ( GetTeamNumber() == pAttacker->GetTeamNumber() ) )
		 bShouldBleed = CSGameRules()->IsFriendlyFireOn();

	if ( m_takedamage != DAMAGE_YES )
		return;

	m_LastHitGroup = ptr->hitgroup;
//=============================================================================
// HPE_BEGIN:
// [menglish] Used when calculating the position this player was hit at in the bone space
//=============================================================================
	m_LastHitBox = ptr->hitbox;
//=============================================================================
// HPE_END
//=============================================================================

	m_nForceBone = ptr->physicsbone;	//Save this bone for ragdoll

	float flDamage = info.GetDamage();

	bool bHeadShot = false;

	if ( m_bImmunity )
	{
		bShouldBleed = false;
	}
	else if ( bHitShield )
	{
		flDamage = 0;
		bShouldBleed = false;
		bShouldSpark = true;
	}
	else if( info.GetDamageType() & DMG_SHOCK )
	{
		bShouldBleed = false;
	}
	else if( info.GetDamageType() & DMG_BLAST )
	{
		if ( ArmorValue() > 0 )
			 bShouldBleed = false;

		if ( bShouldBleed == true )
		{
			// punch view if we have no armor
			QAngle punchAngle = GetPunchAngle();
			punchAngle.x = flDamage * -0.1;

			if ( punchAngle.x < -4 )
				punchAngle.x = -4;

			SetPunchAngle( punchAngle );
		}
	}
	else
	{
//=============================================================================
// HPE_BEGIN:
// [menglish] Calculate the position this player was hit at in the bone space
//=============================================================================
		 
		matrix3x4_t boneTransformToWorld, boneTransformToObject;
		GetBoneTransform(GetHitboxBone(ptr->hitbox), boneTransformToWorld);
		MatrixInvert(boneTransformToWorld, boneTransformToObject);
		VectorTransform(ptr->endpos, boneTransformToObject, m_vLastHitLocationObjectSpace);
		 
//=============================================================================
// HPE_END
//=============================================================================

		switch ( ptr->hitgroup )
		{
		case HITGROUP_GENERIC:
			break;

		case HITGROUP_HEAD:

			if ( m_bHasHelmet )
			{
//				bShouldBleed = false;
				bShouldSpark = true;
			}

			flDamage *= 4;

			if ( !m_bHasHelmet )
			{
				QAngle punchAngle = GetPunchAngle();
				punchAngle.x = flDamage * -0.5;

				if ( punchAngle.x < -12 )
					punchAngle.x = -12;

				punchAngle.z = flDamage * random->RandomFloat(-1,1);

				if ( punchAngle.z < -9 )
					punchAngle.z = -9;

				else if ( punchAngle.z > 9 )
					punchAngle.z = 9;

				SetPunchAngle( punchAngle );
			}

			bHeadShot = true;

			break;

		case HITGROUP_CHEST:

			flDamage *= 1.0;

			if ( ArmorValue() <= 0 )
			{
				QAngle punchAngle = GetPunchAngle();
				punchAngle.x = flDamage * -0.1;

				if ( punchAngle.x < -4 )
					punchAngle.x = -4;

				SetPunchAngle( punchAngle );
			}
			break;

		case HITGROUP_STOMACH:

			flDamage *= 1.25;

			if ( ArmorValue() <= 0 )
			{
				QAngle punchAngle = GetPunchAngle();
				punchAngle.x = flDamage * -0.1;

				if ( punchAngle.x < -4 )
					punchAngle.x = -4;

				SetPunchAngle( punchAngle );
			}

			break;

		case HITGROUP_LEFTARM:
		case HITGROUP_RIGHTARM:
			flDamage *= 1.0;
			break;

		case HITGROUP_LEFTLEG:
		case HITGROUP_RIGHTLEG:
			flDamage *= 0.75;
			break;

		default:
			break;
		}
	}

	// Since this code only runs on the server, make sure it shows the tempents it creates.
	CDisablePredictionFiltering disabler;

	if ( bShouldBleed )
	{
		// This does smaller splotches on the guy and splats blood on the world.
		TraceBleed( flDamage, vecDir, ptr, info.GetDamageType() );

		CEffectData	data;
		data.m_vOrigin = ptr->endpos;
		data.m_vNormal = vecDir * -1;
		data.m_nEntIndex = ptr->m_pEnt ?  ptr->m_pEnt->entindex() : 0;
		data.m_flMagnitude = flDamage;

		// reduce blood effect if target has armor
		if ( ArmorValue() > 0 )
			data.m_flMagnitude *= 0.5f;

		// reduce blood effect if target is hit in the helmet
		if ( ptr->hitgroup == HITGROUP_HEAD && bShouldSpark )
			data.m_flMagnitude *= 0.5;

		DispatchEffect( "csblood", data );
	}
	if ( ( ptr->hitgroup == HITGROUP_HEAD || bHitShield ) && bShouldSpark ) // they hit a helmet
	{
		// show metal spark effect
		g_pEffects->Sparks( ptr->endpos, 1, 1, &ptr->plane.normal );
	}

	if ( !bHitShield )
	{
		CTakeDamageInfo subInfo = info;

		subInfo.SetDamage( flDamage );

		if( bHeadShot )
			subInfo.AddDamageType( DMG_HEADSHOT );

		AddMultiDamage( subInfo, this );
	}
}


void CCSPlayer::Reset()
{
	ResetFragCount();
	ResetDeathCount();
	m_iAccount = 0;
	AddAccount( -mp_startmoney.GetInt(), false );

	//remove any weapons they bought before the round started
	RemoveAllItems( true );

	//RemoveShield();

	AddAccount( CSGameRules()->GetStartMoney(), true );
}

//-----------------------------------------------------------------------------
// Purpose: Displays a hint message to the player
// Input  : *pMessage -
//			bDisplayIfDead -
//			bOverrideClientSettings -
//-----------------------------------------------------------------------------
void CCSPlayer::HintMessage( const char *pMessage, bool bDisplayIfDead, bool bOverrideClientSettings )
{
	if ( ( !bDisplayIfDead && !IsAlive() ) || !IsNetClient() || !m_pHintMessageQueue )
		return;

	if ( bOverrideClientSettings || m_bShowHints )
		m_pHintMessageQueue->AddMessage( pMessage );
}

void CCSPlayer::AddAccountAward( int reason )
{
#if CS_CONTROLLABLE_BOTS_ENABLED
	// we don't want to award a bot money that is being controlled by a player because the player is currently storing the bots money
	if ( IsBot() && HasControlledByPlayer() )
		return;
#endif

	AddAccountAward( reason, CSGameRules()->PlayerCashAwardValue( reason ) );
}

void CCSPlayer::AddAccountAward( int reason, int amount, const CWeaponCSBase *pWeapon )
{
	// no awards in the warmup period
	if ( CSGameRules() && CSGameRules()->IsWarmupPeriod() )
		return;

	if ( amount == 0 )
		return;

	const char* awardReasonToken = NULL;
	const char* sign_string = "+$";
	const char* szWeaponName = NULL;

	extern ConVar cash_player_killed_enemy_default;
	extern ConVar cash_player_killed_enemy_factor;

	switch ( reason )
	{
		case PlayerCashAward::KILL_TEAMMATE:
		awardReasonToken = "#Player_Cash_Award_Kill_Teammate";
		sign_string = "-$";
		break;
		case PlayerCashAward::KILLED_ENEMY:

		awardReasonToken = "#Player_Cash_Award_Killed_Enemy_Generic";

		// if award amount is non-default, use the verbose message.
		if ( pWeapon && (amount != cash_player_killed_enemy_default.GetInt()) )
		{
			szWeaponName = pWeapon->GetCSWpnData().szPrintName;
			awardReasonToken = "#Player_Cash_Award_Killed_Enemy";
		}

		// scale amount by kill award factor convar.
		amount = RoundFloatToInt( amount * cash_player_killed_enemy_factor.GetFloat() );

		break;
		case PlayerCashAward::BOMB_PLANTED:
		awardReasonToken = "#Player_Cash_Award_Bomb_Planted";
		break;
		case PlayerCashAward::BOMB_DEFUSED:
		awardReasonToken = "#Player_Cash_Award_Bomb_Defused";
		break;
		case PlayerCashAward::RESCUED_HOSTAGE:
		awardReasonToken = "#Player_Cash_Award_Rescued_Hostage";
		break;
		case PlayerCashAward::INTERACT_WITH_HOSTAGE:
		awardReasonToken = "#Player_Cash_Award_Interact_Hostage";
		break;
		case PlayerCashAward::DAMAGE_HOSTAGE:
		awardReasonToken = "#Player_Cash_Award_Damage_Hostage";
		sign_string = "-$";
		break;
		case PlayerCashAward::KILL_HOSTAGE:
		awardReasonToken = "#Player_Cash_Award_Kill_Hostage";
		sign_string = "-$";
		break;
		default:
		break;
	}

	char strAmount[64];
	Q_snprintf( strAmount, sizeof( strAmount ), "%s%d", sign_string, abs( amount ) );

	ClientPrint( this, HUD_PRINTTALK, awardReasonToken, strAmount, szWeaponName );

	AddAccount( amount, true, false );
}

void CCSPlayer::AddAccount( int amount, bool bTrackChange, bool bItemBought, const char *pItemName )
{
	m_iAccount += amount;

	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Description of reason for change
	//=============================================================================

	if(amount > 0)
	{
		CCS_GameStats.Event_MoneyEarned( this, amount );
	}
	else if( amount < 0 && bItemBought)
	{
		CCS_GameStats.Event_MoneySpent( this, ABS(amount), pItemName );
	}

	//=============================================================================
	// HPE_END
	//=============================================================================

	if ( m_iAccount < 0 )
		m_iAccount = 0;
	else if ( m_iAccount > mp_maxmoney.GetInt() )
		m_iAccount = mp_maxmoney.GetInt();
}

void CCSPlayer::MarkAsNotReceivingMoneyNextRound()
{
	m_receivesMoneyNextRound = false;
}

bool CCSPlayer::DoesPlayerGetRoundStartMoney()
{
	return m_receivesMoneyNextRound;
}

CCSPlayer* CCSPlayer::Instance( int iEnt )
{
	return dynamic_cast< CCSPlayer* >( CBaseEntity::Instance( INDEXENT( iEnt ) ) );
}

bool CCSPlayer::ShouldPickupItemSilently( CBaseCombatCharacter *pNewOwner )
{
	CCSPlayer *pNewCSOwner = dynamic_cast< CCSPlayer* >(pNewOwner);
	if ( !pNewCSOwner || !CSGameRules() || CSGameRules()->IsFreezePeriod() /*|| pNewCSOwner->CanPlayerBuy( false )*/ )
		return false; // turns out that item touch calls happen in between FinishMove and the trigger touch so CanPlayerBuy always returns false in this case.....

	//if ( pNewCSOwner->GetAbsVelocity().Length2D() < (CS_PLAYER_SPEED_RUN * CS_PLAYER_SPEED_WALK_MODIFIER) )
	if ( pNewCSOwner->GetAbsVelocity().Length2D() < 150 )
		return true;

	return false;
}


void CCSPlayer::DropC4()
{
}


bool CCSPlayer::HasDefuser()
{
	return m_bHasDefuser;
}

void CCSPlayer::RemoveDefuser()
{
	m_bHasDefuser = false;
}

void CCSPlayer::GiveDefuser(bool bPickedUp /* = false */)
{
	if ( !m_bHasDefuser )
	{
		bool bIsSilentPickup = ShouldPickupItemSilently( this );
		IGameEvent * event = gameeventmanager->CreateEvent( "item_pickup" );
		if( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetString( "item", "defuser" );
			event->SetBool( "silent", bIsSilentPickup );
			gameeventmanager->FireEvent( event );
		}

		if ( !bIsSilentPickup )
			EmitSound( "Player.PickupWeapon" );
	}

	m_bHasDefuser = true;

	//=============================================================================
	// HPE_BEGIN:
	// [dwenger] Added for fun-fact support
	//=============================================================================

	m_bPickedUpDefuser = bPickedUp;

	//=============================================================================
	// HPE_END
	//=============================================================================
}

// player blinded by a flashbang
void CCSPlayer::Blind( float holdTime, float fadeTime, float startingAlpha )
{
	// Don't flash a spectator.
	color32 clr = {255, 255, 255, 255};

	clr.a = startingAlpha;

	// estimate when we can see again
	float oldBlindUntilTime = m_blindUntilTime;
	float oldBlindStartTime = m_blindStartTime;
	m_blindUntilTime = MAX( m_blindUntilTime, gpGlobals->curtime + holdTime + 0.5f * fadeTime );
	m_blindStartTime = gpGlobals->curtime;

	fadeTime /= 1.4f;

	if ( gpGlobals->curtime > oldBlindUntilTime )
	{
		// The previous flashbang is wearing off, or completely gone
		m_flFlashDuration = fadeTime;
		m_flFlashMaxAlpha = startingAlpha;
	}
	else
	{
		// The previous flashbang is still going strong - only extend the duration
		float remainingDuration = oldBlindStartTime + m_flFlashDuration - gpGlobals->curtime;

		float flNewDuration = Max( remainingDuration, fadeTime );

		// The flashbang client effect runs off a network var change callback... Make sure the bits for duration get
		// sent by changing it a tiny bit whenever these end up being equal.
		if ( m_flFlashDuration == flNewDuration )
			flNewDuration += 0.01f;

		m_flFlashDuration = flNewDuration;
		m_flFlashMaxAlpha = Max( m_flFlashMaxAlpha.Get(), startingAlpha );
	}


	if ( m_bUseNewAnimstate && m_PlayerAnimStateCSGO )
	{
		// Magic numbers to reduce the fade time to within 'perceptible' range.
		// Players can see well enough to shoot back somewhere around 50% white plus burn-in effect.
		// Varies by player and amount of panic ;)
		// So this makes raised arm goes down earlier, making it a better representation of actual blindness.
		float flAdjustedHold = holdTime * 0.45f;
		float flAdjustedEnd = fadeTime * 0.7f;

		//DevMsg( "Flashing. Time is: %f. Params: holdTime: %f, fadeTime: %f, alpha: %f\n", gpGlobals->curtime, holdTime, fadeTime, m_flFlashMaxAlpha );

		m_PlayerAnimStateCSGO->m_flFlashedAmountEaseOutStart = gpGlobals->curtime + flAdjustedHold;
		m_PlayerAnimStateCSGO->m_flFlashedAmountEaseOutEnd = gpGlobals->curtime + flAdjustedEnd;

		// This check moves the ease-out start and end to account for a non-255 starting alpha.
		// However it looks like starting alpha is ALWAYS 255, since no current code path seems to ever pass in less.
		if ( m_flFlashMaxAlpha < 255 )
		{
			float flScaleBack = 1.0f - (( flAdjustedEnd / 255.0f ) * m_flFlashMaxAlpha);
			m_PlayerAnimStateCSGO->m_flFlashedAmountEaseOutStart -= flScaleBack;
			m_PlayerAnimStateCSGO->m_flFlashedAmountEaseOutEnd -= flScaleBack;
		}

		// when fade out time is very soon, don't pull the arm up all the way. It looks silly and robotic.
		if ( flAdjustedEnd < 1.5f )
		{
			m_PlayerAnimStateCSGO->m_flFlashedAmountEaseOutStart -= 1.0f;
		}
	}
}

void CCSPlayer::Deafen( float flDistance )
{
	// Spectators don't get deafened
	if ( (GetObserverMode() == OBS_MODE_NONE)  ||  (GetObserverMode() == OBS_MODE_IN_EYE) )
	{
		// dsp presets are defined in hl2/scripts/dsp_presets.txt

		int effect;

		if( flDistance < 600 )
		{
			effect = 134;
		}
		else if( flDistance < 800 )
		{
			effect = 135;
		}
		else if( flDistance < 1000 )
		{
			effect = 136;
		}
		else
		{
			// too far for us to get an effect
			return;
		}

		CSingleUserRecipientFilter user( this );
		enginesound->SetPlayerDSP( user, effect, false );

		//TODO: bots can't hear sound for a while?
	}
}

void CCSPlayer::GiveShield( void )
{
#ifdef CS_SHIELD_ENABLED
	m_bHasShield = true;
	m_bShieldDrawn = false;

	if ( HasSecondaryWeapon() )
	{
		CBaseCombatWeapon *pWeapon = Weapon_GetSlot( WEAPON_SLOT_PISTOL );
		pWeapon->SetModel( pWeapon->GetViewModel() );
		pWeapon->Deploy();
	}

	CBaseViewModel *pVM = GetViewModel( 1 );

	if ( pVM )
	{
		ShowViewModel( true );
		pVM->RemoveEffects( EF_NODRAW );
		pVM->SetWeaponModel( SHIELD_VIEW_MODEL, GetActiveWeapon() );
		pVM->SendViewModelMatchingSequence( 1 );
	}
#endif
}

void CCSPlayer::RemoveShield( void )
{
#ifdef CS_SHIELD_ENABLED
	m_bHasShield = false;

	CBaseViewModel *pVM = GetViewModel( 1 );

	if ( pVM )
	{
		pVM->AddEffects( EF_NODRAW );
	}
#endif
}

void CCSPlayer::RemoveAllItems( bool removeSuit )
{
	if( HasDefuser() )
	{
		RemoveDefuser();
	}

	if ( HasShield() )
	{
		RemoveShield();
	}

	m_bHasNightVision = false;
	m_bNightVisionOn = false;

	// [dwenger] Added for fun-fact support
	m_bPickedUpDefuser = false;
	m_bDefusedWithPickedUpKit = false;

	m_nPreferredGrenadeDrop = 0;

	if ( removeSuit )
	{
		m_bHasHelmet = false;
		SetArmorValue( 0 );
	}

	BaseClass::RemoveAllItems( removeSuit );
}

void CCSPlayer::ObserverRoundRespawn()
{
	ClearFlashbangScreenFade();

	// did we change our name last round?
	if ( m_szNewName[0] != 0 )
	{
		// ... and force the name change now.  After this happens, the gamerules will get
		// a ClientSettingsChanged callback from the above ClientCommand, but the name
		// matches what we're setting here, so it will do nothing.
		ChangeName( m_szNewName );
		m_szNewName[0] = 0;
	}
}

void CCSPlayer::RoundRespawn()
{
	//MIKETODO: menus
	//if ( m_iMenu != Menu_ChooseAppearance )
	{
		// Put them back into the game.
		StopObserverMode();
		State_Transition( STATE_ACTIVE );
		respawn( this, false );
		m_nButtons = 0;
		SetNextThink( TICK_NEVER_THINK );
	}

	m_receivesMoneyNextRound = true; // reset this variable so they can receive their cash next round.

	//If they didn't die, this will print out their damage info
	OutputDamageGiven();
	OutputDamageTaken();
	ResetDamageCounters();
}

void CCSPlayer::CheckTKPunishment( void )
{
	// teamkill punishment..
	if ( (m_bJustKilledTeammate == true) && mp_tkpunish.GetInt() )
	{
		m_bJustKilledTeammate = false;
		m_bPunishedForTK = true;
		CommitSuicide();
	}
}

bool CCSPlayer::Weapon_Switch( CBaseCombatWeapon *pWeapon, int viewmodelindex /*= 0*/ )
{
	if ( IsLookingAtWeapon() )
	{
		StopLookingAtWeapon();
	}

	bool bBaseClassSwitch = BaseClass::Weapon_Switch( pWeapon, viewmodelindex );

	if ( bBaseClassSwitch )
		m_bDuckOverride = false;

	if ( pWeapon )
	{
		CWeaponCSBase* pCSWeapon = dynamic_cast< CWeaponCSBase* >( pWeapon );
		if ( pCSWeapon )
		{
			if ( pCSWeapon->GetWeaponType() == WEAPONTYPE_GRENADE )
			{	// When switching to grenade remember the preferred grenade
				m_nPreferredGrenadeDrop = pCSWeapon->GetCSWeaponID();
			}
		}

		MDLCACHE_CRITICAL_SECTION();
		// Add a deploy event to let the 3rd person animation system know to update to the current weapon and optionally play a deploy animation if it exists.
		if ( (gpGlobals->curtime - pWeapon->m_flLastTimeInAir) < 0.1f )
		{
			// if the weapon was flying through the air VERY recently, assume we 'caught' it and play a catch anim
			DoAnimationEvent( PLAYERANIMEVENT_CATCH_WEAPON );
		}
		else
		{
			DoAnimationEvent( PLAYERANIMEVENT_DEPLOY );
		}
	}

	return bBaseClassSwitch;
}

CWeaponCSBase* CCSPlayer::GetActiveCSWeapon() const
{
	return dynamic_cast< CWeaponCSBase* >( GetActiveWeapon() );
}

void CCSPlayer::PreThink()
{
	BaseClass::PreThink();
	if ( m_bAutoReload )
	{
		m_bAutoReload = false;
		m_nButtons |= IN_RELOAD;
	}

	if ( m_afButtonLast != m_nButtons )
		m_flLastMovement = gpGlobals->curtime;

	if ( g_fGameOver )
		return;

	State_PreThink();

	if ( m_pHintMessageQueue )
		m_pHintMessageQueue->Update();

	//Reset bullet force accumulator, only lasts one frame
	m_vecTotalBulletForce = vec3_origin;

	if ( mp_autokick.GetBool() && !IsBot() && !IsHLTV() && !IsAutoKickDisabled() && ( GetTeamNumber() == TEAM_CT || GetTeamNumber() == TEAM_TERRORIST )  )
	{
		if ( m_flLastMovement + CSGameRules()->GetRoundLength()*2 < gpGlobals->curtime )
		{
			UTIL_ClientPrintAll( HUD_PRINTCONSOLE, "#Game_idle_kick", GetPlayerName() );
			engine->ServerCommand( UTIL_VarArgs( "kickid %d\n", GetUserID() ) );
			m_flLastMovement = gpGlobals->curtime;
		}
	}
#ifndef _XBOX
	// CS would like their players to continue to update their LastArea since it is displayed in the hud voice chat UI
	// But we won't do the population tracking while dead.
	CNavArea *area = TheNavMesh->GetNavArea( GetAbsOrigin(), 1000 );
	if (area && area != m_lastNavArea)
	{
		m_lastNavArea = area;
		if ( area->GetPlace() != UNDEFINED_PLACE )
		{
			const char *placeName = TheNavMesh->PlaceToName( area->GetPlace() );
			if ( placeName && *placeName )
			{
				Q_strncpy( m_szLastPlaceName.GetForModify(), placeName, MAX_PLACE_NAME_LENGTH );
			}
		}
	}
#endif
}

void CCSPlayer::MoveToNextIntroCamera()
{
	m_pIntroCamera = gEntList.FindEntityByClassname( m_pIntroCamera, "point_viewcontrol" );

	// if m_pIntroCamera is NULL we just were at end of list, start searching from start again
	if(!m_pIntroCamera)
		m_pIntroCamera = gEntList.FindEntityByClassname(m_pIntroCamera, "point_viewcontrol");

	// find the target
	CBaseEntity *Target = NULL;

	if( m_pIntroCamera )
	{
		Target = gEntList.FindEntityByName( NULL, STRING(m_pIntroCamera->m_target) );
	}

	// if we still couldn't find a camera, goto T spawn
	if(!m_pIntroCamera)
		m_pIntroCamera = gEntList.FindEntityByClassname(m_pIntroCamera, "info_player_terrorist");

	SetViewOffset( vec3_origin );	// no view offset
	UTIL_SetSize( this, vec3_origin, vec3_origin ); // no bbox

	if( !Target ) //if there are no cameras(or the camera has no target, find a spawn point and black out the screen
	{
		if ( m_pIntroCamera.IsValid() )
			SetAbsOrigin( m_pIntroCamera->GetAbsOrigin() + VEC_VIEW );

		SetAbsAngles( QAngle( 0, 0, 0 ) );

		m_pIntroCamera = NULL;  // never update again
		return;
	}


	Vector vCamera = Target->GetAbsOrigin() - m_pIntroCamera->GetAbsOrigin();
	Vector vIntroCamera = m_pIntroCamera->GetAbsOrigin();

	VectorNormalize( vCamera );

	QAngle CamAngles;
	VectorAngles( vCamera, CamAngles );

	SetAbsOrigin( vIntroCamera );
	SetAbsAngles( CamAngles );
	SnapEyeAngles( CamAngles );
	m_fIntroCamTime = gpGlobals->curtime + 6;
}

class NotVIP
{
public:
	bool operator()( CBasePlayer *player )
	{
		CCSPlayer *csPlayer = static_cast< CCSPlayer * >(player);
		csPlayer->MakeVIP( false );

		return true;
	}
};

// Expose the VIP selection to plugins, since we don't have an official VIP mode.  This
// allows plugins to access the (limited) VIP functionality already present (scoreboard
// identification and radar color).
CON_COMMAND( cs_make_vip, "Marks a player as the VIP" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	if ( args.ArgC() != 2 )
	{
		return;
	}

	CCSPlayer *player = static_cast< CCSPlayer * >(UTIL_PlayerByIndex( atoi( args[1] ) ));
	if ( !player )
	{
		// Invalid value clears out VIP
		NotVIP notVIP;
		ForEachPlayer( notVIP );
		return;
	}

	player->MakeVIP( true );
}

void CCSPlayer::MakeVIP( bool isVIP )
{
	if ( isVIP )
	{
		NotVIP notVIP;
		ForEachPlayer( notVIP );
	}
	m_isVIP = isVIP;
}

bool CCSPlayer::IsVIP() const
{
	return m_isVIP;
}

void CCSPlayer::DropShield( void )
{
#ifdef CS_SHIELD_ENABLED
	//Drop an item_defuser
	Vector vForward, vRight;
	AngleVectors( GetAbsAngles(), &vForward, &vRight, NULL );

	RemoveShield();

	CBaseAnimating *pShield = (CBaseAnimating *)CBaseEntity::Create( "item_shield", WorldSpaceCenter(), GetLocalAngles() );
	pShield->ApplyAbsVelocityImpulse( vForward * 200 + vRight * random->RandomFloat( -50, 50 ) );

	CBaseCombatWeapon *pActive = GetActiveWeapon();

	if ( pActive )
	{
		pActive->Deploy();
	}
#endif
}

void CCSPlayer::SetShieldDrawnState( bool bState )
{
#ifdef CS_SHIELD_ENABLED
	m_bShieldDrawn = bState;
#endif
}

bool CCSPlayer::CSWeaponDrop( CBaseCombatWeapon *pWeapon, bool bDropShield, bool bThrowForward )
{
	bool bSuccess = false;

	if ( HasShield() && bDropShield == true )
	{
		DropShield();
		return true;
	}

	if ( pWeapon )
	{
		Vector vForward;

		AngleVectors( EyeAngles(), &vForward, NULL, NULL );
		//GetVectors( &vForward, NULL, NULL );
		Vector vTossPos = WorldSpaceCenter();

		if( bThrowForward )
			vTossPos = vTossPos + vForward * 64;

		Weapon_Drop( pWeapon, &vTossPos, NULL );

		pWeapon->SetSolidFlags( FSOLID_NOT_STANDABLE | FSOLID_TRIGGER | FSOLID_USE_TRIGGER_BOUNDS );
		pWeapon->SetMoveCollide( MOVECOLLIDE_FLY_BOUNCE );

		CWeaponCSBase *pCSWeapon = dynamic_cast< CWeaponCSBase* >( pWeapon );

		if( pCSWeapon )
		{
			pCSWeapon->SetWeaponModelIndex( pCSWeapon->GetCSWpnData().szWorldModel );

			//Find out the index of the ammo type
			/*int iAmmoIndex = pCSWeapon->GetPrimaryAmmoType();

			//If it has an ammo type, find out how much the player has
			if( iAmmoIndex != -1 )
			{
				// Check to make sure we don't have other weapons using this ammo type
				bool bAmmoTypeInUse = false;
				if ( IsAlive() && GetHealth() > 0 )
				{
					for ( int i=0; i<MAX_WEAPONS; ++i )
					{
						CBaseCombatWeapon *pOtherWeapon = GetWeapon(i);
						if ( pOtherWeapon && pOtherWeapon != pWeapon && pOtherWeapon->GetPrimaryAmmoType() == iAmmoIndex )
						{
							bAmmoTypeInUse = true;
							break;
						}
					}
				}

				if ( !bAmmoTypeInUse )
				{
					int iAmmoToDrop = GetAmmoCount( iAmmoIndex );

					//Add this much to the dropped weapon
					pCSWeapon->SetExtraAmmoCount( iAmmoToDrop );

					//Remove all ammo of this type from the player
					SetAmmoCount( 0, iAmmoIndex );
				}
			}*/
		}

		//=========================================
		// Teleport the weapon to the player's hand
		//=========================================
		int iBIndex = -1;
		int iWeaponBoneIndex = -1;

		MDLCACHE_CRITICAL_SECTION();
		CStudioHdr *hdr = pWeapon->GetModelPtr();
		// If I have a hand, set the weapon position to my hand bone position.
		if ( hdr && hdr->numbones() > 0 )
		{
			// Assume bone zero is the root
			for ( iWeaponBoneIndex = 0; iWeaponBoneIndex < hdr->numbones(); ++iWeaponBoneIndex )
			{
				iBIndex = LookupBone( hdr->pBone( iWeaponBoneIndex )->pszName() );
				// Found one!
				if ( iBIndex != -1 )
				{
					break;
				}
			}

			if ( iWeaponBoneIndex == hdr->numbones() )
				 return true;

			if ( iBIndex == -1 )
			{
				iBIndex = LookupBone( "ValveBiped.Bip01_R_Hand" );
			}
		}
		else
		{
			iBIndex = LookupBone( "ValveBiped.Bip01_R_Hand" );
		}

		if ( iBIndex != -1)
		{
			Vector origin;
			QAngle angles;
			matrix3x4_t transform;

			// Get the transform for the weapon bonetoworldspace in the NPC
			GetBoneTransform( iBIndex, transform );

			// find offset of root bone from origin in local space
			// Make sure we're detached from hierarchy before doing this!!!
			pWeapon->StopFollowingEntity();
			pWeapon->SetAbsOrigin( Vector( 0, 0, 0 ) );
			pWeapon->SetAbsAngles( QAngle( 0, 0, 0 ) );
			pWeapon->InvalidateBoneCache();
			matrix3x4_t rootLocal;
			pWeapon->GetBoneTransform( iWeaponBoneIndex, rootLocal );

			// invert it
			matrix3x4_t rootInvLocal;
			MatrixInvert( rootLocal, rootInvLocal );

			matrix3x4_t weaponMatrix;
			ConcatTransforms( transform, rootInvLocal, weaponMatrix );
			MatrixAngles( weaponMatrix, angles, origin );

			pWeapon->Teleport( &origin, &angles, NULL );

			//Have to teleport the physics object as well

			IPhysicsObject *pWeaponPhys = pWeapon->VPhysicsGetObject();

			if( pWeaponPhys )
			{
				Vector vPos;
				QAngle vAngles;
				pWeaponPhys->GetPosition( &vPos, &vAngles );
				pWeaponPhys->SetPosition( vPos, angles, true );

				AngularImpulse	angImp(0,0,0);
				Vector vecAdd = GetAbsVelocity();
				pWeaponPhys->AddVelocity( &vecAdd, &angImp );
			}
		}

		bSuccess = true;
	}

	return bSuccess;
}

bool CCSPlayer::CSWeaponDrop( CBaseCombatWeapon *pWeapon, Vector targetPos, bool bDropShield )
{
	bool bSuccess = false;

	if ( HasShield() && bDropShield == true )
	{
		DropShield();
		return true;
	}

	if ( pWeapon )
	{
		Vector vForward;

		AngleVectors( EyeAngles(), &vForward, NULL, NULL );
		//GetVectors( &vForward, NULL, NULL );

		Weapon_Drop( pWeapon, &targetPos, NULL );

		pWeapon->SetSolidFlags( FSOLID_NOT_STANDABLE | FSOLID_TRIGGER | FSOLID_USE_TRIGGER_BOUNDS );
		pWeapon->SetMoveCollide( MOVECOLLIDE_FLY_BOUNCE );

		CWeaponCSBase *pCSWeapon = dynamic_cast< CWeaponCSBase* >(pWeapon);

		if ( pCSWeapon )
		{
			pCSWeapon->SetWeaponModelIndex( pCSWeapon->GetCSWpnData().szWorldModel );

			// set silencer bodygroup
			if ( pCSWeapon->FindBodygroupByName( "silencer" ) > -1 )
				pCSWeapon->SetBodygroup( pCSWeapon->FindBodygroupByName( "silencer" ), pCSWeapon->IsSilenced() ? 0 : 1 );

			//Find out the index of the ammo type
			/*int iAmmoIndex = pCSWeapon->GetPrimaryAmmoType();

			//If it has an ammo type, find out how much the player has
			if ( iAmmoIndex != -1 )
			{
				// Check to make sure we don't have other weapons using this ammo type
				bool bAmmoTypeInUse = false;
				if ( IsAlive() && GetHealth() > 0 )
				{
					for ( int i = 0; i<MAX_WEAPONS; ++i )
					{
						CBaseCombatWeapon *pOtherWeapon = GetWeapon( i );
						if ( pOtherWeapon && pOtherWeapon != pWeapon && pOtherWeapon->GetPrimaryAmmoType() == iAmmoIndex )
						{
							bAmmoTypeInUse = true;
							break;
						}
					}
				}

				if ( !bAmmoTypeInUse )
				{
					int iAmmoToDrop = GetAmmoCount( iAmmoIndex );

					//Add this much to the dropped weapon
					pCSWeapon->SetExtraAmmoCount( iAmmoToDrop );

					//Remove all ammo of this type from the player
					SetAmmoCount( 0, iAmmoIndex );
				}
			}*/
		}

		//=========================================
		// Teleport the weapon to the player's hand
		//=========================================
		int iBIndex = -1;
		int iWeaponBoneIndex = -1;

		MDLCACHE_CRITICAL_SECTION();

		if ( !m_bUseNewAnimstate )
		{

			// now we use the weapon_bone to drop the item from.  Previously we were incorrectly using the root position from the character
			iBIndex = LookupBone( "ValveBiped.weapon_bone" );
			iWeaponBoneIndex = pWeapon->LookupBone( "ValveBiped.weapon_bone" );

			// dkorus: If we hit this assert, the model changed and we no longer have a valid "ValveBiped.weapon_bone" to use for our weapon drop position
			//		   This code will have to change to match the new bone name
			AssertMsg( iBIndex != -1, "Missing weapon bone from player!  Make sure the bone exists and or that the string is updated." );

			if ( iBIndex == -1 || iWeaponBoneIndex == -1 )
			{
				iBIndex = LookupBone( "ValveBiped.Bip01_R_Hand" );
				iWeaponBoneIndex = 0; // use the root
			}

			if ( iBIndex != -1 )  
			{
				Vector origin;
				QAngle angles;
				matrix3x4_t transform;

				// Get the transform for the weapon bonetoworldspace in the NPC
				GetBoneTransform( iBIndex, transform );

				// find offset of root bone from origin in local space
				// Make sure we're detached from hierarchy before doing this!!!
				pWeapon->StopFollowingEntity();
				MatrixAngles( transform, angles, origin );

				pWeapon->SetAbsOrigin( Vector( 0, 0, 0 ) );
				pWeapon->SetAbsAngles( QAngle( 0, 0, 0 ) );
				pWeapon->InvalidateBoneCache();
				matrix3x4_t rootLocal;
				pWeapon->GetBoneTransform( iWeaponBoneIndex, rootLocal );

				// invert it
				matrix3x4_t rootInvLocal;
				MatrixInvert( rootLocal, rootInvLocal );

				matrix3x4_t weaponMatrix;
				ConcatTransforms( transform, rootInvLocal, weaponMatrix );
				MatrixAngles( weaponMatrix, angles, origin );

				// run a hull trace to prevent throwing guns through walls or world geometry
				trace_t trDropTrace;
				UTIL_TraceHull( EyePosition(), origin, Vector( -5, -5, -5 ), Vector( 5, 5, 5 ), MASK_SOLID, this, COLLISION_GROUP_PLAYER_MOVEMENT, &trDropTrace );
				if ( trDropTrace.fraction != 1.0 )
				{
					////uncomment to see debug visualization
					//debugoverlay->AddBoxOverlay( origin, Vector(-5,-5,-5), Vector(5,5,5), QAngle(0,0,0), 0,200,0,128, 4.0f );
					//debugoverlay->AddBoxOverlay( EyePosition(), Vector(-5,-5,-5), Vector(5,5,5), QAngle(0,0,0), 200,0,0,128, 4.0f );
					//debugoverlay->AddLineOverlay( EyePosition(), origin, 255,0,0, true, 4.0f );

					// move the weapon drop position to a valid point between the player's eyes (assumed valid) and their right hand (assumed invalid)
					origin -= (( origin - EyePosition() ) * trDropTrace.fraction);

					//debugoverlay->AddBoxOverlay( origin, Vector(-5,-5,-5), Vector(5,5,5), QAngle(0,0,0), 0,0,200,128, 4.0f );
				}

				pWeapon->Teleport( &origin, &angles, NULL );
			
				//Have to teleport the physics object as well
				IPhysicsObject *pWeaponPhys = pWeapon->VPhysicsGetObject();

				if( pWeaponPhys )
				{
					Vector vPos;
					QAngle vAngles;

					pWeaponPhys->GetPosition( &vPos, &vAngles );
					pWeaponPhys->SetPosition( vPos, vAngles, true );

					AngularImpulse	angImp(0,0,0 );
					Vector vecAdd = (GetAbsVelocity() * 0.5f) + Vector( 0, 0, 110 );
					pWeaponPhys->AddVelocity( &vecAdd, &angImp );
				}
			}

		}
		else
		{
			Assert( pWeapon->GetModel() );

			Vector vecWeaponThrowFromPos = EyePosition();
			QAngle angWeaponThrowFromAngle = EyeAngles();

			int nPlayerRightHandAttachment = LookupAttachment( "weapon_hand_R" );
			if ( nPlayerRightHandAttachment != -1 )
			{
				bool bAttachSuccess = GetAttachment( nPlayerRightHandAttachment, vecWeaponThrowFromPos );
				Assert( bAttachSuccess ); bAttachSuccess;
			}
			else
			{
				DevWarning( "Warning: Can't find player's right hand attachment! [weapon_hand_R]\n" );
			}

			pWeapon->StopFollowingEntity();

			// run a hull trace to prevent throwing guns through walls or world geometry
			// Note we do a conservative trace here that blocks against more stuff than is absolutely necessary
			// (we could figure out what kind of object is being thrown, and use a different trace based on that,
			// but it doesn't really matter since it will just move the drop point slightly back towards your head)
			trace_t trDropTrace;
			UTIL_TraceHull( EyePosition(), vecWeaponThrowFromPos, Vector( -5, -5, -5 ), Vector( 5, 5, 5 ), MASK_PLAYERSOLID|CONTENTS_GRENADECLIP, this, COLLISION_GROUP_PLAYER_MOVEMENT, &trDropTrace );
			if ( trDropTrace.fraction != 1.0 )
			{

				//uncomment to see debug visualization
				//debugoverlay->AddBoxOverlay( vecWeaponThrowFromPos, Vector(-5,-5,-5), Vector(5,5,5), QAngle(0,0,0), 0,200,0,128, 4.0f );
				//debugoverlay->AddBoxOverlay( EyePosition(), Vector(-5,-5,-5), Vector(5,5,5), QAngle(0,0,0), 200,0,0,128, 4.0f );
				//debugoverlay->AddLineOverlay( EyePosition(), vecWeaponThrowFromPos, 255,0,0, true, 4.0f );

				// move the weapon drop position to a valid point between the player's eyes (assumed valid) and their right hand (assumed invalid)
				vecWeaponThrowFromPos -= (( vecWeaponThrowFromPos - EyePosition() ) * trDropTrace.fraction);
			}
			
			//debugoverlay->AddBoxOverlay( vecWeaponThrowFromPos, Vector(-1,-1,-1), Vector(1,1,1), QAngle(0,0,0), 0,0,200,128, 4.0f );

			pWeapon->SetAbsOrigin( vecWeaponThrowFromPos );
			pWeapon->SetAbsAngles( angWeaponThrowFromAngle );
			
			if ( pWeapon->m_hWeaponWorldModel.Get() )
				pWeapon->Teleport( &vecWeaponThrowFromPos, &angWeaponThrowFromAngle, NULL );

			//Have to teleport the physics object as well
			IPhysicsObject *pWeaponPhys = pWeapon->VPhysicsGetObject();

			if ( pWeaponPhys )
			{
				Vector vPos;
				QAngle vAngles;

				pWeaponPhys->GetPosition( &vPos, &vAngles );
				pWeaponPhys->SetPosition( vPos, vAngles, true );

				AngularImpulse	angImp(0,0,0 );
				Vector vecAdd = (GetAbsVelocity() * 0.5f) + Vector( 0, 0, 110 );
				pWeaponPhys->AddVelocity( &vecAdd, &angImp );
			}
		}

		bSuccess = true;
	}

	return bSuccess;
}


void CCSPlayer::TransferInventory( CCSPlayer* pTargetPlayer )
{
	// first, transfer money
	pTargetPlayer->m_iAccount = m_iAccount;

	// check is any slot has weapon in it and if so - give it
	CBaseCombatWeapon* pKnife = Weapon_GetSlot( WEAPON_SLOT_KNIFE ); // TODO: check for taser conflicts
	CWeaponCSBase* pPistol = dynamic_cast< CWeaponCSBase* >( Weapon_GetSlot( WEAPON_SLOT_PISTOL ) );
	CWeaponCSBase* pPrimary = dynamic_cast< CWeaponCSBase* >( Weapon_GetSlot( WEAPON_SLOT_RIFLE ) );
	CBaseCombatWeapon* pC4 = Weapon_GetSlot( WEAPON_SLOT_C4 );

	if ( pKnife )
	{
		pTargetPlayer->GiveNamedItem( pKnife->GetClassname() );
		UTIL_Remove( pKnife ); // TODO: check for any problems because of this
	}

	if ( pPistol )
	{
		pTargetPlayer->GiveNamedItem( pPistol->GetClassname() );

		// transfer ammo as well
		CWeaponCSBase* pTransferedPistol = dynamic_cast< CWeaponCSBase* >( pTargetPlayer->Weapon_GetSlot( WEAPON_SLOT_PISTOL ) );
		if ( pTransferedPistol )
		{
			pTransferedPistol->SetReserveAmmoCount( AMMO_POSITION_PRIMARY, pPistol->GetReserveAmmoCount( AMMO_POSITION_PRIMARY ) );
			pTransferedPistol->SetReserveAmmoCount( AMMO_POSITION_SECONDARY, pPistol->GetReserveAmmoCount( AMMO_POSITION_SECONDARY ) );
			pTransferedPistol->m_iClip1 = pPistol->m_iClip1;
			pTransferedPistol->m_iClip2 = pPistol->m_iClip2;
		}
		// also transfer silencer state
		pTransferedPistol->SetSilencer( pPistol->IsSilenced() );

		UTIL_Remove( pPistol ); // TODO: check for any problems because of this
	}

	if ( pPrimary )
	{
		pTargetPlayer->GiveNamedItem( pPrimary->GetClassname() );

		// transfer ammo as well
		CWeaponCSBase* pTransferedPrimary = dynamic_cast< CWeaponCSBase* >( pTargetPlayer->Weapon_GetSlot( WEAPON_SLOT_RIFLE ) );
		if ( pTransferedPrimary )
		{
			pTransferedPrimary->SetReserveAmmoCount( AMMO_POSITION_PRIMARY, pPrimary->GetReserveAmmoCount( AMMO_POSITION_PRIMARY ) );
			pTransferedPrimary->SetReserveAmmoCount( AMMO_POSITION_SECONDARY, pPrimary->GetReserveAmmoCount( AMMO_POSITION_SECONDARY ) );
			pTransferedPrimary->m_iClip1 = pPrimary->m_iClip1;
			pTransferedPrimary->m_iClip2 = pPrimary->m_iClip2;
		}
		// also transfer silencer state
		pTransferedPrimary->SetSilencer( pPrimary->IsSilenced() );

		UTIL_Remove( pPrimary ); // TODO: check for any problems because of this
	}

	if ( pC4 )
	{
		pTargetPlayer->GiveNamedItem( pC4->GetClassname() );
		UTIL_Remove( pC4 ); // TODO: check for any problems because of this
	}

	// now do the same process with grenades
	const char* GrenadeClassnames[] =
	{
		"weapon_molotov",
		"weapon_incgrenade",
		"weapon_smokegrenade",
		"weapon_hegrenade",
		"weapon_flashbang",
		"weapon_decoy",
	};
	CSWeaponID GrenadeInfo[] =
	{
		WEAPON_MOLOTOV,
		WEAPON_INCGRENADE,
		WEAPON_SMOKEGRENADE,
		WEAPON_HEGRENADE,
		WEAPON_FLASHBANG,
		WEAPON_DECOY,
	};

	CWeaponCSBase* pGrenade;
	for ( int i = 0; i < ARRAYSIZE( GrenadeClassnames ); i++ )
	{
		pGrenade = dynamic_cast< CWeaponCSBase* >( Weapon_OwnsThisType( GrenadeClassnames[i] ) );
		if ( pGrenade )
		{
			pTargetPlayer->GiveNamedItem( GrenadeClassnames[i] );
			if ( pTargetPlayer->Weapon_OwnsThisType( GrenadeClassnames[i] ) )
			{
				int ammoType = GetWeaponInfo( GrenadeInfo[i] )->iAmmoType;
				pTargetPlayer->SetAmmoCount( GetAmmoCount( ammoType ), ammoType );
			}
		}
	}

	pTargetPlayer->SetArmorValue( ArmorValue() );
	pTargetPlayer->m_bHasHelmet = m_bHasHelmet;
	pTargetPlayer->m_bHasNightVision = m_bHasNightVision;

	if ( HasDefuser() )
		pTargetPlayer->GiveDefuser();

	// as part of transferring inventory, remove what WE have
	SetArmorValue( 0 );
	m_bHasHelmet = false;
	m_bHasNightVision = false;
	m_bNightVisionOn = false;

	RemoveAllItems( true );
}

bool CCSPlayer::DropRifle( bool fromDeath )
{
	bool bSuccess = false;

	CBaseCombatWeapon *pWeapon = Weapon_GetSlot( WEAPON_SLOT_RIFLE );
	if ( pWeapon )
	{
		bSuccess = CSWeaponDrop( pWeapon, false );
	}

	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Add the dropped weapon to the dropped equipment list
	//=============================================================================
	if( fromDeath && bSuccess )
	{
		m_hDroppedEquipment[DROPPED_WEAPON] = static_cast<CBaseEntity *>(pWeapon);
	}
	//=============================================================================
	// HPE_END
	//=============================================================================

	return bSuccess;
}


bool CCSPlayer::DropPistol( bool fromDeath )
{
	bool bSuccess = false;

	CBaseCombatWeapon *pWeapon = Weapon_GetSlot( WEAPON_SLOT_PISTOL );
	if ( pWeapon )
	{
		bSuccess = CSWeaponDrop( pWeapon, false );
		m_bUsingDefaultPistol = false;
	}
	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Add the dropped weapon to the dropped equipment list
	//=============================================================================
	if( fromDeath && bSuccess )
	{
		m_hDroppedEquipment[DROPPED_WEAPON] = static_cast<CBaseEntity *>(pWeapon);
	}
	//=============================================================================
	// HPE_END
	//=============================================================================

	return bSuccess;
}

bool CCSPlayer::HasPrimaryWeapon( void )
{
	bool bSuccess = false;

	CBaseCombatWeapon *pWeapon = Weapon_GetSlot( WEAPON_SLOT_RIFLE );

	if ( pWeapon )
	{
		bSuccess = true;
	}

	return bSuccess;
}


bool CCSPlayer::HasSecondaryWeapon( void )
{
	bool bSuccess = false;

	CBaseCombatWeapon *pWeapon = Weapon_GetSlot( WEAPON_SLOT_PISTOL );
	if ( pWeapon )
	{
		bSuccess = true;
	}

	return bSuccess;
}

bool CCSPlayer::IsInBuyZone()
{
	if ( mp_buy_anywhere.GetInt() == 1 ||
		 mp_buy_anywhere.GetInt() == GetTeamNumber() )
		 return true;

	return m_bInBuyZone && !IsVIP();
}

bool CCSPlayer::CanPlayerBuy( bool display )
{
	// is the player in a buy zone?
	if ( !IsInBuyZone() )
	{
		return false;
	}

	CCSGameRules* mp = CSGameRules();

	// Don't allow buying in the last few seconds of warmup because everybody should be freezed, but sometimes people aren't
	// also fixes buy on the very moment that round starts which might cause the bought weapon to spawn, but touched by the
	// player in the actual match time next frame and have a powerful gun for the first pistol round.
	if ( CSGameRules()->IsWarmupPeriod() && ( CSGameRules()->GetWarmupPeriodEndTime() - 3 < gpGlobals->curtime ) )
		return false;

	// is the player alive?
	if ( m_lifeState != LIFE_ALIVE )
	{
		return false;
	}

	int buyTime = (int)mp_buytime.GetFloat();

	if ( mp->IsBuyTimeElapsed() )
	{
		if ( display == true )
		{
			char strBuyTime[16];
			Q_snprintf( strBuyTime, sizeof( strBuyTime ), "%d", buyTime );
			ClientPrint( this, HUD_PRINTCENTER, "#Cant_buy", strBuyTime );
		}

		return false;
	}

	if ( m_bIsVIP )
	{
		if ( display == true )
			ClientPrint( this, HUD_PRINTCENTER, "#VIP_cant_buy" );

		return false;
	}

	if ( mp->m_bCTCantBuy && ( GetTeamNumber() == TEAM_CT ) )
	{
		if ( display == true )
			ClientPrint( this, HUD_PRINTCENTER, "#CT_cant_buy" );

		return false;
	}

	if ( mp->m_bTCantBuy && ( GetTeamNumber() == TEAM_TERRORIST ) )
	{
		if ( display == true )
			ClientPrint( this, HUD_PRINTCENTER, "#Terrorist_cant_buy" );

		return false;
	}

	return true;
}


BuyResult_e CCSPlayer::AttemptToBuyVest( void )
{
	if ( CSGameRules()->IsArmorFree() )
		return BUY_NOT_ALLOWED;

	int iKevlarPrice = KEVLAR_PRICE;

	if ( CSGameRules()->IsBlackMarket() )
	{
		iKevlarPrice = CSGameRules()->GetBlackMarketPriceForWeapon( WEAPON_KEVLAR );
	}

	if ( ArmorValue() >= 100 )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_Kevlar" );
		return BUY_ALREADY_HAVE;
	}
	else if ( m_iAccount < iKevlarPrice )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Not_Enough_Money" );
		return BUY_CANT_AFFORD;
	}
	else
	{
		if ( m_bHasHelmet )
		{
			if( !m_bIsInAutoBuy && !m_bIsInRebuy )
				ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_Helmet_Bought_Kevlar" );
		}

		IGameEvent * event = gameeventmanager->CreateEvent( "item_pickup" );
		if( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetString( "item", "vest" );
			event->SetBool( "silent", false );
			gameeventmanager->FireEvent( event );
		}

		EmitSound( "Player.PickupWeapon" );

		GiveNamedItem( "item_kevlar" );
		AddAccount( -iKevlarPrice, true, true, "item_kevlar" );
		BlackMarketAddWeapon( "item_kevlar", this );
		return BUY_BOUGHT;
	}
}


BuyResult_e CCSPlayer::AttemptToBuyAssaultSuit( void )
{
	if ( CSGameRules()->IsArmorFree() )
		return BUY_NOT_ALLOWED;

	// WARNING: This price logic also exists in C_CSPlayer::GetCurrentAssaultSuitPrice
	// and must be kept in sync if changes are made.

	int fullArmor = ArmorValue() >= 100 ? 1 : 0;

	int price = 0, enoughMoney = 0;

	int iHelmetPrice = HELMET_PRICE;
	int iKevlarPrice = KEVLAR_PRICE;
	int iAssaultSuitPrice = ASSAULTSUIT_PRICE;

	if ( CSGameRules()->IsBlackMarket() )
	{
		iKevlarPrice = CSGameRules()->GetBlackMarketPriceForWeapon( WEAPON_KEVLAR );
		iAssaultSuitPrice = CSGameRules()->GetBlackMarketPriceForWeapon( WEAPON_ASSAULTSUIT );

		iHelmetPrice = iAssaultSuitPrice - iKevlarPrice;
	}

	if ( fullArmor && m_bHasHelmet )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_Kevlar_Helmet" );
		return BUY_ALREADY_HAVE;
	}
	else if ( fullArmor && !m_bHasHelmet && m_iAccount >= iHelmetPrice )
	{
		enoughMoney = 1;
		price = iHelmetPrice;
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_Kevlar_Bought_Helmet" );
	}
	else if ( !fullArmor && m_bHasHelmet && m_iAccount >= iKevlarPrice )
	{
		enoughMoney = 1;
		price = iKevlarPrice;
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_Helmet_Bought_Kevlar" );
	}
	else if ( m_iAccount >= iAssaultSuitPrice )
	{
		enoughMoney = 1;
		price = iAssaultSuitPrice;
	}

	// process the result
	if ( !enoughMoney )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Not_Enough_Money" );
		return BUY_CANT_AFFORD;
	}
	else
	{
		IGameEvent * event = gameeventmanager->CreateEvent( "item_pickup" );
		if( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetString( "item", "vesthelm" );
			event->SetBool( "silent", false );
			gameeventmanager->FireEvent( event );
		}

		EmitSound( "Player.PickupWeapon" );

		GiveNamedItem( "item_assaultsuit" );
		AddAccount( -price, true, true, "item_assaultsuit" );
		BlackMarketAddWeapon( "item_assaultsuit", this );
		return BUY_BOUGHT;
	}
}

BuyResult_e CCSPlayer::AttemptToBuyShield( void )
{
#ifdef CS_SHIELD_ENABLED
	if ( HasShield() )		// prevent this guy from buying more than 1 Defuse Kit
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_One" );
		return BUY_ALREADY_HAVE;
	}
	else if ( m_iAccount < SHIELD_PRICE )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Not_Enough_Money" );
		return BUY_CANT_AFFORD;
	}
	else
	{
		if ( HasSecondaryWeapon() )
		{
			CBaseCombatWeapon *pWeapon = Weapon_GetSlot( WEAPON_SLOT_PISTOL );
			CWeaponCSBase *pCSWeapon = dynamic_cast< CWeaponCSBase* >( pWeapon );

			if ( pCSWeapon && pCSWeapon->GetCSWpnData().m_bCanUseWithShield == false )
				 return;
		}

		if ( HasPrimaryWeapon() )
			 DropRifle();

		GiveShield();

		CPASAttenuationFilter filter( this, "Player.PickupWeapon" );
		EmitSound( filter, entindex(), "Player.PickupWeapon" );

		m_bAnythingBought = true;
		AddAccount( -SHIELD_PRICE, true, true, "item_shield" );
		return BUY_BOUGHT;
	}
#else
	ClientPrint( this, HUD_PRINTCENTER, "Tactical shield disabled" );
	return BUY_NOT_ALLOWED;
#endif
}

BuyResult_e CCSPlayer::AttemptToBuyDefuser( void )
{
	CCSGameRules *MPRules = CSGameRules();

	if( ( GetTeamNumber() == TEAM_CT ) && ( MPRules->IsBombDefuseMap() || MPRules->IsHostageRescueMap() ) )
	{
		if ( HasDefuser() )		// prevent this guy from buying more than 1 Defuse Kit
		{
			if( !m_bIsInAutoBuy && !m_bIsInRebuy )
				ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_One" );
			return BUY_ALREADY_HAVE;
		}
		else if ( m_iAccount < DEFUSEKIT_PRICE )
		{
			if( !m_bIsInAutoBuy && !m_bIsInRebuy )
				ClientPrint( this, HUD_PRINTCENTER, "#Not_Enough_Money" );
			return BUY_CANT_AFFORD;
		}
		else
		{
			GiveDefuser();

			CPASAttenuationFilter filter( this, "Player.PickupWeapon" );
			EmitSound( filter, entindex(), "Player.PickupWeapon" );

			AddAccount( -DEFUSEKIT_PRICE, true, true, "item_defuser" );
			return BUY_BOUGHT;
		}
	}

	return BUY_NOT_ALLOWED;
}

BuyResult_e CCSPlayer::AttemptToBuyNightVision( void )
{
	int iNVGPrice = NVG_PRICE;

	if ( CSGameRules()->IsBlackMarket() )
	{
		iNVGPrice = CSGameRules()->GetBlackMarketPriceForWeapon( WEAPON_NVG );
	}

	if ( m_bHasNightVision == TRUE )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_One" );
		return BUY_ALREADY_HAVE;
	}
	else if ( m_iAccount < iNVGPrice )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Not_Enough_Money" );
		return BUY_CANT_AFFORD;
	}
	else
	{
		IGameEvent * event = gameeventmanager->CreateEvent( "item_pickup" );
		if( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetString( "item", "nvgs" );
			event->SetBool( "silent", false );
			gameeventmanager->FireEvent( event );
		}

		EmitSound( "Player.PickupWeapon" );

		GiveNamedItem( "item_nvgs" );
		AddAccount( -iNVGPrice, true, true );
		BlackMarketAddWeapon( "nightvision", this );

		if ( !(m_iDisplayHistoryBits & DHF_NIGHTVISION) )
		{
			HintMessage( "#Hint_use_nightvision", false );
			m_iDisplayHistoryBits |= DHF_NIGHTVISION;
		}
		return BUY_BOUGHT;
	}
}

BuyResult_e CCSPlayer::AttemptToBuyTaser( void )
{
	if ( Weapon_OwnsThisType( "weapon_taser" ) )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Already_Have_One" );
		return BUY_ALREADY_HAVE;
	}
	else if ( m_iAccount < TASER_PRICE )
	{
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
			ClientPrint( this, HUD_PRINTCENTER, "#Not_Enough_Money" );
		return BUY_CANT_AFFORD;
	}
	else
	{
		IGameEvent * event = gameeventmanager->CreateEvent( "item_pickup" );
		if( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetString( "item", "nvgs" );
			event->SetBool( "silent", false );
			gameeventmanager->FireEvent( event );
		}

		EmitSound( "Player.PickupWeapon" );

		GiveNamedItem( "weapon_taser" );
		AddAccount( -TASER_PRICE, true, true );

		return BUY_BOUGHT;
	}
}


// Handles the special "buy" alias commands we're creating to accommodate the buy
// scripts players use (now that we've rearranged the buy menus and broken the scripts)
//=============================================================================
// HPE_BEGIN:
//[tj]  This is essentially a shim so I can easily check the return
//      value without adding new code to all the return points.
//=============================================================================

BuyResult_e CCSPlayer::HandleCommand_Buy( const char *item )
{
	const char* loadoutItem = CSLoadout()->GetWeaponFromSlot( this, CSLoadout()->GetSlotFromWeapon( this, item ) );
	if ( loadoutItem != NULL )
		item = loadoutItem;

	BuyResult_e result = HandleCommand_Buy_Internal(item);
	if (result == BUY_BOUGHT)
	{
		m_bMadePurchseThisRound = true;
		CCS_GameStats.IncrementStat(this, CSSTAT_ITEMS_PURCHASED, 1);
	}
	return result;
}

BuyResult_e CCSPlayer::HandleCommand_Buy_Internal( const char* wpnName ) 
//=============================================================================
// HPE_END
//=============================================================================
{
	BuyResult_e result = CanPlayerBuy( false ) ? BUY_PLAYER_CANT_BUY : BUY_INVALID_ITEM; // set some defaults

	// translate the new weapon names to the old ones that are actually being used.
	wpnName = GetTranslatedWeaponAlias(wpnName);

	CCSWeaponInfo *pWeaponInfo = GetWeaponInfo( AliasToWeaponID( wpnName ) );
	if ( pWeaponInfo == NULL )
	{
		// UPD: oh wait, you can't rebuy ammo in cs:go...
		/*if ( Q_stricmp( wpnName, "primammo" ) == 0 )
		{
			result = AttemptToBuyAmmo( 0 );

			// it buys more ammo than it should be because
			// GetReserveAmmoMax() returns max ammo for ammo
			// type and not for weapon! It's happening because
			// it checks if player (not weapon) has gun's type
			// of ammo and if so, uses it's max capacity instead
			// of gun's max capacity because player for some
			// resaon has some ammo but he shouldn't
			// solution: always return 0 in pPlayer->GetAmmoCount()?
		}
		else if ( Q_stricmp( wpnName, "secammo" ) == 0 )
		{
			result = AttemptToBuyAmmo( 1 );
		}
		else*/ if ( Q_stristr( wpnName, "defuser" )  )
		{
			if( CanPlayerBuy( true ) )
			{
				result = AttemptToBuyDefuser();
			}
		}
		else if ( Q_stristr( wpnName, "taser" )  )
		{
			if ( CanPlayerBuy( true ) )
				result = AttemptToBuyTaser();
		}
	}
	else
	{

		if( !CanPlayerBuy( true ) )
		{
			return BUY_PLAYER_CANT_BUY;
		}

		BuyResult_e equipResult = BUY_INVALID_ITEM;

		if ( Q_stristr( wpnName, "kevlar" ) )
		{
			equipResult = AttemptToBuyVest();
		}
		else if ( Q_stristr( wpnName, "assaultsuit" ) )
		{
			equipResult = AttemptToBuyAssaultSuit();
		}
		else if ( Q_stristr( wpnName, "shield" ) )
		{
			equipResult = AttemptToBuyShield();
		}
		else if ( Q_stristr( wpnName, "nightvision" )  )
		{
			equipResult = AttemptToBuyNightVision();
		}

		if ( equipResult != BUY_INVALID_ITEM )
		{
			if ( equipResult == BUY_BOUGHT )
			{
				BuildRebuyStruct();
			}
			return equipResult; // intentional early return here
		}

		bool bPurchase = false;

		// MIKETODO: assasination maps have a specific set of weapons that can be used in them.
		if ( pWeaponInfo->m_iTeam != TEAM_UNASSIGNED && GetTeamNumber() != pWeaponInfo->m_iTeam )
		{
			result = BUY_NOT_ALLOWED;
			if ( pWeaponInfo->m_WrongTeamMsg[0] != 0 )
			{
				ClientPrint( this, HUD_PRINTCENTER, "#Alias_Not_Avail", pWeaponInfo->m_WrongTeamMsg );
			}
		}
		else if ( pWeaponInfo->GetWeaponPrice() <= 0 )
		{
			// ClientPrint( this, HUD_PRINTCENTER, "#Cant_buy_this_item", pWeaponInfo->m_WrongTeamMsg );
		}
		else if( pWeaponInfo->m_WeaponType == WEAPONTYPE_GRENADE )
		{
			// make sure the player can afford this item.
			if ( m_iAccount >= pWeaponInfo->GetWeaponPrice() )
			{
				bPurchase = true;

				const char* weaponName = pWeaponInfo->szClassName;
				if ( strncmp( weaponName, "weapon_", 7 ) == 0 )
				{
					weaponName += 7;
				}

				switch ( CanAcquire( AliasToWeaponID( weaponName ), AcquireMethod::Buy ) )
				{
					case AcquireResult::ReachedGrenadeTypeLimit:
					case AcquireResult::ReachedGrenadeTotalLimit:
					case AcquireResult::AlreadyOwned:
					case AcquireResult::AlreadyPurchased:
					{
						result = BUY_ALREADY_HAVE;
						if ( !m_bIsInAutoBuy && !m_bIsInRebuy )
							ClientPrint( this, HUD_PRINTCENTER, "#Cannot_Carry_Anymore" );
						bPurchase = false;
					}
				}
			}
		}
		else if ( !Weapon_OwnsThisType( pWeaponInfo->szClassName ) )	//don't buy duplicate weapons
		{
			// do they have enough money?
			if ( m_iAccount >= pWeaponInfo->GetWeaponPrice() )
			{
				if ( m_lifeState != LIFE_DEAD )
				{
					if ( pWeaponInfo->iSlot == WEAPON_SLOT_PISTOL )
					{
						DropPistol();
					}
					else if ( pWeaponInfo->iSlot == WEAPON_SLOT_RIFLE )
					{
						DropRifle();
					}
				}

				bPurchase = true;
			}
			else
			{
				result = BUY_CANT_AFFORD;
				if( !m_bIsInAutoBuy && !m_bIsInRebuy )
					ClientPrint( this, HUD_PRINTCENTER, "#Not_Enough_Money" );
			}
		}
		else
		{
			result = BUY_ALREADY_HAVE;
		}

		if ( HasShield() )
		{
			if ( pWeaponInfo->m_bCanUseWithShield == false )
			{
				result = BUY_NOT_ALLOWED;
				bPurchase = false;
			}
		}

		if( bPurchase )
		{
			result = BUY_BOUGHT;

			if ( bPurchase && pWeaponInfo->iSlot == WEAPON_SLOT_PISTOL )
				m_bUsingDefaultPistol = false;

			GiveNamedItem( pWeaponInfo->szClassName );
            AddAccount( -pWeaponInfo->GetWeaponPrice(), true, true, pWeaponInfo->szClassName );
			BlackMarketAddWeapon( wpnName, this );
		}
	}

	if ( result == BUY_BOUGHT )
	{
		BuildRebuyStruct();
	}

	return result;
}


BuyResult_e CCSPlayer::BuyGunAmmo( CBaseCombatWeapon *pWeapon, bool bBlinkMoney )
{
	if ( !CanPlayerBuy( false ) )
	{
		return BUY_PLAYER_CANT_BUY;
	}

	// Ensure that the weapon uses ammo
	int nAmmo = pWeapon->GetPrimaryAmmoType();
	if ( nAmmo == -1 )
	{
		return BUY_ALREADY_HAVE;
	}

	// Can only buy if the player does not already have full ammo
	int maxcarry = pWeapon->GetReserveAmmoMax( AMMO_POSITION_PRIMARY );

	if ( pWeapon->GetReserveAmmoCount( AMMO_POSITION_PRIMARY ) >= maxcarry )
	{
		return BUY_ALREADY_HAVE;
	}

	// Purchase the ammo if the player has enough money
	if ( m_iAccount >= GetCSAmmoDef()->GetCost( nAmmo ) )
	{
		GiveAmmo( GetCSAmmoDef()->GetBuySize( nAmmo ), nAmmo, true );
		AddAccount( -GetCSAmmoDef()->GetCost( nAmmo ), true, true, GetCSAmmoDef()->GetAmmoOfIndex( nAmmo )->pName  );
		return BUY_BOUGHT;
	}

	if ( bBlinkMoney )
	{
		// Not enough money.. let the player know
		if( !m_bIsInAutoBuy && !m_bIsInRebuy )
					ClientPrint( this, HUD_PRINTCENTER, "#Not_Enough_Money" );
	}

	return BUY_CANT_AFFORD;
}


BuyResult_e CCSPlayer::BuyAmmo( int nSlot, bool bBlinkMoney )
{
	if ( !CanPlayerBuy( false ) )
	{
		return BUY_PLAYER_CANT_BUY;
	}

	if ( nSlot < 0 || nSlot > 1 )
	{
		return BUY_INVALID_ITEM;
	}

	// Buy one ammo clip for all weapons in the given slot
	//
	//  nSlot == 1 : Primary weapons
	//  nSlot == 2 : Secondary weapons

	CBaseCombatWeapon *pSlot = Weapon_GetSlot( nSlot );
	if ( !pSlot )
		return BUY_INVALID_ITEM;

	//MIKETODO: shield.
	//if ( player->HasShield() && player->m_rgpPlayerItems[2] )
	//	 pItem = player->m_rgpPlayerItems[2];

	return BuyGunAmmo( pSlot, bBlinkMoney );
}


BuyResult_e CCSPlayer::AttemptToBuyAmmo( int iAmmoType )
{
	Assert( iAmmoType == 0 || iAmmoType == 1 );

	BuyResult_e result = BuyAmmo( iAmmoType, true );

	if ( result == BUY_BOUGHT )
	{
		while ( BuyAmmo( iAmmoType, false ) == BUY_BOUGHT )
		{
			// empty loop - keep buying
		}

		return BUY_BOUGHT;
	}

	return result;
}

BuyResult_e CCSPlayer::AttemptToBuyAmmoSingle( int iAmmoType )
{
	Assert( iAmmoType == 0 || iAmmoType == 1 );

	BuyResult_e result = BuyAmmo( iAmmoType, true );

	if ( result == BUY_BOUGHT )
	{
		BuildRebuyStruct();
	}

	return result;
}

const char *RadioEventName[ RADIO_NUM_EVENTS+1 ] =
{
	"RADIO_INVALID",

	"EVENT_START_RADIO_1",

	"EVENT_RADIO_GO_GO_GO",
	"EVENT_RADIO_TEAM_FALL_BACK",
	"EVENT_RADIO_STICK_TOGETHER_TEAM",
	"EVENT_RADIO_HOLD_THIS_POSITION",
	"EVENT_RADIO_FOLLOW_ME",

	"EVENT_START_RADIO_2",

	"EVENT_RADIO_AFFIRMATIVE",
	"EVENT_RADIO_NEGATIVE",
	"EVENT_RADIO_CHEER",
	"EVENT_RADIO_COMPLIMENT",
	"EVENT_RADIO_THANKS",

	"EVENT_START_RADIO_3",

	"EVENT_RADIO_ENEMY_SPOTTED",
	"EVENT_RADIO_NEED_BACKUP",
	"EVENT_RADIO_YOU_TAKE_THE_POINT",
	"EVENT_RADIO_SECTOR_CLEAR",
	"EVENT_RADIO_IN_POSITION",

	// unused
	"EVENT_RADIO_COVER_ME",
	"EVENT_RADIO_REGROUP_TEAM",
	"EVENT_RADIO_TAKING_FIRE",
	"EVENT_RADIO_REPORT_IN_TEAM",
	"EVENT_RADIO_REPORTING_IN",
	"EVENT_RADIO_GET_OUT_OF_THERE",
	"EVENT_RADIO_ENEMY_DOWN",
	"EVENT_RADIO_STORM_THE_FRONT",

	"EVENT_RADIO_END",

	NULL		// must be NULL-terminated
};


/**
 * Convert name to RadioType
 */
RadioType NameToRadioEvent( const char *name )
{
	for( int i=0; RadioEventName[i]; ++i )
		if (!stricmp( RadioEventName[i], name ))
			return static_cast<RadioType>( i );

	return RADIO_INVALID;
}


void CCSPlayer::HandleMenu_Radio1( int slot )
{
	if( m_iRadioMessages < 0 )
		return;

	if( m_flRadioTime > gpGlobals->curtime )
		return;

	m_iRadioMessages--;
	m_flRadioTime = gpGlobals->curtime + 1.5;

	switch ( slot )
	{
		case RADIO_COVER_ME :
			Radio( "Radio.CoverMe",   "#Cstrike_TitlesTXT_Cover_me" );
			break;

		case RADIO_YOU_TAKE_THE_POINT :
			Radio( "Radio.YouTakeThePoint", "#Cstrike_TitlesTXT_You_take_the_point" );
			break;

		case RADIO_HOLD_THIS_POSITION :
			Radio( "Radio.HoldPosition",  "#Cstrike_TitlesTXT_Hold_this_position" );
			break;

		case RADIO_REGROUP_TEAM :
			Radio( "Radio.Regroup",   "#Cstrike_TitlesTXT_Regroup_team" );
			break;

		case RADIO_FOLLOW_ME :
			Radio( "Radio.FollowMe",  "#Cstrike_TitlesTXT_Follow_me" );
			break;

		case RADIO_TAKING_FIRE :
			Radio( "Radio.TakingFire", "#Cstrike_TitlesTXT_Taking_fire" );
			break;
	}

	// tell bots about radio message
	IGameEvent * event = gameeventmanager->CreateEvent( "player_radio" );
	if ( event )
	{
		event->SetInt("userid", GetUserID() );
		event->SetInt("slot", slot );
		gameeventmanager->FireEvent( event );
	}
}

void CCSPlayer::HandleMenu_Radio2( int slot )
{
	if( m_iRadioMessages < 0 )
		return;

	if( m_flRadioTime > gpGlobals->curtime )
		return;

	m_iRadioMessages--;
	m_flRadioTime = gpGlobals->curtime + 1.5;

	switch ( slot )
	{
		case RADIO_GO_GO_GO:
		Radio( "Radio.GoGoGo", "#Cstrike_TitlesTXT_Go_go_go" );
		break;

		case RADIO_TEAM_FALL_BACK:
		Radio( "Radio.TeamFallBack", "#Cstrike_TitlesTXT_Team_fall_back" );
		break;

		case RADIO_STICK_TOGETHER_TEAM:
		Radio( "Radio.StickTogether", "#Cstrike_TitlesTXT_Stick_together_team" );
		break;

		case RADIO_THANKS:
		Radio( "Radio.Thanks", "#Cstrike_TitlesTXT_Thanks" );
		break;

		case RADIO_CHEER:
		Radio( "Radio.Cheer", "#Cstrike_TitlesTXT_Cheer" );
		break;

		case RADIO_COMPLIMENT:
		Radio( "Radio.Compliment", "#Cstrike_TitlesTXT_Compliment" );
		break;

		case RADIO_REPORT_IN_TEAM:
		Radio( "Radio.ReportInTeam", "#Cstrike_TitlesTXT_Report_in_team" );
		break;
	}

	// tell bots about radio message
	IGameEvent * event = gameeventmanager->CreateEvent( "player_radio" );
	if ( event )
	{
		event->SetInt("userid", GetUserID() );
		event->SetInt("slot", slot );
		gameeventmanager->FireEvent( event );
	}
}

void CCSPlayer::HandleMenu_Radio3( int slot )
{
	if( m_iRadioMessages < 0 )
		return;

	if( m_flRadioTime > gpGlobals->curtime )
		return;

	m_iRadioMessages--;
	m_flRadioTime = gpGlobals->curtime + 1.5;

	switch ( slot )
	{
		case RADIO_AFFIRMATIVE:
		if ( random->RandomInt( 0, 1 ) )
			Radio( "Radio.Affirmitive", "#Cstrike_TitlesTXT_Affirmative" );
		else
			Radio( "Radio.Roger", "#Cstrike_TitlesTXT_Roger_that" );

		break;

		case RADIO_ENEMY_SPOTTED:
		Radio( "Radio.EnemySpotted", "#Cstrike_TitlesTXT_Enemy_spotted" );
		break;

		case RADIO_NEED_BACKUP:
		Radio( "Radio.NeedBackup", "#Cstrike_TitlesTXT_Need_backup" );
		break;

		case RADIO_SECTOR_CLEAR:
		Radio( "Radio.SectorClear", "#Cstrike_TitlesTXT_Sector_clear" );
		break;

		case RADIO_IN_POSITION:
		Radio( "Radio.InPosition", "#Cstrike_TitlesTXT_In_position" );
		break;

		case RADIO_REPORTING_IN:
		Radio( "Radio.ReportingIn", "#Cstrike_TitlesTXT_Reporting_in" );
		break;

		case RADIO_GET_OUT_OF_THERE:
		Radio( "Radio.GetOutOfThere", "#Cstrike_TitlesTXT_Get_out_of_there" );
		break;

		case RADIO_NEGATIVE:
		Radio( "Radio.Negative", "#Cstrike_TitlesTXT_Negative" );
		break;

		case RADIO_ENEMY_DOWN:
		Radio( "Radio.EnemyDown", "#Cstrike_TitlesTXT_Enemy_down" );
		break;
	}

	// tell bots about radio message
	IGameEvent * event = gameeventmanager->CreateEvent( "player_radio" );
	if ( event )
	{
		event->SetInt("userid", GetUserID() );
		event->SetInt("slot", slot );
		gameeventmanager->FireEvent( event );
	}
}

void UTIL_CSRadioMessage( IRecipientFilter& filter, int iClient, int msg_dest, const char *msg_name, const char *param1 = NULL, const char *param2 = NULL, const char *param3 = NULL, const char *param4 = NULL )
{
	UserMessageBegin( filter, "RadioText" );
		WRITE_BYTE( msg_dest );
		WRITE_BYTE( iClient );
		WRITE_STRING( msg_name );

		if ( param1 )
			WRITE_STRING( param1 );
		else
			WRITE_STRING( "" );

		if ( param2 )
			WRITE_STRING( param2 );
		else
			WRITE_STRING( "" );

		if ( param3 )
			WRITE_STRING( param3 );
		else
			WRITE_STRING( "" );

		if ( param4 )
			WRITE_STRING( param4 );
		else
			WRITE_STRING( "" );

	MessageEnd();
}

void CCSPlayer::ConstructRadioFilter( CRecipientFilter& filter )
{
	filter.MakeReliable();

	int localTeam = GetTeamNumber();

	int i;
	for ( i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CCSPlayer *player = static_cast<CCSPlayer *>( UTIL_PlayerByIndex( i ) );
		if ( !player )
			continue;

		// Skip players ignoring the radio
		if ( player->m_bIgnoreRadio )
			continue;

		if( player->GetTeamNumber() == TEAM_SPECTATOR )
		{
			// add spectators
			if( player->m_iObserverMode == OBS_MODE_IN_EYE || player->m_iObserverMode == OBS_MODE_CHASE )
			{
				filter.AddRecipient( player );
			}
		}
		else if( player->GetTeamNumber() == localTeam )
		{
			// add teammates
			filter.AddRecipient( player );
		}
	}
}

void CCSPlayer::Radio( const char *pszRadioSound, const char *pszRadioText, bool bTriggeredAutomatically )
{
	if( !IsAlive() )
		return;

	if ( IsObserver() )
		return;

	CRecipientFilter filter;
	ConstructRadioFilter( filter );

	if( pszRadioText )
	{
		const char *pszLocationText = CSGameRules()->GetChatLocation( true, this );
		if ( pszLocationText && *pszLocationText )
		{
			UTIL_CSRadioMessage( filter, entindex(), HUD_PRINTTALK, "#Game_radio_location", GetPlayerName(), pszLocationText, pszRadioText );
		}
		else
		{
			UTIL_CSRadioMessage( filter, entindex(), HUD_PRINTTALK, "#Game_radio", GetPlayerName(), pszRadioText );
		}
	}

	if ( ( strncmp( pszRadioSound, "Radio", 5 ) == 0 ) )
	{
		pszRadioSound += 5;
	}

	// god damm this looks like a 3 year old's code
	char strRadioSound[256];

	// special case for agents
	if ( HasAgentSet( GetTeamNumber() ) && !IsBotOrControllingBot() )
	{
		if ( GetTeamNumber() == TEAM_CT )
			Q_snprintf( strRadioSound, sizeof( strRadioSound ), "%s%s", GetCSAgentInfoCT( GetAgentID( GetTeamNumber() ) )->m_szRadioPrefix, pszRadioSound );
		if ( GetTeamNumber() == TEAM_TERRORIST )
			Q_snprintf( strRadioSound, sizeof( strRadioSound ), "%s%s", GetCSAgentInfoT( GetAgentID( GetTeamNumber() ) )->m_szRadioPrefix, pszRadioSound );
	}
	else
		Q_snprintf( strRadioSound, sizeof( strRadioSound ), "%s%s", GetCSClassInfo( m_iClass )->m_szRadioPrefix, pszRadioSound );

	UserMessageBegin ( filter, "SendAudio" );
		WRITE_STRING( strRadioSound );
	MessageEnd();

	//icon over the head for teammates
	TE_RadioIcon( filter, 0.0, this );
}

//-----------------------------------------------------------------------------
// Purpose: Outputs currently connected players to the console
//-----------------------------------------------------------------------------
void CCSPlayer::ListPlayers()
{
	char buf[64];
	for ( int i=1; i <= gpGlobals->maxClients; i++ )
	{
		CCSPlayer *pPlayer = dynamic_cast< CCSPlayer* >( UTIL_PlayerByIndex( i ) );
		if ( pPlayer && !pPlayer->IsDormant() )
		{
			if ( pPlayer->IsBot() )
			{
				Q_snprintf( buf, sizeof(buf), "B %d : %s", pPlayer->GetUserID(), pPlayer->GetPlayerName() );
			}
			else
			{
				Q_snprintf( buf, sizeof(buf), "  %d : %s", pPlayer->GetUserID(), pPlayer->GetPlayerName() );
			}
			ClientPrint( this, HUD_PRINTCONSOLE, buf );
		}
	}
	ClientPrint( this, HUD_PRINTCONSOLE, "\n" );
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : &info -
//-----------------------------------------------------------------------------
void CCSPlayer::OnDamagedByExplosion( const CTakeDamageInfo &info )
{
	float lastDamage = info.GetDamage();

	//Adrian - This is hacky since we might have been damaged by something else
	//but since the round is ending, who cares.
	if ( CSGameRules()->m_bTargetBombed == true )
		 return;

	float distanceFromPlayer = 9999.0f;

	CBaseEntity *inflictor = info.GetInflictor();
	if ( inflictor )
	{
		Vector delta = GetAbsOrigin() - inflictor->GetAbsOrigin();
		distanceFromPlayer = delta.Length();
	}

	bool shock = lastDamage >= 30.0f;

	if ( !shock )
		return;

	m_applyDeafnessTime = gpGlobals->curtime + 0.3;
	m_currentDeafnessFilter = 0;
}

void CCSPlayer::ApplyDeafnessEffect()
{
	// what's happening here is that the low-pass filter and the oscillator frequency effects need
	// to fade in and out slowly.  So we have several filters that we switch between to achieve this
	// effect.  The first 3rd of the total effect will be the "fade in" of the effect.  Which means going
	// from filter to filter from the first to the last.  Then we keep on the "last" filter for another
	// third of the total effect time.  Then the last third of the time we go back from the last filter
	// to the first.  Clear as mud?

	// glossary:
	//  filter: an individual filter as defined in dsp_presets.txt
	//  section: one of the sections for the total effect, fade in, full, fade out are the possible sections
	//  effect: the total effect of combining all the sections, the whole of what the player hears from start to finish.

	const int firstGrenadeFilterIndex = 137;
	const int lastGrenadeFilterIndex = 139;
	const float grenadeEffectLengthInSecs = 4.5f; // time of the total effect
	const float fadeInSectionTime = 0.1f;
	const float fadeOutSectionTime = 1.5f;

	const float timeForEachFilterInFadeIn = fadeInSectionTime / (lastGrenadeFilterIndex - firstGrenadeFilterIndex);
	const float timeForEachFilterInFadeOut = fadeOutSectionTime / (lastGrenadeFilterIndex - firstGrenadeFilterIndex);

	float timeIntoEffect = gpGlobals->curtime - m_applyDeafnessTime;

	if (timeIntoEffect >= grenadeEffectLengthInSecs)
	{
		// the effect is done, so reset the deafness variables.
		m_applyDeafnessTime = 0.0f;
		m_currentDeafnessFilter = 0;
		return;
	}

	int section = 0;

	if (timeIntoEffect < fadeInSectionTime)
	{
		section = 0;
	}
	else if (timeIntoEffect < (grenadeEffectLengthInSecs - fadeOutSectionTime))
	{
		section = 1;
	}
	else
	{
		section = 2;
	}

	int filterToUse = 0;

	if (section == 0)
	{
		// fade into the effect.
		int filterIndex = (int)(timeIntoEffect / timeForEachFilterInFadeIn);
		filterToUse = filterIndex += firstGrenadeFilterIndex;
	}
	else if (section == 1)
	{
		// in full effect.
		filterToUse = lastGrenadeFilterIndex;
	}
	else if (section == 2)
	{
		// fade out of the effect
		float timeIntoSection = timeIntoEffect - (grenadeEffectLengthInSecs - fadeOutSectionTime);
		int filterIndex = (int)(timeIntoSection / timeForEachFilterInFadeOut);
		filterToUse = lastGrenadeFilterIndex - filterIndex - 1;
	}

	if (filterToUse != m_currentDeafnessFilter)
	{
		m_currentDeafnessFilter = filterToUse;

		CSingleUserRecipientFilter user( this );
		enginesound->SetPlayerDSP( user, m_currentDeafnessFilter, false );
	}
}


void CCSPlayer::NoteWeaponFired()
{
	Assert( m_pCurrentCommand );
	if( m_pCurrentCommand )
	{
		m_iLastWeaponFireUsercmd = m_pCurrentCommand->command_number;
	}
}


bool CCSPlayer::WantsLagCompensationOnEntity( const CBasePlayer *pPlayer, const CUserCmd *pCmd, const CBitVec<MAX_EDICTS> *pEntityTransmitBits ) const
{
	// No need to lag compensate at all if we're not attacking in this command and
	// we haven't attacked recently.
	if ( !( pCmd->buttons & IN_ATTACK ) && (pCmd->command_number - m_iLastWeaponFireUsercmd > 5) )
	{
		if ( ( pCmd->buttons & IN_ATTACK2 ) == 0 )
			return false;

		CWeaponCSBase *weapon = GetActiveCSWeapon();
		if ( !weapon )
			return false;

		if ( CSLoadout()->IsKnife( weapon->GetCSWeaponID() ) )
			return false;	// IN_ATTACK2 with knife should do lag compensation
	}

	return BaseClass::WantsLagCompensationOnEntity( pPlayer, pCmd, pEntityTransmitBits );
}

// Handles the special "radio" alias commands we're creating to accommodate the scripts players use
// ** Returns true if we've handled the command **
bool HandleRadioAliasCommands( CCSPlayer *pPlayer, const char *pszCommand )
{
	bool bRetVal = false;

	// don't execute them if we are not alive or are an observer
	if ( !pPlayer->IsAlive() || pPlayer->IsObserver() )
		return false;

	// Radio1 commands
	if ( FStrEq( pszCommand, "coverme" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio1( RADIO_COVER_ME );
	}
	else if ( FStrEq( pszCommand, "takepoint" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio1( RADIO_YOU_TAKE_THE_POINT );
	}
	else if ( FStrEq( pszCommand, "holdpos" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio1( RADIO_HOLD_THIS_POSITION );
	}
	else if ( FStrEq( pszCommand, "regroup" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio1( RADIO_REGROUP_TEAM );
	}
	else if ( FStrEq( pszCommand, "followme" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio1( RADIO_FOLLOW_ME );
	}
	else if ( FStrEq( pszCommand, "takingfire" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio1( RADIO_TAKING_FIRE );
	}
	// Radio2 commands
	else if ( FStrEq( pszCommand, "go" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio2( RADIO_GO_GO_GO );
	}
	else if ( FStrEq( pszCommand, "fallback" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio2( RADIO_TEAM_FALL_BACK );
	}
	else if ( FStrEq( pszCommand, "sticktog" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio2( RADIO_STICK_TOGETHER_TEAM );
	}
	else if ( FStrEq( pszCommand, "cheer" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio2( RADIO_CHEER );
	}
	else if ( FStrEq( pszCommand, "thanks" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio2( RADIO_THANKS );
	}
	else if ( FStrEq( pszCommand, "compliment" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio2( RADIO_COMPLIMENT );
	}
	//else if ( FStrEq( pszCommand, "getinpos" ) )
	//{
	//	bRetVal = true;
	//	pPlayer->HandleMenu_Radio2( 4 );
	//}
	//else if ( FStrEq( pszCommand, "stormfront" ) )
	//{
	//	bRetVal = true;
	//	pPlayer->HandleMenu_Radio2( 5 );
	//}
	else if ( FStrEq( pszCommand, "report" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio2( RADIO_REPORT_IN_TEAM );
	}
	// Radio3 commands
	else if ( FStrEq( pszCommand, "roger" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_AFFIRMATIVE );
	}
	else if ( FStrEq( pszCommand, "enemyspot" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_ENEMY_SPOTTED );
	}
	else if ( FStrEq( pszCommand, "needbackup" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_NEED_BACKUP );
	}
	else if ( FStrEq( pszCommand, "sectorclear" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_SECTOR_CLEAR );
	}
	else if ( FStrEq( pszCommand, "inposition" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_IN_POSITION );
	}
	else if ( FStrEq( pszCommand, "reportingin" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_REPORTING_IN );
	}
	else if ( FStrEq( pszCommand, "getout" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_GET_OUT_OF_THERE );
	}
	else if ( FStrEq( pszCommand, "negative" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_NEGATIVE );
	}
	else if ( FStrEq( pszCommand, "enemydown" ) )
	{
		bRetVal = true;
		pPlayer->HandleMenu_Radio3( RADIO_ENEMY_DOWN );
	}

	return bRetVal;
}

bool CCSPlayer::ShouldRunRateLimitedCommand( const CCommand &args )
{
	const char *pcmd = args[0];

	int i = m_RateLimitLastCommandTimes.Find( pcmd );
	if ( i == m_RateLimitLastCommandTimes.InvalidIndex() )
	{
		m_RateLimitLastCommandTimes.Insert( pcmd, gpGlobals->curtime );
		return true;
	}
	else if ( (gpGlobals->curtime - m_RateLimitLastCommandTimes[i]) < CS_COMMAND_MAX_RATE )
	{
		// Too fast.
		return false;
	}
	else
	{
		m_RateLimitLastCommandTimes[i] = gpGlobals->curtime;
		return true;
	}
}

bool CCSPlayer::ClientCommand( const CCommand &args )
{
	const char *pcmd = args[0];

	// Bots mimic our client commands.
/*
	if ( bot_mimic.GetInt() && !( GetFlags() & FL_FAKECLIENT ) )
	{
		for ( int i=1; i <= gpGlobals->maxClients; i++ )
		{
			CCSPlayer *pPlayer = dynamic_cast< CCSPlayer* >( UTIL_PlayerByIndex( i ) );
			if ( pPlayer && pPlayer != this && ( pPlayer->GetFlags() & FL_FAKECLIENT ) )
			{
				pPlayer->ClientCommand( pcmd );
			}
		}
	}
*/

#if defined ( DEBUG )

	if ( FStrEq( pcmd, "bot_cmd" ) )
	{
		CCSPlayer *pPlayer = dynamic_cast< CCSPlayer* >( UTIL_PlayerByIndex( atoi( args[1] ) ) );
		if ( pPlayer && pPlayer != this && ( pPlayer->GetFlags() & FL_FAKECLIENT ) )
		{
			CCommand botArgs( args.ArgC() - 2, &args.ArgV()[2] );
			pPlayer->ClientCommand( botArgs );
			pPlayer->RemoveEffects( EF_NODRAW );
		}
		return true;
	}

	if ( FStrEq( pcmd, "blind" ) )
	{
		if ( ShouldRunRateLimitedCommand( args ) )
		{
			if ( args.ArgC() == 3 )
			{
				Blind( atof( args[1] ), atof( args[2] ) );
			}
			else
			{
				ClientPrint( this, HUD_PRINTCONSOLE, "usage: blind holdtime fadetime\n" );
			}
		}
		return true;
	}

	if ( FStrEq( pcmd, "deafen" ) )
	{
		Deafen( 0.0f );
		return true;
	}

	if ( FStrEq( pcmd, "he_deafen" ) )
	{
		m_applyDeafnessTime = gpGlobals->curtime + 0.3;
		m_currentDeafnessFilter = 0;
		return true;
	}

	if ( FStrEq( pcmd, "hint_reset" ) )
	{
		m_iDisplayHistoryBits = 0;
		return true;
	}

	if ( FStrEq( pcmd, "punch" ) )
	{
		float flDamage = 100;

		QAngle punchAngle = GetPunchAngle();

		punchAngle.x = flDamage * random->RandomFloat ( -0.15, 0.15 );
		punchAngle.y = flDamage * random->RandomFloat ( -0.15, 0.15 );
		punchAngle.z = flDamage * random->RandomFloat ( -0.15, 0.15 );

		clamp( punchAngle.x, -4, punchAngle.x );
		clamp( punchAngle.y, -5, 5 );
		clamp( punchAngle.z, -5, 5 );

		// +y == down
		// +x == left
		// +z == roll clockwise
		if ( args.ArgC() == 4 )
		{
			punchAngle.x = atof(args[1]);
			punchAngle.y = atof(args[2]);
			punchAngle.z = atof(args[3]);
		}

		SetPunchAngle( punchAngle );

		return true;
	}

#endif //DEBUG

	if ( FStrEq( pcmd, "jointeam" ) )
	{
		if ( args.ArgC() < 2 )
		{
			Warning( "Player sent bad jointeam syntax\n" );
		}

		if ( ShouldRunRateLimitedCommand( args ) )
		{
			int iTeam = atoi( args[1] );
			HandleCommand_JoinTeam( iTeam );
		}
		return true;
	}
	else if ( FStrEq( pcmd, "spectate" ) )
	{
		if ( ShouldRunRateLimitedCommand( args ) )
		{
			// instantly join spectators
			HandleCommand_JoinTeam( TEAM_SPECTATOR );
		}
		return true;
	}
	else if ( FStrEq( pcmd, "joingame" ) )
	{
		// player just closed MOTD dialog
		if ( m_iPlayerState == STATE_WELCOME )
		{
			State_Transition( STATE_PICKINGTEAM );
		}

		return true;
	}
	else if ( FStrEq( pcmd, "joinclass" ) )
	{
		if ( args.ArgC() < 2 )
		{
			Warning( "Player sent bad joinclass syntax\n" );
		}

		if ( ShouldRunRateLimitedCommand( args ) )
		{
			int iClass = atoi( args[1] );
			HandleCommand_JoinClass( iClass );
		}
		return true;
	}
	else if ( FStrEq( pcmd, "drop" ) )
	{
		HandleDropWeapon();

		return true;
	}
	else if ( FStrEq( pcmd, "buy" ) )
	{
		BuyResult_e result = BUY_INVALID_ITEM;
		if ( args.ArgC() == 2 )
		{
			result = HandleCommand_Buy( args[1] );
		}
		if ( result == BUY_INVALID_ITEM )
		{
			// Print out a message on the console
			int msg_dest = HUD_PRINTCONSOLE;

			ClientPrint( this, msg_dest, "usage: buy <item>\n" );

			ClientPrint( this, msg_dest, "  glock\n" );
			ClientPrint( this, msg_dest, "  xm1014\n" );
			ClientPrint( this, msg_dest, "  mac10\n" );
			ClientPrint( this, msg_dest, "  aug\n" );
			ClientPrint( this, msg_dest, "  elite\n" );
			ClientPrint( this, msg_dest, "  fiveseven\n" );
			ClientPrint( this, msg_dest, "  ump45\n" );
			ClientPrint( this, msg_dest, "  galilar\n" );
			ClientPrint( this, msg_dest, "  famas\n" );
			ClientPrint( this, msg_dest, "  usp_silencer\n" );
			ClientPrint( this, msg_dest, "  awp\n" );
			ClientPrint( this, msg_dest, "  m249\n" );
			ClientPrint( this, msg_dest, "  nova\n" );
			ClientPrint( this, msg_dest, "  m4a4\n" );
			ClientPrint( this, msg_dest, "  m4a1_silencer\n" );
			ClientPrint( this, msg_dest, "  g3sg1\n" );
			ClientPrint( this, msg_dest, "  deagle\n" );
			ClientPrint( this, msg_dest, "  ak47\n" );
			ClientPrint( this, msg_dest, "  p90\n" );
			ClientPrint( this, msg_dest, "  bizon\n" );
			ClientPrint( this, msg_dest, "  mag7\n" );
			ClientPrint( this, msg_dest, "  negev\n" );
			ClientPrint( this, msg_dest, "  sawedoff\n" );
			ClientPrint( this, msg_dest, "  tec9\n" );
			ClientPrint( this, msg_dest, "  taser\n" );
			ClientPrint( this, msg_dest, "  hkp2000\n" );
			ClientPrint( this, msg_dest, "  mp5sd\n" );
			ClientPrint( this, msg_dest, "  mp7\n" );
			ClientPrint( this, msg_dest, "  mp9\n" );
			ClientPrint( this, msg_dest, "  nova\n" );
			ClientPrint( this, msg_dest, "  p250\n" );
			ClientPrint( this, msg_dest, "  scar20\n" );
			ClientPrint( this, msg_dest, "  sg556\n" );
			ClientPrint( this, msg_dest, "  ssg08\n" );

			ClientPrint( this, msg_dest, "  flashbang\n" );
			ClientPrint( this, msg_dest, "  smokegrenade\n" );
			ClientPrint( this, msg_dest, "  hegrenade\n" );
			ClientPrint( this, msg_dest, "  molotov\n" );
			ClientPrint( this, msg_dest, "  incgrenade\n" );
			ClientPrint( this, msg_dest, "  decoy\n" );

			//ClientPrint( this, msg_dest, "  primammo\n" );
			//ClientPrint( this, msg_dest, "  secammo\n" );
		}

		return true;
	}
//	else if ( FStrEq( pcmd, "buyammo1" ) )
//	{
//		AttemptToBuyAmmoSingle(0);
//		return true;
//	}
//	else if ( FStrEq( pcmd, "buyammo2" ) )
//	{
//		AttemptToBuyAmmoSingle(1);
//		return true;
//	}
	else if ( FStrEq( pcmd, "nightvision" ) )
	{
		if ( ShouldRunRateLimitedCommand( args ) )
		{
			if( m_bHasNightVision )
			{
				if( m_bNightVisionOn )
				{
					CPASAttenuationFilter filter( this );
					EmitSound( filter, entindex(), "Player.NightVisionOff" );
				}
				else
				{
					CPASAttenuationFilter filter( this );
					EmitSound( filter, entindex(), "Player.NightVisionOn" );
				}

				m_bNightVisionOn = !m_bNightVisionOn;
			}
		}
		return true;
	}
	else if ( FStrEq( pcmd, "menuselect" ) )
	{
		return true;
	}
	else if ( HandleRadioAliasCommands( this, pcmd ) )
	{
		return true;
	}
	else if ( FStrEq( pcmd, "listplayers" ) )
	{
		ListPlayers();
		return true;
	}

	else if ( FStrEq( pcmd, "ignorerad" ) )
	{
		m_bIgnoreRadio = !m_bIgnoreRadio;
		if ( m_bIgnoreRadio )
		{
			ClientPrint( this, HUD_PRINTTALK, "#Ignore_Radio" );
		}
		else
		{
			ClientPrint( this, HUD_PRINTTALK, "#Accept_Radio" );
		}
		return true;
	}
	else if ( FStrEq( pcmd, "become_vip" ) )
	{
		//MIKETODO: VIP mode
		/*
		if ( ( CSGameRules()->m_iMapHasVIPSafetyZone == 1 ) && ( m_iTeam == TEAM_CT ) )
		{
			mp->AddToVIPQueue( this );
		}
		*/
		return true;
	}
	else if ( FStrEq( pcmd, "+lookatweapon" ) )
	{
		m_bIsHoldingLookAtWeapon = true;

		if ( ShouldRunRateLimitedCommand( args ) )
		{
			LookAtHeldWeapon();
		}

		return true;
	}
	else if ( FStrEq( pcmd, "-lookatweapon" ) )
	{
		m_bIsHoldingLookAtWeapon = false;

		return true;
	}

	return BaseClass::ClientCommand( args );
}

void CCSPlayer::LookAtHeldWeapon( void )
{
	if ( IsLookingAtWeapon() )
		return;

	int nSequence = ACTIVITY_NOT_AVAILABLE;

	// Need a weapon to taunt
	CWeaponCSBase *pActiveWeapon = GetActiveCSWeapon();
	if ( !pActiveWeapon )
		return;

	// Can't taunt while zoomed, reloading, or switching silencer
	if ( pActiveWeapon->IsWeaponZoomed() || pActiveWeapon->m_bInReload || pActiveWeapon->IsSwitchingSilencer() )
		return;

	// don't let me inspect a shotgun that's reloading
	/*if ( pActiveWeapon->GetWeaponType() == WEAPONTYPE_SHOTGUN && pActiveWeapon->GetShotgunReloadState() != 0 )
	{
		return;
	}*/

#if IRONSIGHT
	if ( pActiveWeapon->m_iIronSightMode == IronSight_should_approach_sighted )
		return;
#endif

	CBaseViewModel *pViewModel = GetViewModel();
	if ( pViewModel )
	{
		nSequence = pViewModel->SelectWeightedSequence( ACT_VM_IDLE_LOWERED );

		if ( nSequence == ACT_INVALID )
			nSequence = pViewModel->LookupSequence( "lookat01" );

		if ( nSequence != ACTIVITY_NOT_AVAILABLE )
		{
			m_flLookWeaponEndTime = gpGlobals->curtime + pViewModel->SequenceDuration( nSequence );
			m_bIsLookingAtWeapon = true;

			pViewModel->ForceCycle( 0 );
			pViewModel->ResetSequence( nSequence );
		}
	}

}


// returns true if the selection has been handled and the player's menu
// can be closed...false if the menu should be displayed again
bool CCSPlayer::HandleCommand_JoinTeam( int team )
{
#if CS_CONTROLLABLE_BOTS_ENABLED
	if ( IsControllingBot() )
		return false;
#endif

	CCSGameRules *mp = CSGameRules();

	if ( !GetGlobalTeam( team ) )
	{
		DevWarning( "HandleCommand_JoinTeam( %d ) - invalid team index.\n", team );
		return false;
	}

	// If this player is a VIP, don't allow him to switch teams/appearances unless the following conditions are met :
	// a) There is another TEAM_CT player who is in the queue to be a VIP
	// b) This player is dead

	//MIKETODO: handle this when doing VIP mode
	/*
	if ( m_bIsVIP == true )
	{
		if ( !IsDead() )
		{
			ClientPrint( this, HUD_PRINTCENTER, "#Cannot_Switch_From_VIP" );
			MenuReset();
			return true;
		}
		else if ( mp->IsVIPQueueEmpty() == true )
		{
			ClientPrint( this, HUD_PRINTCENTER, "#Cannot_Switch_From_VIP" );
			MenuReset();
			return true;
		}
	}

	//MIKETODO: VIP mode

	case 3:
		if ( ( mp->m_iMapHasVIPSafetyZone == 1 ) && ( m_iTeam == TEAM_CT ) )
		{
			mp->AddToVIPQueue( player );
			MenuReset();
			return true;
		}
		else
		{
			return false;
		}
		break;
	*/

	// If we already died and changed teams once, deny
	if( m_bTeamChanged && team != m_iOldTeam && team != TEAM_SPECTATOR )
	{
		ClientPrint( this, HUD_PRINTCENTER, "#Only_1_Team_Change" );
		return true;
	}

	// check if we're limited in our team selection
	if ( team == TEAM_UNASSIGNED && !IsBot() )
	{
		team = mp->GetHumanTeam(); // returns TEAM_UNASSIGNED if we're unrestricted
	}

	if ( team == TEAM_UNASSIGNED )
	{
		// Attempt to auto-select a team, may set team to T, CT or SPEC
		team = mp->SelectDefaultTeam( !IsBot() );

		if ( team == TEAM_UNASSIGNED )
		{
			// still team unassigned, try to kick a bot if possible

			// kick a bot to allow human to join
			if (cv_bot_auto_vacate.GetBool() && !IsBot())
			{
				team = (random->RandomInt( 0, 1 ) == 0) ? TEAM_TERRORIST : TEAM_CT;
				if (UTIL_KickBotFromTeam( team ) == false)
				{
					// no bots on that team, try the other
					team = (team == TEAM_CT) ? TEAM_TERRORIST : TEAM_CT;
					if (UTIL_KickBotFromTeam( team ) == false)
					{
						// couldn't kick any bots, fail
						team = TEAM_UNASSIGNED;
					}
				}
			}

			if (team == TEAM_UNASSIGNED)
			{
				ClientPrint( this, HUD_PRINTCENTER, "#All_Teams_Full" );
				ShowViewPortPanel( PANEL_TEAM );
				return false;
			}
		}
	}

	if ( team == GetTeamNumber() )
	{
		// if we don't have an agent and also map factions are disabled (or there are no default factions for current map) let the players choose a faction
		if ( !HasAgentSet( GetTeamNumber() ) && (!CSGameRules()->UseMapFactionsForThisPlayer(this) || CSGameRules()->GetMapFactionsForThisPlayer(this) == -1) )
		{
			// Let people change class (skin) by re-joining the same team
			if ( GetTeamNumber() == TEAM_TERRORIST )
			{
				ShowViewPortPanel( PANEL_CLASS_TER );
			}
			else if ( GetTeamNumber() == TEAM_CT )
			{
				ShowViewPortPanel( PANEL_CLASS_CT );
			}
			return true;	// we wouldn't change the team
		}
		else
		{
			if ( HasAgentSet(GetTeamNumber()) )
				HandleCommand_JoinClass( GetCSAgentInfoT( GetAgentID( GetTeamNumber() ) )->m_iClass );
			else if ( CSGameRules()->UseMapFactionsForThisPlayer(this) && CSGameRules()->GetMapFactionsForThisPlayer(this) > -1 )
			{
				HandleCommand_JoinClass( CSGameRules()->GetMapFactionsForThisPlayer(this) );
			}
			return true;
		}
	}

	if ( mp->TeamFull( team ) )
	{
		// attempt to kick a bot to make room for this player
		bool madeRoom = false;
		if (cv_bot_auto_vacate.GetBool() && !IsBot())
		{
			if (UTIL_KickBotFromTeam( team ))
				madeRoom = true;
		}

		if (!madeRoom)
		{
			if ( team == TEAM_TERRORIST )
			{
				ClientPrint( this, HUD_PRINTCENTER, "#Terrorists_Full" );
			}
			else if ( team == TEAM_CT )
			{
				ClientPrint( this, HUD_PRINTCENTER, "#CTs_Full" );
			}

			ShowViewPortPanel( PANEL_TEAM );
			return false;
		}
	}

	// check if humans are restricted to a single team (Tour of Duty, etc)
	if ( !IsBot() && team != TEAM_SPECTATOR)
	{
		int humanTeam = mp->GetHumanTeam();
		if ( humanTeam != TEAM_UNASSIGNED && humanTeam != team )
		{
			if ( humanTeam == TEAM_TERRORIST )
			{
				ClientPrint( this, HUD_PRINTCENTER, "#Humans_Join_Team_T" );
			}
			else if ( humanTeam == TEAM_CT )
			{
				ClientPrint( this, HUD_PRINTCENTER, "#Humans_Join_Team_CT" );
			}

			ShowViewPortPanel( PANEL_TEAM );
			return false;
		}
	}

	if ( team == TEAM_SPECTATOR )
	{
		// Prevent this is the cvar is set
		if ( !mp_allowspectators.GetInt() && !IsHLTV() )
		{
			ClientPrint( this, HUD_PRINTCENTER, "#Cannot_Be_Spectator" );
			return false;
		}

		if ( GetTeamNumber() != TEAM_UNASSIGNED && State_Get() == STATE_ACTIVE )
		{
			m_fNextSuicideTime = gpGlobals->curtime;	// allow the suicide to work

			CommitSuicide();

			// add 1 to frags to balance out the 1 subtracted for killing yourself
			IncrementFragCount( 1 );
		}

		ChangeTeam( TEAM_SPECTATOR );
		m_iClass = (int)CS_CLASS_NONE;
		m_iSkin = 0;

		if ( !(m_iDisplayHistoryBits & DHF_SPEC_DUCK) )
		{
			m_iDisplayHistoryBits |= DHF_SPEC_DUCK;
			HintMessage( "#Spec_Duck", true, true );
		}

		// do we have fadetoblack on? (need to fade their screen back in)
		if ( mp_fadetoblack.GetBool() )
		{
			color32_s clr = { 0,0,0,255 };
			UTIL_ScreenFade( this, clr, 0, 0, FFADE_IN | FFADE_PURGE );
		}

		return true;
	}

	// If the code gets this far, the team is not TEAM_UNASSIGNED


	if (mp->TeamStacked( team, GetTeamNumber() ))//players are allowed to change to their own team so they can just change their model
	{
		// attempt to kick a bot to make room for this player
		bool madeRoom = false;
		if (cv_bot_auto_vacate.GetBool() && !IsBot())
		{
			if (UTIL_KickBotFromTeam( team ))
				madeRoom = true;
		}

		if (!madeRoom)
		{
			// The specified team is full
			ClientPrint(
				this,
				HUD_PRINTCENTER,
				( team == TEAM_TERRORIST ) ?	"#Too_Many_Terrorists" : "#Too_Many_CTs" );

			ShowViewPortPanel( PANEL_TEAM );
			return false;
		}
	}

	// Show the appropriate Choose Appearance menu
	// This must come before ClientKill() for CheckWinConditions() to function properly

	// Switch their actual team...
	ChangeTeam( team );

	return true;
}


bool CCSPlayer::HandleCommand_JoinClass( int iClass )
{
#if CS_CONTROLLABLE_BOTS_ENABLED
	if ( IsControllingBot() )
		return false;
#endif

	if( iClass == CS_CLASS_NONE )
	{
		// User choosed random class
		switch ( GetTeamNumber() )
		{
			case TEAM_TERRORIST :	iClass = RandomInt(FIRST_T_CLASS, LAST_T_CLASS);
									break;

			case TEAM_CT :			iClass = RandomInt(FIRST_CT_CLASS, LAST_CT_CLASS);
									break;

			default	:				iClass = CS_CLASS_NONE;
									break;
		}
	}

	// clamp to valid classes
	switch ( GetTeamNumber() )
	{
		case TEAM_TERRORIST:
			iClass = clamp( iClass, FIRST_T_CLASS, LAST_T_CLASS );
			break;
		case TEAM_CT:
			iClass = clamp( iClass, FIRST_CT_CLASS, LAST_CT_CLASS );
			break;
		default:
			iClass = CS_CLASS_NONE;
	}

	// Reset the player's state
	if ( State_Get() == STATE_ACTIVE )
	{
		CSGameRules()->CheckWinConditions();
	}

	if ( !IsBot() && State_Get() == STATE_ACTIVE ) // Bots are responsible about only switching classes when they join.
	{
		// Kill player if switching classes while alive.
		// This mimics goldsrc CS 1.6, and prevents a player from hiding, and switching classes to
		// make the opposing team think there are more enemies than there really are.
		CommitSuicide();
	}

	if ( !HasAgentSet( GetTeamNumber() ) )
	{
		m_iClass = iClass;
		switch ( m_iClass )
		{
			case CS_CLASS_PHOENIX_CONNNECTION:
			{
				m_iSkin = RandomInt( 0, TPhoenixPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_L337_KREW:
			{
				m_iSkin = RandomInt( 0, TLeetPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_SEPARATIST:
			{
				m_iSkin = RandomInt( 0, TSeparatistPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_BALKAN:
			{
				m_iSkin = RandomInt( 0, TBalkanPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_PROFESSIONAL:
			{
				m_iSkin = RandomInt( 0, TProfessionalPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_ANARCHIST:
			{
				m_iSkin = RandomInt( 0, TAnarchistPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_PIRATE:
			{
				m_iSkin = RandomInt( 0, TPiratePlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_SEAL_TEAM_6:
			{
				m_iSkin = RandomInt( 0, CTST6PlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_GSG_9:
			{
				m_iSkin = RandomInt( 0, CTGSG9PlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_SAS:
			{
				m_iSkin = RandomInt( 0, CTSASPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_GIGN:
			{
				m_iSkin = RandomInt( 0, CTGIGNPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_FBI:
			{
				m_iSkin = RandomInt( 0, CTFBIPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_IDF:
			{
				m_iSkin = RandomInt( 0, CTIDFPlayerModels.Count() - 1 );
				break;
			}
			case CS_CLASS_SWAT:
			{
				m_iSkin = RandomInt( 0, CTSWATPlayerModels.Count() - 1 );
				break;
			}
		}
	}
	else
	{
		if ( GetTeamNumber() == TEAM_CT )
			m_iClass = GetCSAgentInfoCT( GetAgentID(GetTeamNumber()) )->m_iClass;
		if ( GetTeamNumber() == TEAM_TERRORIST )
			m_iClass = GetCSAgentInfoT( GetAgentID(GetTeamNumber()) )->m_iClass;
	}

	if (State_Get() == STATE_PICKINGCLASS)
	{
// 		SetModelFromClass();
		GetIntoGame();
	}

	return true;
}


/*
void CheckStartMoney( void )
{
	if ( mp_startmoney.GetInt() > 16000 )
	{
		mp_startmoney.SetInt( 16000 );
	}
	else if ( mp_startmoney.GetInt() < 800 )
	{
		mp_startmoney.SetInt( 800 );
	}
}
*/

void CCSPlayer::GetIntoGame()
{
	// Set their model and if they're allowed to spawn right now, put them into the world.
	//SetPlayerModel( iClass );

	SetFOV( this, 0 );
	m_flLastMovement = gpGlobals->curtime;

	CCSGameRules *MPRules = CSGameRules();

/*	//MIKETODO: Escape gameplay ?
	if ( ( MPRules->m_bMapHasEscapeZone == true ) && ( m_iTeam == TEAM_CT ) )
	{
		m_iAccount = 0;

		CheckStartMoney();
		AddAccount( (int)startmoney.value, true );
	}
	*/


	//****************New Code by SupraFiend************
	if ( !MPRules->FPlayerCanRespawn( this ) )
	{
		// This player is joining in the middle of a round or is an observer. Put them directly into observer mode.
		//pev->deadflag		= DEAD_RESPAWNABLE;
		//pev->classname		= MAKE_STRING("player");
		//pev->flags		   &= ( FL_PROXY | FL_FAKECLIENT );	// clear flags, but keep proxy and bot flags that might already be set
		//pev->flags		   |= FL_CLIENT | FL_SPECTATOR;
		//SetThink(PlayerDeathThink);
		if ( !(m_iDisplayHistoryBits & DHF_SPEC_DUCK) )
		{
			m_iDisplayHistoryBits |= DHF_SPEC_DUCK;
			HintMessage( "#Spec_Duck", true, true );
		}

		State_Transition( STATE_OBSERVER_MODE );

		m_wasNotKilledNaturally = true;

		MPRules->CheckWinConditions();
	}
	else// else spawn them right in
	{
		State_Transition( STATE_ACTIVE );

		Spawn();

		MPRules->CheckWinConditions();

		//=============================================================================
		// HPE_BEGIN:
		// [menglish] Have the rules update anything related to a player spawning in late
		//=============================================================================

		MPRules->SpawningLatePlayer(this);

		//=============================================================================
		// HPE_END
		//=============================================================================

		if( MPRules->m_flRestartRoundTime == 0.0f )
		{
			//Bomb target, no bomber and no bomb lying around.
			if( !MPRules->IsWarmupPeriod() && MPRules->IsBombDefuseMap() && !MPRules->IsThereABomber() && !MPRules->IsThereABomb() )
				MPRules->GiveC4(); //Checks for terrorists.
		}

		// If a new terrorist is entering the fray, then up the # of potential escapers.
		if ( GetTeamNumber() == TEAM_TERRORIST )
			MPRules->m_iNumEscapers++;

		//=============================================================================
		// HPE_BEGIN:
		// [menglish] Reset Round Based Achievement Variables
		//=============================================================================

		ResetRoundBasedAchievementVariables();

		//=============================================================================
		// HPE_END
		//=============================================================================

	}
}


int CCSPlayer::PlayerClass() const
{
	return m_iClass;
}



bool CCSPlayer::SelectSpawnSpot( const char *pEntClassName, CBaseEntity* &pSpot )
{
	// Find the next spawn spot.
	pSpot = gEntList.FindEntityByClassname( pSpot, pEntClassName );

	if ( pSpot == NULL ) // skip over the null point
		pSpot = gEntList.FindEntityByClassname( pSpot, pEntClassName );

	CBaseEntity *pFirstSpot = pSpot;
	do
	{
		if ( pSpot )
		{
			// check if pSpot is valid
			if ( g_pGameRules->IsSpawnPointValid( pSpot, this ) )
			{
				if ( pSpot->GetAbsOrigin() == Vector( 0, 0, 0 ) )
				{
					pSpot = gEntList.FindEntityByClassname( pSpot, pEntClassName );
					continue;
				}

				// if so, go to pSpot
				return true;
			}
		}
		// increment pSpot
		pSpot = gEntList.FindEntityByClassname( pSpot, pEntClassName );
	} while ( pSpot != pFirstSpot ); // loop if we're not back to the start

	DevMsg("CCSPlayer::SelectSpawnSpot: couldn't find valid spawn point.\n");

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Called directly after we select a spawn point and teleport to it
//-----------------------------------------------------------------------------
void CCSPlayer::PostSpawnPointSelection()
{
	if ( m_storedSpawnAngle.LengthSqr() > 0.0f || m_storedSpawnPosition != vec3_origin )
	{
		Teleport( &m_storedSpawnPosition, &m_storedSpawnAngle, &vec3_origin );
		m_storedSpawnPosition = vec3_origin;
		m_storedSpawnAngle.Init();
	}
}

CBaseEntity* CCSPlayer::EntSelectSpawnPoint()
{
	CBaseEntity *pSpot;

	/* MIKETODO: VIP
		// VIP spawn point *************
		if ( ( g_pGameRules->IsDeathmatch() ) && ( ((CBasePlayer*)pPlayer)->m_bIsVIP == TRUE) )
		{
			//ALERT (at_console,"Looking for a VIP spawn point\n");
			// Randomize the start spot
			//for ( int i = RANDOM_LONG(1,5); i > 0; i-- )
			pSpot = UTIL_FindEntityByClassname( NULL, "info_vip_start" );
			if ( !FNullEnt( pSpot ) )  // skip over the null point
				goto ReturnSpot;
			else
				goto CTSpawn;
		}

		//
		// the counter-terrorist spawns at "info_player_start"
		else
	*/

	pSpot = NULL;
	if ( CSGameRules()->IsLogoMap() )
	{
		// This is a logo map. Don't allow movement or logos or menus.
		SelectSpawnSpot( "info_player_logo", pSpot );
		LockPlayerInPlace();
		goto ReturnSpot;
	}
	else
	{
		if ( GetTeamNumber() == TEAM_CT )
		{
			pSpot = g_pLastCTSpawn;
			if ( SelectSpawnSpot( "info_player_counterterrorist", pSpot ))
			{

				g_pLastCTSpawn = pSpot;
				goto ReturnSpot;
			}
		}

		/*********************************************************/
		// The terrorist spawn points
		else if ( GetTeamNumber() == TEAM_TERRORIST )
		{
			pSpot = g_pLastTerroristSpawn;

			if ( SelectSpawnSpot( "info_player_terrorist", pSpot ) )
			{
				g_pLastTerroristSpawn = pSpot;
				goto ReturnSpot;
			}
		}
	}


	// If startspot is set, (re)spawn there.
	if ( !gpGlobals->startspot || !strlen(STRING(gpGlobals->startspot)))
	{
		pSpot = gEntList.FindEntityByClassname(NULL, "info_player_terrorist");
		if ( pSpot )
			goto ReturnSpot;
	}
	else
	{
		pSpot = gEntList.FindEntityByTarget( NULL, STRING(gpGlobals->startspot) );
		if ( pSpot )
			goto ReturnSpot;
	}

ReturnSpot:
	if ( !pSpot )
	{
		if( CSGameRules()->IsLogoMap() )
			Warning( "PutClientInServer: no info_player_logo on level\n" );
		else
			Warning( "PutClientInServer: no info_player_start on level\n" );

		return CBaseEntity::Instance( INDEXENT(0) );
	}

	return pSpot;
}


void CCSPlayer::SetProgressBarTime( int barTime )
{
	m_iProgressBarDuration = barTime;
	m_flProgressBarStartTime = this->m_flSimulationTime;
}


void CCSPlayer::PlayerDeathThink()
{
}


void CCSPlayer::State_Transition( CSPlayerState newState )
{
	State_Leave();
	State_Enter( newState );
}


void CCSPlayer::State_Enter( CSPlayerState newState )
{
	m_iPlayerState = newState;
	m_pCurStateInfo = State_LookupInfo( newState );

	if ( cs_ShowStateTransitions.GetInt() == -1 || cs_ShowStateTransitions.GetInt() == entindex() )
	{
		if ( m_pCurStateInfo )
			Msg( "ShowStateTransitions: entering '%s'\n", m_pCurStateInfo->m_pStateName );
		else
			Msg( "ShowStateTransitions: entering #%d\n", newState );
	}

	// Initialize the new state.
	if ( m_pCurStateInfo && m_pCurStateInfo->pfnEnterState )
		(this->*m_pCurStateInfo->pfnEnterState)();
}


void CCSPlayer::State_Leave()
{
	if ( m_pCurStateInfo && m_pCurStateInfo->pfnLeaveState )
	{
		(this->*m_pCurStateInfo->pfnLeaveState)();
	}
}


void CCSPlayer::State_PreThink()
{
	if ( m_pCurStateInfo && m_pCurStateInfo->pfnPreThink )
	{
		(this->*m_pCurStateInfo->pfnPreThink)();
	}
}


CCSPlayerStateInfo* CCSPlayer::State_LookupInfo( CSPlayerState state )
{
	// This table MUST match the
	static CCSPlayerStateInfo playerStateInfos[] =
	{
		{ STATE_ACTIVE,			"STATE_ACTIVE",			&CCSPlayer::State_Enter_ACTIVE, NULL, &CCSPlayer::State_PreThink_ACTIVE },
		{ STATE_WELCOME,		"STATE_WELCOME",		&CCSPlayer::State_Enter_WELCOME, NULL, &CCSPlayer::State_PreThink_WELCOME },
		{ STATE_PICKINGTEAM,	"STATE_PICKINGTEAM",	&CCSPlayer::State_Enter_PICKINGTEAM, NULL,	&CCSPlayer::State_PreThink_OBSERVER_MODE },
		{ STATE_PICKINGCLASS,	"STATE_PICKINGCLASS",	&CCSPlayer::State_Enter_PICKINGCLASS, NULL,	&CCSPlayer::State_PreThink_OBSERVER_MODE },
		{ STATE_DEATH_ANIM,		"STATE_DEATH_ANIM",		&CCSPlayer::State_Enter_DEATH_ANIM,	NULL, &CCSPlayer::State_PreThink_DEATH_ANIM },
		{ STATE_DEATH_WAIT_FOR_KEY,	"STATE_DEATH_WAIT_FOR_KEY",	&CCSPlayer::State_Enter_DEATH_WAIT_FOR_KEY,	NULL, &CCSPlayer::State_PreThink_DEATH_WAIT_FOR_KEY },
		{ STATE_OBSERVER_MODE,	"STATE_OBSERVER_MODE",	&CCSPlayer::State_Enter_OBSERVER_MODE,	NULL, &CCSPlayer::State_PreThink_OBSERVER_MODE },
		{ STATE_DORMANT,		"STATE_DORMANT",		NULL, NULL, NULL }
	};

	for ( int i=0; i < ARRAYSIZE( playerStateInfos ); i++ )
	{
		if ( playerStateInfos[i].m_iPlayerState == state )
			return &playerStateInfos[i];
	}

	return NULL;
}


void CCSPlayer::PhysObjectSleep()
{
	IPhysicsObject *pObj = VPhysicsGetObject();
	if ( pObj )
		pObj->Sleep();
}


void CCSPlayer::PhysObjectWake()
{
	IPhysicsObject *pObj = VPhysicsGetObject();
	if ( pObj )
		pObj->Wake();
}


void CCSPlayer::State_Enter_WELCOME()
{
	StartObserverMode( OBS_MODE_ROAMING );

	// Important to set MOVETYPE_NONE or our physics object will fall while we're sitting at one of the intro cameras.
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );

	PhysObjectSleep();

	const ConVar *hostname = cvar->FindVar( "hostname" );
	const char *title = (hostname) ? hostname->GetString() : "MESSAGE OF THE DAY";

	// Show info panel (if it's not a simple demo map).
	if ( !CSGameRules()->IsLogoMap() )
	{
		if ( CommandLine()->FindParm( "-makereslists" ) ) // don't show the MOTD when making reslists
		{
			engine->ClientCommand( edict(), "jointeam 3\n" );
		}
		else
		{
			KeyValues *data = new KeyValues("data");
			data->SetString( "title", title );		// info panel title
			data->SetString( "type", "1" );			// show userdata from stringtable entry
			data->SetString( "msg",	"motd" );		// use this stringtable entry
			data->SetInt( "cmd", TEXTWINDOW_CMD_JOINGAME );	// exec this command if panel closed
			data->SetBool( "unload", sv_motd_unload_on_dismissal.GetBool() );

			ShowViewPortPanel( PANEL_INFO, true, data );

			data->deleteThis();
		}
	}
}


void CCSPlayer::State_PreThink_WELCOME()
{
	// Verify some state.
	Assert( IsSolidFlagSet( FSOLID_NOT_SOLID ) );
	Assert( GetAbsVelocity().Length() == 0 );

	// Update whatever intro camera it's at.
	if( m_pIntroCamera && (gpGlobals->curtime >= m_fIntroCamTime) )
	{
		MoveToNextIntroCamera();
	}
}


void CCSPlayer::State_Enter_PICKINGTEAM()
{
	ShowViewPortPanel( "team" ); // show the team menu
}


void CCSPlayer::State_Enter_DEATH_ANIM()
{
	if ( HasWeapons() )
	{
		// we drop the guns here because weapons that have an area effect and can kill their user
		// will sometimes crash coming back from CBasePlayer::Killed() if they kill their owner because the
		// player class sometimes is freed. It's safer to manipulate the weapons once we know
		// we aren't calling into any of their code anymore through the player pointer.
		PackDeadPlayerItems();
	}

	// Used for a timer.
	m_flDeathTime = gpGlobals->curtime;

	m_bAbortFreezeCam = false;

	StartObserverMode( OBS_MODE_DEATHCAM );	// go to observer mode
	RemoveEffects( EF_NODRAW );	// still draw player body

	if ( mp_fadetoblack.GetBool() )
	{
		color32_s clr = {0,0,0,255};
		UTIL_ScreenFade( this, clr, 3, 3, FFADE_OUT | FFADE_STAYOUT );
		//Don't perform any freezecam stuff if we are fading to black
		State_Transition( STATE_DEATH_WAIT_FOR_KEY );
	}
}


//=============================================================================
// HPE_BEGIN:
// [menglish, pfreese] Added freeze cam logic
//=============================================================================
 
void CCSPlayer::State_PreThink_DEATH_ANIM()
{
	// If the anim is done playing, go to the next state (waiting for a keypress to
	// either respawn the guy or put him into observer mode).
	if ( GetFlags() & FL_ONGROUND )
	{
		float flForward = GetAbsVelocity().Length() - 20;
		if (flForward <= 0)
		{
			SetAbsVelocity( vec3_origin );
		}
		else
		{
			Vector vAbsVel = GetAbsVelocity();
			VectorNormalize( vAbsVel );
			vAbsVel *= flForward;
			SetAbsVelocity( vAbsVel );
		}
	}

	float fDeathEnd = m_flDeathTime + CS_DEATH_ANIMATION_TIME;
	float fFreezeEnd = fDeathEnd + spec_freeze_traveltime.GetFloat() + spec_freeze_time.GetFloat();
	float fFreezeLock = fDeathEnd + spec_freeze_time_lock.GetFloat();

	// transition to Freezecam mode once the death animation is complete
	if ( gpGlobals->curtime >= fDeathEnd )
	{
		if ( GetObserverTarget() && GetObserverTarget() != this &&
			!m_bAbortFreezeCam && gpGlobals->curtime < fFreezeEnd && GetObserverMode() != OBS_MODE_FREEZECAM)
		{
			StartObserverMode( OBS_MODE_FREEZECAM );
		}
		else if(GetObserverMode() == OBS_MODE_FREEZECAM)
		{
			if ( m_bAbortFreezeCam && ( mp_forcecamera.GetInt() != OBS_ALLOW_NONE || CSGameRules()->IsWarmupPeriod() ) )
			{
				if ( IsAbleToInstantRespawn() )
				{
					State_Transition( STATE_ACTIVE );
					respawn( this, false );
					m_nButtons = 0;
					SetNextThink( TICK_NEVER_THINK );
				}
				else
				{
					State_Transition( STATE_OBSERVER_MODE );
				}
			}
		}
	}

	// Don't transfer to observer state until the freeze cam is done
	// Players in competitive mode may bypass this mode with a key press
	if ( (gpGlobals->curtime > fFreezeEnd) ||
		 (gpGlobals->curtime > fFreezeLock && (m_nButtons & ~IN_SCORE) && mp_deathcam_skippable.GetBool()) )
	{
		if ( IsAbleToInstantRespawn() )
		{
			State_Transition( STATE_ACTIVE );
			respawn( this, false );
			m_nButtons = 0;
			SetNextThink( TICK_NEVER_THINK );
		}
		else
		{
			State_Transition( STATE_OBSERVER_MODE );
		}
	}
}
 
//=============================================================================
// HPE_END
//=============================================================================


void CCSPlayer::State_Enter_DEATH_WAIT_FOR_KEY()
{
	// Remember when we died, so we can automatically put them into observer mode
	// if they don't hit a key soon enough.

	m_lifeState = LIFE_DEAD;

	StopAnimation();

	// Don't do this.  The ragdoll system expects to be able to read from this player on 
	// the next update and will read it at the new origin if this is set.
	// Since it is more complicated to redesign the ragdoll system to not need that data
	// it is easier to cause a less obvious bug than popping ragdolls
	//AddEffects( EF_NOINTERP );
}


void CCSPlayer::State_PreThink_DEATH_WAIT_FOR_KEY()
{
	// once we're done animating our death and we're on the ground, we want to set movetype to None so our dead body won't do collisions and stuff anymore
	// this prevents a bug where the dead body would go to a player's head if he walked over it while the dead player was clicking their button to respawn
	if ( GetMoveType() != MOVETYPE_NONE && (GetFlags() & FL_ONGROUND) )
		SetMoveType( MOVETYPE_NONE );

	// if the player has been dead for one second longer than allowed by forcerespawn,
	// forcerespawn isn't on. Send the player off to an intermission camera until they
	// choose to respawn.

	bool fAnyButtonDown = (m_nButtons & ~IN_SCORE) != 0;
	if ( mp_fadetoblack.GetBool() )
		fAnyButtonDown = false;

	// after a certain amount of time switch to observer mode even if they don't press a key.
	if (gpGlobals->curtime >= (m_flDeathTime + DEATH_ANIMATION_TIME + 3.0))
	{
		fAnyButtonDown = true;
	}

	if ( fAnyButtonDown )
	{
		if ( IsAbleToInstantRespawn() )
		{
			State_Transition( STATE_ACTIVE );
			respawn( this, false );
			m_nButtons = 0;
			SetNextThink( TICK_NEVER_THINK );
		}
		else
		{
			if ( GetObserverTarget() )
			{
				StartReplayMode( 8, 8, GetObserverTarget()->entindex() );
			}

			State_Transition( STATE_OBSERVER_MODE );
		}
	}
}

void CCSPlayer::State_Enter_OBSERVER_MODE()
{
	// do we have fadetoblack on? (need to fade their screen back in)
	if ( mp_fadetoblack.GetBool() && mp_forcecamera.GetInt() != OBS_ALLOW_NONE)
	{
		color32_s clr = { 0,0,0,255 };
		UTIL_ScreenFade( this, clr, 0, 0, FFADE_IN | FFADE_PURGE );
	}

	int observerMode = m_iObserverLastMode;
	if ( IsNetClient() )
	{
		const char *pIdealMode = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_spec_mode" );
		if ( pIdealMode )
		{
			int nIdealMode = atoi( pIdealMode );

			if ( nIdealMode < OBS_MODE_IN_EYE )
			{
				nIdealMode = OBS_MODE_IN_EYE;
			}
			else if ( nIdealMode > OBS_MODE_ROAMING )
			{
				nIdealMode = OBS_MODE_ROAMING;
			}

			observerMode = nIdealMode;
		}
	}

	StartObserverMode( observerMode );

	PhysObjectSleep();
}

void CCSPlayer::State_Leave_OBSERVER_MODE()
{
#if CS_CONTROLLABLE_BOTS_ENABLED
	m_bCanControlObservedBot = false;
#endif
}

void CCSPlayer::State_PreThink_OBSERVER_MODE()
{
	// Make sure nobody has changed any of our state.
//	Assert( GetMoveType() == MOVETYPE_FLY );
	Assert( m_takedamage == DAMAGE_NO );
	Assert( IsSolidFlagSet( FSOLID_NOT_SOLID ) );
//	Assert( IsEffectActive( EF_NODRAW ) );

	// Must be dead.
	Assert( m_lifeState == LIFE_DEAD );
	Assert( pl.deadflag );

#if CS_CONTROLLABLE_BOTS_ENABLED
	m_bCanControlObservedBot = false;
	if ( GetObserverMode() >= OBS_MODE_IN_EYE )
	{
		CCSBot * pBot = ToCSBot( GetObserverTarget() );
		if ( CanControlBot( pBot ) )
		{
			m_bCanControlObservedBot = true;
		}
	}
#endif
}


void CCSPlayer::State_Enter_PICKINGCLASS()
{
	if ( CommandLine()->FindParm( "-makereslists" ) ) // don't show the menu when making reslists
	{
		engine->ClientCommand( edict(), "joinclass 0\n" );
		return;
	}

	// go to spec mode, if dying keep deathcam
	if ( GetObserverMode() == OBS_MODE_DEATHCAM )
	{
		StartObserverMode( OBS_MODE_DEATHCAM );
	}
	else
	{
		StartObserverMode( OBS_MODE_FIXED );
	}

	m_iClass = (int)CS_CLASS_NONE;

	PhysObjectSleep();
  
	if ( CSGameRules()->GetMapFactionsForThisPlayer(this) > -1 )
	{
		HandleCommand_JoinClass( CSGameRules()->GetMapFactionsForThisPlayer(this) );
	}
	else
	{
		// show the class menu:
		if ( GetTeamNumber() == TEAM_TERRORIST )
		{
			if ( HasAgentSet( TEAM_TERRORIST ) )
				HandleCommand_JoinClass( GetCSAgentInfoT( GetAgentID( TEAM_TERRORIST ) )->m_iClass );
			else
				ShowViewPortPanel( PANEL_CLASS_TER );
		}
		else if ( GetTeamNumber() == TEAM_CT )
		{
			if ( HasAgentSet( TEAM_CT ) )
				HandleCommand_JoinClass( GetCSAgentInfoCT( GetAgentID( TEAM_CT ) )->m_iClass );
			else
				ShowViewPortPanel( PANEL_CLASS_CT );
		}
		else
		{
			HandleCommand_JoinClass( 0 );
		}
	}
}

void CCSPlayer::State_Enter_ACTIVE()
{
	SetMoveType( MOVETYPE_WALK );
	RemoveSolidFlags( FSOLID_NOT_SOLID );
	m_Local.m_iHideHUD = 0;
	PhysObjectWake();
}


void CCSPlayer::State_PreThink_ACTIVE()
{
	// Calculate timeout for immunity
	if ( mp_respawn_immunitytime.GetFloat() > 0 || (CSGameRules() && CSGameRules()->IsWarmupPeriod()) )
	{
		if ( m_bImmunity )
		{
			if ( gpGlobals->curtime > m_fImmuneToDamageTime )
			{
				// Player immunity has timed out
				ClearImmunity();

			}
			// or if we've moved and there's more than 1s of immunity left. Check for 1s because the above case adds 1s.
			else if ( IsAbleToInstantRespawn() && m_bHasMovedSinceSpawn && (m_fImmuneToDamageTime - gpGlobals->curtime > 1.0f) )
			{
				m_fImmuneToDamageTime = gpGlobals->curtime + 1.0f;
			}
		}
	}

	// We only allow noclip here only because noclip is useful for debugging.
	// It would be nice if the noclip command set some flag so we could tell that they
	// did it intentionally.
	if ( IsEFlagSet( EFL_NOCLIP_ACTIVE ) )
	{
//		Assert( GetMoveType() == MOVETYPE_NOCLIP );
	}
	else
	{
//		Assert( GetMoveType() == MOVETYPE_WALK );
	}

	Assert( !IsSolidFlagSet( FSOLID_NOT_SOLID ) );
}

bool CCSPlayer::StartObserverMode( int mode )
{
	if ( !BaseClass::StartObserverMode( mode ) )
		return false;

	// When you enter observer mode, you are no longer planting the bomb or crouch-jumping.
	m_bDuckOverride = false;
	m_duckUntilOnGround = false;

	return true;
}


void CCSPlayer::Weapon_Equip( CBaseCombatWeapon *pWeapon )
{
	CWeaponCSBase *pCSWeapon = dynamic_cast< CWeaponCSBase* >( pWeapon );
	if ( pCSWeapon )
	{
		// For rifles, pistols, or the knife, drop our old weapon in this slot.
		if ( pCSWeapon->GetSlot() == WEAPON_SLOT_RIFLE ||
			pCSWeapon->GetSlot() == WEAPON_SLOT_PISTOL )
		{
			CBaseCombatWeapon *pDropWeapon = Weapon_GetSlot( pCSWeapon->GetSlot() );
			if ( pDropWeapon )
			{
				CSWeaponDrop( pDropWeapon, false, true );
			}
		}
		else if ( pCSWeapon->GetCSWpnData().m_WeaponType == WEAPONTYPE_GRENADE || pCSWeapon->GetCSWpnData().m_WeaponType == WEAPONTYPE_STACKABLEITEM )
		{
			//if we already have this weapon, just add the ammo and destroy it
			if( Weapon_OwnsThisType( pCSWeapon->GetClassname() ) )
			{
				Weapon_EquipAmmoOnly( pWeapon );
				UTIL_Remove( pCSWeapon );
				return;
			}
		}

		pCSWeapon->SetSolidFlags( FSOLID_NOT_SOLID );
		pCSWeapon->SetOwnerEntity( this );
	}

	BaseClass::Weapon_Equip( pWeapon );

	// old players don't know how to unhide their world models a little bit into their deploys,
	// because old players don't have deploy animations at all.
	if ( !m_bUseNewAnimstate && pWeapon && pWeapon->GetWeaponWorldModel() )
	{
		pWeapon->ShowWeaponWorldModel( true );
	}
}

bool CCSPlayer::Weapon_CanUse( CBaseCombatWeapon *pBaseWeapon )
{
	CWeaponCSBase *pWeapon = dynamic_cast< CWeaponCSBase* >( pBaseWeapon );

	if ( pWeapon )
	{
		if ( pWeapon->IsA(WEAPON_TASER) && !pWeapon->HasAnyAmmo() )
			return false;

		if ( CanAcquire( pWeapon->GetCSWeaponID(), AcquireMethod::PickUp ) != AcquireResult::Allowed )
			return false;

		// Don't give weapon_c4 to non-terrorists
		if ( pWeapon->GetCSWpnData().m_WeaponType == WEAPONTYPE_C4 && GetTeamNumber() != TEAM_TERRORIST && mp_anyone_can_pickup_c4.GetBool() == false )
		{
			return false;
		}
	}

	return true;
}

bool CCSPlayer::BumpWeapon( CBaseCombatWeapon *pBaseWeapon )
{
	CWeaponCSBase *pWeapon = dynamic_cast< CWeaponCSBase* >(pBaseWeapon);
	if ( !pWeapon )
	{
		Assert( !pWeapon );
		pBaseWeapon->AddSolidFlags( FSOLID_NOT_SOLID );
		pBaseWeapon->AddEffects( EF_NODRAW );
		Weapon_Equip( pBaseWeapon );
		return true;
	}

	CBaseCombatCharacter *pOwner = pWeapon->GetOwner();

	// Can I have this weapon type?
	if ( pOwner || !Weapon_CanUse( pWeapon ) || !g_pGameRules->CanHavePlayerItem( this, pWeapon ) )
	{
		extern int gEvilImpulse101;
		if ( gEvilImpulse101 )
		{
			UTIL_Remove( pWeapon );
		}
		return false;
	}

	// Even if we already have a grenade in this slot, we can pickup another one if we don't already
	// own this type of grenade.
	bool bPickupGrenade = (pWeapon->GetWeaponType() == WEAPONTYPE_GRENADE);

	bool bStackableItem = (pWeapon->GetWeaponType() == WEAPONTYPE_STACKABLEITEM);
	/*
	// ----------------------------------------
	// If I already have it just take the ammo
	// ----------------------------------------
	if ( !bPickupGrenade && Weapon_SlotOccupied( pWeapon ) )
	{
	Weapon_EquipAmmoOnly( pWeapon );
	// Only remove me if I have no ammo left
	// Can't just check HasAnyAmmo because if I don't use clips, I want to be removed,
	if ( pWeapon->UsesClipsForAmmo1() && pWeapon->HasPrimaryAmmo() )
	return false;

	UTIL_Remove( pWeapon );
	return false;
	}
	*/

	if ( HasShield() && pWeapon->GetCSWpnData().m_bCanUseWithShield == false )
		return false;

	bool bPickupTaser = ( pWeapon->IsA( WEAPON_TASER ) );
	if ( bPickupTaser )
	{
		CBaseCombatWeapon *pOwnedTaser = Weapon_OwnsThisType( "weapon_taser" );
		if ( pOwnedTaser )
			return false;
	}

	bool bPickupC4 = (pWeapon->GetWeaponType() == WEAPONTYPE_C4);
	if ( bPickupC4 )
	{
		// we're only allowed to pick up one c4 at a time
		CBaseCombatWeapon *pC4 = Weapon_OwnsThisType( "weapon_c4" );
		if ( pC4 )
			return false;

		// see if we're trying to pick up the bomb without being able to "see" it
		// prevent picking it up through a thin wall
		float flDist = (pWeapon->GetAbsOrigin() - GetAbsOrigin()).AsVector2D().Length();
		if ( flDist > 34 )
		{
			trace_t tr;
			UTIL_TraceLine( pWeapon->GetAbsOrigin(), EyePosition(), MASK_VISIBLE, this, COLLISION_GROUP_DEBRIS, &tr );
			if ( tr.fraction < 1.0 )
				return false;
		}
	}

	//	bool bPickupCarriableItem = ( pWeapon->GetCSWpnData().m_WeaponType == WEAPONTYPE_CARRIABLEITEM );
	//	if ( bPickupCarriableItem && Weapon_SlotOccupied( pWeapon ) )
	//	{
	//		CBaseCarribleItem *pPickupItem = static_cast<CBaseCarribleItem*>(pWeapon);
	//		if ( pPickupItem )
	//		{
	//			CBaseCarribleItem *pOwnedItem = static_cast<CBaseCarribleItem*>(Weapon_OwnsThisType( pPickupItem->GetClassname(), pPickupItem->GetSubType() ));
	//			if ( pOwnedItem && pOwnedItem->GetCurrentItems() < pOwnedItem->GetMaxItems() )
	//			{
	//				pOwnedItem->AddAmmo( pPickupItem->GetCurrentItems() );
	//				UTIL_Remove( pPickupItem );
	//			}
	//			
	//			return false;
	//		}
	//	}

	if ( bPickupC4 || bStackableItem || bPickupGrenade || bPickupTaser || /*bPickupCarriableItem || */ !Weapon_SlotOccupied( pWeapon ) )
	{
		// we have to do this here because picking up weapons placed in the world don't have their clips set
		// TODO: give the weapon a clip on spawn and not when picked up!
		if ( !pWeapon->GetPreviousOwner() )
			StockPlayerAmmo( pWeapon );

		pWeapon->CheckRespawn();

		pWeapon->AddSolidFlags( FSOLID_NOT_SOLID );
		pWeapon->AddEffects( EF_NODRAW );

		CCSPlayer* donor = pWeapon->GetDonor();
		if ( donor )
		{
			CCS_GameStats.Event_PlayerDonatedWeapon( donor );
			pWeapon->SetDonor( NULL );
		}

		Weapon_Equip( pWeapon );

		// Made obsolete when ammo was moved from player to weapon
		// 		int iExtraAmmo = pWeapon->GetExtraAmmoCount();
		// 		
		// 		if( iExtraAmmo /*&& !bPickupGrenade*/ /*&& !bPickupCarriableItem*/ )
		// 		{
		// 			//Find out the index of the ammo
		// 			int iAmmoIndex = pWeapon->GetPrimaryAmmoType();
		// 
		// 			if( iAmmoIndex != -1 )
		// 			{
		// 				//Remove the extra ammo from the weapon
		// 				pWeapon->SetExtraAmmoCount(0 );
		// 
		// 				//Give it to the player
		// 				SetAmmoCount( iExtraAmmo, iAmmoIndex );
		// 			}
		// 		}

		bool bIsSilentPickup = ShouldPickupItemSilently( this );
		IGameEvent * event = gameeventmanager->CreateEvent( "item_pickup" );
		if ( event )
		{
			const char *weaponName = pWeapon->GetClassname();
			if ( strncmp( weaponName, "weapon_", 7 ) == 0 )
			{
				weaponName += 7;
			}
			event->SetInt( "userid", GetUserID() );
			event->SetString( "item", weaponName );
			event->SetBool( "silent", bIsSilentPickup );
			gameeventmanager->FireEvent( event );
		}

		if ( !bIsSilentPickup )
			EmitSound( "Player.PickupWeapon" );

		return true;
	}

	return false;
}


void CCSPlayer::ResetStamina( void )
{
	m_flStamina = 0.0f;
}

void CCSPlayer::RescueZoneTouch( inputdata_t &inputdata )
{
	m_bInHostageRescueZone = true;
	if ( GetTeamNumber() == TEAM_CT && !(m_iDisplayHistoryBits & DHF_IN_RESCUE_ZONE) )
	{
		HintMessage( "#Hint_hostage_rescue_zone", false );
		m_iDisplayHistoryBits |= DHF_IN_RESCUE_ZONE;
	}

	// if the player is carrying a hostage when he touches the rescue zone, pass the touch input to it
	if ( m_hCarriedHostage && m_hCarriedHostage.Get() )
	{
		variant_t emptyVariant;
		m_hCarriedHostage.Get()->AcceptInput( "OnRescueZoneTouch", NULL, NULL, emptyVariant, 0 );
	}
}

//------------------------------------------------------------------------------------------
CON_COMMAND( timeleft, "prints the time remaining in the match" )
{
	CCSPlayer *pPlayer = ToCSPlayer( UTIL_GetCommandClient() );
	if ( pPlayer && pPlayer->m_iNextTimeCheck >= gpGlobals->curtime )
	{
		return; // rate limiting
	}

	int iTimeRemaining = (int)CSGameRules()->GetMapRemainingTime();

	if ( iTimeRemaining < 0 )
	{
		if ( pPlayer )
		{
			ClientPrint( pPlayer, HUD_PRINTTALK, "#Game_no_timelimit" );
		}
		else
		{
			Msg( "* No Time Limit *\n" );
		}
	}
	else if ( iTimeRemaining == 0 )
	{
		if ( pPlayer )
		{
			ClientPrint( pPlayer, HUD_PRINTTALK, "#Game_last_round" );
		}
		else
		{
			Msg( "* Last Round *\n" );
		}
	}
	else
	{
		int iMinutes, iSeconds;
		iMinutes = iTimeRemaining / 60;
		iSeconds = iTimeRemaining % 60;

		char minutes[8];
		char seconds[8];

		Q_snprintf( minutes, sizeof(minutes), "%d", iMinutes );
		Q_snprintf( seconds, sizeof(seconds), "%2.2d", iSeconds );

		if ( pPlayer )
		{
			ClientPrint( pPlayer, HUD_PRINTTALK, "#Game_timelimit", minutes, seconds );
		}
		else
		{
			Msg( "Time Remaining:  %s:%s\n", minutes, seconds );
		}
	}

	if ( pPlayer )
	{
		pPlayer->m_iNextTimeCheck = gpGlobals->curtime + 1;
	}
}

//------------------------------------------------------------------------------------------
/**
 * Emit given sound that only we can hear
 */
void CCSPlayer::EmitPrivateSound( const char *soundName )
{
	CSoundParameters params;
	if (!GetParametersForSound( soundName, params, NULL ))
		return;

	CSingleUserRecipientFilter filter( this );
	EmitSound( filter, entindex(), soundName );
}


//=====================
//Autobuy
//=====================
static void AutoBuy( void )
{
	CCSPlayer *player = ToCSPlayer( UTIL_GetCommandClient() );

	if ( player )
		player->AutoBuy();
}
static ConCommand autobuy( "autobuy", AutoBuy, "Attempt to purchase items with the order listed in cl_autobuy" );

//==============================================
//AutoBuy - do the work of deciding what to buy
//==============================================
void CCSPlayer::AutoBuy()
{
	if ( !IsInBuyZone() )
	{
		EmitPrivateSound( "BuyPreset.CantBuy" );
		return;
	}

	const char *autobuyString = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_autobuy" );
	if ( !autobuyString || !*autobuyString )
	{
		EmitPrivateSound( "BuyPreset.AlreadyBought" );
		return;
	}

	bool boughtPrimary = false, boughtSecondary = false;

	m_bIsInAutoBuy = true;
	ParseAutoBuyString(autobuyString, boughtPrimary, boughtSecondary);
	m_bIsInAutoBuy = false;

	m_bAutoReload = true;

	//TODO ?: stripped out all the attempts to buy career weapons.
	// as we're not porting cs:cz, these were skipped
}

void CCSPlayer::ParseAutoBuyString(const char *string, bool &boughtPrimary, bool &boughtSecondary)
{
	char command[32];
	int nBuffSize = sizeof(command) - 1; // -1 to leave space for the NULL at the end of the string
	const char *c = string;

	if (c == NULL)
	{
		EmitPrivateSound( "BuyPreset.AlreadyBought" );
		return;
	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;

	// loop through the string of commands, trying each one in turn.
	while (*c != 0)
	{
		int i = 0;
		// copy the next word into the command buffer.
		while ((*c != 0) && (*c != ' ') && (i < nBuffSize))
		{
			command[i] = *(c);
			++c;
			++i;
		}
		if (*c == ' ')
		{
			++c; // skip the space.
		}

		command[i] = 0; // terminate the string.

		// clear out any spaces.
		i = 0;
		while (command[i] != 0)
		{
			if (command[i] == ' ')
			{
				command[i] = 0;
				break;
			}
			++i;
		}

		// make sure we actually have a command.
		if (strlen(command) == 0)
		{
			continue;
		}

		AutoBuyInfoStruct * commandInfo = GetAutoBuyCommandInfo(command);

		if (ShouldExecuteAutoBuyCommand(commandInfo, boughtPrimary, boughtSecondary))
		{
			BuyResult_e result = HandleCommand_Buy( command );

			overallResult = CombineBuyResults( overallResult, result );

			// check to see if we actually bought a primary or secondary weapon this time.
			PostAutoBuyCommandProcessing(commandInfo, boughtPrimary, boughtSecondary);
		}
	}

	if ( overallResult == BUY_CANT_AFFORD )
	{
		EmitPrivateSound( "BuyPreset.CantBuy" );
	}
	else if ( overallResult == BUY_ALREADY_HAVE )
	{
		EmitPrivateSound( "BuyPreset.AlreadyBought" );
	}
	else if ( overallResult == BUY_BOUGHT )
	{
		g_iAutoBuyPurchases++;
	}
}

BuyResult_e CCSPlayer::CombineBuyResults( BuyResult_e prevResult, BuyResult_e newResult )
{
	if ( newResult == BUY_BOUGHT )
	{
		prevResult = BUY_BOUGHT;
	}
	else if ( prevResult != BUY_BOUGHT &&
		(newResult == BUY_CANT_AFFORD || newResult == BUY_INVALID_ITEM || newResult == BUY_PLAYER_CANT_BUY ) )
	{
		prevResult = BUY_CANT_AFFORD;
	}

	return prevResult;
}

//==============================================
//PostAutoBuyCommandProcessing
//==============================================
void CCSPlayer::PostAutoBuyCommandProcessing(const AutoBuyInfoStruct *commandInfo, bool &boughtPrimary, bool &boughtSecondary)
{
	if (commandInfo == NULL)
	{
		return;
	}

	char classname[64];
	Q_strcpy( classname, commandInfo->m_classname );

	const char* loadoutWeapon = CSLoadout()->GetWeaponFromSlot( this, CSLoadout()->GetSlotFromWeapon( this, commandInfo->m_command ) );
	if ( loadoutWeapon != NULL )
		Q_snprintf( classname, sizeof( classname ), "weapon_%s", loadoutWeapon );

	CBaseCombatWeapon *pPrimary = Weapon_GetSlot( WEAPON_SLOT_RIFLE );
	CBaseCombatWeapon *pSecondary = Weapon_GetSlot( WEAPON_SLOT_PISTOL );

	if ((pPrimary != NULL) && (stricmp(pPrimary->GetClassname(), classname) == 0))
	{
		// I just bought the gun I was trying to buy.
		boughtPrimary = true;
	}
	else if ((pPrimary == NULL) && ((commandInfo->m_class & AUTOBUYCLASS_SHIELD) == AUTOBUYCLASS_SHIELD) && HasShield())
	{
		// the shield is a primary weapon even though it isn't a "real" weapon.
		boughtPrimary = true;
	}
	else if ((pSecondary != NULL) && (stricmp(pSecondary->GetClassname(), classname) == 0))
	{
		// I just bought the pistol I was trying to buy.
		boughtSecondary = true;
	}
}

bool CCSPlayer::ShouldExecuteAutoBuyCommand(const AutoBuyInfoStruct *commandInfo, bool boughtPrimary, bool boughtSecondary)
{
	if (commandInfo == NULL)
	{
		return false;
	}

	if ((boughtPrimary) && ((commandInfo->m_class & AUTOBUYCLASS_PRIMARY) != 0) && ((commandInfo->m_class & AUTOBUYCLASS_AMMO) == 0))
	{
		// this is a primary weapon and we already have one.
		return false;
	}

	if ((boughtSecondary) && ((commandInfo->m_class & AUTOBUYCLASS_SECONDARY) != 0) && ((commandInfo->m_class & AUTOBUYCLASS_AMMO) == 0))
	{
		// this is a secondary weapon and we already have one.
		return false;
	}

	if( commandInfo->m_class & AUTOBUYCLASS_ARMOR && ArmorValue() >= 100 )
	{
		return false;
	}

	return true;
}

AutoBuyInfoStruct *CCSPlayer::GetAutoBuyCommandInfo(const char *command)
{
	int i = 0;
	AutoBuyInfoStruct *ret = NULL;
	AutoBuyInfoStruct *temp = &(g_autoBuyInfo[i]);

	// loop through all the commands till we find the one that matches.
	while ((ret == NULL) && (temp->m_class != (AutoBuyClassType)0))
	{
		temp = &(g_autoBuyInfo[i]);
		++i;

		if (stricmp(temp->m_command, command) == 0)
		{
			ret = temp;
		}
	}

	return ret;
}

//==============================================
//PostAutoBuyCommandProcessing
//- reorders the tokens in autobuyString based on the order of tokens in the priorityString.
//==============================================
void CCSPlayer::PrioritizeAutoBuyString(char *autobuyString, const char *priorityString)
{
	char newString[256];
	int newStringPos = 0;
	char priorityToken[32];

	if ((priorityString == NULL) || (autobuyString == NULL))
	{
		return;
	}

	const char *priorityChar = priorityString;

	while (*priorityChar != 0)
	{
		int i = 0;

		// get the next token from the priority string.
		while ((*priorityChar != 0) && (*priorityChar != ' '))
		{
			priorityToken[i] = *priorityChar;
			++i;
			++priorityChar;
		}
		priorityToken[i] = 0;

		// skip spaces
		while (*priorityChar == ' ')
		{
			++priorityChar;
		}

		if (strlen(priorityToken) == 0)
		{
			continue;
		}

		// see if the priority token is in the autobuy string.
		// if  it is, copy that token to the new string and blank out
		// that token in the autobuy string.
		char *autoBuyPosition = strstr(autobuyString, priorityToken);
		if (autoBuyPosition != NULL)
		{
			while ((*autoBuyPosition != 0) && (*autoBuyPosition != ' '))
			{
				newString[newStringPos] = *autoBuyPosition;
				*autoBuyPosition = ' ';
				++newStringPos;
				++autoBuyPosition;
			}

			newString[newStringPos++] = ' ';
		}
	}

	// now just copy anything left in the autobuyString to the new string in the order it's in already.
	char *autobuyPosition = autobuyString;
	while (*autobuyPosition != 0)
	{
		// skip spaces
		while (*autobuyPosition == ' ')
		{
			++autobuyPosition;
		}

		// copy the token over to the new string.
		while ((*autobuyPosition != 0) && (*autobuyPosition != ' '))
		{
			newString[newStringPos] = *autobuyPosition;
			++newStringPos;
			++autobuyPosition;
		}

		// add a space at the end.
		newString[newStringPos++] = ' ';
	}

	// terminate the string.  Trailing spaces shouldn't matter.
	newString[newStringPos] = 0;

	Q_snprintf(autobuyString, sizeof(autobuyString), "%s", newString);
}


//==============================================================
// ReBuy
// system for attempting to buy the weapons you had last round
//==============================================================
static void Rebuy( void )
{
	CCSPlayer *player = ToCSPlayer( UTIL_GetCommandClient() );

	if ( player )
		player->Rebuy();
}
static ConCommand rebuy( "rebuy", Rebuy, "Attempt to repurchase items with the order listed in cl_rebuy" );

void CCSPlayer::BuildRebuyStruct()
{
	if (m_bIsInRebuy)
	{
		// if we are in the middle of a rebuy, we don't want to update the buy struct.
		return;
	}

	CBaseCombatWeapon *primary = Weapon_GetSlot( WEAPON_SLOT_RIFLE );
	CBaseCombatWeapon *secondary = Weapon_GetSlot( WEAPON_SLOT_PISTOL );

	// do the primary weapon/ammo stuff.
	if (primary == NULL)
	{
		// count a shieldgun as a primary.
		if (HasShield())
		{
			//m_rebuyStruct.m_primaryWeapon = WEAPON_SHIELDGUN;
			Q_strncpy( m_rebuyStruct.m_szPrimaryWeapon, "shield", sizeof(m_rebuyStruct.m_szPrimaryWeapon) );
			m_rebuyStruct.m_primaryAmmo = 0; // shields don't have ammo.
		}
		else
		{

			m_rebuyStruct.m_szPrimaryWeapon[0] = 0;	// if we don't have a shield and we don't have a primary weapon, we got nuthin.
			m_rebuyStruct.m_primaryAmmo = 0;		// can't have ammo if we don't have a gun right?
		}
	}
	else
	{
		//strip off the "weapon_"

		const char *wpnName = primary->GetClassname();

		Q_strncpy( m_rebuyStruct.m_szPrimaryWeapon, wpnName + 7, sizeof(m_rebuyStruct.m_szPrimaryWeapon) );

		if( primary->GetPrimaryAmmoType() != -1 )
		{
			m_rebuyStruct.m_primaryAmmo = GetAmmoCount( primary->GetPrimaryAmmoType() );
		}
	}

	// do the secondary weapon/ammo stuff.
	if (secondary == NULL)
	{
		m_rebuyStruct.m_szSecondaryWeapon[0] = 0;
		m_rebuyStruct.m_secondaryAmmo = 0; // can't have ammo if we don't have a gun right?
	}
	else
	{
		const char *wpnName = secondary->GetClassname();

		Q_strncpy( m_rebuyStruct.m_szSecondaryWeapon, wpnName + 7, sizeof(m_rebuyStruct.m_szSecondaryWeapon) );

		if( secondary->GetPrimaryAmmoType() != -1 )
		{
			m_rebuyStruct.m_secondaryAmmo = GetAmmoCount( secondary->GetPrimaryAmmoType() );
		}
	}

	CBaseCombatWeapon *pGrenade;

	//MATTTODO: right now you can't buy more than one grenade. make it so you can
	//buy more and query the number you have.
	// HE Grenade
	pGrenade = Weapon_OwnsThisType( "weapon_hegrenade" );
	if ( pGrenade && pGrenade->GetPrimaryAmmoType() != -1 )
	{
		m_rebuyStruct.m_heGrenade = GetAmmoCount(pGrenade->GetPrimaryAmmoType());
	}
	else
		m_rebuyStruct.m_heGrenade = 0;


	// flashbang
	pGrenade = Weapon_OwnsThisType( "weapon_flashbang" );
	if ( pGrenade && pGrenade->GetPrimaryAmmoType() != -1 )
	{
		m_rebuyStruct.m_flashbang = GetAmmoCount(pGrenade->GetPrimaryAmmoType());
	}
	else
		m_rebuyStruct.m_flashbang = 0;

	// smokegrenade
	pGrenade = Weapon_OwnsThisType( "weapon_smokegrenade" );
	if ( pGrenade /*&& pGrenade->GetPrimaryAmmoType() != -1*/ )
	{
		m_rebuyStruct.m_smokeGrenade = 1; //GetAmmoCount(pGrenade->GetPrimaryAmmoType());
	}
	else
		m_rebuyStruct.m_smokeGrenade = 0;

	// decoy
	pGrenade = Weapon_OwnsThisType( "weapon_decoy" );
	if ( pGrenade /*&& pGrenade->GetPrimaryAmmoType() != -1*/ )
	{
		m_rebuyStruct.m_decoy = 1; //GetAmmoCount(pGrenade->GetPrimaryAmmoType());
	}
	else
		m_rebuyStruct.m_decoy = 0;

	// incgrenade
						pGrenade = Weapon_OwnsThisType( "weapon_incgrenade" );
	CBaseCombatWeapon*	pMolotov = Weapon_OwnsThisType( "weapon_molotov" );
	if ( pGrenade || pMolotov )
	{
		m_rebuyStruct.m_molotov = 1; //GetAmmoCount(pGrenade->GetPrimaryAmmoType());
	}
	else
		m_rebuyStruct.m_molotov = 0;

	// defuser
	m_rebuyStruct.m_defuser = HasDefuser();

	// taser
	m_rebuyStruct.m_taser = (Weapon_OwnsThisType( "weapon_taser" )) ? true : false;

	// night vision
	m_rebuyStruct.m_nightVision = m_bHasNightVision.Get();	//cast to avoid strange compiler warning

	// check for armor.
	m_rebuyStruct.m_armor = ( m_bHasHelmet ? 2 : ( ArmorValue() > 0 ? 1 : 0 ) );
}

void CCSPlayer::Rebuy( void )
{
	if ( !IsInBuyZone() )
	{
		EmitPrivateSound( "BuyPreset.CantBuy" );
		return;
	}

	const char *rebuyString = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_rebuy" );
	if ( !rebuyString || !*rebuyString )
	{
		EmitPrivateSound( "BuyPreset.AlreadyBought" );
		return;
	}

	m_bIsInRebuy = true;
	BuyResult_e overallResult = BUY_ALREADY_HAVE;

	char token[256];
	rebuyString = engine->ParseFile( rebuyString, token, sizeof( token ) );

	while (rebuyString != NULL)
	{
		BuyResult_e result = BUY_ALREADY_HAVE;

		if (!Q_strncmp(token, "PrimaryWeapon", 14))
		{
			result = RebuyPrimaryWeapon();
		}/*
		else if (!Q_strncmp(token, "PrimaryAmmo", 12))
		{
			result = RebuyPrimaryAmmo();
		}*/
		else if (!Q_strncmp(token, "SecondaryWeapon", 16))
		{
			result = RebuySecondaryWeapon();
		}/*
		else if (!Q_strncmp(token, "SecondaryAmmo", 14))
		{
			result = RebuySecondaryAmmo();
		}*/
		else if ( Q_strcasecmp(token, "Taser" ) == 0 )		// TODO[pmf]: handle this better
		{
			result = RebuyTaser();
		}
		else if (!Q_strncmp(token, "HEGrenade", 10))
		{
			result = RebuyHEGrenade();
		}
		else if (!Q_strncmp(token, "Flashbang", 10))
		{
			result = RebuyFlashbang();
		}
		else if (!Q_strncmp(token, "SmokeGrenade", 13))
		{
			result = RebuySmokeGrenade();
		}
		else if (!Q_strncmp(token, "Decoy", 6))
		{
			result = RebuyDecoy();
		}
		else if (!Q_strncmp(token, "Molotov", 7))
		{
			result = RebuyMolotov();
		}
		else if (!Q_strncmp(token, "Defuser", 8))
		{
			result = RebuyDefuser();
		}
		else if (!Q_strncmp(token, "NightVision", 12))
		{
			result = RebuyNightVision();
		}
		else if (!Q_strncmp(token, "Armor", 6))
		{
			result = RebuyArmor();
		}

		overallResult = CombineBuyResults( overallResult, result );

		rebuyString = engine->ParseFile( rebuyString, token, sizeof( token ) );
	}

	m_bIsInRebuy = false;

	// after we're done buying, the user is done with their equipment purchasing experience.
	// so we are effectively out of the buy zone.
//	if (TheTutor != NULL)
//	{
//		TheTutor->OnEvent(EVENT_PLAYER_LEFT_BUY_ZONE);
//	}

	m_bAutoReload = true;

	if ( overallResult == BUY_CANT_AFFORD )
	{
		EmitPrivateSound( "BuyPreset.CantBuy" );
	}
	else if ( overallResult == BUY_ALREADY_HAVE )
	{
		EmitPrivateSound( "BuyPreset.AlreadyBought" );
	}
	else if ( overallResult == BUY_BOUGHT )
	{
		g_iReBuyPurchases++;
	}
}

BuyResult_e CCSPlayer::RebuyPrimaryWeapon()
{
	CBaseCombatWeapon *primary = Weapon_GetSlot( WEAPON_SLOT_RIFLE );
	if (primary != NULL)
	{
		return BUY_ALREADY_HAVE;	// don't drop primary weapons via rebuy - if the player picked up a different weapon, he wants to keep it.
	}

	if( strlen( m_rebuyStruct.m_szPrimaryWeapon ) > 0 )
		return HandleCommand_Buy(m_rebuyStruct.m_szPrimaryWeapon);

	return BUY_ALREADY_HAVE;
}

BuyResult_e CCSPlayer::RebuySecondaryWeapon()
{
	CBaseCombatWeapon *pistol = Weapon_GetSlot( WEAPON_SLOT_PISTOL );
	if (pistol != NULL && !m_bUsingDefaultPistol)
	{
		return BUY_ALREADY_HAVE;	// don't drop pistols via rebuy if we've bought one other than the default pistol
	}

	if( strlen( m_rebuyStruct.m_szSecondaryWeapon ) > 0 )
		return HandleCommand_Buy(m_rebuyStruct.m_szSecondaryWeapon);

	return BUY_ALREADY_HAVE;
}

BuyResult_e CCSPlayer::RebuyTaser()
{
	if ( m_rebuyStruct.m_taser )
		return HandleCommand_Buy( "taser" );

	return BUY_INVALID_ITEM;
}
/*
BuyResult_e CCSPlayer::RebuyPrimaryAmmo()
{
	CBaseCombatWeapon *primary = Weapon_GetSlot( WEAPON_SLOT_RIFLE );

	if (primary == NULL)
	{
		return BUY_ALREADY_HAVE;	// can't buy ammo when we don't even have a gun.
	}

	// Ensure that the weapon uses ammo
	int nAmmo = primary->GetPrimaryAmmoType();
	if ( nAmmo == -1 )
	{
		return BUY_ALREADY_HAVE;
	}

	// if we had more ammo before than we have now, buy more.
	if (m_rebuyStruct.m_primaryAmmo > GetAmmoCount( nAmmo ))
	{
		return HandleCommand_Buy("primammo");
	}

	return BUY_ALREADY_HAVE;
}


BuyResult_e CCSPlayer::RebuySecondaryAmmo()
{
	CBaseCombatWeapon *secondary = Weapon_GetSlot( WEAPON_SLOT_PISTOL );

	if (secondary == NULL)
	{
		return BUY_ALREADY_HAVE; // can't buy ammo when we don't even have a gun.
	}

	// Ensure that the weapon uses ammo
	int nAmmo = secondary->GetPrimaryAmmoType();
	if ( nAmmo == -1 )
	{
		return BUY_ALREADY_HAVE;
	}

	if (m_rebuyStruct.m_secondaryAmmo > GetAmmoCount( nAmmo ))
	{
		return HandleCommand_Buy("secammo");
	}

	return BUY_ALREADY_HAVE;
}
*/
BuyResult_e CCSPlayer::RebuyHEGrenade()
{
	CBaseCombatWeapon *pGrenade = Weapon_OwnsThisType( "weapon_hegrenade" );

	int numGrenades = 0;

	if( pGrenade )
	{
		int nAmmo = pGrenade->GetPrimaryAmmoType();
		if ( nAmmo == -1 )
		{
			return BUY_ALREADY_HAVE;
		}

		numGrenades = GetAmmoCount( nAmmo );
	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;
	int numToBuy = MAX( 0, m_rebuyStruct.m_heGrenade - numGrenades );
	for (int i = 0; i < numToBuy; ++i)
	{
		BuyResult_e result = HandleCommand_Buy("hegrenade");
		overallResult = CombineBuyResults( overallResult, result );
	}

	return overallResult;
}

BuyResult_e CCSPlayer::RebuyFlashbang()
{
	CBaseCombatWeapon *pGrenade = Weapon_OwnsThisType( "weapon_flashbang" );

	int numGrenades = 0;

	if( pGrenade )
	{
		int nAmmo = pGrenade->GetPrimaryAmmoType();
		if ( nAmmo == -1 )
		{
			return BUY_ALREADY_HAVE;
		}
		numGrenades = GetAmmoCount( nAmmo );

	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;
	int numToBuy = MAX( 0, m_rebuyStruct.m_flashbang - numGrenades );
	for (int i = 0; i < numToBuy; ++i)
	{
		BuyResult_e result = HandleCommand_Buy("flashbang");
		overallResult = CombineBuyResults( overallResult, result );
	}

	return overallResult;
}

BuyResult_e CCSPlayer::RebuySmokeGrenade()
{
	CBaseCombatWeapon *pGrenade = Weapon_OwnsThisType( "weapon_smokegrenade" );

	int numGrenades = 0;

	if( pGrenade )
	{
		int nAmmo = pGrenade->GetPrimaryAmmoType();
		if ( nAmmo == -1 )
		{
			return BUY_ALREADY_HAVE;
		}

		numGrenades = GetAmmoCount( nAmmo );
	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;
	int numToBuy = MAX( 0, m_rebuyStruct.m_smokeGrenade - numGrenades );
	for (int i = 0; i < numToBuy; ++i)
	{
		BuyResult_e result = HandleCommand_Buy("smokegrenade");
		overallResult = CombineBuyResults( overallResult, result );
	}

	return overallResult;
}

BuyResult_e CCSPlayer::RebuyDecoy()
{
	CBaseCombatWeapon *pGrenade = Weapon_OwnsThisType( "weapon_decoy" );

	int numGrenades = 0;

	if( pGrenade )
	{
		int nAmmo = pGrenade->GetPrimaryAmmoType();
		if ( nAmmo == -1 )
		{
			return BUY_ALREADY_HAVE;
		}

		numGrenades = GetAmmoCount( nAmmo );
	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;
	int numToBuy = MAX( 0, m_rebuyStruct.m_decoy - numGrenades );
	for (int i = 0; i < numToBuy; ++i)
	{
		BuyResult_e result = HandleCommand_Buy("decoy");
		overallResult = CombineBuyResults( overallResult, result );
	}

	return overallResult;
}

BuyResult_e CCSPlayer::RebuyMolotov()
{
	CBaseCombatWeapon *pIncGrenade = Weapon_OwnsThisType( "weapon_incgrenade" );
	CBaseCombatWeapon *pMolotov = Weapon_OwnsThisType( "weapon_molotov" );

	int numGrenades = 0;

	if ( pIncGrenade || pMolotov )
	{
		int nAmmoInc = pIncGrenade ? pIncGrenade->GetPrimaryAmmoType() : 0;
		int nAmmoMol = pMolotov ? pMolotov->GetPrimaryAmmoType() : 0;
		if ( nAmmoInc == -1 && nAmmoMol == -1 )
		{
			return BUY_ALREADY_HAVE;
		}

		numGrenades = GetAmmoCount( nAmmoInc + nAmmoMol ); // PiMoN: is it really working?
	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;
	int numToBuy = MAX( 0, m_rebuyStruct.m_molotov - numGrenades );
	for (int i = 0; i < numToBuy; ++i)
	{
		BuyResult_e result = HandleCommand_Buy( GetTeamNumber() == TEAM_CT ? "incgrenade" : "molotov" );
		overallResult = CombineBuyResults( overallResult, result );
	}

	return overallResult;
}

BuyResult_e CCSPlayer::RebuyDefuser()
{
	//If we don't have a defuser, and we want one, buy it!
	if( !HasDefuser() && m_rebuyStruct.m_defuser )
	{
		return HandleCommand_Buy("defuser");
	}

	return BUY_ALREADY_HAVE;
}

BuyResult_e CCSPlayer::RebuyNightVision()
{
	//if we don't have night vision and we want one, buy it!
	if( !m_bHasNightVision && m_rebuyStruct.m_nightVision )
	{
		return HandleCommand_Buy("nvgs");
	}

	return BUY_ALREADY_HAVE;
}

BuyResult_e CCSPlayer::RebuyArmor()
{
	if (m_rebuyStruct.m_armor > 0 )
	{
		int armor = 0;

		if( m_bHasHelmet )
			armor = 2;
		else if( ArmorValue() > 0 )
			armor = 1;

		if( armor < m_rebuyStruct.m_armor )
		{
			if (m_rebuyStruct.m_armor == 1)
			{
				return HandleCommand_Buy("vest");
			}
			else
			{
				return HandleCommand_Buy("vesthelm");
			}
		}

	}

	return BUY_ALREADY_HAVE;
}

bool CCSPlayer::IsUseableEntity( CBaseEntity *pEntity, unsigned int requiredCaps )
{
	CWeaponCSBase *pCSWepaon = dynamic_cast<CWeaponCSBase*>(pEntity);

	if( pCSWepaon )
	{
		// we can't USE dropped weapons
		return true;
	}

	CBaseCSGrenadeProjectile *pGrenade = dynamic_cast<CBaseCSGrenadeProjectile*>(pEntity);
	if ( pGrenade )
	{
		// we can't USE thrown grenades
	}

	return BaseClass::IsUseableEntity( pEntity, requiredCaps );
}

CBaseEntity *CCSPlayer::FindUseEntity()
{
	CBaseEntity *entity = NULL;

	// Check to see if the bomb is close enough to use before attempting to use anything else.

	if ( CSGameRules()->IsBombDefuseMap() && GetTeamNumber() == TEAM_CT )
	{
		// This is done separately since there might be something blocking our LOS to it
		// but we might want to use it anyway if it's close enough.  This should eliminate
		// the vast majority of bomb placement exploits (places where the bomb can be planted
		// but can't be "used".  This also mimics goldsrc cstrike behavior.
		CBaseEntity *bomb = gEntList.FindEntityByClassname( NULL, PLANTED_C4_CLASSNAME );
		if (bomb != NULL)
		{
			Vector bombPos = bomb->GetAbsOrigin();
			Vector vecLOS = EyePosition() - bombPos;

			if (vecLOS.LengthSqr() < (96*96)) // 64 is the distance in Goldsrc.  However since Goldsrc did distance from the player's origin and we're doing distance from the player's eye, make the radius a bit bigger.
			{
				// bomb is close enough, now make sure the player is facing the bomb.
				Vector forward;
				AngleVectors(EyeAngles(), &forward, NULL, NULL);

				vecLOS.NormalizeInPlace();

				float flDot = DotProduct(forward, vecLOS);
				if (flDot < -0.7) // 0.7 taken from Goldsrc, +/- ~45 degrees
				{
					entity = bomb;
				}
			}
		}
	}

	if ( entity == NULL )
	{
		entity = BaseClass::FindUseEntity();
	}

	return entity;
}

void CCSPlayer::StockPlayerAmmo( CBaseCombatWeapon *pNewWeapon )
{
	CWeaponCSBase *pWeapon =  dynamic_cast< CWeaponCSBase * >( pNewWeapon );

	if ( pWeapon )
	{
		if ( pWeapon->GetWpnData().iFlags & ITEM_FLAG_EXHAUSTIBLE )
			return;

		int nAmmo = pWeapon->GetPrimaryAmmoType();

		if ( nAmmo != -1 )
		{
			pWeapon->SetReserveAmmoCount( AMMO_POSITION_PRIMARY, 9999 );
			pWeapon->m_iClip1 = pWeapon->GetMaxClip1();
		}

		return;
	}

	pWeapon = dynamic_cast< CWeaponCSBase * >(Weapon_GetSlot( WEAPON_SLOT_RIFLE ));

	if ( pWeapon )
	{
		int nAmmo = pWeapon->GetPrimaryAmmoType();

		if ( nAmmo != -1 )
		{
			pWeapon->SetReserveAmmoCount( AMMO_POSITION_PRIMARY, 9999 );
			pWeapon->m_iClip1 = pWeapon->GetMaxClip1();
		}
	}

	pWeapon = dynamic_cast< CWeaponCSBase * >(Weapon_GetSlot( WEAPON_SLOT_PISTOL ));

	if ( pWeapon )
	{
		int nAmmo = pWeapon->GetPrimaryAmmoType();

		if ( nAmmo != -1 )
		{
			pWeapon->SetReserveAmmoCount( AMMO_POSITION_PRIMARY, 9999 );
			pWeapon->m_iClip1 = pWeapon->GetMaxClip1();
		}
	}
}

CBaseEntity	*CCSPlayer::GiveNamedItem( const char *pszName, int iSubType )
{
	EHANDLE pent;

	if ( !pszName || !pszName[0] )
		return  NULL;

#ifndef CS_SHIELD_ENABLED
	if ( !Q_stricmp( pszName, "weapon_shield" ) )
		return NULL;
#endif

	pent = CreateEntityByName(pszName);
	if ( pent == NULL )
	{
		Msg( "NULL Ent in GiveNamedItem!\n" );
		return NULL;
	}

	pent->SetLocalOrigin( GetLocalOrigin() );
	pent->AddSpawnFlags( SF_NORESPAWN );

	CBaseCombatWeapon *pWeapon = dynamic_cast<CBaseCombatWeapon*>( (CBaseEntity*)pent );
	if ( pWeapon )
	{
		if ( iSubType )
		{
			pWeapon->SetSubType( iSubType );
		}
	}

	DispatchSpawn( pent );

	m_bIsBeingGivenItem = true;
	if ( pent != NULL && !(pent->IsMarkedForDeletion()) )
	{
		pent->Touch( this );
	}
	m_bIsBeingGivenItem = false;

	StockPlayerAmmo( pWeapon );

	return pent;
}

void CCSPlayer::DoAnimStateEvent( PlayerAnimEvent_t evt )
{
	m_PlayerAnimState->DoAnimationEvent( evt );
}

void CCSPlayer::DoAnimationEvent( PlayerAnimEvent_t event, int nData )
{
	if ( m_bUseNewAnimstate )
	{
		// run the event on the server
		m_PlayerAnimStateCSGO->DoAnimationEvent( event, nData );
		return;
	}
	else
	{
		if ( event == PLAYERANIMEVENT_THROW_GRENADE )
		{
			// Grenade throwing has to synchronize exactly with the player's grenade weapon going away,
			// and events get delayed a bit, so we let CCSPlayerAnimState pickup the change to this
			// variable.
			m_iThrowGrenadeCounter = (m_iThrowGrenadeCounter+1 ) % (1<<THROWGRENADE_COUNTER_BITS );
		}
		else
		{
			m_PlayerAnimState->DoAnimationEvent( event, nData );
			TE_PlayerAnimEvent( this, event, nData );	// Send to any clients who can see this guy.
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CCSPlayer::FlashlightIsOn( void )
{
	return IsEffectActive( EF_DIMLIGHT );
}

extern ConVar flashlight;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CCSPlayer::FlashlightTurnOn( void )
{
	if( flashlight.GetInt() > 0 && IsAlive() )
	{
		AddEffects( EF_DIMLIGHT );
		EmitSound( "Player.FlashlightOn" );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CCSPlayer::FlashlightTurnOff( void )
{
	RemoveEffects( EF_DIMLIGHT );

	if( IsAlive() )
	{
		EmitSound( "Player.FlashlightOff" );
	}
}

bool CCSPlayer::HandleDropWeapon( CBaseCombatWeapon *pWeapon, bool bSwapping )
{

	CWeaponCSBase *pCSWeapon = dynamic_cast< CWeaponCSBase* >( pWeapon ? pWeapon : GetActiveWeapon() );

	if( pCSWeapon )
	{
/*
		CBaseCarribleItem *pItem = dynamic_cast< CBaseCarribleItem * >( pCSWeapon );
		if ( pItem  )
		{
			pItem->DropItem();
				
			// decrement the ammo
			pItem->DecrementAmmo( this );
			// if that was the last item, delete this one
			if ( pItem->GetCurrentItems() <= 0 )
			{
				CSWeaponDrop( pItem, true, true );
				UTIL_Remove( pItem );
				UpdateAddonBits();
			}

			return false;
		}
*/
		
		// [dwenger] Determine value of dropped item.
		if ( !pCSWeapon->IsAPriorOwner( this ) )
		{
			pCSWeapon->AddToPriorOwnerList( this );
			CCS_GameStats.IncrementStat(this, CSTAT_ITEMS_DROPPED_VALUE, pCSWeapon->GetCSWpnData().GetWeaponPrice() );
		}

		// PiMoN: uncomment this when we have healthshots
		/*if ( pCSWeapon->IsA( WEAPON_HEALTHSHOT ) )
		{
			CItem_Healthshot* pHealth = dynamic_cast< CItem_Healthshot* >( pCSWeapon );
			if ( pHealth )
			{
				pHealth->DropHealthshot();
				ClientPrint( this, HUD_PRINTCENTER, "#Cstrike_TitlesTXT_YouDroppedWeapon", pCSWeapon->GetPrintName() );
				
			}
			return true;
		}*/

		CSWeaponType type = pCSWeapon->GetWeaponType();
		switch ( type )
		{
		// Only certail weapons can be dropped when drop is initiated by player
		case WEAPONTYPE_PISTOL:
		case WEAPONTYPE_SUBMACHINEGUN:
		case WEAPONTYPE_RIFLE:
		case WEAPONTYPE_SHOTGUN:
		case WEAPONTYPE_SNIPER_RIFLE:
		case WEAPONTYPE_MACHINEGUN:
		case WEAPONTYPE_C4:
		{
			if (CSGameRules()->GetCanDonateWeapon() && !pCSWeapon->GetDonated() )
			{
				pCSWeapon->SetDonated(true );
				pCSWeapon->SetDonor(this );
			}
			CSWeaponDrop( pCSWeapon, true, true );

			if ( IsAlive() && !bSwapping )
				ClientPrint( this, HUD_PRINTCENTER, "#Cstrike_TitlesTXT_YouDroppedWeapon", pCSWeapon->GetPrintName() );
		}
		break;

		default:
		{
			// let dedicated servers optionally allow droppable knives
			if ( type == WEAPONTYPE_KNIFE && mp_drop_knife_enable.GetBool( ) )
			{
				if ( CSGameRules( )->GetCanDonateWeapon( ) && !pCSWeapon->GetDonated( ) )
				{
					pCSWeapon->SetDonated( true );
					pCSWeapon->SetDonor( this );
				}
				CSWeaponDrop( pCSWeapon, true, true );

				if ( IsAlive( ) && !bSwapping )
					ClientPrint( this, HUD_PRINTCENTER, "#Cstrike_TitlesTXT_YouDroppedWeapon", pCSWeapon->GetPrintName( ) );
			}
			else if ( IsAlive( ) && !bSwapping )
			{
				ClientPrint( this, HUD_PRINTCENTER, "#Cstrike_TitlesTXT_CannotDropWeapon", pCSWeapon->GetPrintName( ) );
			}
		}
		break;
		}

		return true;
	}

	return false;
}

//Drop the appropriate weapons:
// Defuser if we have one
// C4 if we have one
// The best weapon we have, first check primary,
// then secondary and drop the best one

//=============================================================================
// HPE_BEGIN:
// [tj] Added a parameter so we know if it was death that caused the drop
// [menglish] Clear all previously dropped equipment and add the c4 to the dropped equipment
//=============================================================================
 
void CCSPlayer::DropWeapons( bool fromDeath, bool friendlyFire )
{
	for ( int i = 0; i < DROPPED_COUNT; ++i )
	{
		m_hDroppedEquipment[i] = NULL;
	}

	CBaseCombatWeapon *pC4 = Weapon_OwnsThisType( "weapon_c4" );
	if ( pC4 )
	{
		CSWeaponDrop( pC4, false, true );
		if( fromDeath )
		{
			if( friendlyFire )
			{
				(static_cast<CC4*> (pC4))->SetDroppedFromDeath(true);
			}
			m_hDroppedEquipment[DROPPED_C4] = static_cast<CBaseEntity *>(pC4);
		}
	}

	//NOTE: Function continues beyond comment block. This is just the part I touched.
 
//=============================================================================
// HPE_END
//=============================================================================


	if( HasDefuser() && !CSGameRules()->IsWarmupPeriod() )
	{
		//Drop an item_defuser
		Vector vForward, vRight;
		AngleVectors( GetAbsAngles(), &vForward, &vRight, NULL );

		CBaseAnimating *pDefuser = (CBaseAnimating *)CBaseEntity::Create( "item_defuser", WorldSpaceCenter(), GetLocalAngles(), this );
		pDefuser->ApplyAbsVelocityImpulse( vForward * 200 + vRight * random->RandomFloat( -50, 50 ) );

		RemoveDefuser();

		//=============================================================================
		// HPE_BEGIN:
		// [menglish] Add the newly created defuser to the dropped equipment list
		//=============================================================================
		 
		if(fromDeath)
		{
			m_hDroppedEquipment[DROPPED_DEFUSE] = static_cast<CBaseEntity *>(pDefuser);
		}
		 
		//=============================================================================
		// HPE_END
		//=============================================================================
	}

	if( HasShield() )
	{
		DropShield();
	}
	else
	{
		//drop the best weapon we have
		if( !DropRifle( true ) )
			DropPistol( true );
	}


	// wills: note - this may seem counter-intuitive below,
	// but the player can only play grenade-related animations 
	// (like throwing) while 'holding' the grenade WEAPON. This means
	// it's possible to still be holding the grenade WEAPON even
	// after the actual grenade itself is flying away, so the
	// player's throw anim can smoothly finish. That's why we need
	// to check if the grenade has emitted a projectile; we don't
	// want to drop a duplicate of the thrown grenade if we're killed
	// AFTER the grenade is in flight but BEFORE the throw anim is over.

	bool bGrenadeDropped = false;

	// drop any live grenades so they explode
	CBaseCSGrenade* pGrenade = dynamic_cast< CBaseCSGrenade* >( GetActiveCSWeapon() );

	if ( pGrenade && pGrenade->m_bHasEmittedProjectile )
		pGrenade = NULL; // the currently active grenade weapon, while active, is NOT eligible to drop because it has thrown a projectile into the world.

	if ( pGrenade )
	{
		if ( pGrenade->IsPinPulled() || pGrenade->IsBeingThrown() )
		{
			// NOTE[pmf]: Molotov is excluded from this list. Consider making this a weapon property
			if (
				pGrenade->ClassMatches( "weapon_hegrenade" ) ||
				pGrenade->ClassMatches( "weapon_flashbang" ) ||
				pGrenade->ClassMatches( "weapon_smokegrenade" ) ||
				pGrenade->ClassMatches( "weapon_decoy" ) )
			{
				pGrenade->DropGrenade();
				pGrenade->DecrementAmmo( this );
				bGrenadeDropped = true;
			}
		}
	}

	if ( !bGrenadeDropped )
	{
		// drop the "best" grenade remaining according to the following priorities
		const char* GrenadePriorities[] =
		{
			"weapon_molotov",	// first slot might get overridden by player last held grenade type below
			"weapon_molotov",
			"weapon_incgrenade",
			"weapon_smokegrenade",
			"weapon_hegrenade",
			"weapon_flashbang",
			"weapon_decoy",
		};

		switch ( m_nPreferredGrenadeDrop )
		{
			case WEAPON_FLASHBANG: GrenadePriorities[0] = "weapon_flashbang"; break;
			case WEAPON_MOLOTOV: GrenadePriorities[0] = "weapon_molotov"; break;
			case WEAPON_INCGRENADE: GrenadePriorities[0] = "weapon_incgrenade"; break;
			case WEAPON_HEGRENADE: GrenadePriorities[0] = "weapon_hegrenade"; break;
			case WEAPON_SMOKEGRENADE: GrenadePriorities[0] = "weapon_smokegrenade"; break;
			case WEAPON_DECOY: GrenadePriorities[0] = "weapon_decoy"; break;
		}
		m_nPreferredGrenadeDrop = 0; // after we drop a preferred grenade make sure we reset the field

		for ( int i = 0; ( i < ARRAYSIZE( GrenadePriorities ) ) && !bGrenadeDropped; ++i )
		{
			pGrenade = dynamic_cast< CBaseCSGrenade* >( Weapon_OwnsThisType( GrenadePriorities[i] ) );
			if ( pGrenade && pGrenade->HasAmmo() && !pGrenade->m_bHasEmittedProjectile )
			{
				bGrenadeDropped = CSWeaponDrop( pGrenade, false );
			}
		}
	}

	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Add whichever, if any, grenade was dropped
	//=============================================================================

	if( pGrenade && bGrenadeDropped )
	{
		m_hDroppedEquipment[DROPPED_GRENADE] = static_cast<CBaseEntity *>( pGrenade );
	}

	//=============================================================================
	// HPE_END
	//=============================================================================

	if ( m_hCarriedHostage != NULL && GetNumFollowers() > 0 )
	{
		CHostage *pHostage = dynamic_cast< CHostage * >(m_hCarriedHostage.Get());
		if ( pHostage )
			pHostage->DropHostage( GetAbsOrigin() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Put the player in the specified team
//-----------------------------------------------------------------------------
void CCSPlayer::ChangeTeam( int iTeamNum )
{
	if ( !GetGlobalTeam( iTeamNum ) )
	{
		Warning( "CCSPlayer::ChangeTeam( %d ) - invalid team index.\n", iTeamNum );
		return;
	}

	int iOldTeam = GetTeamNumber();

	// if this is our current team, just abort
	if ( iTeamNum == iOldTeam )
		return;

	//=============================================================================
	// HPE_BEGIN:	
	//=============================================================================
	 
	// [tj] Added a parameter so we know if it was death that caused the drop
	// Drop Our best weapon
	DropWeapons(false, false);

	// [tj] Clear out dominations
	RemoveNemesisRelationships();
	 
	//=============================================================================
	// HPE_END
	//=============================================================================
	

	// Always allow a change to spectator, and don't count it as one of our team changes.
	// We now store the old team, so if a player changes once to one team, then to spectator,
	// they won't be able to change back to their old old team, but will still be able to join
	// the team they initially changed to.
	if( iTeamNum != TEAM_SPECTATOR )
	{
		m_bTeamChanged = true;
	}
	else
	{
		m_iOldTeam = iOldTeam;
	}

	// do the team change:
	BaseClass::ChangeTeam( iTeamNum );

	//reset class
	m_iClass = (int)CS_CLASS_NONE;
	m_iSkin = 0;

	// update client state

	if ( iTeamNum == TEAM_UNASSIGNED )
	{
		State_Transition( STATE_OBSERVER_MODE );
	}
	else if ( iTeamNum == TEAM_SPECTATOR )
	{
		//=============================================================================
		// HPE_BEGIN:
		// [tj] Removed these lines so players keep their money when switching to spectator.
		//=============================================================================		
		//Reset money
		//m_iAccount = 0;		
		//=============================================================================
		// HPE_END
		//=============================================================================		
		RemoveAllItems( true );

		State_Transition( STATE_OBSERVER_MODE );
	}
	else // active player
	{
		if ( iOldTeam == TEAM_SPECTATOR )
		{
			// If they're switching from being a spectator to ingame player
			//=============================================================================
			// HPE_BEGIN:
			// [tj] Changed this so players either retain their existing money or, 
			//		if they have less than the default, give them the default.
			//=============================================================================
			int startMoney = CSGameRules()->GetStartMoney();
			if (startMoney > m_iAccount)
			{
				m_iAccount = startMoney;
			} 			
			//=============================================================================
			// HPE_END
			//============================================================================= 
		}

		// bots get to this state on TEAM_UNASSIGNED, yet they are marked alive.  Don't kill them.
		else if ( iOldTeam != TEAM_UNASSIGNED  && !IsDead() )
		{
			// Kill player if switching teams while alive
			CommitSuicide();
		}

		// Put up the class selection menu.
		State_Transition( STATE_PICKINGCLASS );
	}

	// Initialize the player counts now that a player has switched teams
	int NumDeadCT, NumDeadTerrorist, NumAliveTerrorist, NumAliveCT;
	CSGameRules()->InitializePlayerCounts( NumAliveTerrorist, NumAliveCT, NumDeadTerrorist, NumDeadCT );
}

//-----------------------------------------------------------------------------
// Purpose: Put the player in the specified team without penalty
//-----------------------------------------------------------------------------
void CCSPlayer::SwitchTeam( int iTeamNum )
{
	if ( !GetGlobalTeam( iTeamNum ) || (iTeamNum != TEAM_CT && iTeamNum != TEAM_TERRORIST) )
	{
		Warning( "CCSPlayer::SwitchTeam( %d ) - invalid team index.\n", iTeamNum );
		return;
	}

	int iOldTeam = GetTeamNumber();

	// if this is our current team, just abort
	if ( iTeamNum == iOldTeam )
		return;

	// Always allow a change to spectator, and don't count it as one of our team changes.
	// We now store the old team, so if a player changes once to one team, then to spectator,
	// they won't be able to change back to their old old team, but will still be able to join
	// the team they initially changed to.
	m_bTeamChanged = true;

	// do the team change:
	BaseClass::ChangeTeam( iTeamNum );

	if( HasDefuser() )
	{
		RemoveDefuser();
	}

	//reset class
	switch ( m_iClass )
	{
	// Terrorist -> CT
	case CS_CLASS_PHOENIX_CONNNECTION:
		m_iClass = (int)CS_CLASS_SEAL_TEAM_6;
		break;
	case CS_CLASS_L337_KREW:
		m_iClass = (int)CS_CLASS_GSG_9;
		break;
	case CS_CLASS_SEPARATIST:
		m_iClass = (int)CS_CLASS_SAS;
		break;
	case CS_CLASS_BALKAN:
		m_iClass = (int)CS_CLASS_GIGN;
		break;
	case CS_CLASS_PROFESSIONAL:
		m_iClass = (int)CS_CLASS_FBI;
		break;
	case CS_CLASS_ANARCHIST:
		m_iClass = (int)CS_CLASS_IDF;
		break;
	case CS_CLASS_PIRATE:
		m_iClass = (int)CS_CLASS_SWAT;
		break;

	// CT -> Terrorist
	case CS_CLASS_SEAL_TEAM_6:
		m_iClass = (int)CS_CLASS_PHOENIX_CONNNECTION;
		break;
	case CS_CLASS_GSG_9:
		m_iClass = (int)CS_CLASS_L337_KREW;
		break;
	case CS_CLASS_SAS:
		m_iClass = (int)CS_CLASS_SEPARATIST;
		break;
	case CS_CLASS_GIGN:
		m_iClass = (int)CS_CLASS_BALKAN;
		break;
	case CS_CLASS_FBI:
		m_iClass = (int)CS_CLASS_PROFESSIONAL;
		break;
	case CS_CLASS_IDF:
		m_iClass = (int)CS_CLASS_ANARCHIST;
		break;
	case CS_CLASS_SWAT:
		m_iClass = (int)CS_CLASS_PIRATE;
		break;

	case CS_CLASS_NONE:
	default:
		break;
	}

	// Initialize the player counts now that a player has switched teams
	int NumDeadCT, NumDeadTerrorist, NumAliveTerrorist, NumAliveCT;
	CSGameRules()->InitializePlayerCounts( NumAliveTerrorist, NumAliveCT, NumDeadTerrorist, NumDeadCT );
}

void CCSPlayer::ModifyOrAppendPlayerCriteria( AI_CriteriaSet& set )
{
	// this is for giving player info to the hostage response system
	// and is as yet unused.
	// Eventually we could give the hostage a few tidbits about this player,
	// eg their health, what weapons they have, and the hostage could
	// comment accordingly.

	//do not append any player data to the Criteria!

	//we don't know which player we should be caring about
}

static unsigned int s_BulletGroupCounter = 0;

void CCSPlayer::StartNewBulletGroup()
{
	s_BulletGroupCounter++;
}

CDamageRecord::CDamageRecord( CCSPlayer * pPlayerDamager, CCSPlayer * pPlayerRecipient, int iDamage, int iCounter, int iActualHealthRemoved )
{
	if ( pPlayerDamager )
	{
		m_PlayerDamager = pPlayerDamager;
		m_PlayerDamagerControlledBot = pPlayerDamager->IsControllingBot() ? pPlayerDamager->GetControlledBot() : NULL;
		Q_strncpy( m_szPlayerDamagerName, pPlayerDamager->GetPlayerName(), sizeof(m_szPlayerDamagerName) );
	}
	else
	{
		Q_strncpy( m_szPlayerDamagerName, "World", sizeof(m_szPlayerDamagerName) );
	}
	
	if ( pPlayerRecipient )
	{
		m_PlayerRecipient = pPlayerRecipient;
		m_PlayerRecipientControlledBot = pPlayerRecipient->IsControllingBot() ? pPlayerRecipient->GetControlledBot() : NULL;
		Q_strncpy( m_szPlayerRecipientName, pPlayerRecipient->GetPlayerName(), sizeof(m_szPlayerRecipientName) );
	}
	else
	{
		Q_strncpy( m_szPlayerRecipientName, "World", sizeof(m_szPlayerRecipientName) );
	}

	m_iDamage = iDamage;
	m_iActualHealthRemoved = iActualHealthRemoved;
	m_iNumHits = 1;
	m_iLastBulletUpdate = iCounter;
}

bool CDamageRecord::IsDamageRecordStillValidForDamagerAndRecipient( CCSPlayer * pPlayerDamager, CCSPlayer * pPlayerRecipient )
{
	if ( ( pPlayerDamager   != m_PlayerDamager )   || 
		 ( pPlayerRecipient != m_PlayerRecipient ) ||
		 ( pPlayerDamager != NULL   && pPlayerDamager->IsControllingBot()   && m_PlayerDamagerControlledBot   != pPlayerDamager->GetControlledBot() ) ||
		 ( pPlayerRecipient != NULL && pPlayerRecipient->IsControllingBot() && m_PlayerRecipientControlledBot != pPlayerRecipient->GetControlledBot() )  )
	{
		return false;
	}

	return true;
}

//=======================================================
// Remember this amount of damage that we dealt for stats
//=======================================================
void CCSPlayer::RecordDamage( CCSPlayer* damageDealer, CCSPlayer* damageTaker, int iDamageDealt, int iActualHealthRemoved )
{
	FOR_EACH_LL( m_DamageList, i )
	{

		if ( m_DamageList[i]->IsDamageRecordStillValidForDamagerAndRecipient( damageDealer, damageTaker ) )
		{
			m_DamageList[i]->AddDamage( iDamageDealt, s_BulletGroupCounter, iActualHealthRemoved );
			return;
		}
	}

	CDamageRecord *record = new CDamageRecord( damageDealer, damageTaker, iDamageDealt, s_BulletGroupCounter, iActualHealthRemoved );
	int k = m_DamageList.AddToTail();
	m_DamageList[k] = record;
}

//=======================================================
// Reset our damage given and taken counters
//=======================================================
void CCSPlayer::ResetDamageCounters()
{
	m_DamageList.PurgeAndDeleteElements();
}

void CCSPlayer::RemoveSelfFromOthersDamageCounters()
{
	// Now clear out any reference of this player in other players' damage lists.
	CUtlVector< CCSPlayer * > playerVector;
	CollectPlayers( &playerVector );

	FOR_EACH_VEC( playerVector, i )
	{
		CCSPlayer *player = playerVector[ i ];

		if ( playerVector[ i ] == this )
			continue;

		FOR_EACH_LL( player->m_DamageList, j )
		{
			if ( player->m_DamageList[j]->GetPlayerDamagerPtr() == this || player->m_DamageList[j]->GetPlayerRecipientPtr() == this )
			{
				delete player->m_DamageList[ j ];
				player->m_DamageList.Remove( j );
				break;
			}
		}
	}
}

//=======================================================
// Output the damage that we dealt to other players
//=======================================================
void CCSPlayer::OutputDamageTaken( void )
{
	bool bPrintHeader = true;
	CDamageRecord *pRecord;
	char buf[64];
	int msg_dest = HUD_PRINTCONSOLE;

	FOR_EACH_LL( m_DamageList, i )
	{
		if( bPrintHeader )
		{
			ClientPrint( this, msg_dest, "Player: %s1 - Damage Taken\n", GetPlayerName() );
			ClientPrint( this, msg_dest, "-------------------------\n" );
			bPrintHeader = false;
		}
		pRecord = m_DamageList[i];

		if( pRecord && pRecord->GetPlayerRecipientPtr() == this )
		{
			if (pRecord->GetNumHits() == 1 )
			{
				Q_snprintf( buf, sizeof(buf ), "%d in %d hit", pRecord->GetDamage(), pRecord->GetNumHits() );
			}
			else
			{
				Q_snprintf( buf, sizeof(buf ), "%d in %d hits", pRecord->GetDamage(), pRecord->GetNumHits() );
			}
			ClientPrint( this, msg_dest, "Damage Taken from \"%s1\" - %s2\n", pRecord->GetPlayerDamagerName(), buf );
		}		
	}
}

//=======================================================
// Output the damage that we took from other players
//=======================================================
void CCSPlayer::OutputDamageGiven( void )
{
	bool bPrintHeader = true;
	CDamageRecord *pRecord;
	char buf[64];
	int msg_dest = HUD_PRINTCONSOLE;

	FOR_EACH_LL( m_DamageList, i )
	{
		if( bPrintHeader )
		{
			ClientPrint( this, msg_dest, "Player: %s1 - Damage Given\n", GetPlayerName() );
			ClientPrint( this, msg_dest, "-------------------------\n" );
			bPrintHeader = false;
		}

		pRecord = m_DamageList[i];

		if( pRecord && pRecord->GetPlayerDamagerPtr() == this )
		{	
			if (pRecord->GetNumHits() == 1 )
			{
				Q_snprintf( buf, sizeof(buf ), "%d in %d hit", pRecord->GetDamage(), pRecord->GetNumHits() );
			}
			else
			{
				Q_snprintf( buf, sizeof(buf ), "%d in %d hits", pRecord->GetDamage(), pRecord->GetNumHits() );
			}
			ClientPrint( this, msg_dest, "Damage Given to \"%s1\" - %s2\n", pRecord->GetPlayerRecipientName(), buf );
		}		
	}
}

void CCSPlayer::CreateViewModel( int index /*=0*/ )
{
	Assert( index >= 0 && index < MAX_VIEWMODELS );

	if ( GetViewModel( index ) )
		return;

	CPredictedViewModel *vm = ( CPredictedViewModel * )CreateEntityByName( "predicted_viewmodel" );
	if ( vm )
	{
		vm->SetAbsOrigin( GetAbsOrigin() );
		vm->SetOwner( this );
		vm->SetIndex( index );
		DispatchSpawn( vm );
		vm->FollowEntity( this, false );
		m_hViewModel.Set( index, vm );
	}
}

bool CCSPlayer::HasC4() const
{
	return ( Weapon_OwnsThisType( "weapon_c4" ) != NULL );
}

int CCSPlayer::GetNextObserverSearchStartPoint( bool bReverse )
{
#if CS_CONTROLLABLE_BOTS_ENABLED
	// Brock H. - TR - 05/05/09
	// If the server is set up to allow controllable bots, 
	// and if we don't already have a target, 
	// then start with the nearest controllable bot.
	if ( cv_bot_controllable.GetBool() )
	{
		if ( !IsValidObserverTarget( m_hObserverTarget.Get() ) )
		{
			if ( CCSBot *pBot = FindNearestControllableBot( true ) )
			{
				return pBot->entindex();
			}
		}
	}
#endif

	// If we are currently watching someone who is dead, they must have died while we were watching (since
	// a dead guy is not a valid pick to start watching).  He was given his killer as an observer target
	// when he died, so let's start by trying to observe his killer.  If we fail, we'll use the normal way.
	// And this is just the start point anyway, but we want to start the search here in case it is okay.
	if( m_hObserverTarget && !m_hObserverTarget->IsAlive() )
	{
		CCSPlayer *targetPlayer = ToCSPlayer(m_hObserverTarget);
		if( targetPlayer && targetPlayer->GetObserverTarget() )
			return targetPlayer->GetObserverTarget()->entindex();
	}

	return BaseClass::GetNextObserverSearchStartPoint( bReverse );
}

void CCSPlayer::PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force )
{
	BaseClass::PlayStepSound( vecOrigin, psurface, fvol, force );

	if ( !sv_footsteps.GetFloat() )
		return;

	if ( !psurface )
		return;

	IGameEvent * event = gameeventmanager->CreateEvent( "player_footstep" );
	if ( event )
	{
		event->SetInt("userid", GetUserID() );
		gameeventmanager->FireEvent( event );
	}

	m_bMadeFootstepNoise = true;
}


void CCSPlayer::SelectDeathPose( const CTakeDamageInfo &info )
{
	MDLCACHE_CRITICAL_SECTION();
	if ( !GetModelPtr() )
		return;

	Activity aActivity = ACT_INVALID;
	int iDeathFrame = 0;

	if ( m_bUseNewAnimstate && m_PlayerAnimStateCSGO )
	{
		float flDeathYaw = 0;
		m_PlayerAnimStateCSGO->SelectDeathPose( info, m_LastHitGroup, aActivity, flDeathYaw );
		SetDeathPoseYaw( flDeathYaw );
	}
	else
	{
		SelectDeathPoseActivityAndFrame( this, info, m_LastHitGroup, aActivity, iDeathFrame );
	}

	if ( aActivity == ACT_INVALID )
	{
		SetDeathPose( ACT_INVALID );
		SetDeathPoseFrame( 0 );
		return;
	}

	SetDeathPose( SelectWeightedSequence( aActivity ) );
	SetDeathPoseFrame( iDeathFrame );
}


void CCSPlayer::HandleAnimEvent( animevent_t *pEvent )
{
	if ( pEvent->event == 4001 || pEvent->event == 4002 )
	{
		// Ignore these for now - soon we will be playing footstep sounds based on these events
		// that mark footfalls in the anims.
	}
	else if ( pEvent->event == AE_WPN_UNHIDE )
	{
		CWeaponCSBase *pWeapon = GetActiveCSWeapon();
		if ( pWeapon && pWeapon->GetWeaponWorldModel() )
		{
			pWeapon->ShowWeaponWorldModel( true );
		}
	}
	else if ( pEvent->event == AE_CL_EJECT_MAG || pEvent->event == AE_CL_EJECT_MAG_UNHIDE )
	{
		CAnimationLayer *pWeaponLayer = GetAnimOverlay( ANIMATION_LAYER_WEAPON_ACTION );
		if ( pWeaponLayer && pWeaponLayer->m_nDispatchedDst != ACT_INVALID )
		{
			// If the weapon is running a dispatched animation, we can eat these events from the player.
			// The weapon itself assumes the responsibility for these events when dispatched.
		}
		else
		{
			CWeaponCSBase *pWeapon = GetActiveCSWeapon();
			if ( pWeapon && pWeapon->GetWeaponWorldModel() )
			{
				pWeapon->GetWeaponWorldModel()->HandleAnimEvent( pEvent );
			}
		}
	}
	else
	{
		BaseClass::HandleAnimEvent( pEvent );
	}
}


bool CCSPlayer::CanChangeName( void )
{
	if ( IsBot() )
		return true;

	// enforce the minimum interval
	if ( (m_flNameChangeHistory[0] + MIN_NAME_CHANGE_INTERVAL) >= gpGlobals->curtime )
	{
		return false;
	}

	// enforce that we dont do more than NAME_CHANGE_HISTORY_SIZE
	// changes within NAME_CHANGE_HISTORY_INTERVAL
	if ( (m_flNameChangeHistory[NAME_CHANGE_HISTORY_SIZE-1] + NAME_CHANGE_HISTORY_INTERVAL) >= gpGlobals->curtime )
	{
		return false;
	}

	return true;
}

void CCSPlayer::ChangeName( const char *pszNewName )
{
	// make sure name is not too long
	char trimmedName[MAX_PLAYER_NAME_LENGTH];
	Q_strncpy( trimmedName, pszNewName, sizeof( trimmedName ) );

	const char *pszOldName = GetPlayerName();

	// send colored message to everyone
	CReliableBroadcastRecipientFilter filter;
	UTIL_SayText2Filter( filter, this, false, "#Cstrike_Name_Change", pszOldName, trimmedName );

	// broadcast event
	IGameEvent * event = gameeventmanager->CreateEvent( "player_changename" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetString( "oldname", pszOldName );
		event->SetString( "newname", trimmedName );
		gameeventmanager->FireEvent( event );
	}

	// change shared player name
	SetPlayerName( trimmedName );

	// tell engine to use new name
	engine->ClientCommand( edict(), "name \"%s\"", trimmedName );

	// remember time of name change
	for ( int i=NAME_CHANGE_HISTORY_SIZE-1; i>0; i-- )
	{
		m_flNameChangeHistory[i] = m_flNameChangeHistory[i-1];
	}

	m_flNameChangeHistory[0] = gpGlobals->curtime; // last change
}

bool CCSPlayer::StartReplayMode( float fDelay, float fDuration, int iEntity )
{
	if ( !BaseClass::StartReplayMode( fDelay, fDuration, iEntity ) )
		return false;

	CSingleUserRecipientFilter filter( this );
	filter.MakeReliable();

	UserMessageBegin( filter, "KillCam" );
		WRITE_BYTE( OBS_MODE_IN_EYE );

		if ( m_hObserverTarget.Get() )
		{
			WRITE_BYTE( m_hObserverTarget.Get()->entindex() );	// first target
			WRITE_BYTE( entindex() );	//second target
		}
		else
		{
			WRITE_BYTE( entindex() );	// first target
			WRITE_BYTE( 0 );	//second target
		}
	MessageEnd();

	ClientPrint( this, HUD_PRINTCENTER, "Kill Cam Replay" );

	return true;
}

void CCSPlayer::StopReplayMode()
{
	BaseClass::StopReplayMode();

	CSingleUserRecipientFilter filter( this );
	filter.MakeReliable();

	UserMessageBegin( filter, "KillCam" );
		WRITE_BYTE( OBS_MODE_NONE );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
	MessageEnd();
}

void CCSPlayer::PlayUseDenySound()
{
	// Don't do a sound here because it can mute your footsteps giving you an advantage.
	// The CS:S content for this sound is silent anyways.
	//EmitSound( "Player.UseDeny" );
}

//=============================================================================
// HPE_BEGIN:
//=============================================================================

// [menglish, tj] This is where we reset all the per-round information for achievements for this player
void CCSPlayer::ResetRoundBasedAchievementVariables()
{
	m_KillingSpreeStartTime = -1;

	int numCTPlayers = 0, numTPlayers = 0;
	for (int i = 0; i < g_Teams.Count(); i++ )
	{
		if(g_Teams[i])
		{
			if ( g_Teams[i]->GetTeamNumber() == TEAM_CT )
				numCTPlayers = g_Teams[i]->GetNumPlayers();
			else if(g_Teams[i]->GetTeamNumber() == TEAM_TERRORIST)
				numTPlayers = g_Teams[i]->GetNumPlayers();
		}
	}
	m_NumEnemiesKilledThisRound = 0;
	if(GetTeamNumber() == TEAM_CT)
		m_NumEnemiesAtRoundStart = numTPlayers;
	else if(GetTeamNumber() == TEAM_TERRORIST)
		m_NumEnemiesAtRoundStart = numCTPlayers;


	//Clear the previous owner field for currently held weapons
	CWeaponCSBase* pWeapon = dynamic_cast< CWeaponCSBase * >(Weapon_GetSlot( WEAPON_SLOT_RIFLE ));
	if ( pWeapon )
	{
		pWeapon->SetPreviousOwner(NULL);
	}
	pWeapon = dynamic_cast< CWeaponCSBase * >(Weapon_GetSlot( WEAPON_SLOT_PISTOL));
	if ( pWeapon )
	{
		pWeapon->SetPreviousOwner(NULL);
	}

	//Clear list of weapons used to get kills
	m_killWeapons.RemoveAll();

	//Clear sliding window of kill times
	m_killTimes.RemoveAll();

	//clear round kills
	m_enemyPlayersKilledThisRound.RemoveAll();

	m_killsWhileBlind = 0;

	m_bSurvivedHeadshotDueToHelmet = false;

	m_gooseChaseStep = GC_NONE;
	m_defuseDefenseStep = DD_NONE;
	m_pGooseChaseDistractingPlayer = NULL;

	m_bMadeFootstepNoise = false;

	m_bombPickupTime = -1;

	m_bMadePurchseThisRound = false;

	m_bKilledDefuser = false;
	m_bKilledRescuer = false;
	m_maxGrenadeKills = 0;
	m_grenadeDamageTakenThisRound = 0;

	//=============================================================================
	// HPE_BEGIN:
	// [dwenger] Needed for fun-fact implementation
	//=============================================================================

	WieldingKnifeAndKilledByGun(false);

	m_WeaponTypesUsed.RemoveAll();

	m_bPickedUpDefuser = false;
	m_bDefusedWithPickedUpKit = false;

	//=============================================================================
	// HPE_END
	//=============================================================================
}


/**
 *	static public CCSPlayer::GetCSWeaponIDCausingDamage()
 *
 *		Helper function to get the ID of the weapon used to kill a player.
 *		This is slightly non-trivial because the grenade because a separate
 *		entity when thrown.
 *
 *  Parameters:
 * 		info -
 *			
 *	Returns:
 *		int -
 */
CSWeaponID CCSPlayer::GetWeaponIdCausingDamange( const CTakeDamageInfo &info )
{
	CBaseEntity *pInflictor = info.GetInflictor();
	CCSPlayer *pAttacker = ToCSPlayer(info.GetAttacker());
	if (pAttacker == pInflictor)
	{
		CWeaponCSBase* pAttackerWeapon = dynamic_cast< CWeaponCSBase * >(pAttacker->GetActiveWeapon());
		if (!pAttackerWeapon)
			return WEAPON_NONE;

		return pAttackerWeapon->GetCSWeaponID();
	}
	else if (pInflictor && V_strcmp(pInflictor->GetClassname(), "hegrenade_projectile") == 0)
	{
		return WEAPON_HEGRENADE;
	}
	return WEAPON_NONE;
}


//=============================================================================
// HPE_BEGIN:
// [dwenger] adding tracking for weapon used fun fact
//=============================================================================
void CCSPlayer::PlayerUsedFirearm( CBaseCombatWeapon* pBaseWeapon )
{
	if ( pBaseWeapon )
	{
		CWeaponCSBase* pWeapon = dynamic_cast< CWeaponCSBase* >( pBaseWeapon );

		if ( pWeapon )
		{
			CSWeaponType weaponType = pWeapon->GetCSWpnData().m_WeaponType;
			CSWeaponID weaponID = pWeapon->GetCSWeaponID();

			if ( weaponType != WEAPONTYPE_KNIFE && weaponType != WEAPONTYPE_C4 && weaponType != WEAPONTYPE_GRENADE )
			{
				if ( m_WeaponTypesUsed.Find( weaponID ) == -1 )
				{
					// Add this weapon to the list of weapons used by the player
					m_WeaponTypesUsed.AddToTail( weaponID );
				}
			}
		}
	}
}


/**
 *	public CCSPlayer::ProcessPlayerDeathAchievements()
 *
 *		Do Achievement processing whenever a player is killed
 *
 *  Parameters:
 * 		pAttacker -
 * 		pVictim -
 * 		info -
 */
void CCSPlayer::ProcessPlayerDeathAchievements( CCSPlayer *pAttacker, CCSPlayer *pVictim, const CTakeDamageInfo &info )
{
	Assert(pVictim != NULL);
	CBaseEntity *pInflictor = info.GetInflictor();	
  
	// all these achievements require a valid attacker on a different team
	if ( pAttacker != NULL && pVictim != NULL && pVictim->GetTeamNumber() != pAttacker->GetTeamNumber() )
	{
		// get the weapon used - some of the achievements will need this data
		CWeaponCSBase* pAttackerWeapon = dynamic_cast< CWeaponCSBase * >(pAttacker->GetActiveWeapon());

		//=============================================================================
		// HPE_BEGIN:
		// [dwenger] Fun-fact processing
		//=============================================================================

		CWeaponCSBase* pVictimWeapon = dynamic_cast< CWeaponCSBase* >(pVictim->GetActiveWeapon());

		//=============================================================================
		// HPE_END
		//=============================================================================

		CSWeaponID attackerWeaponId = GetWeaponIdCausingDamange(info);

		if (pVictim->m_bIsDefusing)
		{
			pAttacker->AwardAchievement(CSKilledDefuser);			
			pAttacker->m_bKilledDefuser = true;

			if (attackerWeaponId == WEAPON_HEGRENADE)
			{
				pAttacker->AwardAchievement(CSKilledDefuserWithGrenade);
			}
		}

		// [pfreese] Achievement check for attacker killing player while reloading
		if (pVictim->IsReloading())
		{
			pAttacker->AwardAchievement(CSKillEnemyReloading);
		}

		if (pVictim->IsRescuing())
		{
			// Ensure the killer did not injure any hostages
			if ( !pAttacker->InjuredAHostage() && pVictim->GetNumFollowers() == g_Hostages.Count() )
			{
				pAttacker->AwardAchievement(CSKilledRescuer);
				pAttacker->m_bKilledRescuer = true;
			}
		}

		// [menglish] Achievement check for doing 95% or more damage to a player and having another player kill them
		FOR_EACH_LL( pVictim->m_DamageList, i )
		{
			if ( pVictim->m_DamageList[i]->IsDamageRecordValidPlayerToPlayer() && 
				 pVictim->m_DamageList[i]->GetPlayerRecipientPtr() == pVictim && 
				 pVictim->m_DamageList[i]->GetPlayerDamagerPtr() != pAttacker &&
				 (pVictim->m_DamageList[i]->GetDamage() >= pVictim->GetMaxHealth() - AchievementConsts::DamageNoKill_MaxHealthLeftOnKill ) &&
				 pVictim->IsOtherEnemy( pVictim->m_DamageList[i]->GetPlayerDamagerPtr() ) &&
				 !pVictim->m_DamageList[i]->GetPlayerDamagerPtr()->HasControlledBotThisRound() &&
				 !pVictim->m_DamageList[i]->GetPlayerDamagerPtr()->HasBeenControlledThisRound() )
			{
				pVictim->m_DamageList[i]->GetPlayerDamagerPtr()->AwardAchievement( CSDamageNoKill );
			}
		}

		pAttacker->m_NumEnemiesKilledThisRound++;

		//store a list of kill times for spree tracking
		pAttacker->m_killTimes.AddToTail(gpGlobals->curtime);

		//Add the victim to the list of players killed this round
		pAttacker->m_enemyPlayersKilledThisRound.AddToTail(pVictim);

		//Calculate Avenging for all players the victim has killed
		for ( int avengedIndex = 0; avengedIndex < pVictim->m_enemyPlayersKilledThisRound.Count(); avengedIndex++ )        
		{
			CCSPlayer* avengedPlayer = pVictim->m_enemyPlayersKilledThisRound[avengedIndex];

			if (avengedPlayer)
			{
				//Make sure you are avenging someone on your own team (This is the expected flow. Just here to avoid edge cases like team-switching).
				if (pAttacker->GetTeamNumber() == avengedPlayer->GetTeamNumber())
				{
					CCS_GameStats.Event_PlayerAvengedTeammate(pAttacker, pVictim->m_enemyPlayersKilledThisRound[avengedIndex]);
				}
			}
		}



		//remove elements older than a certain time
		while (pAttacker->m_killTimes.Count() > 0 && pAttacker->m_killTimes[0] + AchievementConsts::KillingSpree_WindowTime < gpGlobals->curtime)
		{
			pAttacker->m_killTimes.Remove(0);
		}

		//If we killed enough players in the time window, award the achievement
		if (pAttacker->m_killTimes.Count() >= AchievementConsts::KillingSpree_Kills)
		{
			pAttacker->m_KillingSpreeStartTime = gpGlobals->curtime;
			pAttacker->AwardAchievement(CSKillingSpree);
		}

		// Did the attacker just kill someone on a killing spree?
		if (pVictim->m_KillingSpreeStartTime >= 0 && pVictim->m_KillingSpreeStartTime - gpGlobals->curtime <= AchievementConsts::KillingSpreeEnder_TimeWindow)
		{
			pAttacker->AwardAchievement(CSKillingSpreeEnder);
		}

		//Check the "killed someone with their own weapon" achievement
		if (pAttackerWeapon && pAttackerWeapon->GetPreviousOwner() == pVictim)
		{
			pAttacker->AwardAchievement(CSKillEnemyWithFormerGun);
		}

		//If this player has killed the entire team award him the achievement
		if (pAttacker->m_NumEnemiesKilledThisRound == pAttacker->m_NumEnemiesAtRoundStart && pAttacker->m_NumEnemiesKilledThisRound >= AchievementConsts::KillEnemyTeam_MinKills)
		{
			pAttacker->AwardAchievement(CSKillEnemyTeam);
		}

		//If this is a posthumous kill award the achievement
		if (!pAttacker->IsAlive() && attackerWeaponId == WEAPON_HEGRENADE)
		{
			CCS_GameStats.IncrementStat(pAttacker, CSSTAT_GRENADE_POSTHUMOUSKILLS, 1);
			ToCSPlayer(pAttacker)->AwardAchievement(CSPosthumousGrenadeKill);
		}

		if (pAttacker->GetActiveWeapon() && pAttacker->GetActiveWeapon()->Clip1() == 0 && pAttackerWeapon && pAttackerWeapon->GetCSWpnData().m_WeaponType != WEAPONTYPE_SNIPER_RIFLE && attackerWeaponId != WEAPON_TASER )
		{
			if (pInflictor == pAttacker)
			{
				pAttacker->AwardAchievement(CSKillEnemyLastBullet);
				CCS_GameStats.IncrementStat(pAttacker, CSSTAT_KILLS_WITH_LAST_ROUND, 1);
			}
		}

		//=============================================================================
		// HPE_BEGIN:
		// [dwenger] Fun-fact processing
		//=============================================================================

		if (pVictimWeapon && pVictimWeapon->GetCSWpnData().m_WeaponType == WEAPONTYPE_KNIFE && pAttackerWeapon &&
			pAttackerWeapon->GetCSWpnData().m_WeaponType != WEAPONTYPE_KNIFE && pAttackerWeapon->GetCSWpnData().m_WeaponType != WEAPONTYPE_C4 && pAttackerWeapon->GetCSWpnData().m_WeaponType != WEAPONTYPE_GRENADE)
		{
			// Victim was wielding knife when killed by a gun
			pVictim->WieldingKnifeAndKilledByGun(true);
		}

		//=============================================================================
		// HPE_END
		//=============================================================================

		//see if this is a unique weapon		
		if (attackerWeaponId != WEAPON_NONE)
		{
			if (pAttacker->m_killWeapons.Find(attackerWeaponId) == -1)
			{
				pAttacker->m_killWeapons.AddToTail(attackerWeaponId);
				if (pAttacker->m_killWeapons.Count() >= AchievementConsts::KillsWithMultipleGuns_MinWeapons)
				{
					pAttacker->AwardAchievement(CSKillsWithMultipleGuns);					
				}
			}
		}

		//Check for kills while blind
		if (pAttacker->IsBlindForAchievement())
		{
			//if this is from a different blinding, restart the kill counter and set the time
			if (pAttacker->m_blindStartTime != pAttacker->m_firstKillBlindStartTime)
			{
				pAttacker->m_killsWhileBlind = 0;
				pAttacker->m_firstKillBlindStartTime = pAttacker->m_blindStartTime;
			}

			++pAttacker->m_killsWhileBlind;
			if (pAttacker->m_killsWhileBlind >= AchievementConsts::KillEnemiesWhileBlind_Kills)
			{
				pAttacker->AwardAchievement(CSKillEnemiesWhileBlind);
			}

			if (pAttacker->m_killsWhileBlind >= AchievementConsts::KillEnemiesWhileBlindHard_Kills)
			{
				pAttacker->AwardAchievement(CSKillEnemiesWhileBlindHard);
			}
		}

		//Check sniper killing achievements
		bool victimZoomed = ( pVictim->GetFOV() != pVictim->GetDefaultFOV() );
		bool attackerZoomed = ( pAttacker->GetFOV() != pAttacker->GetDefaultFOV() );
		bool attackerUsedSniperRifle = pAttackerWeapon && pAttackerWeapon->GetCSWpnData().m_WeaponType == WEAPONTYPE_SNIPER_RIFLE && pInflictor == pAttacker;
		if (victimZoomed && attackerUsedSniperRifle)
		{
			pAttacker->AwardAchievement(CSKillSniperWithSniper);
		}

		if ( CSLoadout()->IsKnife( attackerWeaponId ) && victimZoomed)
		{
			pAttacker->AwardAchievement(CSKillSniperWithKnife);
		}
		if (attackerUsedSniperRifle && !attackerZoomed)
		{
			pAttacker->AwardAchievement(CSHipShot);
		}

		//Kill a player at low health
		if (pAttacker->IsAlive() && pAttacker->GetHealth() <= AchievementConsts::KillWhenAtLowHealth_MaxHealth)
		{
			pAttacker->AwardAchievement(CSKillWhenAtLowHealth);
		}

		//Kill a player with a knife during the pistol round
		if (CSGameRules()->IsPistolRound())
		{
			if ( CSLoadout()->IsKnife( attackerWeaponId ) )
			{
				pAttacker->AwardAchievement(CSPistolRoundKnifeKill);
			}
		}

		//[tj] Check for dual elites fight
		CWeaponCSBase* victimWeapon = pVictim->GetActiveCSWeapon();

		if (victimWeapon)
		{
			CSWeaponID victimWeaponID = victimWeapon->GetCSWeaponID();

			if (attackerWeaponId == WEAPON_ELITE && victimWeaponID == WEAPON_ELITE)
			{
				pAttacker->AwardAchievement(CSWinDualDuel);
			}
		}

		//[tj] See if the attacker or defender are in the air [sbodenbender] dont include ladders
		bool attackerInAir = pAttacker->GetMoveType() != MOVETYPE_LADDER && pAttacker->GetNearestSurfaceBelow(AchievementConsts::KillInAir_MinimumHeight) == NULL;
		bool victimInAir = pVictim->GetMoveType() != MOVETYPE_LADDER && pVictim->GetNearestSurfaceBelow(AchievementConsts::KillInAir_MinimumHeight) == NULL;

		if (attackerInAir)
		{
			pAttacker->AwardAchievement(CSKillWhileInAir);
		}
		if (victimInAir)
		{
			pAttacker->AwardAchievement(CSKillEnemyInAir);
		}
		if (attackerInAir && victimInAir)
		{
			pAttacker->AwardAchievement(CSKillerAndEnemyInAir);
		}

		//[tj] advance to the next stage of the defuse defense achievement
		if (pAttacker->m_defuseDefenseStep == DD_STARTED_DEFUSE)
		{
			pAttacker->m_defuseDefenseStep = DD_KILLED_TERRORIST;            
		}

		if (pVictim->HasC4() && pVictim->GetBombPickuptime() + AchievementConsts::KillBombPickup_MaxTime > gpGlobals->curtime)
		{
			pAttacker->AwardAchievement(CSKillBombPickup);
		}
		
	}


	//If you kill a friendly player while blind (from an enemy player), give the guy that blinded you an achievement    
	if ( pAttacker != NULL && pVictim != NULL && pVictim->GetTeamNumber() == pAttacker->GetTeamNumber() && pAttacker->IsBlind())
	{
		CCSPlayer* flashbangAttacker = pAttacker->GetLastFlashbangAttacker();
		if (flashbangAttacker && pAttacker->GetTeamNumber() != flashbangAttacker->GetTeamNumber())
		{
			flashbangAttacker->AwardAchievement(CSCauseFriendlyFireWithFlashbang);
		}
	}

	// do a scan to determine count of players still alive
	int livePlayerCount = 0;
	int teamCount[TEAM_MAXCOUNT];
	int teamIgnoreCount[TEAM_MAXCOUNT];
	memset(teamCount, 0, sizeof(teamCount));
	memset(teamIgnoreCount, 0, sizeof(teamIgnoreCount));
	CCSPlayer *pAlivePlayer = NULL;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CCSPlayer* pPlayer = (CCSPlayer*)UTIL_PlayerByIndex( i );
		if (pPlayer)
		{
			int teamNum = pPlayer->GetTeamNumber();
			if ( teamNum >= 0 )
			{
				++teamCount[teamNum];
				if (pPlayer->WasNotKilledNaturally())
				{
					teamIgnoreCount[teamNum]++;
				}
			}
			if (pPlayer->IsAlive() && pPlayer != pVictim)
			{
				++livePlayerCount;
				pAlivePlayer = pPlayer;
			}
		}
	}

	// Achievement check for being the last player alive in a match
	if (pAlivePlayer)
	{		
		int alivePlayerTeam = pAlivePlayer->GetTeamNumber();
		int alivePlayerOpposingTeam = alivePlayerTeam == TEAM_CT ? TEAM_TERRORIST : TEAM_CT;
		if (livePlayerCount == 1 
			&& CSGameRules()->m_iRoundWinStatus == WINNER_NONE
			&& teamCount[alivePlayerTeam] - teamIgnoreCount[alivePlayerTeam] >= AchievementConsts::LastPlayerAlive_MinPlayersOnTeam
			&& teamCount[alivePlayerOpposingTeam] - teamIgnoreCount[alivePlayerOpposingTeam] >= AchievementConsts::DefaultMinOpponentsForAchievement
			&& ( !(pAlivePlayer->m_iDisplayHistoryBits & DHF_FRIEND_KILLED) ))
		{
			pAlivePlayer->AwardAchievement(CSLastPlayerAlive);
		}
	}

	// [tj] Added hook into player killed stat that happens before weapon drop
	CCS_GameStats.Event_PlayerKilled_PreWeaponDrop(pVictim, info);
}

//[tj]  traces up to maxTrace units down and returns any standable object it hits
//      (doesn't check slope for standability)
CBaseEntity* CCSPlayer::GetNearestSurfaceBelow(float maxTrace)
{
	trace_t trace;
	Ray_t ray;

	Vector traceStart = this->GetAbsOrigin();
	Vector traceEnd = traceStart;
	traceEnd.z -= maxTrace;

	Vector minExtent = this->m_Local.m_bDucked  ? VEC_DUCK_HULL_MIN_SCALED( this ) : VEC_HULL_MIN_SCALED( this );
	Vector maxExtent = this->m_Local.m_bDucked  ? VEC_DUCK_HULL_MAX_SCALED( this ) : VEC_HULL_MAX_SCALED( this );

	ray.Init( traceStart, traceEnd, minExtent, maxExtent );
	UTIL_TraceRay( ray, MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );

	return trace.m_pEnt;
}

// [tj] Added a way to react to the round ending before we reset.
//      It is important to note that this happens before the bomb explodes, so a player may die
//      after this from a bomb explosion or a late kill after a defuse/detonation/rescue.
void CCSPlayer::OnRoundEnd(int winningTeam, int reason)
{    
	if (winningTeam == WINNER_CT || winningTeam == WINNER_TER)
	{
		int losingTeamId = (winningTeam == TEAM_CT) ? TEAM_TERRORIST : TEAM_CT;
		
		CTeam* losingTeam = GetGlobalTeam(losingTeamId);

		int losingTeamPlayers = 0;

		if (losingTeam)
		{
			losingTeamPlayers = losingTeam->GetNumPlayers();
			
			int ignoreCount = 0;
			for ( int i = 1; i <= gpGlobals->maxClients; i++ )
			{
				CCSPlayer* pPlayer = (CCSPlayer*)UTIL_PlayerByIndex( i );
				if (pPlayer)
				{
					int teamNum = pPlayer->GetTeamNumber();
					if ( teamNum == losingTeamId )
					{					
						if (pPlayer->WasNotKilledNaturally())
						{
							ignoreCount++;
						}
					}
					
				}
			}

			losingTeamPlayers -= ignoreCount;
		}

		//Check fast round win achievement
		if (    IsAlive() && 
				gpGlobals->curtime - CSGameRules()->GetRoundStartTime() < AchievementConsts::FastRoundWin_Time &&
				GetTeamNumber() == winningTeam &&
				losingTeamPlayers >= AchievementConsts::DefaultMinOpponentsForAchievement)
		{
			AwardAchievement(CSFastRoundWin);
		}

		//Check goosechase achievement
		if (IsAlive() && reason == Target_Bombed && m_gooseChaseStep == GC_STOPPED_AFTER_GETTING_SHOT && m_pGooseChaseDistractingPlayer)
		{
			m_pGooseChaseDistractingPlayer->AwardAchievement(CSGooseChase);
		}

		//Check Defuse Defense achievement
		if (IsAlive() && reason == Bomb_Defused && m_defuseDefenseStep == DD_KILLED_TERRORIST)
		{
			AwardAchievement(CSDefuseDefense);
		}

		//Check silent win
		if (m_NumEnemiesKilledThisRound > 0 && GetTeamNumber() == winningTeam && !m_bMadeFootstepNoise)
		{
			AwardAchievement(CSSilentWin);
		}

		//Process && Check "win rounds without buying" achievement
		if (GetTeamNumber() == winningTeam && !m_bMadePurchseThisRound)
		{
			m_roundsWonWithoutPurchase++;
			if (m_roundsWonWithoutPurchase > AchievementConsts::WinRoundsWithoutBuying_Rounds)
			{
				AwardAchievement(CSWinRoundsWithoutBuying);
			}
		}
		else
		{
			m_roundsWonWithoutPurchase = 0;
		}
	}

	m_lastRoundResult = reason;
}

void CCSPlayer::OnPreResetRound()
{
	//Check headshot survival achievement
	if (IsAlive() && m_bSurvivedHeadshotDueToHelmet)
	{
		AwardAchievement(CSSurvivedHeadshotDueToHelmet);
	}

	if (IsAlive() && m_grenadeDamageTakenThisRound > AchievementConsts::SurviveGrenade_MinDamage)
	{
		AwardAchievement(CSSurviveGrenade);
	}


	//Check achievement for surviving attacks from multiple players.
	if (IsAlive())
	{
		int numberOfEnemyDamagers = GetNumEnemyDamagers();

		if (numberOfEnemyDamagers >= AchievementConsts::SurviveManyAttacks_NumberDamagingPlayers)
		{
			AwardAchievement(CSSurviveManyAttacks);
		}
	}
}

void CCSPlayer::OnCanceledDefuse()
{
	if (m_gooseChaseStep == GC_SHOT_DURING_DEFUSE)
	{
		m_gooseChaseStep = GC_STOPPED_AFTER_GETTING_SHOT;
	}
}


void CCSPlayer::OnStartedDefuse()
{
	if (m_defuseDefenseStep == DD_NONE)
	{
		m_defuseDefenseStep = DD_STARTED_DEFUSE;
	}

	if ( !IsBot() && m_flDefusingTalkTimer < gpGlobals->curtime )
	{
		Radio( "Radio.DefusingBomb", "#Cstrike_TitlesTXT_Defusing_Bomb" );
		m_flDefusingTalkTimer = gpGlobals->curtime + 6.0f;
	}
}
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCSPlayer::AttemptToExitFreezeCam( void )
{
	float fEndFreezeTravel = m_flDeathTime + CS_DEATH_ANIMATION_TIME + spec_freeze_traveltime.GetFloat();
	if ( gpGlobals->curtime < fEndFreezeTravel )
		return;

	m_bAbortFreezeCam = true;
}

//-----------------------------------------------------------------------------
// Purpose: Sets whether this player is dominating the specified other player
//-----------------------------------------------------------------------------
void CCSPlayer::SetPlayerDominated( CCSPlayer *pPlayer, bool bDominated )
{
	int iPlayerIndex = pPlayer->entindex();
	m_bPlayerDominated.Set( iPlayerIndex, bDominated );
	pPlayer->SetPlayerDominatingMe( this, bDominated );
}

//-----------------------------------------------------------------------------
// Purpose: Sets whether this player is being dominated by the other player
//-----------------------------------------------------------------------------
void CCSPlayer::SetPlayerDominatingMe( CCSPlayer *pPlayer, bool bDominated )
{
	int iPlayerIndex = pPlayer->entindex();
	m_bPlayerDominatingMe.Set( iPlayerIndex, bDominated );
}


//-----------------------------------------------------------------------------
// Purpose: Returns whether this player is dominating the specified other player
//-----------------------------------------------------------------------------
bool CCSPlayer::IsPlayerDominated( int iPlayerIndex )
{
	return m_bPlayerDominated.Get( iPlayerIndex );
}

bool CCSPlayer::IsPlayerDominatingMe( int iPlayerIndex )
{
	return m_bPlayerDominatingMe.Get( iPlayerIndex );
}

//=============================================================================
// HPE_BEGIN:
// [menglish] MVP functions
//=============================================================================
 
void CCSPlayer::IncrementNumMVPs( CSMvpReason_t mvpReason )
{
	//=============================================================================
	// HPE_BEGIN:
	// [Forrest] Allow MVP to be turned off for a server
	//=============================================================================
	if ( sv_nomvp.GetBool() )
	{
		Msg( "Round MVP disabled: sv_nomvp is set.\n" );
		return;
	}
	//=============================================================================
	// HPE_END
	//=============================================================================

	m_iMVPs++;
	CCS_GameStats.Event_MVPEarned( this );
	IGameEvent *mvpEvent = gameeventmanager->CreateEvent( "round_mvp" );

	if ( mvpEvent )
	{
		mvpEvent->SetInt( "userid", GetUserID() );
		mvpEvent->SetInt( "reason", mvpReason );
		gameeventmanager->FireEvent( mvpEvent );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sets the number of rounds this player has caused to be won for their team
//-----------------------------------------------------------------------------
void CCSPlayer::SetNumMVPs( int iNumMVP )
{
	m_iMVPs = iNumMVP;
}
//-----------------------------------------------------------------------------
// Purpose: Returns the number of rounds this player has caused to be won for their team
//-----------------------------------------------------------------------------
int CCSPlayer::GetNumMVPs()
{
	return m_iMVPs;
}
 
//=============================================================================
// HPE_END
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: Removes all nemesis relationships between this player and others
//-----------------------------------------------------------------------------
void CCSPlayer::RemoveNemesisRelationships()
{
	for ( int i = 1 ; i <= gpGlobals->maxClients ; i++ )
	{
		CCSPlayer *pTemp = ToCSPlayer( UTIL_PlayerByIndex( i ) );
		if ( pTemp && pTemp != this )
		{        
			// set this player to be not dominating anyone else
			SetPlayerDominated( pTemp, false );

			// set no one else to be dominating this player		
			pTemp->SetPlayerDominated( this, false );
		}
	}	
}

void CCSPlayer::CheckMaxGrenadeKills(int grenadeKills)
{
	if (grenadeKills > m_maxGrenadeKills)
	{
		m_maxGrenadeKills = grenadeKills;
	}
}

void CCSPlayer::CommitSuicide( bool bExplode /*= false*/, bool bForce /*= false*/ )
{
	m_wasNotKilledNaturally = true;
	BaseClass::CommitSuicide(bExplode, bForce);
}

void CCSPlayer::CommitSuicide( const Vector &vecForce, bool bExplode /*= false*/, bool bForce /*= false*/ )
{
	m_wasNotKilledNaturally = true;
	BaseClass::CommitSuicide(vecForce, bExplode, bForce);
}

int CCSPlayer::GetNumEnemyDamagers()
{
	int numberOfEnemyDamagers = 0;
	FOR_EACH_LL( m_DamageList, i )
	{
		if ( m_DamageList[i]->IsDamageRecordValidPlayerToPlayer() &&
			 m_DamageList[i]->GetPlayerRecipientPtr() == this &&
			 IsOtherEnemy( m_DamageList[i]->GetPlayerDamagerPtr() ) )
		{
			numberOfEnemyDamagers++;
		}
	}
	return numberOfEnemyDamagers;
}


int CCSPlayer::GetNumEnemiesDamaged()
{
	int numberOfEnemiesDamaged = 0;
	FOR_EACH_LL( m_DamageList, i )
	{
		if ( m_DamageList[i]->IsDamageRecordValidPlayerToPlayer() &&
			 m_DamageList[i]->GetPlayerDamagerPtr() == this &&
			 IsOtherEnemy( m_DamageList[i]->GetPlayerRecipientPtr() ) )
		{
			numberOfEnemiesDamaged++;
		}
	}
	return numberOfEnemiesDamaged;
}

//=============================================================================
// HPE_END
//=============================================================================

#if CS_CONTROLLABLE_BOTS_ENABLED
void CCSPlayer::SavePreControlData()
{
	m_PreControlData.m_iClass	= GetClass();
	m_PreControlData.m_iSkin	= m_iSkin;
	m_PreControlData.m_iAccount	= m_iAccount;
	m_PreControlData.m_iFrags	= FragCount();
	m_PreControlData.m_iAssists = AssistsCount();
	m_PreControlData.m_iDeaths	= DeathCount();
}

bool CCSPlayer::CanControlBot( CCSBot *pBot, bool bSkipTeamCheck )
{
	if ( !cv_bot_controllable.GetBool() )
		return false;

	if ( !pBot )
		return false;

	if ( !pBot->IsAlive() )
		return false;

	if ( !bSkipTeamCheck && IsOtherEnemy( pBot->entindex() ) )
		return false;

	if ( !bSkipTeamCheck && !IsValidObserverTarget(pBot ) )
		return false;

	if ( pBot->HasControlledByPlayer() )
		return false;

	if ( pBot->IsDefusingBomb() )
		return false;

	// Can't control a bot that is setting a bomb
	const CC4 *pC4 = dynamic_cast<CC4*>( pBot->GetActiveWeapon() );
	if ( pC4 && pC4->m_bStartedArming )
		return false;

	if ( CSGameRules()->m_iRoundWinStatus != WINNER_NONE )
		return false;

	if ( CSGameRules()->IsFreezePeriod() )
		return false;

	if ( CSGameRules()->IsWarmupPeriod() )
		return false;

	if ( !bSkipTeamCheck && IsAlive() )
		return false;

	return true;
}

bool CCSPlayer::TakeControlOfBot( CCSBot *pBot, bool bSkipTeamCheck )
{
	if ( !CanControlBot(pBot, bSkipTeamCheck ) )
		return false;

	// First Save off our pre-control settings
	SavePreControlData();

	// Save off stuff we want from the bot
	// Position / Orientation
	// Appearance
	// Health, Armor, Stamina
	const Vector vecBotPosition = pBot->GetAbsOrigin();
	QAngle vecBotAngles = pBot->GetAbsAngles();
	const int nBotClass = pBot->GetClass();
	const int nBotSkin = pBot->m_iSkin;
	const int nBotHealth = pBot->GetHealth();
	const float flBotStamina = pBot->m_flStamina;
	const float flBotVelocityModifier = pBot->m_flVelocityModifier;
	const bool bBotDucked = pBot->m_Local.m_bDucked;
	const bool bBotDucking = pBot->m_Local.m_bDucking;
	const bool bBotFL_DUCKING = (pBot->GetFlags() & FL_DUCKING ) != 0;
	const bool bBotFL_ANIMDUCKING = ( pBot->GetFlags() & FL_ANIMDUCKING ) != 0;
	const float flBotDuckAmount = pBot->m_flDuckAmount;
	const MoveType_t eBotMoveType = pBot->GetMoveType();



	CWeaponCSBase * pBotWeapon = pBot->GetActiveCSWeapon();
	CBaseViewModel * pBotVM = pBot->GetViewModel();
	
	const float flBotNextAttack = pBot->GetNextAttack();
	const float flBotWeaponNextPrimaryAttack = pBotWeapon ? pBotWeapon->m_flNextPrimaryAttack : gpGlobals->curtime;
	const float flBotWeaponNextSecondaryAttack = pBotWeapon ? pBotWeapon->m_flNextSecondaryAttack : gpGlobals->curtime;
	const float flBotWeaponTimeWeaponIdle = pBotWeapon ?  pBotWeapon->m_flTimeWeaponIdle : gpGlobals->curtime;
	const bool bBotWeaponInReload = pBotWeapon ? pBotWeapon->m_bInReload : false;
	//char szBotAnimExtension[32]; pBot->m_szAnimExtension;
	//pBotWeapon->m_IdealActivity;
	//pBotWeapon->m_nIdealSequence;
	const Activity eBotWeaponActivity = pBotWeapon ? pBotWeapon->GetActivity() : ACT_IDLE;
	//pBotWeapon->m_nSequence;
	const float flBotWeaponCycle = pBotWeapon ? pBotWeapon->GetCycle() : 0.0f;
	

	const float flBotVMCycle = pBotVM ? pBotVM->GetCycle() : 0.0f;
	char szBotWeaponClassname[64];
	szBotWeaponClassname[0] = 0;
	if ( pBotWeapon )
	{
		V_strncpy( szBotWeaponClassname, pBotWeapon->GetClassname(), sizeof(szBotWeaponClassname ) );
	}
	//const Activity eBotActivity = GetActivity();
	//pBotWeapon->m_flAccuracy;
	//pBot->m_iShotsFired;
	//pBotWeapon->m_bDelayFire;
	
	if ( bSkipTeamCheck && pBot->GetTeamNumber() != GetTeamNumber() )
	{
		// player needs to switch teams before controlling this bot
		SwitchTeam( pBot->GetTeamNumber() );
	}
	
	if ( bSkipTeamCheck && HasControlledBot() )
	{
		CCSBot *pOldBot = ToCSBot( GetControlledBot() );
		pBot->SwitchTeam( GetTeamNumber() );
		ReleaseControlOfBot();
		pOldBot->Spawn();
		pOldBot->Teleport( &GetAbsOrigin(), &GetAbsAngles(), &vec3_origin );
		pOldBot->State_Transition( STATE_ACTIVE );

	}

	// Next set the control EHANDLEs
	SetControlledBot( pBot );
	pBot->SetControlledByPlayer( this );
	m_bIsControllingBot = true;
	m_iControlledBotEntIndex = pBot->entindex();

	// [wills] Trying to squash T-pose orphaned wearables. Note: it isn't great to remove wearables all over the place, 
	// since it may trigger an unnecessary texture re-composite, which is potentially costly.
	// RemoveAllWearables();

	// If we have a ragdoll, cut it loose now
	if ( CCSRagdoll *pRagdoll = dynamic_cast< CCSRagdoll* >( m_hRagdoll.Get() ) )
	{
		pRagdoll->m_hPlayer = NULL;
		m_hRagdoll = NULL;
	}



	// Now copy over various things from the bot
	m_iClass = nBotClass;
	m_iSkin = nBotSkin;

	
	// Make the bot dormant, so he no longer thinks, transmits, or simulates
	pBot->MakeDormant();
	pBot->State_Transition( STATE_DORMANT );
	pBot->m_iHealth = 0;
	pBot->m_lifeState = LIFE_DEAD;
	pBot->m_flVelocityModifier = 0.0f;

	m_flVelocityModifier = flBotVelocityModifier;		// GET FROM BOT?!?!?

	// Finally, run some normal spawn logic 
	// Here, I'm trying to copy what happens when we call CCSPlayer::Spawn, in some places 
	// using values from the bot rather than init values

	StopObserverMode();
	State_Transition( STATE_ACTIVE );

	bool hasChangedTeamTemp = m_bTeamChanged;

	// HACK: Bots sometimes have some roll applied when the player takes them over due to acceleration lean
	// which gets stuck on when the player takes them over. Easiest just to clear the roll on the bot when taking over
	vecBotAngles.z = 0;

	SetCSSpawnLocation( vecBotPosition, vecBotAngles );
	Spawn();
	m_bHasControlledBotThisRound = true;
	pBot->m_bHasBeenControlledByPlayerThisRound = true;

	m_fImmuneToDamageTime = 0;
	m_bImmunity = false;

	m_bTeamChanged = hasChangedTeamTemp; // dkorus: we want m_bTeamChanged to persist past the Spawn() call.  This is how we acomplish this

	m_flStamina = flBotStamina;		// FROM BOT
	State_Transition( m_iPlayerState );
	pBot->TransferInventory( this );

	m_iHealth = nBotHealth;
	m_lifeState = LIFE_ALIVE;

	m_bDuckOverride = false;

	// afk check disabled for players whose first action is taking over a bot
	m_bHasMovedSinceSpawn = true;

	SetMoveType( eBotMoveType );
	m_Local.m_bDucked = bBotDucked;
	m_Local.m_bDucking = bBotDucking;
	if ( bBotFL_DUCKING )
		AddFlag( FL_DUCKING );
	else
		RemoveFlag( FL_DUCKING );
	if ( bBotFL_ANIMDUCKING )
		AddFlag( FL_ANIMDUCKING );
	else
		RemoveFlag( FL_ANIMDUCKING );
	m_flDuckAmount = flBotDuckAmount;

	pBot->DispatchUpdateTransmitState();
	DispatchUpdateTransmitState();

	CBaseCombatWeapon* pWeapon = pBotWeapon ? Weapon_OwnsThisType( pBotWeapon->GetClassname() ) : NULL;

	if ( pWeapon )
	{
		Weapon_Switch( pWeapon, 0 );

		pWeapon->SendWeaponAnim( eBotWeaponActivity );
		pWeapon->SetCycle( flBotWeaponCycle );
		pWeapon->m_flTimeWeaponIdle = flBotWeaponTimeWeaponIdle;
		pWeapon->m_flNextPrimaryAttack = flBotWeaponNextPrimaryAttack;
		pWeapon->m_flNextSecondaryAttack = flBotWeaponNextSecondaryAttack;
		pWeapon->m_bInReload = bBotWeaponInReload;

		if ( CBaseViewModel * pVM = GetViewModel() )
		{
			pVM->SetCycle( flBotVMCycle );
		}


		SetNextAttack( flBotNextAttack );
	}

	if ( pBot->IsRescuing() )
	{
		// Tell the hostages controlled by the bot that they should now follow this player
		for ( int iHostage=0; iHostage < g_Hostages.Count(); iHostage++ )
		{
			CHostage *pHostage = g_Hostages[iHostage];

			if ( pHostage && pHostage->GetLeader() == pBot )
			{
				pHostage->Follow( this );
				pBot->m_hCarriedHostage = NULL;
				m_hCarriedHostage = pHostage;
			}
		}

		if ( HOSTAGE_RULE_CAN_PICKUP && pBot->m_hCarriedHostageProp != NULL )
		{
			// transfer any carried hostages and refresh the viewmodel
			CHostageCarriableProp *pHostageProp = static_cast< CHostageCarriableProp* >( pBot->m_hCarriedHostageProp.Get() );
			if ( pHostageProp )
			{
				pBot->m_hCarriedHostageProp = NULL;
				pHostageProp->SetAbsOrigin( GetAbsOrigin() );
				pHostageProp->SetParent( this );
				pHostageProp->SetOwnerEntity( this );
				pHostageProp->FollowEntity( this );
				m_hCarriedHostageProp = pHostageProp;
			}
		
			CBaseViewModel *vm = pBot->GetViewModel( HOSTAGE_VIEWMODEL );
			UTIL_Remove( vm );
			pBot->m_hViewModel.Set( HOSTAGE_VIEWMODEL, 0 );
		}
	}

	RefreshCarriedHostage( true );

	IGameEvent * event = gameeventmanager->CreateEvent( "bot_takeover" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "botid", pBot->GetUserID() );
		event->SetInt( "index", GetClientIndex() );

		gameeventmanager->FireEvent( event );
	}

	return true;
}

void CCSPlayer::ReleaseControlOfBot()
{
	if( m_bIsControllingBot == false )
		return;

	CCSBot *pBot = ToCSBot( m_hControlledBot.Get() );


	if ( pBot )
	{
		pBot->SetControlledByPlayer( NULL );

		TransferInventory( pBot );
		Msg( "    %s RELEASED CONTROL of %s\n", GetPlayerName(), pBot->GetPlayerName() );

		pBot->RemoveEFlags( EFL_DORMANT );
	}
	else
	{
		// dkorus: make sure we clear out any items the player has and reset states.  This makes sure he doesn't keep the bot's items into the next round
		SetArmorValue( 0 );
		m_bHasHelmet = false;
		m_bHasNightVision = false;
		m_bNightVisionOn = false;

		RemoveAllItems( true );
	}
	m_iClass = m_PreControlData.m_iClass;
	m_iSkin = m_PreControlData.m_iSkin;
	m_iAccount = m_PreControlData.m_iAccount;

	SetControlledBot( NULL );
	//UpdateAppearanceIndex();
	m_bIsControllingBot = false;
	m_iControlledBotEntIndex = -1;

	DispatchUpdateTransmitState();
}

/*
CBaseEntity * CCSPlayer::FindNearestThrownGrenade(bool bReverse)
{
	// early out if the option is disabled by the server
	if ( !cv_bot_controllable.GetBool() )
		return NULL;

	float32 flNearestDistSqr = 0.0f;
	CCSBot *pNearestBot = NULL;

	for ( int idx = 1; idx <= gpGlobals->maxClients; ++idx )
	{
		CCSBot *pBot = ToCSBot( UTIL_PlayerByIndex( idx ) );

		if ( !pBot )
			continue;

		if ( !CanControlBot( pBot ) )
			continue;

		if ( bMustBeValidObserverTarget && !IsValidObserverTarget( pBot ) )
			continue;

		const float flDistSqr = GetAbsOrigin().DistToSqr( pBot->GetAbsOrigin() );

		if ( pNearestBot == NULL || flDistSqr < flNearestDistSqr )
		{
			flNearestDistSqr = flDistSqr;
			pNearestBot = pBot;
		}
	}

	return pNearestBot;
}
*/

CCSBot* CCSPlayer::FindNearestControllableBot( bool bMustBeValidObserverTarget )
{
	// early out if the option is disabled by the server
	if ( !cv_bot_controllable.GetBool() )
		return NULL;

	float32 flNearestDistSqr = 0.0f;
	CCSBot *pNearestBot = NULL;

	for ( int idx = 1; idx <= gpGlobals->maxClients; ++idx )
	{
		CCSBot *pBot = ToCSBot( UTIL_PlayerByIndex( idx ) );

		if ( !pBot )
			continue;

		if ( !CanControlBot(pBot ) )
			continue;

		if ( bMustBeValidObserverTarget && !IsValidObserverTarget(pBot ) )
			continue;

		const float flDistSqr = GetAbsOrigin().DistToSqr( pBot->GetAbsOrigin() );

		if ( pNearestBot == NULL || flDistSqr < flNearestDistSqr )
		{
			flNearestDistSqr = flDistSqr;
			pNearestBot = pBot;
		}
	}

	return pNearestBot;
}
#endif // CS_CONTROLLABLE_BOTS_ENABLED

bool CCSPlayer::HasAgentSet( int team )
{
	if ( IsBotOrControllingBot() )
		return false;

	if ( team == TEAM_CT )
		return ( m_iLoadoutSlotAgentCT > 0 );
	if ( team == TEAM_TERRORIST )
		return ( m_iLoadoutSlotAgentT > 0 );

	return false;
}

int CCSPlayer::GetAgentID( int team )
{
	if ( IsBotOrControllingBot() )
		return 0;

	if ( team == TEAM_CT )
		return m_iLoadoutSlotAgentCT;
	if ( team == TEAM_TERRORIST )
		return m_iLoadoutSlotAgentT;

	return 0;
}

void CCSPlayer::ObserverUse( bool bIsPressed )
{
	if ( !bIsPressed )
		return;
	
#if CS_CONTROLLABLE_BOTS_ENABLED
 	CBasePlayer * target = ToBasePlayer( GetObserverTarget() );
 
 	if ( target && target->IsBot() )
 	{
 		if ( m_bCanControlObservedBot )
 		{
			CCSPlayer *pPlayer = this;

			CCSBot *pBot = ToCSBot( pPlayer->GetObserverTarget() );

			if ( pBot != NULL && pBot->IsBot() )
			{
				if ( !pPlayer->IsDead() )
				{
					Msg( "Player %s tried to take control of bot %s but was disallowed by the server\n", pPlayer->GetPlayerName(), pBot->GetPlayerName() );
				}
				else if ( !cv_bot_controllable.GetBool() )
				{
					Msg( "Player %s tried to take control of bot %s but was disallowed by the server\n", pPlayer->GetPlayerName(), pBot->GetPlayerName() );
				}
				else if ( pPlayer->TakeControlOfBot( pBot ) )
				{
					Msg( "Player %s took control bot %s (%d)\n", pPlayer->GetPlayerName(), pBot->GetPlayerName(), pBot->entindex() );
				}
				else
				{
					Msg( "Player %s tried to take control of bot %s but failed\n", pPlayer->GetPlayerName(), pBot->GetPlayerName() );
				}
			}
			else 
			{
				Msg( "Player %s tried to take control of bot but none could be found\n", pPlayer->GetPlayerName() );
			}

 			return;
 		}
 	}
#endif
	
	BaseClass::ObserverUse( bIsPressed );

}

void CCSPlayer::IncrementFragCount( int nCount )
{
#if CS_CONTROLLABLE_BOTS_ENABLED
	// calculate frag count properly for a bot-controlled player
	if ( IsControllingBot() )
	{
		CCSPlayer* controlledPlayerScorer = GetControlledBot();
		if ( controlledPlayerScorer )
		{
			controlledPlayerScorer->IncrementFragCount( nCount );
		}
		return;
	}
#endif

	m_iFrags += nCount;
	pl.frags = m_iFrags;
}

void CCSPlayer::IncrementDeathCount( int nCount )
{
#if CS_CONTROLLABLE_BOTS_ENABLED
	// calculate death count properly for a bot-controlled player
	if ( IsControllingBot() )
	{
		CCSPlayer* controlledPlayerScorer = GetControlledBot();
		if ( controlledPlayerScorer )
		{
			controlledPlayerScorer->IncrementDeathCount( nCount );
		}
		return;
	}
#endif

	m_iDeaths += nCount;
	pl.deaths = m_iDeaths;
}

void CCSPlayer::IncrementAssistsCount( int nCount )
{
#if CS_CONTROLLABLE_BOTS_ENABLED
	// calculate death count properly for a bot-controlled player
	if ( IsControllingBot() )
	{
		CCSPlayer* controlledPlayerScorer = GetControlledBot();
		if ( controlledPlayerScorer )
		{
			controlledPlayerScorer->IncrementAssistsCount( nCount );
		}
		return;
	}
#endif

	m_iAssists += nCount;
	pl.assists = m_iAssists;
}

void UTIL_AwardMoneyToTeam( int iAmount, int iTeam, CBaseEntity *pIgnore )
{
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CCSPlayer *pPlayer = (CCSPlayer*) UTIL_PlayerByIndex( i );

		if ( !pPlayer )
			continue;

		if ( pPlayer->GetTeamNumber() != iTeam )
			continue;

		if ( pPlayer == pIgnore )
			continue;

		pPlayer->AddAccount( iAmount );
	}
}

