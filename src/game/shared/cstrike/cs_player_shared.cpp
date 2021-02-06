//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "weapon_csbase.h"
#include "decals.h"
#include "cs_gamerules.h"
#include "weapon_c4.h"
#include "in_buttons.h"
#include "datacache/imdlcache.h"

#ifdef CLIENT_DLL
	#include "c_cs_player.h"
	#include "c_cs_playerresource.h"
	#include "c_cs_hostage.h"
	#include "c_plantedc4.h"

	#define CCSPlayerResource C_CS_PlayerResource
#else
	#include "cs_player.h"
	#include "soundent.h"
	#include "bot/cs_bot.h"
	#include "KeyValues.h"
	#include "triggers.h"
	#include "cs_gamestats.h"
	#include "cs_simple_hostage.h"
	#include "predicted_viewmodel.h"
	//#include "cs_player_resource.h"
	#include "cs_simple_hostage.h"
#endif

#include "tier0/vprof.h"

#include "cs_playeranimstate.h"
#include "basecombatweapon_shared.h"
#include "util_shared.h"
#include "takedamageinfo.h"
#include "effect_dispatch_data.h"
#include "engine/ivdebugoverlay.h"
#include "obstacle_pushaway.h"
#include "props_shared.h"
#include "ammodef.h"
#include "cs_loadout.h"

ConVar sv_showimpacts("sv_showimpacts", "0", FCVAR_REPLICATED, "Shows client (red) and server (blue) bullet impact point (1=both, 2=client-only, 3=server-only)" );
ConVar sv_showplayerhitboxes( "sv_showplayerhitboxes", "0", FCVAR_REPLICATED, "Show lag compensated hitboxes for the specified player index whenever a player fires." );

extern ConVar mp_respawn_on_death_ct;
extern ConVar mp_respawn_on_death_t;

#define	CS_MASK_SHOOT (MASK_SOLID|CONTENTS_DEBRIS)
#define MAX_PENETRATION_DISTANCE 90 // this is 7.5 feet

#define CS_MAX_WALLBANG_TRAIL_LENGTH 800

void DispatchEffect( const char *pName, const CEffectData &data );

#if defined( CLIENT_DLL )
#define CPlantedC4 C_PlantedC4
#endif


#ifdef _DEBUG

	// This is some extra code to collect weapon accuracy stats:

	struct bulletdata_s
	{
		float	timedelta;	// time delta since first shot of this round
		float	derivation;	// derivation for first shoot view angle
		int		count;
	};

	#define STATS_MAX_BULLETS	50

	static bulletdata_s s_bullet_stats[STATS_MAX_BULLETS];

	Vector	s_firstImpact = Vector(0,0,0);
	float	s_firstTime = 0;
	float	s_LastTime = 0;
	int		s_bulletCount = 0;

	void ResetBulletStats()
	{
		s_firstTime = 0;
		s_LastTime = 0;
		s_bulletCount = 0;
		s_firstImpact = Vector(0,0,0);
		Q_memset( s_bullet_stats, 0, sizeof(s_bullet_stats) );
	}

	void PrintBulletStats()
	{
		for (int i=0; i<STATS_MAX_BULLETS; i++ )
		{
			if (s_bullet_stats[i].count == 0)
				break;

			Msg("%3i;%3i;%.4f;%.4f\n", i, s_bullet_stats[i].count,
				s_bullet_stats[i].timedelta, s_bullet_stats[i].derivation );
		}
	}

	void AddBulletStat( float time, float dist, Vector &impact )
	{
		if ( time > s_LastTime + 2.0f )
		{
			// time delta since last shoot is bigger than 2 seconds, start new row
			s_LastTime = s_firstTime = time;
			s_bulletCount = 0;
			s_firstImpact = impact;

		}
		else
		{
			s_LastTime = time;
			s_bulletCount++;
		}

		if ( s_bulletCount >= STATS_MAX_BULLETS )
			s_bulletCount = STATS_MAX_BULLETS -1;

		if ( dist < 1 )
			dist = 1;

		int i = s_bulletCount;

		float offset = VectorLength( s_firstImpact - impact );

		float timedelta = time - s_firstTime;
		float derivation = offset / dist;

		float weight = (float)s_bullet_stats[i].count/(float)(s_bullet_stats[i].count+1);

		s_bullet_stats[i].timedelta *= weight;
		s_bullet_stats[i].timedelta += (1.0f-weight) * timedelta;

		s_bullet_stats[i].derivation *= weight;
		s_bullet_stats[i].derivation += (1.0f-weight) * derivation;

		s_bullet_stats[i].count++;
	}

	CON_COMMAND( stats_bullets_reset, "Reset bullet stats")
	{
		ResetBulletStats();
	}

	CON_COMMAND( stats_bullets_print, "Print bullet stats")
	{
		PrintBulletStats();
	}

#endif

Vector CCSPlayer::Weapon_ShootPosition()
{
	Vector vecPos = BaseClass::Weapon_ShootPosition();

	// fail out to un-altered position
	if ( !m_bUseNewAnimstate || !m_PlayerAnimStateCSGO )
		return vecPos;

	// warning: the modify eye position call will query and set up bones
	// on the game server it is called when giving weapon items or firing bullets
	m_PlayerAnimStateCSGO->ModifyEyePosition( vecPos );

	return vecPos;
}

bool CCSPlayer::IsAbleToInstantRespawn( void )
{
	if ( CSGameRules() )
	{
		if ( CSGameRules()->IsWarmupPeriod() )
			return true;
	}

	// if we use respawn waves AND the next respawn wave is past AND our team is able to respawn OR it is the warmup period
	return (	CSGameRules() && ( ( mp_respawn_on_death_ct.GetBool() && GetTeamNumber() == TEAM_CT ) || 
		( mp_respawn_on_death_t.GetBool() && GetTeamNumber() == TEAM_TERRORIST ) ) );
}

float CCSPlayer::GetPlayerMaxSpeed()
{
	if ( GetMoveType() == MOVETYPE_NONE )
	{
		return CS_PLAYER_SPEED_STOPPED;
	}

	if ( IsObserver() )
	{
		// Player gets speed bonus in observer mode
		return CS_PLAYER_SPEED_OBSERVER;
	}

	bool bValidMoveState = ( State_Get() == STATE_ACTIVE || State_Get() == STATE_OBSERVER_MODE );
	if ( !bValidMoveState || m_bIsDefusing || m_bIsGrabbingHostage || CSGameRules()->IsFreezePeriod() )
	{
		// Player should not move during the freeze period
		return CS_PLAYER_SPEED_STOPPED;
	}

	float speed = BaseClass::GetPlayerMaxSpeed();

	if ( IsVIP() == true )  // VIP is slow due to the armour he's wearing
	{
		speed = MIN(speed, CS_PLAYER_SPEED_VIP);
	}
	else if ( m_hCarriedHostage != NULL )
	{
		speed = CS_PLAYER_SPEED_HAS_HOSTAGE;
	}
	else
	{

		CWeaponCSBase *pWeapon = dynamic_cast<CWeaponCSBase*>( GetActiveWeapon() );

		if ( pWeapon )
		{
			if ( HasShield() && IsShieldDrawn() )
			{
				speed = MIN(speed, CS_PLAYER_SPEED_SHIELD);
			}
			else
			{
				speed = MIN(speed, pWeapon->GetMaxSpeed());
			}
		}
	}

	return speed;
}

bool CCSPlayer::IsOtherEnemy( int nEntIndex )
{
	CCSPlayer *pPlayer = (CCSPlayer*)UTIL_PlayerByIndex( nEntIndex );
	if ( !pPlayer )
	{
		// client doesn't have a pointer to enemy players outside our PVS
#if defined ( CLIENT_DLL )	

		// we are never an enemy of ourselves
		if ( entindex() == nEntIndex )
			return false;

		C_CS_PlayerResource *pCSPR = GetCSResources();
		if ( pCSPR )
		{
			int nOtherTeam = pCSPR->GetTeam( nEntIndex );
			int nTeam = GetTeamNumber();

			/*if ( mp_teammates_are_enemies.GetBool() && nTeam == nOtherTeam )
			{
				return true;
			}*/

			return nTeam != nOtherTeam;
		}
#endif

		return false;
	}

	return IsOtherEnemy( pPlayer );
}

bool CCSPlayer::IsOtherEnemy( CCSPlayer *pPlayer )
{
	if ( !pPlayer )
		return false;

	// we are never an enemy of ourselves
	if ( entindex() == pPlayer->entindex() )
		return false;
	
	int nOtherTeam = pPlayer->GetTeamNumber();

	int nTeam = GetTeamNumber();


	/*if ( mp_teammates_are_enemies.GetBool() && nTeam == nOtherTeam )
	{
		return true;
	}*/

	return nTeam != nOtherTeam;
}



bool CCSPlayer::GetUseConfigurationForHighPriorityUseEntity( CBaseEntity *pEntity, CConfigurationForHighPriorityUseEntity_t &cfg )
{
	if ( dynamic_cast<CPlantedC4*>( pEntity ) )
	{
		if ( CSGameRules() && CSGameRules()->IsBombDefuseMap() &&
			( this->GetTeamNumber() == TEAM_CT ) )
		{
			cfg.m_pEntity = pEntity;
		}
		else
		{
			// it's a high-priority entity, but not used by the player team
			cfg.m_pEntity = NULL;
		}
		cfg.m_ePriority = cfg.k_EPriority_Bomb;
		cfg.m_eDistanceCheckType = cfg.k_EDistanceCheckType_2D;
		cfg.m_pos = pEntity->GetAbsOrigin() + Vector( 0, 0, 3 );
		cfg.m_flMaxUseDistance = 62;		// Cannot use if > 62 units away
		cfg.m_flLosCheckDistance = 36;		// Check LOS if > 36 units away (2D)
		cfg.m_flDotCheckAngle = -0.7;		// 0.7 taken from Goldsrc, +/- ~45 degrees
		cfg.m_flDotCheckAngleMax = -0.5;	// 0.3 for it going outside the range during continuous use (120-degree cone)
		return true;
	}
	else if ( dynamic_cast<CHostage*>( pEntity ) )
	{
		cfg.m_pEntity = pEntity;
		cfg.m_ePriority = cfg.k_EPriority_Hostage;
		cfg.m_eDistanceCheckType = cfg.k_EDistanceCheckType_3D;
		cfg.m_pos = pEntity->EyePosition();
		cfg.m_flMaxUseDistance = 62;		// Cannot use if > 62 units away
		cfg.m_flLosCheckDistance = 32;		// Check LOS if > 32 units away (2D)
		cfg.m_flDotCheckAngle = -0.7;		// 0.7 taken from Goldsrc, +/- ~45 degrees
		cfg.m_flDotCheckAngleMax = -0.5;	// 0.5 for it going outside the range during continuous use (120-degree cone)
		return true;
	}
	return false;
}



CBaseEntity *CCSPlayer::GetUsableHighPriorityEntity( void )
{
	// This is done separately since there might be something blocking our LOS to it
	// but we might want to use it anyway if it's close enough.  This should eliminate
	// the vast majority of bomb placement exploits (places where the bomb can be planted
	// but can't be "used".  This also mimics goldsrc cstrike behavior.

	CBaseEntity *pEntsNearPlayer[64];
	// 64 is the distance in Goldsrc.  However since Goldsrc did distance from the player's origin and we're doing distance from the player's eye, make the radius a bit bigger.
	int iEntsNearPlayer = UTIL_EntitiesInSphere( pEntsNearPlayer, 64, EyePosition(), 72, FL_OBJECT );
	if( iEntsNearPlayer != 0 )
	{
		CConfigurationForHighPriorityUseEntity_t cfgBestHighPriorityEntity;
		cfgBestHighPriorityEntity.m_pEntity = NULL;
		cfgBestHighPriorityEntity.m_ePriority = cfgBestHighPriorityEntity.k_EPriority_Default;

		for( int i = 0; i != iEntsNearPlayer; ++i )
		{
			CBaseEntity *pEntity = pEntsNearPlayer[i];
			Assert( pEntity != NULL );
			CConfigurationForHighPriorityUseEntity_t cfgUseSettings;
			if ( !GetUseConfigurationForHighPriorityUseEntity( pEntity, cfgUseSettings ) )
				continue; // not a high-priority entity
			if ( !cfgUseSettings.m_pEntity )
				continue; // not used by the player
			if ( cfgUseSettings.m_ePriority < cfgBestHighPriorityEntity.m_ePriority )
				continue; // we already have a higher priority entity
			
			if ( !cfgUseSettings.UseByPlayerNow( this, cfgUseSettings.k_EPlayerUseType_Start ) )
				continue; // cannot start use by the player right now
				
			// This high-priority entity passes the checks, remember it as best
			if ( cfgUseSettings.IsBetterForUseThan( cfgBestHighPriorityEntity ) )
				cfgBestHighPriorityEntity = cfgUseSettings;
		}

		return cfgBestHighPriorityEntity.m_pEntity;
	}

	return NULL;
}



bool CConfigurationForHighPriorityUseEntity_t::IsBetterForUseThan( CConfigurationForHighPriorityUseEntity_t const &other ) const
{
	if ( !m_pEntity )
		return false;
	if ( !other.m_pEntity )
		return true;
	if ( m_ePriority < other.m_ePriority )
		return false;
	if ( m_ePriority > other.m_ePriority )
		return true;
	if ( m_flDotCheckAngleMax < other.m_flDotCheckAngleMax ) // We are looking at it with a better angle
		return true;
	if ( m_flMaxUseDistance < other.m_flMaxUseDistance ) // This entity is closer to user
		return true;
	return false;
}

bool CConfigurationForHighPriorityUseEntity_t::UseByPlayerNow( CCSPlayer *pPlayer, EPlayerUseType_t ePlayerUseType )
{
	if ( !pPlayer )
		return false;

	// entity is close enough, now make sure the player is facing the bomb.
	float flDistTo = FLT_MAX;
	switch ( m_eDistanceCheckType )
	{
	case k_EDistanceCheckType_2D:
		flDistTo = pPlayer->EyePosition().AsVector2D().DistTo( m_pos.AsVector2D() );
		break;
	case k_EDistanceCheckType_3D:
		flDistTo = pPlayer->EyePosition().DistTo( m_pos );
		break;
	default:
		Assert( false );
	}
	// UTIL_EntitiesInSphere gives strange results where I can find it when my eyes are at an angle, but not when I'm right on top of it
	// because of that, make sure it's in our radius, but check the 2d los and make sure we are as close or closer than we need to be in 1.6
	if ( flDistTo > m_flMaxUseDistance )
		return false;

	// if it's more than 36 units away (2d), we should check LOS
	if ( flDistTo > m_flLosCheckDistance )
	{
		trace_t tr;
		UTIL_TraceLine( pPlayer->EyePosition(), m_pos, (MASK_VISIBLE|CONTENTS_WATER|CONTENTS_SLIME), pPlayer, COLLISION_GROUP_DEBRIS, &tr );
		// if we can't trace to the bomb at this distance, then we fail
		if ( tr.fraction < 0.98 )
			return false;
	}

	Vector vecLOS = pPlayer->EyePosition() - m_pos;
	Vector forward;
	AngleVectors( pPlayer->EyeAngles(), &forward, NULL, NULL );

	vecLOS.NormalizeInPlace();

	float flDot = DotProduct(forward, vecLOS);
	float flCheckAngle = ( ePlayerUseType == k_EPlayerUseType_Start ) ? m_flDotCheckAngle : m_flDotCheckAngleMax;
	if ( flDot >= flCheckAngle )
		return false;

	// Remember the actual settings of this entity
	m_flDotCheckAngle = m_flDotCheckAngleMax = flDot;
	m_flLosCheckDistance = m_flMaxUseDistance = flDistTo;
	return true;
}

void CCSPlayer::GiveCarriedHostage( EHANDLE hHostage )
{
	if ( !IsAlive() )
		return;

	m_hCarriedHostage = hHostage;

	RefreshCarriedHostage( true );
}

void CCSPlayer::RefreshCarriedHostage( bool bForceCreate )
{
#ifndef CLIENT_DLL 
	if ( m_hCarriedHostage == NULL )
		return;

	if ( m_hCarriedHostageProp == NULL )
	{
		CHostageCarriableProp *pHostageProp = dynamic_cast< CHostageCarriableProp* >(CreateEntityByName( "hostage_carriable_prop" ));

		if ( pHostageProp )
		{
			pHostageProp->SetAbsOrigin( GetAbsOrigin() );
			pHostageProp->SetSolid( SOLID_NONE );
			pHostageProp->SetModel( "models/hostage/hostage_carry.mdl" );
			pHostageProp->SetModelName( MAKE_STRING( "models/hostage/hostage_carry.mdl" ) );
			pHostageProp->SetParent( this );
			pHostageProp->SetOwnerEntity( this );
			pHostageProp->FollowEntity( this );
			m_hCarriedHostageProp = pHostageProp;

			CRecipientFilter filter;
			filter.MakeReliable();
			filter.AddRecipient( this );
			UTIL_ClientPrintFilter( filter, HUD_PRINTCENTER, "#Cstrike_TitlesTXT_CarryingHostage" );
		}
	}

	if ( bForceCreate && GetViewModel( HOSTAGE_VIEWMODEL ) )
	{
		CBaseViewModel *vm = GetViewModel( HOSTAGE_VIEWMODEL );
		UTIL_Remove( vm );
		m_hViewModel.Set( HOSTAGE_VIEWMODEL, 0 );
	}

	CPredictedViewModel *vm = NULL;

	CBaseViewModel *pVM = GetViewModel( HOSTAGE_VIEWMODEL );
	if ( pVM )
		vm = (CPredictedViewModel *)pVM;
	else
	{
		vm = (CPredictedViewModel *)CreateEntityByName( "predicted_viewmodel" );
		bForceCreate = true;
	}

	if ( vm )
	{
		vm->SetAbsOrigin( GetAbsOrigin() );
		vm->SetOwner( this );
		vm->SetIndex( HOSTAGE_VIEWMODEL );
		int nAct = ACT_VM_IDLE;
		if ( bForceCreate )
		{
			nAct = ACT_VM_DRAW;
			DispatchSpawn( vm );
		}
		vm->FollowEntity( this, false );
		vm->SetModel( "models/hostage/v_hostage_arm.mdl" );

		int	idealSequence = vm->SelectWeightedSequence( (Activity)nAct );
		if ( idealSequence >= 0 )
		{
			vm->SendViewModelMatchingSequence( idealSequence );
		}
		vm->SetShouldIgnoreOffsetAndAccuracy( true );

		m_hViewModel.Set( HOSTAGE_VIEWMODEL, vm );

		m_hHostageViewModel = vm;
	}

#endif
}

void CCSPlayer::RemoveCarriedHostage( void )
{
	m_hCarriedHostage = NULL;

#ifndef CLIENT_DLL 
	if ( m_hCarriedHostageProp )
	{
		CBaseAnimating *pHostageProp = dynamic_cast< CBaseAnimating* >(m_hCarriedHostageProp.Get());
		if ( pHostageProp )
		{
			pHostageProp->FollowEntity( NULL );
			UTIL_Remove( pHostageProp );
		}

		m_hCarriedHostageProp = NULL;
	}

	if ( m_hHostageViewModel || dynamic_cast<CPredictedViewModel*>(GetViewModel( HOSTAGE_VIEWMODEL )) )
	{
		CPredictedViewModel *pHostageVM = dynamic_cast< CPredictedViewModel* >(m_hHostageViewModel.Get());
		if ( !pHostageVM )
			pHostageVM = dynamic_cast<CPredictedViewModel*>(GetViewModel( HOSTAGE_VIEWMODEL ));

		if ( pHostageVM )
		{
			pHostageVM->FollowEntity( NULL );
			UTIL_Remove( pHostageVM );
		}

		m_hHostageViewModel = 0;

		m_hViewModel.Set( HOSTAGE_VIEWMODEL, 0 );
	}
#endif
}

bool CCSPlayer::IsBotOrControllingBot()
{
	if ( IsBot() )
		return true;
#if CS_CONTROLLABLE_BOTS_ENABLED
	if ( IsControllingBot() )
		return true;
#endif

	return false;
}

void CCSPlayer::GetBulletTypeParameters(
	int iBulletType,
	float &fPenetrationPower,
	float &flPenetrationDistance )
{
	//MIKETODO: make ammo types come from a script file.
	if ( IsAmmoType( iBulletType, BULLET_PLAYER_50AE ) )
	{
		fPenetrationPower = 30;
		flPenetrationDistance = 1000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_762MM ) )
	{
		fPenetrationPower = 39;
		flPenetrationDistance = 5000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_556MM ) ||
			  IsAmmoType( iBulletType, BULLET_PLAYER_556MM_BOX ) )
	{
		fPenetrationPower = 35;
		flPenetrationDistance = 4000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_338MAG ) )
	{
		fPenetrationPower = 45;
		flPenetrationDistance = 8000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_9MM ) )
	{
		fPenetrationPower = 21;
		flPenetrationDistance = 800.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_BUCKSHOT ) )
	{
		fPenetrationPower = 0;
		flPenetrationDistance = 0.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_45ACP ) )
	{
		fPenetrationPower = 15;
		flPenetrationDistance = 500.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_357SIG ) )
	{
		fPenetrationPower = 25;
		flPenetrationDistance = 800.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_57MM ) )
	{
		fPenetrationPower = 30;
		flPenetrationDistance = 2000.0;
	}
	else if ( IsAmmoType( iBulletType, AMMO_TYPE_TASERCHARGE ) )
	{
		fPenetrationPower = 0;
		flPenetrationDistance = 0.0;
	}
	else
	{
		// What kind of ammo is this?
		Assert( false );
		fPenetrationPower = 0;
		flPenetrationDistance = 0.0;
	}
}

static void GetMaterialParameters( int iMaterial, float &flPenetrationModifier, float &flDamageModifier )
{
	switch ( iMaterial )
	{
		case CHAR_TEX_METAL :
			flPenetrationModifier = 0.5;  // If we hit metal, reduce the thickness of the brush we can't penetrate
			flDamageModifier = 0.3;
			break;
		case CHAR_TEX_DIRT :
			flPenetrationModifier = 0.5;
			flDamageModifier = 0.3;
			break;
		case CHAR_TEX_CONCRETE :
			flPenetrationModifier = 0.4;
			flDamageModifier = 0.25;
			break;
		case CHAR_TEX_GRATE	:
			flPenetrationModifier = 1.0;
			flDamageModifier = 0.99;
			break;
		case CHAR_TEX_VENT :
			flPenetrationModifier = 0.5;
			flDamageModifier = 0.45;
			break;
		case CHAR_TEX_TILE :
			flPenetrationModifier = 0.65;
			flDamageModifier = 0.3;
			break;
		case CHAR_TEX_COMPUTER :
			flPenetrationModifier = 0.4;
			flDamageModifier = 0.45;
			break;
		case CHAR_TEX_WOOD :
			flPenetrationModifier = 1.0;
			flDamageModifier = 0.6;
			break;
		default :
			flPenetrationModifier = 1.0;
			flDamageModifier = 0.5;
			break;
	}

	Assert( flPenetrationModifier > 0 );
	Assert( flDamageModifier < 1.0f ); // Less than 1.0f for avoiding infinite loops
}


static bool TraceToExit( Vector start, Vector dir, Vector &end, trace_t &trEnter, trace_t &trExit, float flStepSize, float flMaxDistance )
{
	float flDistance = 0;
	Vector last = start;
	int nStartContents = 0;

	while ( flDistance <= flMaxDistance )
	{
		flDistance += flStepSize;

		end = start + ( flDistance * dir );

		Vector vecTrEnd = end - ( flStepSize * dir );

		if ( nStartContents == 0 )
			nStartContents = UTIL_PointContents( end );

		int nCurrentContents = UTIL_PointContents( end );

		if ( (nCurrentContents & CS_MASK_SHOOT) == 0 || ((nCurrentContents & CONTENTS_HITBOX) && nStartContents != nCurrentContents) )
		{
			// this gets a bit more complicated and expensive when we have to deal with displacements
			UTIL_TraceLine( end, vecTrEnd, CS_MASK_SHOOT|CONTENTS_HITBOX, NULL, &trExit );

			// we exited the wall into a player's hitbox
			if ( trExit.startsolid == true && (trExit.surface.flags & SURF_HITBOX)/*( nStartContents & CONTENTS_HITBOX ) == 0 && (nCurrentContents & CONTENTS_HITBOX)*/ )
			{
				// do another trace, but skip the player to get the actual exit surface 
				UTIL_TraceLine( end, start, CS_MASK_SHOOT, trExit.m_pEnt, COLLISION_GROUP_NONE, &trExit );
				if ( trExit.DidHit() && trExit.startsolid == false )
				{
					end = trExit.endpos;
					return true;
				}
			}
			else if ( trExit.DidHit() && trExit.startsolid == false )
			{
				bool bStartIsNodraw = !!( trEnter.surface.flags & (SURF_NODRAW) );
				bool bExitIsNodraw = !!( trExit.surface.flags & (SURF_NODRAW) );
				if ( bExitIsNodraw && IsBreakableEntity( trExit.m_pEnt ) && IsBreakableEntity( trEnter.m_pEnt ) )
				{
					// we have a case where we have a breakable object, but the mapper put a nodraw on the backside
					end = trExit.endpos;
					return true;
				}
				else if ( bExitIsNodraw == false || (bStartIsNodraw && bExitIsNodraw) ) // exit nodraw is only valid if our entrace is also nodraw
				{
					Vector vecNormal = trExit.plane.normal;
					float flDot = dir.Dot( vecNormal );
					if ( flDot <= 1.0f )
					{
						// get the real end pos
						end = end - ( (flStepSize * trExit.fraction) * dir );
						return true;
					}
				}
			}
			else if ( trEnter.DidHitNonWorldEntity() && IsBreakableEntity( trEnter.m_pEnt ) )
			{
				// if we hit a breakable, make the assumption that we broke it if we can't find an exit (hopefully..)
				// fake the end pos
				trExit = trEnter;
				trExit.endpos = start + ( 1.0f * dir );
				return true;
			}
		}
	}

	return false;
}

inline void UTIL_TraceLineIgnoreTwoEntities( const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask,
					 const IHandleEntity *ignore, const IHandleEntity *ignore2, int collisionGroup, trace_t *ptr )
{
	Ray_t ray;
	ray.Init( vecAbsStart, vecAbsEnd );
	CTraceFilterSkipTwoEntities traceFilter( ignore, ignore2, collisionGroup );
	enginetrace->TraceRay( ray, mask, &traceFilter, ptr );
	if( r_visualizetraces.GetBool() )
	{
		DebugDrawLine( ptr->startpos, ptr->endpos, 255, 0, 0, true, -1.0f );
	}
}

ConVar sv_server_verify_blood_on_player( "sv_server_verify_blood_on_player", "1", FCVAR_CHEAT | FCVAR_REPLICATED );

#ifndef CLIENT_DLL
static const int kMaxNumPenetrationsSupported = 4;
struct DelayedDamageInfoData_t
{
	CTakeDamageInfo m_info;
	trace_t m_tr;

	typedef CUtlVectorFixedGrowable< DelayedDamageInfoData_t, kMaxNumPenetrationsSupported > Array;
};
#endif

void CCSPlayer::FireBullet( 
	Vector vecSrc,	// shooting postion
	const QAngle &shootAngles,  //shooting angle
	float flDistance, // max distance 
	float flPenetration, // the power of the penetration
	int nPenetrationCount,
	int iBulletType, // ammo type
	int iDamage, // base damage
	float flRangeModifier, // damage range modifier
	CBaseEntity *pevAttacker, // shooter
	bool bDoEffects,
	float xSpread, float ySpread
	)
{
	float fCurrentDamage = iDamage;   // damage of the bullet at it's current trajectory
	float flCurrentDistance = 0.0;  //distance that the bullet has traveled so far
		
	Vector vecDirShooting, vecRight, vecUp;
	AngleVectors( shootAngles, &vecDirShooting, &vecRight, &vecUp );
	
	// MIKETODO: put all the ammo parameters into a script file and allow for CS-specific params.
	float flPenetrationPower = 0;		// thickness of a wall that this bullet can penetrate
	float flPenetrationDistance = 0;	// distance at which the bullet is capable of penetrating a wall
	float flDamageModifier = 0.5f;		// default modification of bullets power after they go through a wall.
	float flPenetrationModifier = 1.0f;

	GetBulletTypeParameters( iBulletType, flPenetrationPower, flPenetrationDistance );

	// we use the max penetrations on this gun to figure out how much penetration it's capable of
	flPenetrationPower = flPenetration; 

	if ( !pevAttacker )
		pevAttacker = this;  // the default attacker is ourselves

	// add the spray 
	Vector vecDir = vecDirShooting + xSpread * vecRight + ySpread * vecUp;

	VectorNormalize( vecDir );


	//Adrian: visualize server/client player positions
	//This is used to show where the lag compesator thinks the player should be at.
#if 0 
	for ( int k = 1; k <= gpGlobals->maxClients; k++ )
	{
		CBasePlayer *clientClass = (CBasePlayer *)CBaseEntity::Instance( k );

		if ( clientClass == NULL ) 
			 continue;

		if ( k == entindex() ) 
			 continue;

#ifdef CLIENT_DLL
		debugoverlay->AddBoxOverlay( clientClass->GetAbsOrigin(), clientClass->WorldAlignMins(), clientClass->WorldAlignMaxs(), QAngle( 0, 0, 0), 255,0,0,127, 4 );
#else
		NDebugOverlay::Box( clientClass->GetAbsOrigin(), clientClass->WorldAlignMins(), clientClass->WorldAlignMaxs(), 0,0,255,127, 4 );
#endif

	}

#endif

#ifndef CLIENT_DLL
	// [pfreese] Track number player entities killed with this bullet
	int iPenetrationKills = 0;
	int numPlayersHit = 0;

	// [menglish] Increment the shots fired for this player
	CCS_GameStats.Event_ShotFired( this, GetActiveWeapon() );
#endif

	bool bFirstHit = true;

	const CBaseCombatCharacter *lastPlayerHit = NULL;	// this includes players, bots, and hostages

#ifdef CLIENT_DLL
	Vector vecWallBangHitStart, vecWallBangHitEnd;
	vecWallBangHitStart.Init();
	vecWallBangHitEnd.Init();
	bool bWallBangStarted = false;
	bool bWallBangEnded = false;
	bool bWallBangHeavyVersion = false;
#endif

	bool bBulletHitPlayer = false;

	MDLCACHE_CRITICAL_SECTION();

#ifndef CLIENT_DLL
	DelayedDamageInfoData_t::Array arrPendingDamage;
#endif

	bool bShotHitTeammate = false;

	float flDist_aim = 0;
	Vector vHitLocation = Vector( 0,0,0 );

	while ( fCurrentDamage > 0 )
	{
		Vector vecEnd = vecSrc + vecDir * (flDistance-flCurrentDistance);

		trace_t tr; // main enter bullet trace

		UTIL_TraceLineIgnoreTwoEntities( vecSrc, vecEnd, CS_MASK_SHOOT|CONTENTS_HITBOX, this, lastPlayerHit, COLLISION_GROUP_NONE, &tr );
		{
			CTraceFilterSkipTwoEntities filter( this, lastPlayerHit, COLLISION_GROUP_NONE );

			// Check for player hitboxes extending outside their collision bounds
			const float rayExtension = 40.0f;
			UTIL_ClipTraceToPlayers( vecSrc, vecEnd + vecDir * rayExtension, CS_MASK_SHOOT|CONTENTS_HITBOX, &filter, &tr );
		}

		if ( !flDist_aim )
		{
			flDist_aim = ( tr.fraction != 1.0 ) ? ( tr.startpos - tr.endpos ).Length() : 0;
		}

		if ( flDist_aim )
		{
			vHitLocation = tr.endpos;
		}

		lastPlayerHit = dynamic_cast<const CBaseCombatCharacter *>(tr.m_pEnt);

		if ( lastPlayerHit )
		{
			if ( lastPlayerHit->GetTeamNumber() == GetTeamNumber() )
			{
				bShotHitTeammate = true;
			}

			bBulletHitPlayer = true;
		}

		if ( tr.fraction == 1.0f )
			break; // we didn't hit anything, stop tracing shoot

#ifdef CLIENT_DLL
		if ( !bWallBangStarted && !bBulletHitPlayer )
		{
			vecWallBangHitStart = tr.endpos;
			vecWallBangHitEnd = tr.endpos;
			bWallBangStarted = true;

			if ( fCurrentDamage > 20 )
				bWallBangHeavyVersion = true;
		}
		else if ( !bWallBangEnded )
		{
			vecWallBangHitEnd = tr.endpos;

			if ( bBulletHitPlayer )
				bWallBangEnded = true;
		}
#endif


#if defined( _DEBUG ) && !defined( CLIENT_DLL )	
		if ( bFirstHit )
			AddBulletStat( gpGlobals->realtime, VectorLength( vecSrc-tr.endpos), tr.endpos );
#endif

		bFirstHit = false;
#ifndef CLIENT_DLL
		//
		// Propogate a bullet impact event
		// @todo Add this for shotgun pellets (which dont go thru here)
		//
		IGameEvent * event = gameeventmanager->CreateEvent( "bullet_impact" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetFloat( "x", tr.endpos.x );
			event->SetFloat( "y", tr.endpos.y );
			event->SetFloat( "z", tr.endpos.z );
			gameeventmanager->FireEvent( event );
		}
#endif

		/************* MATERIAL DETECTION ***********/
		surfacedata_t *pSurfaceData = physprops->GetSurfaceData( tr.surface.surfaceProps );
		int iEnterMaterial = pSurfaceData->game.material;

		GetMaterialParameters( iEnterMaterial, flPenetrationModifier, flDamageModifier );

		bool hitGrate = ( tr.contents & CONTENTS_GRATE ) != 0;


#ifdef CLIENT_DLL
		if ( sv_showimpacts.GetInt() == 1 || sv_showimpacts.GetInt() == 2 )
		{
			// draw red client impact markers
			debugoverlay->AddBoxOverlay( tr.endpos, Vector(-2,-2,-2), Vector(2,2,2), QAngle( 0, 0, 0), 255,0,0,127, 4 );
		}		

#else
		if ( sv_showimpacts.GetInt() == 1 || sv_showimpacts.GetInt() == 3 )
		{
			// draw blue server impact markers
			NDebugOverlay::Box( tr.endpos, Vector(-2,-2,-2), Vector(2,2,2), 0,0,255,127, 4 );
		}
#endif

		// draw green boxes where the shot originated from
		//NDebugOverlay::Box( vecSrc, Vector(-1,-1,-1), Vector(1,1,1), 0,255,90,90, 10 );

		//calculate the damage based on the distance the bullet travelled.
		flCurrentDistance += tr.fraction * (flDistance-flCurrentDistance);
		fCurrentDamage *= pow (flRangeModifier, (flCurrentDistance / 500));

#ifndef CLIENT_DLL
		// the value of iPenetration when the round reached its max penetration distance
		int nPenetrationAtMaxDistance = 0;
		// save off how many penetrations this bullet had in case we reached max distance and stomp the value later
		int const numPenetrationsInitiallyAllowedForThisBullet = nPenetrationCount;
#endif

		// check if we reach penetration distance, no more penetrations after that
		// or if our modifyer is super low, just stop the bullet
		if ( (flCurrentDistance > flPenetrationDistance && flPenetration > 0 ) ||
			flPenetrationModifier < 0.1 )
		{
#ifndef CLIENT_DLL
			nPenetrationAtMaxDistance = 0;
#endif
			// Setting nPenetrationCount to zero prevents the bullet from penetrating object at max distance
			// and will no longer trace beyond the exit point, however "numPenetrationsInitiallyAllowedForThisBullet"
			// is saved off to allow correct determination whether the hit on the object at max distance had
			// *previously* penetrated anything or not. In case of a direct hit over 3000 units the saved off
			// value would be max penetrations value and will determine a direct hit and not a penetration hit.
			// However it is important that all tracing further stops past this point (as the code does at
			// the time of writing) because otherwise next trace will think that 4 penetrations have already
			// occurred.
			nPenetrationCount = 0;
		}

#ifndef CLIENT_DLL
		// This just keeps track of sounds for AIs (it doesn't play anything).
		CSoundEnt::InsertSound( SOUND_BULLET_IMPACT, tr.endpos, 400, 0.2f, this );
#endif

		int iDamageType = DMG_BULLET | DMG_NEVERGIB;
		CWeaponCSBase* pActiveWeapon = GetActiveCSWeapon();
		if ( pActiveWeapon && pActiveWeapon->IsA( WEAPON_TASER ) )
		{
			iDamageType = DMG_SHOCK | DMG_NEVERGIB;
		}

		if( bDoEffects )
		{
			// See if the bullet ended up underwater + started out of the water
			if ( enginetrace->GetPointContents( tr.endpos ) & (CONTENTS_WATER|CONTENTS_SLIME) )
			{	
				trace_t waterTrace;
				UTIL_TraceLine( vecSrc, tr.endpos, (MASK_SHOT|CONTENTS_WATER|CONTENTS_SLIME), this, COLLISION_GROUP_NONE, &waterTrace );
				
				if( waterTrace.allsolid != 1 )
				{
					CEffectData	data;
 					data.m_vOrigin = waterTrace.endpos;
					data.m_vNormal = waterTrace.plane.normal;
					data.m_flScale = random->RandomFloat( 8, 12 );

					if ( waterTrace.contents & CONTENTS_SLIME )
					{
						data.m_fFlags |= FX_WATER_IN_SLIME;
					}

					DispatchEffect( "gunshotsplash", data );
				}
			}
			else
			{
				//Do Regular hit effects

				// Don't decal nodraw surfaces
				if ( !( tr.surface.flags & (SURF_SKY|SURF_NODRAW|SURF_HINT|SURF_SKIP) ) )
				{
					//CBaseEntity *pEntity = tr.m_pEnt;
					UTIL_ImpactTrace( &tr, iDamageType );
				}
			}
		}

#ifndef CLIENT_DLL
		// decal players on the server to eliminate the disparity between where the client thinks the decal went and where it actually went
		// we want to eliminate the case where a player sees a blood decal on someone, but they are at 100 health
		if ( sv_server_verify_blood_on_player.GetBool() && tr.DidHit() && tr.m_pEnt && tr.m_pEnt->IsPlayer() )
		{
			UTIL_ImpactTrace( &tr, iDamageType );
		}
#endif

#ifdef CLIENT_DLL
#if USE_TRACERS
		// create the tracer
		CreateWeaponTracer( vecSrc, tr.endpos );
#endif
#endif

		// add damage to entity that we hit
		
#ifndef CLIENT_DLL
		CBaseEntity *pEntity = tr.m_pEnt;

		// [pfreese] Check if enemy players were killed by this bullet, and if so,
		// add them to the iPenetrationKills count

		DelayedDamageInfoData_t &delayedDamage = arrPendingDamage.Element( arrPendingDamage.AddToTail() );
		delayedDamage.m_tr = tr;

		int nObjectsPenetrated = kMaxNumPenetrationsSupported - ( numPenetrationsInitiallyAllowedForThisBullet + nPenetrationAtMaxDistance );
		CTakeDamageInfo &info = delayedDamage.m_info;
		info.Set( pevAttacker, pevAttacker, GetActiveWeapon(), fCurrentDamage, iDamageType, 0, nObjectsPenetrated );

		// [dkorus] note:  This is the number of players hit up to this point, not the total number this bullet WILL hit.
		info.SetDamagedOtherPlayers( numPlayersHit );

		info.SetAmmoType( iBulletType );
		CalculateBulletDamageForce( &info, iBulletType, vecDir, tr.endpos );

		bool bWasAlive = pEntity->IsAlive();

		// === Damage applied later ===

		if ( bWasAlive && pEntity->IsPlayer() && IsOtherEnemy( pEntity->entindex() ) )
		{
			numPlayersHit++;
		}
#endif

		// [dkorus] note: values are changed inside of HandleBulletPenetration
		bool bulletStopped = HandleBulletPenetration( flPenetration, iEnterMaterial, hitGrate, tr, vecDir, pSurfaceData, flPenetrationModifier,
			flDamageModifier, bDoEffects, iDamageType, flPenetrationPower, nPenetrationCount, vecSrc, flDistance,
			flCurrentDistance, fCurrentDamage );

		// [dkorus] bulletStopped is true if the bullet can no longer continue penetrating materials
		if ( bulletStopped )
			break;



	}

#ifndef CLIENT_DLL
	if ( bBulletHitPlayer && !bShotHitTeammate )
	{	// Guarantee that the bullet that hit an enemy trumps the player viewangles
		// that are locked in for the duration of the server simulation ticks
		m_iLockViewanglesTickNumber = gpGlobals->tickcount;
		m_qangLockViewangles = pl.v_angle;
	}
#endif

#ifndef CLIENT_DLL
	FOR_EACH_VEC( arrPendingDamage, idxDamage )
	{
		ClearMultiDamage();

		CTakeDamageInfo &info = arrPendingDamage[idxDamage].m_info;
		trace_t &tr = arrPendingDamage[idxDamage].m_tr;

		CBaseEntity *pEntity = tr.m_pEnt;
		bool bWasAlive = pEntity->IsAlive();

		pEntity->DispatchTraceAttack( info, vecDir, &tr );
		TraceAttackToTriggers( info, tr.startpos, tr.endpos, vecDir );

		ApplyMultiDamage();

		if ( bWasAlive && !pEntity->IsAlive() && pEntity->IsPlayer() && IsOtherEnemy( pEntity->entindex() ) )
		{
			++iPenetrationKills;
		}
	}
#endif

#ifdef CLIENT_DLL
	if ( bWallBangStarted )
	{
		float flWallBangLength = (vecWallBangHitEnd - vecWallBangHitStart).Length();
		if ( flWallBangLength > 0 && flWallBangLength < CS_MAX_WALLBANG_TRAIL_LENGTH )
		{
			QAngle temp;
			VectorAngles( vecWallBangHitEnd - vecWallBangHitStart, temp );

			CEffectData	data;
 			data.m_vOrigin = vecWallBangHitStart;
			data.m_vStart = vecWallBangHitEnd;
			data.m_vAngles = temp;
			//data.m_vNormal = vecWallBangHitStart - vecWallBangHitEnd;
			data.m_flScale = 1.0f;
			
			if ( bWallBangHeavyVersion )
			{
				DispatchEffect( "impact_wallbang_heavy", data );
			}
			else
			{
				DispatchEffect( "impact_wallbang_heavy", data );
			}

			//debugoverlay->AddLineOverlay( vecWallBangHitStart, vecWallBangHitEnd, 0, 255, 0, false, 3 );
		}
	}
#endif

#ifndef CLIENT_DLL
	// [pfreese] If we killed at least two enemies with a single bullet, award the
	// TWO_WITH_ONE_SHOT achievement

	if ( iPenetrationKills >= 2 )
	{
		AwardAchievement( CSKillTwoWithOneShot );
	}
#endif


}





// [dkorus] helper for FireBullet
//			changes iPenetration to updated value
//			returns TRUE if we should stop processing more hits after this one
//			returns FALSE if we can continue processing
bool CCSPlayer::HandleBulletPenetration( float &flPenetration,
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
										 float &fCurrentDamage)
{
	bool bIsNodraw = !!( tr.surface.flags & (SURF_NODRAW) );

	bool bFailedPenetrate = false;

	// check if bullet can penetrarte another entity
	if ( nPenetrationCount == 0 && !hitGrate && !bIsNodraw 
		 && iEnterMaterial != CHAR_TEX_GLASS && iEnterMaterial != CHAR_TEX_GRATE )
		bFailedPenetrate = true; // no, stop

	// If we hit a grate with iPenetration == 0, stop on the next thing we hit
	if ( flPenetration <= 0 || nPenetrationCount <= 0 )
		bFailedPenetrate = true;

	Vector penetrationEnd;

	// find exact penetration exit
	trace_t exitTr;
	if ( !TraceToExit( tr.endpos, vecDir, penetrationEnd, tr, exitTr, 4, MAX_PENETRATION_DISTANCE ) )
	{
		// ended in solid
		if ( (UTIL_PointContents ( tr.endpos ) & CS_MASK_SHOOT) == 0 )
		{
			bFailedPenetrate = true;
		}
	}
	
	if ( bFailedPenetrate == true )
	{
		return true;
	}

	//debugoverlay->AddBoxOverlay( exitTr.endpos, Vector(-1,-1,-1), Vector(1,1,1), QAngle( 0, 0, 0), 255,255,0,127, 400 );

	// get material at exit point
	surfacedata_t *pExitSurfaceData = physprops->GetSurfaceData( exitTr.surface.surfaceProps );
	int iExitMaterial = pExitSurfaceData->game.material;

	// percent of total damage lost automatically on impacting a surface
	float flDamLostPercent = 0.16;

	// since some railings in de_inferno are CONTENTS_GRATE but CHAR_TEX_CONCRETE, we'll trust the
	// CONTENTS_GRATE and use a high damage modifier.
	if ( hitGrate || bIsNodraw || iEnterMaterial == CHAR_TEX_GLASS || iEnterMaterial == CHAR_TEX_GRATE )
	{
		// If we're a concrete grate (TOOLS/TOOLSINVISIBLE texture) allow more penetrating power.
		if ( iEnterMaterial == CHAR_TEX_GLASS || iEnterMaterial == CHAR_TEX_GRATE )
		{
			flPenetrationModifier = 3.0f;
			flDamLostPercent = 0.05;
		}
		else
			flPenetrationModifier = 1.0f;

		flDamageModifier = 0.99f;
	}
	else
	{
		// check the exit material and average the exit and entrace values
		float flExitPenetrationModifier;
		float flExitDamageModifier;
		GetMaterialParameters( pExitSurfaceData->game.material, flExitPenetrationModifier, flExitDamageModifier );
		flPenetrationModifier = (flPenetrationModifier + flExitPenetrationModifier) / 2;
		flDamageModifier = (flDamageModifier + flExitDamageModifier) / 2;
	}

	// if enter & exit point is wood we assume this is 
	// a hollow crate and give a penetration bonus
	if ( iEnterMaterial == iExitMaterial )
	{
		if ( iExitMaterial == CHAR_TEX_WOOD )
		{
			flPenetrationModifier = 3;
		}
		else if ( iExitMaterial == CHAR_TEX_PLASTIC )
		{
			flPenetrationModifier = 2;
		}
	}

	float flTraceDistance = VectorLength( exitTr.endpos - tr.endpos );

	float flPenMod = MAX( 0, (1 / flPenetrationModifier) );

	float flPercentDamageChunk = fCurrentDamage * flDamLostPercent;
	float flPenWepMod = flPercentDamageChunk + MAX( 0, (3 / flPenetrationPower) * 1.25 ) * (flPenMod * 3.0);

	float flLostDamageObject = ((flPenMod * (flTraceDistance*flTraceDistance)) / 24);
	float flTotalLostDamage = flPenWepMod + flLostDamageObject;

	// reduce damage power each time we hit something other than a grate
	fCurrentDamage -= MAX( 0, flTotalLostDamage );

	if ( fCurrentDamage < 1 )
		return true;

	// penetration was successful

	// bullet did penetrate object, exit Decal
	if ( bDoEffects )
	{
		UTIL_ImpactTrace( &exitTr, iDamageType );
	}

#ifndef CLIENT_DLL
	// decal players on the server to eliminate the disparity between where the client thinks the decal went and where it actually went
	// we want to eliminate the case where a player sees a blood decal on someone, but they are at 100 health
	if ( sv_server_verify_blood_on_player.GetBool() && tr.DidHit() && tr.m_pEnt && tr.m_pEnt->IsPlayer() )
	{
		UTIL_ImpactTrace( &tr, iDamageType );
	}
#endif

	//setup new start end parameters for successive trace

	//flPenetrationPower -= (flTraceDistance/2) / flPenMod;
	flCurrentDistance += flTraceDistance;

	// NDebugOverlay::Box( exitTr.endpos, Vector(-2,-2,-2), Vector(2,2,2), 0,255,0,127, 8 );

	vecSrc = exitTr.endpos;
	flDistance = (flDistance - flCurrentDistance) * 0.5;

	nPenetrationCount--;
	return false;
}

void CCSPlayer::ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName )
{

#ifdef CLIENT_DLL
	if ( sv_server_verify_blood_on_player.GetBool() )
		return;
#endif

	static ConVar *violence_hblood = cvar->FindVar( "violence_hblood" );
	if ( violence_hblood && !violence_hblood->GetBool() )
		return;

	VPROF( "CCSPlayer::ImpactTrace" );
	Assert( pTrace->m_pEnt );

	CBaseEntity *pEntity = pTrace->m_pEnt;

	// Build the impact data
	CEffectData data;
	data.m_vOrigin = pTrace->endpos;
	data.m_vStart = pTrace->startpos;
	data.m_nSurfaceProp = pTrace->surface.surfaceProps;
	if ( data.m_nSurfaceProp < 0 )
	{
		data.m_nSurfaceProp = 0;
	}
	data.m_nDamageType = iDamageType;
	data.m_nHitBox = pTrace->hitbox;
#ifdef CLIENT_DLL
	data.m_hEntity = ClientEntityList().EntIndexToHandle( pEntity->entindex() );
#else
	data.m_nEntIndex = pEntity->entindex();

	if ( sv_server_verify_blood_on_player.GetBool() )
	{
		data.m_vOrigin -= GetAbsOrigin();
		data.m_vStart -= GetAbsOrigin();
	}

#endif

	// Send it on its way
	if ( !pCustomImpactName )
	{
		DispatchEffect( "Impact", data );
	}
	else
	{
		DispatchEffect( pCustomImpactName, data );
	}
}

#ifdef CLIENT_DLL

void TE_DynamicLight( IRecipientFilter& filter, float delay,
	const Vector* org, int r, int g, int b, int exponent, float radius, float time, float decay, int nLightIndex = LIGHT_INDEX_TE_DYNAMIC );

#if USE_TRACERS
void CCSPlayer::CreateWeaponTracer( Vector vecStart, Vector vecEnd )
{
	int iTracerFreq = 1;
	C_WeaponCSBase *pWeapon = GetActiveCSWeapon();

	if ( pWeapon )
	{
		// if this is a local player, start at attachment on view model
		// else start on attachment on weapon model
		int iEntIndex = entindex();
		int iUseAttachment = TRACER_DONT_USE_ATTACHMENT;
		int iAttachment = 1;

		C_CSPlayer *pLocalPlayer = NULL;
		bool bUseObserverTarget = false;

		pLocalPlayer = C_CSPlayer::GetLocalCSPlayer();
		if ( !pLocalPlayer )
			return;

		if ( pLocalPlayer->GetObserverTarget() == this &&
			pLocalPlayer->GetObserverMode() == OBS_MODE_IN_EYE )
		{
			bUseObserverTarget = true;
		}

		C_BaseCombatWeapon *pActiveWeapon = GetActiveWeapon();
		C_BaseViewModel *pViewModel = GetViewModel(WEAPON_VIEWMODEL);

		CBaseWeaponWorldModel *pWeaponWorldModel = NULL;
		if ( pActiveWeapon && (!pViewModel || this->ShouldDraw()) )
			pWeaponWorldModel = pActiveWeapon->GetWeaponWorldModel();

		if ( pWeaponWorldModel && pWeaponWorldModel->HasDormantOwner() )
		{
			// This is likely a player firing from around a corner, where this client can't see them.
			// Don't modify the tracer start position, since our local world weapon model position is not reliable.
		}
		else if (pWeaponWorldModel)
		{
			iAttachment = pWeaponWorldModel->LookupAttachment( "muzzle_flash" );
			if ( iAttachment > 0 )
				pWeaponWorldModel->GetAttachment( iAttachment, vecStart );
		}
		else if ( pViewModel )
		{
			iAttachment = pViewModel->LookupAttachment( "1" );
			pViewModel->GetAttachment( iAttachment, vecStart );
		}

		// bail if we're at the origin
		if ( vecStart.LengthSqr() <= 0 )
			return;

		// muzzle flash dynamic light
		CPVSFilter filter( vecStart );
		TE_DynamicLight( filter, 0.0, &vecStart, 255, 192, 64, 5, 70, 0.05, 768 );

		int	nBulletNumber = (pWeapon->GetMaxClip1() - pWeapon->Clip1()) + 1;
		iTracerFreq = pWeapon->GetCSWpnData().m_iTracerFrequency[pWeapon->m_weaponMode];
		if ( ( iTracerFreq != 0 ) && ( nBulletNumber % iTracerFreq ) == 0 )
		{
			const char *pszTracerEffect = GetTracerType();
			if ( pszTracerEffect && pszTracerEffect[0] )
			{
				UTIL_ParticleTracer( pszTracerEffect, vecStart, vecEnd, iEntIndex, iUseAttachment, true );
			}
		}
		else
		{
			// just do the whiz sound
			FX_TracerSound( vecStart, vecEnd, TRACER_TYPE_DEFAULT );
		}
		
	}
}
#endif
#endif

void CCSPlayer::UpdateStepSound( surfacedata_t *psurface, const Vector &vecOrigin, const Vector &vecVelocity )
{
	if ( IsBot() && IsDormant() )
		return;

	if (!IsAlive())
		return;

	float speedSqr = vecVelocity.LengthSqr();

	float flWalkSpeed = (CS_PLAYER_SPEED_RUN * CS_PLAYER_SPEED_WALK_MODIFIER);

	if ( ( speedSqr < flWalkSpeed * flWalkSpeed ) || m_bIsWalking )
	{
		if ( speedSqr < 10.0 )
		{
			// If we stop, reset the step sound tracking.
			// This makes step sounds play a consistent time after
			// we start running making it easier to co-ordinate suit and
			// step sounds.
			SetStepSoundTime( STEPSOUNDTIME_NORMAL, false );
		}

		return; // player is not running, no footsteps
	}

	BaseClass::UpdateStepSound( psurface, vecOrigin, vecVelocity  );
}


// GOOSEMAN : Kick the view..
void CCSPlayer::KickBack( float up_base, float lateral_base, float up_modifier, float lateral_modifier, float up_max, float lateral_max, int direction_change )
{
	float flKickUp;
	float flKickLateral;

	if (m_iShotsFired == 1) // This is the first round fired
	{
		flKickUp = up_base;
		flKickLateral = lateral_base;
	}
	else
	{
		flKickUp = up_base + m_iShotsFired*up_modifier;
		flKickLateral = lateral_base + m_iShotsFired*lateral_modifier;
	}


	QAngle angle = GetPunchAngle();

	angle.x -= flKickUp;
	if ( angle.x < -1 * up_max )
		angle.x = -1 * up_max;

	if ( m_iDirection == 1 )
	{
		angle.y += flKickLateral;
		if (angle.y > lateral_max)
			angle.y = lateral_max;
	}
	else
	{
		angle.y -= flKickLateral;
		if ( angle.y < -1 * lateral_max )
			angle.y = -1 * lateral_max;
	}

	if ( !SharedRandomInt( "KickBack", 0, direction_change ) )
		m_iDirection = 1 - m_iDirection;

	SetPunchAngle( angle );
}


bool CCSPlayer::CanMove() const
{
	// When we're in intro camera mode, it's important to return false here
	// so our physics object doesn't fall out of the world.
	if ( GetMoveType() == MOVETYPE_NONE )
		return false;

	if ( IsObserver() )
		return true; // observers can move all the time

	bool bValidMoveState = (State_Get() == STATE_ACTIVE || State_Get() == STATE_OBSERVER_MODE);

	if ( m_bIsDefusing || m_bIsGrabbingHostage || !bValidMoveState || CSGameRules()->IsFreezePeriod() )
	{
		return false;
	}
	else
	{
		// Can't move while planting C4.
		CC4 *pC4 = dynamic_cast< CC4* >( GetActiveWeapon() );
		if ( pC4 && pC4->m_bStartedArming )
			return false;

		return true;
	}
}


void CCSPlayer::OnJump( float fImpulse )
{
	CWeaponCSBase* pActiveWeapon = GetActiveCSWeapon();
	if ( pActiveWeapon != NULL )
		pActiveWeapon->OnJump(fImpulse);
}


void CCSPlayer::OnLand( float fVelocity )
{
	CWeaponCSBase* pActiveWeapon = GetActiveCSWeapon();
	if ( pActiveWeapon != NULL )
		pActiveWeapon->OnLand(fVelocity);
}


//-------------------------------------------------------------------------------------------------------------------------------
/**
* Track the last time we were on a ladder, along with the ladder's normal and where we
* were grabbing it, so we don't reach behind us and grab it again as we are trying to
* dismount.
*/
void CCSPlayer::SurpressLadderChecks( const Vector& pos, const Vector& normal )
{
	m_ladderSurpressionTimer.Start( 1.0f );
	m_lastLadderPos = pos;
	m_lastLadderNormal = normal;
}


//-------------------------------------------------------------------------------------------------------------------------------
/**
* Prevent us from re-grabbing the same ladder we were just on:
*  - if the timer is elapsed, let us grab again
*  - if the normal is different, let us grab
*  - if the 2D pos is very different, let us grab, since it's probably a different ladder
*/
bool CCSPlayer::CanGrabLadder( const Vector& pos, const Vector& normal )
{
	if ( m_ladderSurpressionTimer.GetRemainingTime() <= 0.0f )
	{
		return true;
	}

	const float MaxDist = 64.0f;
	if ( pos.AsVector2D().DistToSqr( m_lastLadderPos.AsVector2D() ) < MaxDist * MaxDist )
	{
		return false;
	}

	if ( normal != m_lastLadderNormal )
	{
		return true;
	}

	return false;
}


void CCSPlayer::SetAnimation( PLAYER_ANIM playerAnim )
{
	// In CS, its CPlayerAnimState object manages ALL the animation state.
	return;
}


CWeaponCSBase* CCSPlayer::CSAnim_GetActiveWeapon()
{
	return GetActiveCSWeapon();
}


bool CCSPlayer::CSAnim_CanMove()
{
	return CanMove();
}

int CCSPlayer::GetCarryLimit( CSWeaponID weaponId )
{
	const CCSWeaponInfo *pWeaponInfo = GetWeaponInfo( weaponId );
	if ( pWeaponInfo == NULL )
		return 0;

	if ( pWeaponInfo->m_WeaponType == WEAPONTYPE_GRENADE )
	{
		return GetAmmoDef()->MaxCarry( pWeaponInfo->iAmmoType, this );	// We still use player-stored ammo for grenades.
	}

	return 1;
}

AcquireResult::Type CCSPlayer::CanAcquire( CSWeaponID weaponId, AcquireMethod::Type acquireMethod )
{
	const CCSWeaponInfo *pWeaponInfo = NULL;
	if ( weaponId == WEAPON_NONE )
		return AcquireResult::InvalidItem;

	pWeaponInfo = GetWeaponInfo( weaponId );

	if ( pWeaponInfo == NULL )
		return AcquireResult::InvalidItem;

	int nType = pWeaponInfo->m_WeaponType;

// 	if ( acquireMethod == AcquireMethod::Buy )
// 	{
// 		bool bFoundInLoadout = false;
// 		int nStartSearchPos = LOADOUT_POSITION_INVALID;
// 
// 		if ( nType == WEAPONTYPE_PISTOL )
// 			nStartSearchPos = LOADOUT_POSITION_SECONDARY0;
// 		else if ( nType == WEAPONTYPE_SHOTGUN || nType == WEAPONTYPE_MACHINEGUN )
// 			nStartSearchPos = LOADOUT_POSITION_HEAVY0;
// 		else if ( nType == WEAPONTYPE_SUBMACHINEGUN )
// 			nStartSearchPos = LOADOUT_POSITION_SMG0;
// 		else if ( nType == WEAPONTYPE_RIFLE || nType == WEAPONTYPE_SNIPER_RIFLE )
// 			nStartSearchPos = LOADOUT_POSITION_RIFLE0;
// 
// 		// make sure that we have this item equipped in our inventory loadout
// 		for ( int i = nStartSearchPos; i < (nStartSearchPos+6); ++i )
// 		{
// 			CEconItemView *pItemView = Inventory()->GetItemInLoadout( GetTeamNumber(), i );
// 			if ( pItemView && pItemView->GetStaticData() )
// 			{
// 				if ( pItemView == pItem || ( V_stricmp( pItemView->GetStaticData()->GetDefinitionName(), WeaponIdAsString( weaponId ) ) == 0 ) )
// 				{
// 					bFoundInLoadout = true;
// 					break;
// 				}
// 			}
// 		}
// 		if ( nStartSearchPos != LOADOUT_POSITION_INVALID && !bFoundInLoadout )
// 			return AcquireResult::NotAllowedForPurchase;
// 	}

	if ( nType == WEAPONTYPE_GRENADE )
	{
		// make sure we aren't exceeding the ammo max for this grenade type
		int carryLimitThisGrenade = GetCarryLimit( weaponId );

		int carryLimitAllGrenades = ammo_grenade_limit_total.GetInt();

		CBaseCombatWeapon* pGrenadeWeapon = Weapon_OwnsThisType( WeaponIdAsString( weaponId ) );
		if ( pGrenadeWeapon != NULL )
		{
			int nAmmoType = pGrenadeWeapon->GetPrimaryAmmoType();

			if( nAmmoType != -1 )
			{
				int thisGrenadeCarried = GetAmmoCount(nAmmoType );
				if ( thisGrenadeCarried >= carryLimitThisGrenade )
				{
					return AcquireResult::ReachedGrenadeTypeLimit;
				}
			}
		}

		// count how many grenades of any type the player is currently carrying
		int allGrenadesCarried = 0;
		for ( int i = 0; i < MAX_WEAPONS; ++i )
		{
			CWeaponCSBase* pWeapon = dynamic_cast<CWeaponCSBase*>( GetWeapon(i) );
			if ( pWeapon != NULL && pWeapon->IsKindOf( WEAPONTYPE_GRENADE ) )
			{
				int nAmmoType = pWeapon->GetPrimaryAmmoType();
				if( nAmmoType != -1 )
				{
					allGrenadesCarried += GetAmmoCount( nAmmoType );
				}
			}
		}

		if ( allGrenadesCarried >= carryLimitAllGrenades )
		{
			return AcquireResult::ReachedGrenadeTotalLimit;
		}

		// don't allow players with an inferno spawning weapon to pick up another inferno spawning weapon
		if ( weaponId == WEAPON_INCGRENADE )
		{
			 if ( Weapon_OwnsThisType( "weapon_molotov" ) )
				 return AcquireResult::AlreadyOwned;
		}
		else if ( weaponId == WEAPON_MOLOTOV )
		{
			if ( Weapon_OwnsThisType( "weapon_incgrenade" ) )
				return AcquireResult::AlreadyOwned;
		}
	}
	else if ( nType == WEAPONTYPE_STACKABLEITEM )
	{
		int carryLimit = GetAmmoDef()->MaxCarry( pWeaponInfo->iAmmoType, this );

		CBaseCombatWeapon* pItemWeapon = Weapon_OwnsThisType( WeaponIdAsString( weaponId ) );
		if ( pItemWeapon != NULL )
		{
			int nAmmoType = pItemWeapon->GetPrimaryAmmoType();

			if ( nAmmoType != -1 )
			{
				int thisCarried = GetAmmoCount( nAmmoType );
				if ( thisCarried >= carryLimit )
				{
					return AcquireResult::ReachedGrenadeTypeLimit;
				}
			}
		}
	}
	else if ( weaponId == WEAPON_C4 )
	{
		// TODO[pmf]: Data drive this from the scripts
		if ( acquireMethod == AcquireMethod::Buy )
			return AcquireResult::NotAllowedForPurchase;
	}

	// additional constraints for purchasing weapons
	if ( acquireMethod == AcquireMethod::Buy )
	{
		if ( pWeaponInfo->m_iTeam != TEAM_UNASSIGNED && GetTeamNumber() != pWeaponInfo->m_iTeam )
		{
			return AcquireResult::NotAllowedByTeam;
		}

		// special case for flashbangs - no limit
		if ( weaponId == WEAPON_FLASHBANG )
		{
			return AcquireResult::Allowed;
		}

		// don't allow purchasing multiple grenades of a given type per round (even if the player throws the purchased one)
		if ( pWeaponInfo->m_WeaponType == WEAPONTYPE_GRENADE )
		{
			// limit the number of purchases to one more than the number we are allowed to carry
			int carryLimitThisGrenade = GetAmmoDef()->MaxCarry( pWeaponInfo->iAmmoType, this ); // We still use player-stored ammo for grenades.

			// for smoke grenade, we are only allow to buy exactly the amount we are allowed to carry, with other weapons, we can purchase one more than what we can carry per round
			if ( weaponId == WEAPON_SMOKEGRENADE && carryLimitThisGrenade > 0 )
				carryLimitThisGrenade--;
		}

		if ( CSLoadout()->IsKnife(weaponId) )
		{ 
			return AcquireResult::NotAllowedForPurchase; 
		}
	}

	return AcquireResult::Allowed;
}

bool CCSPlayer::UpdateDispatchLayer( CAnimationLayer *pLayer, CStudioHdr *pWeaponStudioHdr, int iSequence )
{
	if ( !pWeaponStudioHdr || !pLayer )
	{
		if ( pLayer )
			pLayer->m_nDispatchedDst = ACT_INVALID;
		return false;
	}	

	if ( pLayer->m_pDispatchedStudioHdr != pWeaponStudioHdr || pLayer->m_nDispatchedSrc != iSequence || pLayer->m_nDispatchedDst >= pWeaponStudioHdr->GetNumSeq() )
	{
		pLayer->m_pDispatchedStudioHdr = pWeaponStudioHdr;
		pLayer->m_nDispatchedSrc = iSequence;
		if ( pWeaponStudioHdr )
		{
			const char *pszSeqName = GetSequenceName( iSequence );
			
#ifdef DEBUG
			if ( V_stristr( pszSeqName, "default" ) )
			{
				AssertMsg( false, "Warning: weapon is attempting to play its default sequence as a dispatched anim.\n" );
			}
#endif

			// check if the weapon has a CT or T specific version of this sequence (denoted by a _t or ct suffix)
			if ( GetTeamNumber() == TEAM_TERRORIST )
			{
				char pszLayerNameT[128];
				V_sprintf_safe( pszLayerNameT, "%s_t", pszSeqName );
				int nTeamSpecificSequenceIndex = pWeaponStudioHdr->LookupSequence( pszLayerNameT );
				if ( nTeamSpecificSequenceIndex > 0 )
				{
					pLayer->m_nDispatchedDst = nTeamSpecificSequenceIndex;
					return true;
				}
			}

			pLayer->m_nDispatchedDst = pWeaponStudioHdr->LookupSequence( pszSeqName );
		}
		else
		{
			pLayer->m_nDispatchedDst = ACT_INVALID;
		}
	}
	return (pLayer->m_nDispatchedDst > 0 );
}

bool CCSPlayer::UpdateLayerWeaponDispatch( CAnimationLayer *pLayer, int iSequence )
{
	CBaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		CBaseWeaponWorldModel *pWeaponWorldModel = pWeapon->GetWeaponWorldModel();
		if ( pWeaponWorldModel )
		{
			return UpdateDispatchLayer( pLayer, pWeaponWorldModel->GetModelPtr(), iSequence );
		}
	}
	return UpdateDispatchLayer( pLayer, NULL, iSequence );
}

float CCSPlayer::GetLayerSequenceCycleRate( CAnimationLayer *pLayer, int iSequence ) 
{ 
	UpdateLayerWeaponDispatch( pLayer, iSequence );
	if ( pLayer->m_nDispatchedDst != ACT_INVALID )
	{
		// weapon world model overrides rate
		return GetSequenceCycleRate( pLayer->m_pDispatchedStudioHdr, pLayer->m_nDispatchedDst );
	}
	return BaseClass::GetLayerSequenceCycleRate( pLayer, iSequence );
}

//--------------------------------------------------------------------------------------------------------------

#define MATERIAL_NAME_LENGTH 16

#ifdef GAME_DLL

class CFootstepControl : public CBaseTrigger
{
public:
	DECLARE_CLASS( CFootstepControl, CBaseTrigger );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	virtual int UpdateTransmitState( void );
	virtual void Spawn( void );

	CNetworkVar( string_t, m_source );
	CNetworkVar( string_t, m_destination );
};

LINK_ENTITY_TO_CLASS( func_footstep_control, CFootstepControl );


BEGIN_DATADESC( CFootstepControl )
	DEFINE_KEYFIELD( m_source, FIELD_STRING, "Source" ),
	DEFINE_KEYFIELD( m_destination, FIELD_STRING, "Destination" ),
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CFootstepControl, DT_FootstepControl )
	SendPropStringT( SENDINFO(m_source) ),
	SendPropStringT( SENDINFO(m_destination) ),
END_SEND_TABLE()

int CFootstepControl::UpdateTransmitState( void )
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

void CFootstepControl::Spawn( void )
{
	InitTrigger();
}

#else

//--------------------------------------------------------------------------------------------------------------

class C_FootstepControl : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_FootstepControl, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	C_FootstepControl( void );
	~C_FootstepControl();

	char m_source[MATERIAL_NAME_LENGTH];
	char m_destination[MATERIAL_NAME_LENGTH];
};

IMPLEMENT_CLIENTCLASS_DT(C_FootstepControl, DT_FootstepControl, CFootstepControl)
	RecvPropString( RECVINFO(m_source) ),
	RecvPropString( RECVINFO(m_destination) ),
END_RECV_TABLE()

CUtlVector< C_FootstepControl * > s_footstepControllers;

C_FootstepControl::C_FootstepControl( void )
{
	s_footstepControllers.AddToTail( this );
}

C_FootstepControl::~C_FootstepControl()
{
	s_footstepControllers.FindAndRemove( this );
}

surfacedata_t * CCSPlayer::GetFootstepSurface( const Vector &origin, const char *surfaceName )
{
	for ( int i=0; i<s_footstepControllers.Count(); ++i )
	{
		C_FootstepControl *control = s_footstepControllers[i];

		if ( FStrEq( control->m_source, surfaceName ) )
		{
			if ( control->CollisionProp()->IsPointInBounds( origin ) )
			{
				return physprops->GetSurfaceData( physprops->GetSurfaceIndex( control->m_destination ) );
			}
		}
	}

	return physprops->GetSurfaceData( physprops->GetSurfaceIndex( surfaceName ) );
}

#endif


