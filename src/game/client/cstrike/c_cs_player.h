//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#ifndef C_CS_PLAYER_H
#define C_CS_PLAYER_H
#ifdef _WIN32
#pragma once
#endif


#include "cs_playeranimstate.h"
#include "c_baseplayer.h"
#include "cs_shareddefs.h"
#include "weapon_csbase.h"
#include "baseparticleentity.h"
#include "beamdraw.h"
#include "cs_gamerules.h"
#include "csgo_playeranimstate.h"

#include "cs_player_shared.h"

class C_PhysicsProp;

extern ConVar cl_disablefreezecam;


#define BONESNAPSHOT_ENTIRE_BODY 0
#define BONESNAPSHOT_UPPER_BODY 1

#define BONESNAPSHOT_DECAYDURATION_MIN 0
#define BONESNAPSHOT_DECAYDURATION_MAX 0.4f

class CBoneSnapshot
{
public:
	void			Update( CBaseAnimating* pEnt, bool bReadOnly = false );	
	inline void		UpdateReadOnly( void ) { Update( m_pEnt, true ); }

	void			SetShouldCapture( void )					{ m_bCapturePending = true; }

	void SetShouldCapture( float flNewDecayDuration )
	{
		m_bCapturePending = true;
		m_flDecayIdealDuration = clamp( flNewDecayDuration, BONESNAPSHOT_DECAYDURATION_MIN, BONESNAPSHOT_DECAYDURATION_MAX );
		m_flDecayStartTime = gpGlobals->curtime;
		m_flDecayEndTime = m_flDecayStartTime + m_flDecayIdealDuration;
	}

	bool			IsCapturePending( void )	{ return (m_bEnabled && m_bCapturePending); }

	void			Enable( void )				{ if ( !m_bEnabled ) { AbandonAnyPending(); } m_bEnabled = true; }
	void			Disable( void )				{ if ( m_bEnabled ) { AbandonAnyPending(); } m_bEnabled = false; }

	float			GetCurrentWeight( void )	{ return m_flWeight; }
	
	void			SetLastBoneSetupTimeIndex( void ) { m_flLastBoneSetupTimeIndex = gpGlobals->curtime; }
	
	void Init( void )
	{
		m_pEnt = NULL;
		m_flWeight = 0;
		memset( m_Weightlist, 0, ARRAYSIZE(m_Weightlist) );
		m_bWeightlistInitialized = false;
		m_bEnabled = false;
		m_bCapturePending = false;
		m_vecSubordinateSnapshots.RemoveAll();
		m_flDecayIdealDuration = 0.25f;
		m_flDecayStartTime = 0;
		m_flDecayEndTime = 0;
		m_vecWorldCapturePos.Init();
		m_flLastBoneSetupTimeIndex = 0;
		m_nStudioRenderHdrId = -1;
	}

	void Init( CBaseAnimating* pEnt )			{ Init(); m_pEnt = pEnt; }
	CBoneSnapshot() { Init(); }

	void AddSubordinate( CBoneSnapshot* pSnapshot )
	{
		if ( !m_vecSubordinateSnapshots.HasElement(pSnapshot) )
		{
			m_vecSubordinateSnapshots[ m_vecSubordinateSnapshots.AddToTail() ] = pSnapshot;
		}
	}

	void AbandonAnyPending( void )
	{
		m_flWeight = 0;
		m_bCapturePending = false;
	}

	void SetWeightListName( const char* szWeightListName ) { m_szWeightlistName = szWeightListName; }

private:

	CBaseAnimating* m_pEnt;
	float			m_flWeight;
	matrix3x4_t		m_Cache[ MAXSTUDIOBONES ];
	const char*		m_szWeightlistName;
	float			m_Weightlist[ MAXSTUDIOBONES ];
	bool			m_bWeightlistInitialized;
	bool			m_bEnabled;
	bool			m_bCapturePending;
	float			m_flDecayIdealDuration;
	float			m_flDecayStartTime;
	float			m_flDecayEndTime;
	Vector			m_vecWorldCapturePos;
	float			m_flLastBoneSetupTimeIndex;
	int				m_nStudioRenderHdrId;

	CUtlVector<CBoneSnapshot*> m_vecSubordinateSnapshots;

	void			CaptureSnapshot( void );
	void			PlaybackSnapshot( void );
	void			InitWeightList( void );
	bool			IsBoneSetupTimeIndexRecent( void ) { return (gpGlobals->curtime - m_flLastBoneSetupTimeIndex) <= 0.25f; }
};


class CAddonModel
{
public:
	CHandle<C_BaseAnimating> m_hEnt;	// The model for the addon.
	int m_iAddon;						// One of the ADDON_ bits telling which model this is.
	int m_iAttachmentPoint;				// Which attachment point on the player model this guy is on.
};



class C_CSPlayer : public C_BasePlayer, public ICSPlayerAnimStateHelpers
{
public:
	DECLARE_CLASS( C_CSPlayer, C_BasePlayer );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_INTERPOLATION();

	C_CSPlayer();
	~C_CSPlayer();

	virtual	void Spawn( void );
	virtual void SetSequence( int nSequence ) OVERRIDE;

	virtual void Simulate();
	virtual void OnSetDormant( bool bDormant );

	void GiveCarriedHostage( EHANDLE hHostage );
	void RefreshCarriedHostage( bool bForceCreate );
	void RemoveCarriedHostage();

	bool HasDefuser() const;

	void GiveDefuser();
	void RemoveDefuser();

	bool HasNightVision() const;

	static C_CSPlayer* GetLocalCSPlayer();
	CSPlayerState State_Get() const;

	virtual float GetMinFOV() const;

	// Get how much $$$ this guy has.
	int GetAccount() const;

	// Returns one of the CS_CLASS_ enums.
	int PlayerClass() const;

	bool IsInBuyZone();
	bool CanShowTeamMenu() const;	// Returns true if we're allowed to show the team menu right now.

	// Get the amount of armor the player has.
	int ArmorValue() const;
	bool HasHelmet() const;
	int GetCurrentAssaultSuitPrice();

#if USE_TRACERS
	// create tracers
	void CreateWeaponTracer( Vector vecStart, Vector vecEnd );
#endif

	virtual const QAngle& EyeAngles();
	virtual const QAngle& GetRenderAngles();
	virtual void CalcObserverView( Vector& eyeOrigin, QAngle& eyeAngles, float& fov );

	virtual void			GetRenderBounds( Vector& theMins, Vector& theMaxs );
	virtual void			GetShadowRenderBounds( Vector &mins, Vector &maxs, ShadowType_t shadowType );
	virtual bool			GetShadowCastDirection( Vector *pDirection, ShadowType_t shadowType ) const;

	virtual void VPhysicsUpdate( IPhysicsObject *pPhysics );

	// Get the ID target entity index. The ID target is the player that is behind our crosshairs, used to
	// display the player's name.
	int GetIDTarget() const;

	virtual void NotifyShouldTransmit( ShouldTransmitState_t state );
	virtual void ClientThink();

	virtual void OnDataChanged( DataUpdateType_t type );
	virtual void PostDataUpdate( DataUpdateType_t updateType );
	virtual bool Interpolate( float currentTime );
	virtual void UpdateStepSound( surfacedata_t *psurface, const Vector &vecOrigin, const Vector &vecVelocity  );
	virtual surfacedata_t * GetFootstepSurface( const Vector &origin, const char *surfaceName );
	virtual void PlayClientJumpSound( void );
	virtual void ValidateModelIndex( void );

	virtual int	GetMaxHealth() const;

	bool		Weapon_CanSwitchTo(C_BaseCombatWeapon *pWeapon);

	virtual void UpdateClientSideAnimation();
	virtual void ProcessMuzzleFlashEvent();
	void HandleTaserAnimation();

	virtual const Vector& GetRenderOrigin( void );

	bool CreateMove( float flInputSampleTime, CUserCmd *pCmd );

	CUtlVector< C_BaseParticleEntity* > m_SmokeGrenades;

	virtual bool ShouldDraw( void );
	virtual bool UpdateDispatchLayer( CAnimationLayer *pLayer, CStudioHdr *pWeaponStudioHdr, int iSequence ) OVERRIDE;
	virtual void AccumulateLayers( IBoneSetup &boneSetup, Vector pos[], Quaternion q[], float currentTime );
	bool UpdateLayerWeaponDispatch( C_AnimationLayer *pLayer, int iSequence );
	virtual float GetLayerSequenceCycleRate( C_AnimationLayer *pLayer, int iSequence );
	virtual void BuildTransformations( CStudioHdr *pStudioHdr, Vector *pos, Quaternion q[], const matrix3x4_t& cameraTransform, int boneMask, CBoneBitList &boneComputed );

	virtual void DoExtraBoneProcessing( CStudioHdr *pStudioHdr, Vector pos[], Quaternion q[], matrix3x4_t boneToWorld[], CBoneBitList &boneComputed, CIKContext *pIKContext ) OVERRIDE;
	
	virtual bool SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime );

	void ReevauluateAnimLOD( int boneMask = 0 );

	virtual C_BaseAnimating * BecomeRagdollOnClient();
	virtual IRagdoll* GetRepresentativeRagdoll() const;

	void ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName );

	// Have this player play the sounds from his view model's reload animation.
	void PlayReloadEffect();

	virtual void FireEvent( const Vector& origin, const QAngle& angles, int event, const char *options );

	virtual void DropPhysicsMag( const char *options ) OVERRIDE;

	bool		 HasC4( void );

	virtual void CreateLightEffects( void ) {}	//no dimlight effects

	// Sometimes the server wants to update the client's cycle to get the two to run in sync (for proper hit detection)
	virtual void SetServerIntendedCycle( float intended ) { m_serverIntendedCycle = intended; }
	virtual float GetServerIntendedCycle( void ) { return m_serverIntendedCycle; }

	virtual bool IsLookingAtWeapon( void ) const { return m_bIsLookingAtWeapon; }
	virtual bool IsHoldingLookAtWeapon( void ) const { return m_bIsHoldingLookAtWeapon; }

	virtual int DrawModel( int flags );

	virtual bool ShouldReceiveProjectedTextures( int flags )
	{
		return ( this != C_BasePlayer::GetLocalPlayer() );
	}

	void ClearSoundEvents()
	{
		m_SoundEvents.RemoveAll();
	}

	CsMusicType_t GetCurrentMusic() { return m_nCurrentMusic; } const

	void SetCurrentMusic( CsMusicType_t nMusicType )
	{
		m_nCurrentMusic = nMusicType;
 		if( nMusicType == CSMUSIC_START )
 		{
			m_flMusicRoundStartTime = gpGlobals->curtime;
		}
		m_flCurrentMusicStartTime = gpGlobals->curtime;
	}

	float GetCurrentMusicElapsed()
	{
		return  gpGlobals->curtime - m_flCurrentMusicStartTime;
	}

	float GetMusicStartRoundElapsed()
	{
		return  gpGlobals->curtime - m_flMusicRoundStartTime;
	}

	// [menglish] Returns whether this player is dominating or is being dominated by the specified player
	bool IsPlayerDominated( int iPlayerIndex );
	bool IsPlayerDominatingMe( int iPlayerIndex );

	virtual void CalcFreezeCamView( Vector& eyeOrigin, QAngle& eyeAngles, float& fov );

	virtual float GetDeathCamInterpolationTime();

	AcquireResult::Type CanAcquire( CSWeaponID weaponId, AcquireMethod::Type acquireMethod );
	int					GetCarryLimit( CSWeaponID weaponId );

	bool IsOtherEnemy( CCSPlayer *pPlayer );
	bool IsOtherEnemy( int nEntIndex );

	virtual void SetModelPointer( const model_t *pModel );


// Called by shared code.
public:

	// IPlayerAnimState overrides.
	virtual CWeaponCSBase* CSAnim_GetActiveWeapon();
	virtual bool CSAnim_CanMove();

	// View model prediction setup
	virtual void		CalcView( Vector& eyeOrigin, QAngle& eyeAngles, float& zNear, float& zFar, float& fov );


	void DoAnimationEvent( PlayerAnimEvent_t event, int nData = 0 );


// Implemented in shared code.
public:
	virtual float GetPlayerMaxSpeed();

	bool GetUseConfigurationForHighPriorityUseEntity( CBaseEntity *pEntity, CConfigurationForHighPriorityUseEntity_t &cfg );
	CBaseEntity *GetUsableHighPriorityEntity( void );

	void GetBulletTypeParameters(
		int iBulletType,
		float &fPenetrationPower,
		float &flPenetrationDistance );

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

	// Returns true if the player is allowed to move.
	bool CanMove() const;

	void OnJump( float fImpulse );
	void OnLand( float fVelocity );

	bool HasC4() const;	// Is this player carrying a C4 bomb?
	bool IsVIP() const;	// Is this player the VIP?

	virtual void SetAnimation( PLAYER_ANIM playerAnim );


public:

	void UpdateIDTarget( void );
	void RemoveAddonModels( void );
	void UpdateMinModels( void );

	void SetActivity( Activity eActivity );
	Activity GetActivity( void ) const;

	void DoAnimStateEvent( PlayerAnimEvent_t evt );

	// Global/static methods
	virtual void ThirdPersonSwitch( bool bThirdperson );

public:

	IPlayerAnimState *m_PlayerAnimState;
	CCSGOPlayerAnimState *m_PlayerAnimStateCSGO;

	virtual Vector Weapon_ShootPosition();

	// Used to control animation state.
	Activity m_Activity;

	CNetworkVar( bool, m_bIsWalking );
	// Predicted variables.
	CNetworkVar( bool, m_bResumeZoom );
	CNetworkVar( int , m_iLastZoom ); // after firing a shot, set the FOV to 90, and after showing the animation, bring the FOV back to last zoom level.
	CNetworkVar( CSPlayerState, m_iPlayerState );	// SupraFiend: this gives the current state in the joining process, the states are listed above
	CNetworkVar( bool, m_bIsDefusing );			// tracks whether this player is currently defusing a bomb
	CNetworkVar( bool, m_bIsGrabbingHostage );	// tracks whether this player is currently grabbing a hostage
	CNetworkVar( bool, m_bHasMovedSinceSpawn ); // Whether player has moved from spawn position
	CNetworkVar( float, m_fImmuneToDamageTime );	// When gun game spawn damage immunity will expire
	CNetworkVar( bool, m_bImmunity );	// tracks whether this player is currently immune in gun game
	CNetworkVar( bool, m_bInBombZone );
	CNetworkVar( bool, m_bInBuyZone );
	CNetworkVar( int, m_iThrowGrenadeCounter );	// used to trigger grenade throw animations.

	CNetworkVar( bool, m_bKilledByTaser );
	CNetworkVar( int, m_iMoveState );		// Is the player trying to run or walk or idle?  Tells us what the player is "trying" to do.

	CNetworkVar( float, m_flLowerBodyYawTarget );
	CNetworkVar( bool, m_bStrafing );

	bool m_bUseNewAnimstate;

	CBoneSnapshot m_boneSnapshots[2];
	bool IsAnyBoneSnapshotPending( void );

	float m_flLastSpawnTimeIndex;

	const PlayerViewmodelArmConfig *m_pViewmodelArmConfig;

	bool IsInHostageRescueZone( void );

	virtual void NotifyOnLayerChangeSequence( const CAnimationLayer* pLayer, const int nNewSequence ) OVERRIDE;
	virtual void NotifyOnLayerChangeWeight( const CAnimationLayer* pLayer, const float flNewWeight ) OVERRIDE;
	virtual void NotifyOnLayerChangeCycle( const CAnimationLayer* pLayer, const float flNewCycle ) OVERRIDE;

	// This is a combination of the ADDON_ flags in cs_shareddefs.h.
	CNetworkVar( int, m_iAddonBits );

	// Clients don't know about holstered weapons, so we need to be told about them here
	CNetworkVar( int, m_iPrimaryAddon );
	CNetworkVar( int, m_iSecondaryAddon );
	CNetworkVar( int, m_iKnifeAddon );

	// How long the progress bar takes to get to the end. If this is 0, then the progress bar
	// should not be drawn.
	CNetworkVar( int, m_iProgressBarDuration );

	// When the progress bar should start.
	CNetworkVar( float, m_flProgressBarStartTime );

	CNetworkVar( bool, m_bDuckOverride ); // force the player to duck regardless of if they're holding crouch

	CNetworkVar( float, m_flStamina );
	CNetworkVar( int, m_iDirection );	// The current lateral kicking direction; 1 = right,  0 = left
	CNetworkVar( int, m_iShotsFired );	// number of shots fired recently
	CNetworkVar( bool, m_bNightVisionOn );
	CNetworkVar( bool, m_bHasNightVision );

    // [dwenger] Added for fun-fact support
    //CNetworkVar( bool, m_bPickedUpDefuser );
    //CNetworkVar( bool, m_bDefusedWithPickedUpKit );

	CNetworkVar( float, m_flVelocityModifier );
	CNetworkVar( float, m_flGroundAccelLinearFracLastTime );

	CNetworkVar( bool, m_bNeedToChangeGloves );

	bool		m_bDetected;

	EHANDLE	m_hRagdoll;

	EHANDLE	m_hCarriedHostage;
	EHANDLE	m_hCarriedHostageProp;
	bool	m_bPlayingHostageCarrySound;

	CWeaponCSBase* GetActiveCSWeapon() const;
	CWeaponCSBase* GetCSWeapon( CSWeaponID id ) const;

	virtual ShadowType_t		ShadowCastType();

#ifdef CS_SHIELD_ENABLED
	bool HasShield( void ) { return m_bHasShield; }
	bool IsShieldDrawn( void ) { return m_bShieldDrawn;	}
	void SetShieldDrawnState( bool bState ) { m_bShieldDrawn = bState; }
#else
	bool HasShield( void ) { return false; }
	bool IsShieldDrawn( void ) { return false; }
	void SetShieldDrawnState( bool bState ) {}
#endif

	float m_flNightVisionAlpha;

	float m_flFlashAlpha;
	float m_flFlashBangTime;
	CNetworkVar( float, m_flFlashMaxAlpha );
	CNetworkVar( float, m_flFlashDuration );

	// Having the RecvProxy in the player allows us to keep the var private
	static void RecvProxy_CycleLatch( const CRecvProxyData *pData, void *pStruct, void *pOut );

	// Bots and hostages auto-duck during jumps
	bool m_duckUntilOnGround;

	Vector m_lastStandingPos; // used by the gamemovement code for finding ladders

	void SurpressLadderChecks( const Vector& pos, const Vector& normal );
	bool CanGrabLadder( const Vector& pos, const Vector& normal );

// [tj] checks if this player has another given player on their Steam friends list.
	bool HasPlayerAsFriend( C_CSPlayer* player );

	bool IsAbleToInstantRespawn( void );

private:
	CountdownTimer m_ladderSurpressionTimer;
	Vector m_lastLadderNormal;
	Vector m_lastLadderPos;

	void UpdateRadar();
	void UpdateSoundEvents();

	void CreateAddonModel( int i );
	void UpdateAddonModels();
	void UpdateHostageCarryModels();

	void PushawayThink();

	void FireGameEvent( IGameEvent *event );

	int		m_iAccount;
	bool	m_bHasHelmet;

public:
	int		m_iClass;

private:
	int		m_ArmorValue;
	QAngle	m_angEyeAngles;
	bool	m_bHasDefuser;
	bool	m_bInHostageRescueZone;
	float	m_fNextThinkPushAway;

    bool    m_bPlayingFreezeCamSound;

#ifdef CS_SHIELD_ENABLED
	bool	m_bHasShield;
	bool	m_bShieldDrawn;
#endif

	Vector m_vecRagdollVelocity;

	CInterpolatedVar< QAngle >	m_iv_angEyeAngles;

	// ID Target
	int					m_iIDEntIndex;
	CountdownTimer		m_delayTargetIDTimer;

	// Show the ID target after the cursor leaves the entity
	int					m_iOldIDEntIndex;
	CountdownTimer		m_holdTargetIDTimer;

	void ReleaseFlashlight( void );
	Beam_t	*m_pFlashlightBeam;

	class CCSSoundEvent
	{
	public:
		string_t m_SoundName;
		float m_flEventTime;	// Play the event when gpGlobals->curtime goes past this.
	};
	CUtlLinkedList<CCSSoundEvent,int> m_SoundEvents;

	// manage per play music
	CsMusicType_t m_nCurrentMusic;
	float m_flCurrentMusicStartTime;
	float m_flMusicRoundStartTime;

	// This is the list of addons hanging off the guy (grenades, C4, nightvision, etc).
	CUtlLinkedList<CAddonModel, int> m_AddonModels;
	int m_iLastAddonBits;
	int m_iLastPrimaryAddon;
	int m_iLastSecondaryAddon;
	int m_iLastKnifeAddon;

	int m_cycleLatch;				// server periodically updates this to fix up our anims, here it is a 4 bit fixed point
	float m_serverIntendedCycle;	// server periodically updates this to fix up our anims, here it is the float we want, or -1 for no override


    // [tj] Network variables that track who are dominating and being dominated by
    CNetworkArray( bool, m_bPlayerDominated, MAX_PLAYERS+1 );		// array of state per other player whether player is dominating other players
    CNetworkArray( bool, m_bPlayerDominatingMe, MAX_PLAYERS+1 );	// array of state per other player whether other players are dominating this player

	CNetworkVar( bool, m_bIsLookingAtWeapon );
	CNetworkVar( bool, m_bIsHoldingLookAtWeapon );

public:
	CNetworkVar( int, m_iLoadoutSlotGlovesCT );
	CNetworkVar( int, m_iLoadoutSlotGlovesT );

	float m_flThirdpersonRecoil;

	// taser items	
	float m_nextTaserShakeTime;
	float m_firstTaserShakeTime;

	C_CSPlayer( const C_CSPlayer & );

public:

	virtual bool	GetAttachment( int number, Vector &origin );
	virtual	bool	GetAttachment( int number, Vector &origin, QAngle &angles );

	bool	IsBotOrControllingBot();

#if CS_CONTROLLABLE_BOTS_ENABLED
	bool IsControllingBot()							const { return m_bIsControllingBot; }
	bool CanControlObservedBot()					const { return m_bCanControlObservedBot; }

	int GetControlledBotIndex()						const { return m_iControlledBotEntIndex; }

private:
	bool		m_bIsControllingBot;	// Are we controlling a bot? 
	// Note that this can be TRUE even if GetControlledPlayer() returns NULL, IFF we started controlling a bot and then the bot was deleted for some reason. 
	bool		m_bCanControlObservedBot;	// True if the player can control the bot s/he is observing
	int			m_iControlledBotEntIndex;

	CNetworkVar( bool, m_bHasControlledBotThisRound );
#endif

private:
	float		m_flNextMagDropTime;
	int			m_nLastMagDropAttachmentIndex;

public:
	Vector		m_vecLastAliveLocalVelocity;
};

C_CSPlayer* GetLocalOrInEyeCSPlayer( void );

inline C_CSPlayer *ToCSPlayer( CBaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsPlayer() )
		return NULL;

	return dynamic_cast<C_CSPlayer*>( pEntity );
}

namespace vgui
{
	class IImage;
}

vgui::IImage* GetDefaultAvatarImage( C_BasePlayer *pPlayer );

bool LineGoesThroughSmoke( Vector from, Vector to, bool grenadeBloat );




#endif // C_CS_PLAYER_H
