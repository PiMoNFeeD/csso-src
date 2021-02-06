//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Player for HL1.
//
// $NoKeywords: $
//=============================================================================//

#ifndef CS_PLAYER_H
#define CS_PLAYER_H
#pragma once


#include "basemultiplayerplayer.h"
#include "server_class.h"
#include "cs_playeranimstate.h"
#include "cs_shareddefs.h"
#include "cs_autobuy.h"
#include "utldict.h"
#include "cs_player_shared.h"
#include "csgo_playeranimstate.h"



class CWeaponCSBase;
class CMenu;
class CHintMessageQueue;
class CNavArea;
class CCSBot;

#include "cs_weapon_parse.h"


void UTIL_AwardMoneyToTeam( int iAmount, int iTeam, CBaseEntity *pIgnore );

#define MENU_STRING_BUFFER_SIZE	1024
#define MENU_MSG_TEXTCHUNK_SIZE	50

enum
{
	MIN_NAME_CHANGE_INTERVAL = 10,			// minimum number of seconds between name changes
	NAME_CHANGE_HISTORY_SIZE = 5,			// number of times a player can change names in NAME_CHANGE_HISTORY_INTERVAL
	NAME_CHANGE_HISTORY_INTERVAL = 600,	// no more than NAME_CHANGE_HISTORY_SIZE name changes can be made in this many seconds
};

extern ConVar bot_mimic;


// Function table for each player state.
class CCSPlayerStateInfo
{
public:
	CSPlayerState m_iPlayerState;
	const char *m_pStateName;
	
	void (CCSPlayer::*pfnEnterState)();	// Init and deinit the state.
	void (CCSPlayer::*pfnLeaveState)();

	void (CCSPlayer::*pfnPreThink)();	// Do a PreThink() in this state.
};


//=======================================
//Record of either damage taken or given.
//Contains the player name that we hurt or that hurt us,
//and the total damage
//=======================================
class CDamageRecord
{
public:
	CDamageRecord( CCSPlayer * pPlayerDamager, CCSPlayer * pPlayerRecipient, int iDamage, int iCounter, int iActualHealthRemoved );

	void AddDamage( int iDamage, int iCounter, int iActualHealthRemoved = 0 )
	{
		m_iDamage += iDamage;
		m_iActualHealthRemoved += iActualHealthRemoved;

		if ( m_iLastBulletUpdate != iCounter || GetPlayerDamagerPtr() == NULL )
			m_iNumHits++;

		m_iLastBulletUpdate = iCounter;
	}

	bool IsDamageRecordStillValidForDamagerAndRecipient( CCSPlayer * pPlayerDamager, CCSPlayer * pPlayerRecipient );
	bool IsDamageRecordValidPlayerToPlayer(void) { return ( this && m_PlayerDamager && m_PlayerRecipient) ? true : false; }

	CCSPlayer* GetPlayerDamagerPtr( void ) { return m_PlayerDamager; }
	CCSPlayer* GetPlayerRecipientPtr( void ) { return m_PlayerRecipient; }

	char *GetPlayerDamagerName( void ) { return m_szPlayerDamagerName; }
	char *GetPlayerRecipientName( void ) { return m_szPlayerRecipientName; }

	int GetDamage( void ) { return m_iDamage; }
	int GetActualHealthRemoved( void ) { return m_iActualHealthRemoved; }
	int GetNumHits( void ) { return m_iNumHits; }

	CCSPlayer* GetPlayerDamagerControlledBotPtr( void ) { return m_PlayerDamagerControlledBot; }
	CCSPlayer* GetPlayerRecipientControlledBotPtr( void ) { return m_PlayerRecipientControlledBot; }

private:
	CHandle<CCSPlayer> m_PlayerDamager;
	CHandle<CCSPlayer> m_PlayerRecipient;

	char m_szPlayerDamagerName[MAX_PLAYER_NAME_LENGTH];
	char m_szPlayerRecipientName[MAX_PLAYER_NAME_LENGTH];
	
	int m_iDamage;		//how much damage was delivered
	int m_iActualHealthRemoved;		//how much damage was actually applied
	int m_iNumHits;		//how many hits
	int	m_iLastBulletUpdate; // update counter

	CHandle<CCSPlayer> m_PlayerDamagerControlledBot;
	CHandle<CCSPlayer> m_PlayerRecipientControlledBot;
};

// Message display history (CCSPlayer::m_iDisplayHistoryBits)
// These bits are set when hint messages are displayed, and cleared at
// different times, according to the DHM_xxx bitmasks that follow

#define DHF_ROUND_STARTED		( 1 << 1 )
#define DHF_HOSTAGE_SEEN_FAR	( 1 << 2 )
#define DHF_HOSTAGE_SEEN_NEAR	( 1 << 3 )
#define DHF_HOSTAGE_USED		( 1 << 4 )
#define DHF_HOSTAGE_INJURED		( 1 << 5 )
#define DHF_HOSTAGE_KILLED		( 1 << 6 )
#define DHF_FRIEND_SEEN			( 1 << 7 )
#define DHF_ENEMY_SEEN			( 1 << 8 )
#define DHF_FRIEND_INJURED		( 1 << 9 )
#define DHF_FRIEND_KILLED		( 1 << 10 )
#define DHF_ENEMY_KILLED		( 1 << 11 )
#define DHF_BOMB_RETRIEVED		( 1 << 12 )
#define DHF_AMMO_EXHAUSTED		( 1 << 15 )
#define DHF_IN_TARGET_ZONE		( 1 << 16 )
#define DHF_IN_RESCUE_ZONE		( 1 << 17 )
#define DHF_IN_ESCAPE_ZONE		( 1 << 18 ) // unimplemented
#define DHF_IN_VIPSAFETY_ZONE	( 1 << 19 ) // unimplemented
#define	DHF_NIGHTVISION			( 1 << 20 )
#define	DHF_HOSTAGE_CTMOVE		( 1 << 21 )
#define	DHF_SPEC_DUCK			( 1 << 22 )

// DHF_xxx bits to clear when the round restarts

#define DHM_ROUND_CLEAR ( \
	DHF_ROUND_STARTED | \
	DHF_HOSTAGE_KILLED | \
	DHF_FRIEND_KILLED | \
	DHF_BOMB_RETRIEVED )


// DHF_xxx bits to clear when the player is restored

#define DHM_CONNECT_CLEAR ( \
	DHF_HOSTAGE_SEEN_FAR | \
	DHF_HOSTAGE_SEEN_NEAR | \
	DHF_HOSTAGE_USED | \
	DHF_HOSTAGE_INJURED | \
	DHF_FRIEND_SEEN | \
	DHF_ENEMY_SEEN | \
	DHF_FRIEND_INJURED | \
	DHF_ENEMY_KILLED | \
	DHF_AMMO_EXHAUSTED | \
	DHF_IN_TARGET_ZONE | \
	DHF_IN_RESCUE_ZONE | \
	DHF_IN_ESCAPE_ZONE | \
	DHF_IN_VIPSAFETY_ZONE | \
	DHF_HOSTAGE_CTMOVE | \
	DHF_SPEC_DUCK )

// radio messages (these must be kept in sync with actual radio) -------------------------------------
enum RadioType
{
	RADIO_INVALID = 0,

	RADIO_START_1,							///< radio messages between this and RADIO_START_2 and part of Radio1()

	RADIO_GO_GO_GO,
	RADIO_TEAM_FALL_BACK,
	RADIO_STICK_TOGETHER_TEAM,
	RADIO_HOLD_THIS_POSITION,
	RADIO_FOLLOW_ME,

	RADIO_START_2,							///< radio messages between this and RADIO_START_3 are part of Radio2()

	RADIO_AFFIRMATIVE,
	RADIO_NEGATIVE,
	RADIO_CHEER,
	RADIO_COMPLIMENT,
	RADIO_THANKS,

	RADIO_START_3,							///< radio messages above this are part of Radio3()

	RADIO_ENEMY_SPOTTED,
	RADIO_NEED_BACKUP,
	RADIO_YOU_TAKE_THE_POINT,
	RADIO_SECTOR_CLEAR,
	RADIO_IN_POSITION,

	// not used
	///////////////////////////////////
	RADIO_COVER_ME,
	RADIO_REGROUP_TEAM,
	RADIO_TAKING_FIRE,
	RADIO_REPORT_IN_TEAM,
	RADIO_REPORTING_IN,
	RADIO_GET_OUT_OF_THERE,
	RADIO_ENEMY_DOWN,
	RADIO_STORM_THE_FRONT,

	RADIO_END,

	RADIO_NUM_EVENTS
};

extern const char *RadioEventName[ RADIO_NUM_EVENTS+1 ];

/**
 * Convert name to RadioType
 */
extern RadioType NameToRadioEvent( const char *name );

enum BuyResult_e
{
	BUY_BOUGHT,
	BUY_ALREADY_HAVE,
	BUY_CANT_AFFORD,
	BUY_PLAYER_CANT_BUY,	// not in the buy zone, is the VIP, is past the timelimit, etc
	BUY_NOT_ALLOWED,		// weapon is restricted by VIP mode, team, etc
	BUY_INVALID_ITEM,
};

//=============================================================================
// HPE_BEGIN:
//=============================================================================

// [tj] The phases for the "Goose Chase" achievement
enum GooseChaseAchievementStep
{
    GC_NONE,    
    GC_SHOT_DURING_DEFUSE,
    GC_STOPPED_AFTER_GETTING_SHOT
};

// [tj] The phases for the "Defuse Defense" achievement
enum DefuseDefenseAchivementStep
{
    DD_NONE,
    DD_STARTED_DEFUSE,
    DD_KILLED_TERRORIST       
};
 
//=============================================================================
// HPE_END
//=============================================================================


//=============================================================================
// >> CounterStrike player
//=============================================================================
class CCSPlayer : public CBaseMultiplayerPlayer, public ICSPlayerAnimStateHelpers
{
public:
	DECLARE_CLASS( CCSPlayer, CBaseMultiplayerPlayer );
	DECLARE_SERVERCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_DATADESC();

	CCSPlayer();
	~CCSPlayer();

	static CCSPlayer *CreatePlayer( const char *className, edict_t *ed );
	static CCSPlayer* Instance( int iEnt );

	virtual void		Precache();
	virtual void		Spawn();
	virtual void		InitialSpawn( void );
	virtual void		PostSpawnPointSelection( void );

	void SetCSSpawnLocation( Vector position, QAngle angle );
	
	virtual void		CheatImpulseCommands( int iImpulse );
	virtual void		PlayerRunCommand( CUserCmd *ucmd, IMoveHelper *moveHelper );
	virtual void		PostThink();

	virtual int			OnTakeDamage( const CTakeDamageInfo &inputInfo );
	virtual int			OnTakeDamage_Alive( const CTakeDamageInfo &info );

	virtual void		Event_Killed( const CTakeDamageInfo &info );

	//=============================================================================
	// HPE_BEGIN:
	// [tj] We have a custom implementation so we can check for achievements.
	//=============================================================================
	
	virtual void		Event_KilledOther( CBaseEntity *pVictim, const CTakeDamageInfo &info );

	//=============================================================================
	// HPE_END
	//=============================================================================

	virtual void		TraceAttack( const CTakeDamageInfo &inputInfo, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator );

	virtual CBaseEntity	*GiveNamedItem( const char *pszName, int iSubType = 0 );
	virtual bool		IsBeingGivenItem() const { return m_bIsBeingGivenItem; }
	
	virtual CBaseEntity *FindUseEntity( void );
	virtual bool		IsUseableEntity( CBaseEntity *pEntity, unsigned int requiredCaps );
	
	virtual void		CreateViewModel( int viewmodelindex = WEAPON_VIEWMODEL );
	virtual void		ShowViewPortPanel( const char * name, bool bShow = true, KeyValues *data = NULL );

	// This passes the event to the client's and server's CPlayerAnimState.
	void DoAnimationEvent( PlayerAnimEvent_t event, int nData = 0 );

	// from CBasePlayer
	virtual void		SetupVisibility( CBaseEntity *pViewEntity, unsigned char *pvs, int pvssize );

	virtual CBaseEntity* FindNextObserverTarget( bool bReverse );

	virtual int 		GetNextObserverSearchStartPoint( bool bReverse );

	virtual bool UpdateDispatchLayer( CAnimationLayer *pLayer, CStudioHdr *pWeaponStudioHdr, int iSequence ) OVERRIDE;

// In shared code.
public:

	// IPlayerAnimState overrides.
	virtual CWeaponCSBase* CSAnim_GetActiveWeapon();
	virtual bool CSAnim_CanMove();

	virtual float GetPlayerMaxSpeed();

	void FireBullet(
		Vector vecSrc,
		const QAngle &shootAngles,
		float flDistance,
		float flPenetration,
		int nPenetrationCount,
		int iBulletType,
		int iDamage,
		float flRangeModifier,
		CBaseEntity *pevAttacker,
		bool bDoEffects,
		float xSpread, float ySpread );

	bool HandleBulletPenetration(
		float &flPenetration,
		int &iEnterMaterial,
		bool &hitGrate,
		trace_t &tr,
		Vector &vecDir,
		surfacedata_t *pSurfaceData,
		float flPenetrationModifier,
		float flDamageModifier,
		bool bDoEffects,
		int iDamageType,
		float flPenetrationPower,
		int &nPenetrationCount,
		Vector &vecSrc,
		float flDistance,
		float flCurrentDistance,
		float &fCurrentDamage );

	void KickBack(
		float up_base,
		float lateral_base,
		float up_modifier,
		float lateral_modifier,
		float up_max,
		float lateral_max,
		int direction_change );

	void GetBulletTypeParameters( 
		int iBulletType, 
		float &fPenetrationPower, 
		float &flPenetrationDistance );

	// Returns true if the player is allowed to move.
	bool CanMove() const;

	void OnJump( float fImpulse );
	void OnLand( float fVelocity );

	bool HasC4() const;	// Is this player carrying a C4 bomb?
	bool IsVIP() const;

	int GetClass( void ) const;

	void MakeVIP( bool isVIP );

	virtual void SetAnimation( PLAYER_ANIM playerAnim );
	void DoAnimStateEvent( PlayerAnimEvent_t evt );

	virtual bool StartReplayMode( float fDelay, float fDuration, int iEntity );
	virtual void StopReplayMode();
	virtual void PlayUseDenySound();

	bool IsOtherEnemy( CCSPlayer *pPlayer );
	bool IsOtherEnemy( int nEntIndex );


public:

	// Simulates a single frame of movement for a player
	void RunPlayerMove( const QAngle& viewangles, float forwardmove, float sidemove, float upmove, unsigned short buttons, byte impulse, float frametime );
	virtual void HandleAnimEvent( animevent_t *pEvent );

	virtual void UpdateStepSound( surfacedata_t *psurface, const Vector &vecOrigin, const Vector &vecVelocity  );
	virtual void PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force );
	
	// from cbasecombatcharacter
	void InitVCollision( const Vector &vecAbsOrigin, const Vector &vecAbsVelocity );
	void VPhysicsShadowUpdate( IPhysicsObject *pPhysics );

	virtual bool IsLookingAtWeapon( void ) const { return m_bIsLookingAtWeapon; }
	virtual bool IsHoldingLookAtWeapon( void ) const { return m_bIsHoldingLookAtWeapon; }
	virtual void StopLookingAtWeapon( void ) { m_bIsLookingAtWeapon = false; m_bIsHoldingLookAtWeapon = false; }
	void ModifyTauntDuration( float flTimingChange ) { m_flLookWeaponEndTime -= flTimingChange; }

	CBaseEntity *GetUsableHighPriorityEntity( void );
	bool GetUseConfigurationForHighPriorityUseEntity( CBaseEntity *pEntity, CConfigurationForHighPriorityUseEntity_t &cfg );
	
	bool HasShield() const;
	bool IsShieldDrawn() const;
	void GiveShield( void );
	void RemoveShield( void );
	bool IsProtectedByShield( void ) const;		// returns true if player has a shield and is currently hidden behind it

	bool HasPrimaryWeapon( void );
	bool HasSecondaryWeapon( void );

	bool IsReloading( void ) const;				// returns true if current weapon is reloading

	void GiveDefaultItems();
	void RemoveAllItems( bool removeSuit );	//overridden to remove the defuser

	// Reset account, get rid of shield, etc..
	void Reset();

	void RoundRespawn( void );
	void ObserverRoundRespawn( void );
	void CheckTKPunishment( void );

	bool	IsBotOrControllingBot();
	bool	HasAgentSet( int team );
	int		GetAgentID( int team );
	bool	m_bNeedToChangeAgent;

	CNetworkVar( bool, m_bNeedToChangeGloves );

	virtual void ObserverUse( bool bIsPressed ); // observer pressed use

	// Add money to this player's account.
	void AddAccount( int amount, bool bTrackChange=true, bool bItemBought=false, const char *pItemName = NULL );
	void AddAccountAward( int reason );
	void AddAccountAward( int reason, int amount, const CWeaponCSBase *pWeapon = NULL );

	void HintMessage( const char *pMessage, bool bDisplayIfDead, bool bOverrideClientSettings = false ); // Displays a hint message to the player
	CHintMessageQueue *m_pHintMessageQueue;
	unsigned int m_iDisplayHistoryBits;
	bool m_bShowHints;
	float m_flLastAttackedTeammate;
	float m_flNextMouseoverUpdate;
	void UpdateMouseoverHints();

	// mark this player as not receiving money at the start of the next round.
	void MarkAsNotReceivingMoneyNextRound();
	bool DoesPlayerGetRoundStartMoney(); // self-explanitory :)

	virtual bool ShouldPickupItemSilently( CBaseCombatCharacter *pNewOwner );

	void DropC4();	// Get rid of the C4 bomb.
	
	CNetworkHandle( CBaseEntity, m_hCarriedHostage );	// networked entity handle
	void GiveCarriedHostage( EHANDLE hHostage );
	void RefreshCarriedHostage( bool bForceCreate );
	void RemoveCarriedHostage();
	CNetworkHandle( CBaseEntity, m_hCarriedHostageProp );	// networked entity handle
	EHANDLE	m_hHostageViewModel;
	
	bool HasDefuser();		// Is this player carrying a bomb defuser?
	void GiveDefuser(bool bPickedUp = false);		// give the player a defuser
	void RemoveDefuser();	// remove defuser from the player and remove the model attachment

    //=============================================================================
    // HPE_BEGIN:
    // [dwenger] Added for fun-fact support
    //=============================================================================

    bool PickedUpDefuser() { return m_bPickedUpDefuser; }
    void SetDefusedWithPickedUpKit(bool bDefusedWithPickedUpKit) { m_bDefusedWithPickedUpKit = bDefusedWithPickedUpKit; }
    bool GetDefusedWithPickedUpKit() { return m_bDefusedWithPickedUpKit; }

    //=============================================================================
    // HPE_END
    //=============================================================================


	//=============================================================================
	// HPE_BEGIN
	// [sbodenbender] Need a different test for player blindness for the achievements
	//=============================================================================

	bool IsBlindForAchievement();	// more stringent than IsBlind; more accurately represents when the player can see again

	//=============================================================================
	// HPE_END
	//=============================================================================

	bool IsBlind( void ) const;		// return true if this player is blind (from a flashbang)
	virtual void Blind( float holdTime, float fadeTime, float startingAlpha = 255 );	// player blinded by a flashbang
	float m_blindUntilTime;
	float m_blindStartTime;

	void Deafen( float flDistance );		//make the player deaf / apply dsp preset to muffle sound

	void ApplyDeafnessEffect();				// apply the deafness effect for a nearby explosion.

	bool IsAutoFollowAllowed( void ) const;		// return true if this player will allow bots to auto follow
	void InhibitAutoFollow( float duration );	// prevent bots from auto-following for given duration
	void AllowAutoFollow( void );				// allow bots to auto-follow immediately
	float m_allowAutoFollowTime;				// bots can auto-follow after this time

	// Have this guy speak a message into his radio.
	void Radio( const char *szRadioSound, const char *szRadioText = NULL, bool bTriggeredAutomatically = false );
	void ConstructRadioFilter( CRecipientFilter& filter );
	float m_flGotHostageTalkTimer;
	float m_flDefusingTalkTimer;
	float m_flC4PlantTalkTimer;

	void EmitPrivateSound( const char *soundName );		///< emit given sound that only we can hear

	bool Weapon_Switch( CBaseCombatWeapon *pWeapon, int viewmodelindex /*= 0*/ );

	CWeaponCSBase* GetActiveCSWeapon() const;

	void PreThink();

	// This is the think function for the player when they first join the server and have to select a team
	void JoiningThink();

	virtual bool ClientCommand( const CCommand &args );

	void LookAtHeldWeapon( void );

	bool HandleCommand_JoinClass( int iClass );
	bool HandleCommand_JoinTeam( int iTeam );

	BuyResult_e HandleCommand_Buy( const char *item );

    BuyResult_e HandleCommand_Buy_Internal( const char * item );

	AcquireResult::Type CanAcquire( CSWeaponID weaponId, AcquireMethod::Type acquireMethod );
	int					GetCarryLimit( CSWeaponID weaponId );

	void HandleMenu_Radio1( int slot );
	void HandleMenu_Radio2( int slot );
	void HandleMenu_Radio3( int slot );

	float m_flRadioTime;	
	int m_iRadioMessages;
	int iRadioMenu;

	void ListPlayers();

	bool m_bIgnoreRadio;

	// Returns one of the CS_CLASS_ enums.
	int PlayerClass() const;

	void MoveToNextIntroCamera();

	// Used to be GETINTOGAME state.
	void GetIntoGame();

	CBaseEntity* EntSelectSpawnPoint();
	
	void SetProgressBarTime( int barTime );
	virtual void PlayerDeathThink();

	virtual bool StartObserverMode( int mode );

	void Weapon_Equip( CBaseCombatWeapon *pWeapon );
	virtual bool BumpWeapon( CBaseCombatWeapon *pWeapon );
	virtual bool Weapon_CanUse( CBaseCombatWeapon *pWeapon );

	void ClearFlashbangScreenFade ( void );
	bool ShouldDoLargeFlinch( int nHitGroup, CBaseEntity *pAttacker );

	void ResetStamina( void );
	bool IsArmored( int nHitGroup );
	void Pain( bool HasArmour, int nDmgTypeBits );
	
	void DeathSound( const CTakeDamageInfo &info );
	
	bool Weapon_CanSwitchTo( CBaseCombatWeapon *pWeapon );

	void ChangeTeam( int iTeamNum );
	void SwitchTeam( int iTeamNum );	// Changes teams without penalty - used for auto team balancing

	void ModifyOrAppendPlayerCriteria( AI_CriteriaSet& set );

	virtual void OnDamagedByExplosion( const CTakeDamageInfo &info );

	// Called whenever this player fires a shot.
	void NoteWeaponFired();
	virtual bool WantsLagCompensationOnEntity( const CBasePlayer *pPlayer, const CUserCmd *pCmd, const CBitVec<MAX_EDICTS> *pEntityTransmitBits ) const;


// ------------------------------------------------------------------------------------------------ //
// Player state management.
// ------------------------------------------------------------------------------------------------ //
public:

	void State_Transition( CSPlayerState newState );	// Cleanup the previous state and enter a new state.
	CSPlayerState State_Get() const;				// Get the current state.


private:
	void State_Enter( CSPlayerState newState );		// Initialize the new state.
	void State_Leave();								// Cleanup the previous state.
	void State_PreThink();							// Update the current state.
	
	// Find the state info for the specified state.
	static CCSPlayerStateInfo* State_LookupInfo( CSPlayerState state );

	// This tells us which state the player is currently in (joining, observer, dying, etc).
	// Each state has a well-defined set of parameters that go with it (ie: observer is movetype_noclip, nonsolid,
	// invisible, etc).
	CNetworkVar( CSPlayerState, m_iPlayerState );

	CCSPlayerStateInfo *m_pCurStateInfo;			// This can be NULL if no state info is defined for m_iPlayerState.

	// tells us whether or not this player gets money at the start of the next round.
	bool m_receivesMoneyNextRound;


	// Specific state handler functions.
	void State_Enter_WELCOME();
	void State_PreThink_WELCOME();

	void State_Enter_PICKINGTEAM();
	void State_Enter_PICKINGCLASS();

	void State_Enter_ACTIVE();
	void State_PreThink_ACTIVE();

	void State_Enter_OBSERVER_MODE();
	void State_Leave_OBSERVER_MODE();
	void State_PreThink_OBSERVER_MODE();

	void State_Enter_DEATH_WAIT_FOR_KEY();
	void State_PreThink_DEATH_WAIT_FOR_KEY();

	void State_Enter_DEATH_ANIM();
	void State_PreThink_DEATH_ANIM();

	int FlashlightIsOn( void );
	void FlashlightTurnOn( void );
	void FlashlightTurnOff( void );

	void UpdateAddonBits();
	void UpdateRadar();

public:

	void				SetDeathPose( const int &iDeathPose ) { m_iDeathPose = iDeathPose; }
	void				SetDeathPoseFrame( const int &iDeathPoseFrame ) { m_iDeathFrame = iDeathPoseFrame; }
	void				SetDeathPoseYaw( const float &flDeathPoseYaw ) { m_flDeathYaw = flDeathPoseYaw; }

	virtual void		IncrementFragCount( int nCount );
	virtual void		IncrementDeathCount( int nCount );
	virtual void		IncrementAssistsCount( int nCount );
	
	void				SelectDeathPose( const CTakeDamageInfo &info );

private:
	int	m_iDeathPose;
	int	m_iDeathFrame;
	float m_flDeathYaw;

// [menglish] Freeze cam function and variable declarations	 
	bool m_bAbortFreezeCam;

protected:
	void AttemptToExitFreezeCam( void );

public:

	CNetworkVar( bool, m_bIsWalking );
	// Predicted variables.
	CNetworkVar( bool, m_bResumeZoom );
	CNetworkVar( int , m_iLastZoom ); // after firing a shot, set the FOV to 90, and after showing the animation, bring the FOV back to last zoom level.
	CNetworkVar( bool, m_bIsDefusing );			// tracks whether this player is currently defusing a bomb
	CNetworkVar( bool, m_bIsGrabbingHostage );			// tracks whether this player is currently grabbing a hostage
	CNetworkVar( float, m_fImmuneToDamageTime );	// When gun game spawn damage immunity will expire
	CNetworkVar( bool, m_bImmunity );	// tracks whether this player is currently immune in gun game
	CNetworkVar( bool, m_bHasMovedSinceSpawn );		// Whether player has moved from spawn position
	int m_LastHitGroup;			// the last body region that took damage

	bool m_bIsFemale;

	float m_fNextMolotovDamageSoundTime;
	// [menglish] Adding two variables, keeping track of damage to the player
	int m_LastHitBox;			// the last body hitbox that took damage
	Vector m_vLastHitLocationObjectSpace; //position where last hit occured in space of the bone associated with the hitbox
	EHANDLE		m_hDroppedEquipment[DROPPED_COUNT];

	void OnHealthshotUsed( void ) { EmitSound( "Healthshot.Success" ); }

	// [tj] overriding the base suicides to trash CS specific stuff
	virtual void CommitSuicide( bool bExplode = false, bool bForce = false );
	virtual void CommitSuicide( const Vector &vecForce, bool bExplode = false, bool bForce = false );

    void WieldingKnifeAndKilledByGun( bool bState ) { m_bWieldingKnifeAndKilledByGun = bState; }
    bool WasWieldingKnifeAndKilledByGun() { return m_bWieldingKnifeAndKilledByGun; }

    // [dwenger] adding tracking for weapon used fun fact
    void PlayerUsedFirearm( CBaseCombatWeapon* pBaseWeapon );
	int GetNumFirearmsUsed() { return m_WeaponTypesUsed.Count(); }

	void ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName );

	CNetworkVar( bool, m_bHasHelmet );				// Does the player have helmet armor
	bool m_bEscaped;			// Has this terrorist escaped yet?

	// Other variables.
	bool m_bIsVIP;				// Are we the VIP?
	int m_iNumSpawns;			// Number of times player has spawned this round
	int m_iOldTeam;				// Keep what team they were last on so we can allow joining spec and switching back to their real team
	bool m_bTeamChanged;		// Just allow one team change per round
	CNetworkVar( int, m_iAccount );	// How much cash this player has.
	int m_iShouldHaveCash;
	
	bool m_bJustKilledTeammate;
	bool m_bPunishedForTK;
	int m_iTeamKills;
	float m_flLastMovement;
	int m_iNextTimeCheck;		// Next time the player can execute a "timeleft" command

	float m_flNameChangeHistory[NAME_CHANGE_HISTORY_SIZE]; // index 0 = most recent change

	bool CanChangeName( void );	// Checks if the player can change his name
	void ChangeName( const char *pszNewName );

	void SetClanTag( const char *pTag );
	const char *GetClanTag( void ) const;


	CNetworkVar( bool, m_bHasDefuser );			    // Does this player have a defuser kit?
	CNetworkVar( bool, m_bHasNightVision );		    // Does this player have night vision?
	CNetworkVar( bool, m_bNightVisionOn );		    // Is the NightVision turned on ?

    //=============================================================================
    // HPE_BEGIN:
    // [dwenger] Added for fun-fact support
    //=============================================================================

    //CNetworkVar( bool, m_bPickedUpDefuser );        // Did player pick up the defuser kit as opposed to buying it?
    //CNetworkVar( bool, m_bDefusedWithPickedUpKit);  // Did player defuse the bomb with a picked-up defuse kit?

    //=============================================================================
    // HPE_END
    //=============================================================================

	float m_flLastRadarUpdateTime;

	// last known navigation area of player - NULL if unknown
	CNavArea *m_lastNavArea;

	// Backup copy of the menu text so the player can change this and the menu knows when to update.
	char	m_MenuStringBuffer[MENU_STRING_BUFFER_SIZE];

	// When the player joins, it cycles their view between trigger_camera entities.
	// This is the current camera, and the time that we'll switch to the next one.
	EHANDLE m_pIntroCamera;
	float m_fIntroCamTime;

	// Set to true each frame while in a bomb zone.
	// Reset after prediction (in PostThink).
	CNetworkVar( bool, m_bInBombZone );
	CNetworkVar( bool, m_bInBuyZone );
	CNetworkVar( bool, m_bKilledByTaser );
	int m_iBombSiteIndex;

	bool m_bUseNewAnimstate;
	virtual void SetModel( const char *szModelName );

	virtual Vector Weapon_ShootPosition();

	CNetworkVar( int, m_iMoveState );		// Is the player trying to run?  Used for state transitioning after a player lands from a jump etc.

	bool IsInBuyZone();
	bool CanPlayerBuy( bool display );

	CNetworkVar( bool, m_bInHostageRescueZone );
	void RescueZoneTouch( inputdata_t &inputdata );

	CNetworkVar( float, m_flStamina );
	CNetworkVar( int, m_iDirection );	// The current lateral kicking direction; 1 = right,  0 = left
	CNetworkVar( int, m_iShotsFired );	// number of shots fired recently
	CNetworkVar( bool, m_bDuckOverride );	// number of shots fired recently

	// Make sure to register changes for armor.
	IMPLEMENT_NETWORK_VAR_FOR_DERIVED( m_ArmorValue );

	CNetworkVar( float, m_flVelocityModifier );
	CNetworkVar( float, m_flGroundAccelLinearFracLastTime );

	int	m_iHostagesKilled;

	void SetShieldDrawnState( bool bState );
	void DropShield( void );
	
	char m_szNewName [MAX_PLAYER_NAME_LENGTH]; // not empty if player requested a namechange
	char m_szClanTag[MAX_CLAN_TAG_LENGTH];

	Vector m_vecTotalBulletForce;	//Accumulator for bullet force in a single frame
	
	CNetworkVar( float, m_flFlashDuration );
	CNetworkVar( float, m_flFlashMaxAlpha );
	
	CNetworkVar( float, m_flProgressBarStartTime );
	CNetworkVar( int, m_iProgressBarDuration );
	CNetworkVar( int, m_iThrowGrenadeCounter );	// used to trigger grenade throw animations.

	CNetworkVar( float, m_flLowerBodyYawTarget );
	CNetworkVar( bool, m_bStrafing );
	
	// Tracks our ragdoll entity.
	CNetworkHandle( CBaseEntity, m_hRagdoll );	// networked entity handle 

	// Bots and hostages auto-duck during jumps
	bool m_duckUntilOnGround;

	Vector m_lastStandingPos; // used by the gamemovement code for finding ladders

	void SurpressLadderChecks( const Vector& pos, const Vector& normal );
	bool CanGrabLadder( const Vector& pos, const Vector& normal );

	void ClearImmunity( void );

	CNetworkVar( bool, m_bDetected );

	int m_iLoadoutSlotKnifeWeaponCT;
	int m_iLoadoutSlotKnifeWeaponT;
	int m_iLoadoutSlotAgentCT;
	int m_iLoadoutSlotAgentT;
	CNetworkVar( int, m_iLoadoutSlotGlovesCT );
	CNetworkVar( int, m_iLoadoutSlotGlovesT );

private:
	CountdownTimer m_ladderSurpressionTimer;
	Vector m_lastLadderNormal;
	Vector m_lastLadderPos;

protected:

	void CreateRagdollEntity();

	bool IsHittingShield( const Vector &vecDirection, trace_t *ptr );

	void PhysObjectSleep();
	void PhysObjectWake();

	bool RunMimicCommand( CUserCmd& cmd );

	bool SelectSpawnSpot( const char *pEntClassName, CBaseEntity* &pSpot );

	void SetModelFromClass( void );

public:
	CNetworkVar( int, m_iClass ); // One of the CS_CLASS_ enums.
	int m_iSkin;

	bool CSWeaponDrop( CBaseCombatWeapon *pWeapon, bool bDropShield = true, bool bThrow = false );
	bool CSWeaponDrop( CBaseCombatWeapon *pWeapon, Vector targetPos, bool bDropShield = true );

	bool HandleDropWeapon( CBaseCombatWeapon *pWeapon = NULL, bool bSwapping = false );

protected:
	void TransferInventory( CCSPlayer* pTargetPlayer );
	bool DropRifle( bool fromDeath = false );
	bool DropPistol( bool fromDeath = false );
	
    //=============================================================================
    // HPE_BEGIN:
    // [tj] Added a parameter so we know if it was death that caused the drop
	// [menglish] New parameter to always know if this is from death and not just an enemy death
    //=============================================================================
     
    void DropWeapons( bool fromDeath, bool friendlyFire );
     
    //=============================================================================
    // HPE_END
    //=============================================================================
    

	virtual int SpawnArmorValue( void ) const { return ArmorValue(); }

	BuyResult_e AttemptToBuyAmmo( int iAmmoType );
	BuyResult_e AttemptToBuyAmmoSingle( int iAmmoType );
	BuyResult_e AttemptToBuyVest( void );
	BuyResult_e AttemptToBuyAssaultSuit( void );
	BuyResult_e AttemptToBuyDefuser( void );
	BuyResult_e AttemptToBuyNightVision( void );
	BuyResult_e AttemptToBuyTaser( void );
	BuyResult_e AttemptToBuyShield( void );
	
	BuyResult_e BuyAmmo( int nSlot, bool bBlinkMoney );
	BuyResult_e BuyGunAmmo( CBaseCombatWeapon *pWeapon, bool bBlinkMoney );

	void PushawayThink();

private:

	IPlayerAnimState *m_PlayerAnimState;
	CCSGOPlayerAnimState *m_PlayerAnimStateCSGO;

	// Aiming heuristics code
	float						m_flIdleTime;		//Amount of time we've been motionless
	float						m_flMoveTime;		//Amount of time we've been in motion
	float						m_flLastDamageTime;	//Last time we took damage
	float						m_flTargetFindTime;

	int							m_lastDamageHealth;		// Last damage given to our health
	int							m_lastDamageArmor;		// Last damage given to our armor

    //=============================================================================
    // HPE_BEGIN:
    // [dwenger] Added for fun-fact support
    //=============================================================================

    bool                        m_bPickedUpDefuser;         // Did player pick up the defuser kit as opposed to buying it?
    bool                        m_bDefusedWithPickedUpKit;  // Did player defuse the bomb with a picked-up defuse kit?
	int							m_nPreferredGrenadeDrop;

    //=============================================================================
    // HPE_END
    //=============================================================================


	// Last usercmd we shot a bullet on.
	int m_iLastWeaponFireUsercmd;

	// Copyed from EyeAngles() so we can send it to the client.
	CNetworkQAngle( m_angEyeAngles );

	bool m_bVCollisionInitted;

	Vector m_storedSpawnPosition;
	QAngle m_storedSpawnAngle;

public:
	CNetworkVar( float, m_flThirdpersonRecoil );

// AutoBuy functions.
public:
	void			AutoBuy(); // this should take into account what the player can afford and should buy the best equipment for them.

	bool			IsInAutoBuy( void ) { return m_bIsInAutoBuy; }
	bool			IsInReBuy( void ) { return m_bIsInRebuy; }

private:
	bool			ShouldExecuteAutoBuyCommand(const AutoBuyInfoStruct *commandInfo, bool boughtPrimary, bool boughtSecondary);
	void			PostAutoBuyCommandProcessing(const AutoBuyInfoStruct *commandInfo, bool &boughtPrimary, bool &boughtSecondary);
	void			ParseAutoBuyString(const char *string, bool &boughtPrimary, bool &boughtSecondary);
	AutoBuyInfoStruct *GetAutoBuyCommandInfo(const char *command);
	void			PrioritizeAutoBuyString(char *autobuyString, const char *priorityString); // reorders the tokens in autobuyString based on the order of tokens in the priorityString.
	BuyResult_e	CombineBuyResults( BuyResult_e prevResult, BuyResult_e newResult );

	bool			m_bIsInAutoBuy;
	bool			m_bAutoReload;

//ReBuy functions

public:
	void			Rebuy();
private:
	void			BuildRebuyStruct();

	BuyResult_e	RebuyPrimaryWeapon();
	//BuyResult_e	RebuyPrimaryAmmo();
	BuyResult_e	RebuySecondaryWeapon();
	//BuyResult_e	RebuySecondaryAmmo();
	BuyResult_e	RebuyTaser();
	BuyResult_e	RebuyHEGrenade();
	BuyResult_e	RebuyFlashbang();
	BuyResult_e	RebuySmokeGrenade();
	BuyResult_e	RebuyDecoy();
	BuyResult_e	RebuyMolotov();
	BuyResult_e	RebuyDefuser();
	BuyResult_e	RebuyNightVision();
	BuyResult_e	RebuyArmor();

	bool			m_bIsInRebuy;
	RebuyStruct		m_rebuyStruct;
	bool			m_bUsingDefaultPistol;

	bool			m_bIsBeingGivenItem;

#ifdef CS_SHIELD_ENABLED
	CNetworkVar( bool, m_bHasShield );
	CNetworkVar( bool, m_bShieldDrawn );
#endif
	
	// This is a combination of the ADDON_ flags in cs_shareddefs.h.
	CNetworkVar( int, m_iAddonBits );

	// Clients don't know about holstered weapons, so we need to tell them the weapon type here
	CNetworkVar( int, m_iPrimaryAddon );
	CNetworkVar( int, m_iSecondaryAddon );
	CNetworkVar( int, m_iKnifeAddon );

//Damage record functions
public:

	static void	StartNewBulletGroup();	// global function

	void RecordDamage( CCSPlayer* damageDealer, CCSPlayer* damageTaker, int iDamageDealt, int iActualHealthRemoved );

	void ResetDamageCounters();	//Reset all lists

	void RemoveSelfFromOthersDamageCounters(); // Additional cleanup to damage counters when not in a round respawn mode.

	void OutputDamageTaken( void );
	void OutputDamageGiven( void );

	void StockPlayerAmmo( CBaseCombatWeapon *pNewWeapon = NULL );

	CUtlLinkedList< CDamageRecord *, int >& GetDamageList() { return m_DamageList; }

private:
	//A unified list of recorded damage that includes giver and taker in each entry
	CUtlLinkedList< CDamageRecord *, int >	m_DamageList;

protected:
	float m_applyDeafnessTime;
	int m_currentDeafnessFilter;

	bool m_isVIP;

// Command rate limiting.
private:

	bool ShouldRunRateLimitedCommand( const CCommand &args );

	// This lets us rate limit the commands the players can execute so they don't overflow things like reliable buffers.
	CUtlDict<float,int>	m_RateLimitLastCommandTimes;

	CNetworkVar(int, m_cycleLatch);	// Every so often, we are going to transmit our cycle to the client to correct divergence caused by PVS changes
	CountdownTimer m_cycleLatchTimer;

//=============================================================================
// HPE_BEGIN:
// [menglish, tj] Achievement-based addition to CS player class.
//=============================================================================
 
public:
	void ResetRoundBasedAchievementVariables();
	void OnRoundEnd(int winningTeam, int reason);
    void OnPreResetRound();

	int GetNumEnemyDamagers();
	int GetNumEnemiesDamaged();
	CBaseEntity* GetNearestSurfaceBelow(float maxTrace);

    // Returns the % of the enemies this player killed in the round
    int GetPercentageOfEnemyTeamKilled();

	//List of times of recent kills to check for sprees
	CUtlVector<float>			m_killTimes; 

    //List of all players killed this round
    CUtlVector<CHandle<CCSPlayer> >      m_enemyPlayersKilledThisRound;

	//List of weapons we have used to kill players with this round
	CUtlVector<int>				m_killWeapons; 

	int m_NumEnemiesKilledThisRound;
	int m_NumEnemiesAtRoundStart;
	int m_KillingSpreeStartTime;

	float m_firstKillBlindStartTime; //This is the start time of the blind effect during which we got our most recent kill.
	int m_killsWhileBlind;
 
    bool m_bIsRescuing;         // tracks whether this player is currently rescuing a hostage
    bool m_bInjuredAHostage;    // tracks whether this player injured a hostage
    int  m_iNumFollowers;       // Number of hostages following this player
	bool m_bSurvivedHeadshotDueToHelmet;

    void IncrementNumFollowers() { m_iNumFollowers++; }
    void DecrementNumFollowers() { m_iNumFollowers--; if (m_iNumFollowers < 0) m_iNumFollowers = 0; }
    int GetNumFollowers() { return m_iNumFollowers; }
    void SetIsRescuing(bool in_bRescuing) { m_bIsRescuing = in_bRescuing; }
    bool IsRescuing() { return m_bIsRescuing; }
    void SetInjuredAHostage(bool in_bInjured) { m_bInjuredAHostage = in_bInjured; }
    bool InjuredAHostage() { return m_bInjuredAHostage; }
    float GetBombPickuptime() { return m_bombPickupTime; }
    void SetBombPickupTime(float time) { m_bombPickupTime = time; }
    CCSPlayer* GetLastFlashbangAttacker() { return m_lastFlashBangAttacker; }
    void SetLastFlashbangAttacker(CCSPlayer* attacker) { m_lastFlashBangAttacker = attacker; }

	static CSWeaponID GetWeaponIdCausingDamange( const CTakeDamageInfo &info );
	static void ProcessPlayerDeathAchievements( CCSPlayer *pAttacker, CCSPlayer *pVictim, const CTakeDamageInfo &info );

    void                        OnCanceledDefuse();
    void                        OnStartedDefuse();
    GooseChaseAchievementStep   m_gooseChaseStep;
    DefuseDefenseAchivementStep m_defuseDefenseStep;
    CHandle<CCSPlayer>          m_pGooseChaseDistractingPlayer;

    int                         m_lastRoundResult; //save the reason for the last round ending.

    bool                        m_bMadeFootstepNoise;

    float                       m_bombPickupTime;

    bool                        m_bMadePurchseThisRound;

    int                         m_roundsWonWithoutPurchase;

	bool						m_bKilledDefuser;
	bool						m_bKilledRescuer;
	int							m_maxGrenadeKills;

	int							m_grenadeDamageTakenThisRound;

	bool						GetKilledDefuser() { return m_bKilledDefuser; } 
	bool						GetKilledRescuer() { return m_bKilledRescuer; } 
	int							GetMaxGrenadeKills() { return m_maxGrenadeKills; }

	void						CheckMaxGrenadeKills(int grenadeKills);

    CHandle<CCSPlayer>                  m_lastFlashBangAttacker;

    void	SetPlayerDominated( CCSPlayer *pPlayer, bool bDominated );    
    void	SetPlayerDominatingMe( CCSPlayer *pPlayer, bool bDominated );
    bool	IsPlayerDominated( int iPlayerIndex );
    bool	IsPlayerDominatingMe( int iPlayerIndex );

	bool	m_wasNotKilledNaturally; //Set if the player is dead from a kill command or late login

	bool	WasNotKilledNaturally() { return m_wasNotKilledNaturally; }

	// [menglish] MVP functions	 
	void	SetNumMVPs( int iNumMVP );
	void	IncrementNumMVPs( CSMvpReason_t mvpReason );
	int		GetNumMVPs();
	 
    void    RemoveNemesisRelationships();
	void	SetDeathFlags( int iDeathFlags ) { m_iDeathFlags = iDeathFlags; }
	int		GetDeathFlags() { return m_iDeathFlags; }

private:
    CNetworkArray( bool, m_bPlayerDominated, MAX_PLAYERS+1 );		// array of state per other player whether player is dominating other players
    CNetworkArray( bool, m_bPlayerDominatingMe, MAX_PLAYERS+1 );	// array of state per other player whether other players are dominating this player

	CNetworkVar( bool, m_bIsLookingAtWeapon );
	CNetworkVar( bool, m_bIsHoldingLookAtWeapon );

	float m_flLookWeaponEndTime;

    // [menglish] number of rounds this player has caused to be won for their team
	int m_iMVPs;

    // [dwenger] adding tracking for fun fact
    bool m_bWieldingKnifeAndKilledByGun;

    // [dwenger] adding tracking for which weapons this player has used in a round
    CUtlVector<CSWeaponID> m_WeaponTypesUsed; 

	int m_iDeathFlags; // Flags holding revenge and domination info about a death

#if CS_CONTROLLABLE_BOTS_ENABLED
public:
	bool IsAbleToInstantRespawn( void );

	bool CanControlBot( CCSBot *pBot ,bool bSkipTeamCheck = false );
	bool TakeControlOfBot( CCSBot *pBot, bool bSkipTeamCheck = false );
	void ReleaseControlOfBot( void );
	CCSBot* FindNearestControllableBot( bool bMustBeValidObserverTarget );
	bool IsControllingBot( void )							const { return m_bIsControllingBot; }

	bool HasControlledBot( void )						const { return m_hControlledBot.Get() != NULL; }
	CCSPlayer* GetControlledBot( void )				const { return static_cast<CCSPlayer*>(m_hControlledBot.Get()); }
	void SetControlledBot( CCSPlayer* pOther )	      { m_hControlledBot = pOther; }

	bool HasControlledByPlayer( void )					const { return m_hControlledByPlayer.Get() != NULL; }
	CCSPlayer* GetControlledByPlayer( void )				const { return static_cast<CCSPlayer*>(m_hControlledByPlayer.Get()); }
	void SetControlledByPlayer( CCSPlayer* pOther )	      { m_hControlledByPlayer = pOther; }

	bool HasBeenControlledThisRound( void ) { return m_bHasBeenControlledByPlayerThisRound; }
	bool HasControlledBotThisRound( void ) {return m_bHasControlledBotThisRound;}



private:
	CNetworkVar( bool, m_bIsControllingBot );	// Are we controlling a bot? 
	// Note that this can be TRUE even if GetControlledPlayer() returns NULL, 
	// IFF we started controlling a bot and then the bot was deleted for some reason. 

	CNetworkVar( bool, m_bCanControlObservedBot );	// set to true if we can take control of the bot we are observing, for client UI feedback. 

	CNetworkVar( int, m_iControlledBotEntIndex);	// Are we controlling a bot? 

	CHandle<CCSPlayer> m_hControlledBot;		// The is the OTHER player that THIS player is controlling
	CHandle<CCSPlayer> m_hControlledByPlayer;	// This is the OTHER player that is controlling THIS player
	bool m_bHasBeenControlledByPlayerThisRound;
	CNetworkVar( bool, m_bHasControlledBotThisRound );


	// Various values from this character before they took control or were controlled
	struct PreControlData
	{
		int m_iClass;		// CS class (such as CS_CLASS_PHOENIX_CONNNECTION or CS_CLASS_SEAL_TEAM_6)
		int m_iSkin;		// CS class skin
		int m_iAccount;		// money
		int m_iFrags;		// kills / score
		int m_iAssists;
		int m_iDeaths;
	};

	void SavePreControlData();

public:

	PreControlData	m_PreControlData;

#endif // #if CS_CONTROLLABLE_BOTS_ENABLED

private:
	// override for weapon driving animations
	bool UpdateLayerWeaponDispatch( CAnimationLayer *pLayer, int iSequence );
public:
	virtual float	GetLayerSequenceCycleRate( CAnimationLayer *pLayer, int iSequence );

};


inline CSPlayerState CCSPlayer::State_Get() const
{
	return m_iPlayerState;
}

inline CCSPlayer *ToCSPlayer( CBaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsPlayer() )
		return NULL;

	return dynamic_cast<CCSPlayer*>( pEntity );
}

inline bool CCSPlayer::IsReloading( void ) const
{
	CBaseCombatWeapon *gun = GetActiveWeapon();
	if (gun == NULL)
		return false;

	return gun->m_bInReload;
}

inline bool CCSPlayer::IsProtectedByShield( void ) const
{ 
	return HasShield() && IsShieldDrawn();
}

inline bool CCSPlayer::IsBlind( void ) const
{ 
	return gpGlobals->curtime < m_blindUntilTime;
}

//=============================================================================
// HPE_BEGIN
// [sbodenbender] Need a different test for player blindness for the achievements
//=============================================================================
inline bool CCSPlayer::IsBlindForAchievement()
{
	return (m_blindStartTime + m_flFlashDuration) > gpGlobals->curtime;
}
//=============================================================================
// HPE_END
//=============================================================================

inline bool CCSPlayer::IsAutoFollowAllowed( void ) const		
{ 
	return (gpGlobals->curtime > m_allowAutoFollowTime); 
}

inline void CCSPlayer::InhibitAutoFollow( float duration )	
{ 
	m_allowAutoFollowTime = gpGlobals->curtime + duration; 
}

inline void CCSPlayer::AllowAutoFollow( void )	
{ 
	m_allowAutoFollowTime = 0.0f; 
}

inline int CCSPlayer::GetClass( void ) const
{
	return m_iClass;
}

inline const char *CCSPlayer::GetClanTag( void ) const
{
	return m_szClanTag;
}

#endif	//CS_PLAYER_H
