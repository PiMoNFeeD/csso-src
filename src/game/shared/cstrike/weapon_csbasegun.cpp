//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "weapon_csbasegun.h"
#include "fx_cs_shared.h"

#ifdef CLIENT_DLL
	#include "c_cs_player.h"
#else
	#include "cs_player.h"
#endif


IMPLEMENT_NETWORKCLASS_ALIASED( WeaponCSBaseGun, DT_WeaponCSBaseGun )

BEGIN_NETWORK_TABLE( CWeaponCSBaseGun, DT_WeaponCSBaseGun )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CWeaponCSBaseGun )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_csbase_gun, CWeaponCSBaseGun );



CWeaponCSBaseGun::CWeaponCSBaseGun()
{
}

void CWeaponCSBaseGun::Spawn()
{
	m_flAccuracy = 0.2;
	m_bDelayFire = false;
	m_zoomFullyActiveTime = -1.0f;

	BaseClass::Spawn();

	ResetPostponeFireReadyTime();
}


bool CWeaponCSBaseGun::Deploy()
{
	CCSPlayer *pPlayer = GetPlayerOwner();
	if ( !pPlayer )
		return false;

	m_flAccuracy = 0.2;
	pPlayer->m_iShotsFired = 0;
	m_bDelayFire = false;
	m_zoomFullyActiveTime = -1.0f;

	return BaseClass::Deploy();
}

void CWeaponCSBaseGun::ItemPostFrame()
{
	CCSPlayer *pPlayer = GetPlayerOwner();

	if ( !pPlayer )
		return;

	//GOOSEMAN : Return zoom level back to previous zoom level before we fired a shot. This is used only for the AWP.
	// And Scout.
	if ( (m_flNextPrimaryAttack <= gpGlobals->curtime) && (pPlayer->m_bResumeZoom == TRUE) )
	{
		pPlayer->m_bResumeZoom = false;
		
		if ( m_iClip1 != 0 || ( GetWeaponFlags() & ITEM_FLAG_NOAUTORELOAD ) )
		{
			m_weaponMode = Secondary_Mode;
			pPlayer->SetFOV( pPlayer, pPlayer->m_iLastZoom, 0.05f );
			m_zoomFullyActiveTime = gpGlobals->curtime + 0.05f;// Make sure we think that we are zooming on the server so we don't get instant acc bonus
		}
	}

	BaseClass::ItemPostFrame();
}


void CWeaponCSBaseGun::PrimaryAttack()
{
	// Derived classes should implement this and call CSBaseGunFire.
	Assert( false );
}

void CWeaponCSBaseGun::SecondaryAttack()
{
	CCSPlayer *pPlayer = GetPlayerOwner();
	if ( pPlayer == NULL )
	{
		Assert( pPlayer != NULL );
		return;
	}
	else
	{
		if ( IsRevolver() && m_flNextSecondaryAttack < gpGlobals->curtime )
		{
			float flCycletimeAlt = GetCSWpnData().m_flCycleTimeAlt;
			m_weaponMode = Secondary_Mode;
			UpdateAccuracyPenalty();
#ifndef CLIENT_DLL
			// Logic for weapon_fire event mimics weapon_csbase.cpp CWeaponCSBase::ItemPostFrame() primary fire implementation
			IGameEvent * event = gameeventmanager->CreateEvent( (HasAmmo()) ? "weapon_fire" : "weapon_fire_on_empty" );
			if ( event )
			{
				const char *weaponName = STRING( m_iClassname );
				if ( strncmp( weaponName, "weapon_", 7 ) == 0 )
				{
					weaponName += 7;
				}

				event->SetInt( "userid", pPlayer->GetUserID() );
				event->SetString( "weapon", weaponName );
				event->SetBool( "silenced", IsSilenced() );
				gameeventmanager->FireEvent( event );
			}
#endif
			CSBaseGunFire( flCycletimeAlt, Secondary_Mode );								// <--	'PEW PEW' HAPPENS HERE
			m_flNextSecondaryAttack = gpGlobals->curtime + flCycletimeAlt;
			return;
		}

		BaseClass::SecondaryAttack();
	}
}

bool CWeaponCSBaseGun::CSBaseGunFire( float flCycleTime, CSWeaponMode weaponMode )
{
	CCSPlayer *pPlayer = GetPlayerOwner();
	if ( !pPlayer )
		return false;

	m_bDelayFire = true;

	if ( m_iClip1 == 0 )
	{
		m_flAccuracy = 0;

		if ( m_bFireOnEmpty )
		{
			PlayEmptySound();

			m_iNumEmptyAttacks++;

			// NOTE[pmf]: we don't want to actually play the dry fire animations, as most seem to depict the weapon actually firing.
			// SendWeaponAnim( ACT_VM_DRYFIRE );

			//++pPlayer->m_iShotsFired;	// don't play "auto" empty clicks -- make the player release the trigger before clicking again
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.2f;

			if ( IsRevolver() )
			{
				m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + ((weaponMode == Primary_Mode) ? (GetCSWpnData().m_flCycleTime) : (GetCSWpnData().m_flCycleTimeAlt));
				BaseClass::SendWeaponAnim( ACT_VM_DRYFIRE ); // empty!
			}
			m_bFireOnEmpty = false;
		}

		return false;
	}

	float flCurAttack = CalculateNextAttackTime( flCycleTime );

	if ( (IsRevolver() && weaponMode == Secondary_Mode) )
	{
		SendWeaponAnim( ACT_VM_SECONDARYATTACK );
	}
	else if ( IsRevolver() )
	{
		BaseClass::SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	}
	else
	{
#if IRONSIGHT
		CIronSightController* pIronSightController = GetIronSightController();
		if ( pIronSightController )
			// hacky but for some reason IsInIronSight() returns false server-side resulting in a wrong anim
			//SendWeaponAnim( pIronSightController->IsInIronSight() ? ACT_VM_SECONDARYATTACK : ACT_VM_PRIMARYATTACK );
			SendWeaponAnim( (weaponMode == Secondary_Mode) ? ACT_VM_SECONDARYATTACK : ACT_VM_PRIMARYATTACK );
		else
#endif
			SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	}

	++pPlayer->m_iShotsFired;
	m_iClip1--;

	// player "shoot" animation
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	FX_FireBullets(
		pPlayer->entindex(),
		pPlayer->Weapon_ShootPosition(),
		pPlayer->EyeAngles() + 2.0f * pPlayer->GetPunchAngle(),
		GetWeaponID(),
		weaponMode,
		CBaseEntity::GetPredictionRandomSeed() & 255,
		GetInaccuracy(),
		GetSpread(), 
		flCurAttack );

	DoFireEffects();

#if IRONSIGHT
#ifdef CLIENT_DLL
	if ( GetIronSightController() )
	{
		GetIronSightController()->IncreaseDotBlur( RandomFloat( 0.22f, 0.28f ) );
	}
#endif
#endif

	SetWeaponIdleTime( gpGlobals->curtime + GetCSWpnData().m_flTimeToIdleAfterFire );

	// update accuracy
	m_fAccuracyPenalty += GetCSWpnData().m_fInaccuracyImpulseFire[weaponMode];

	++pPlayer->m_iShotsFired;

	return true;
}


void CWeaponCSBaseGun::DoFireEffects()
{
	CCSPlayer *pPlayer = GetPlayerOwner();
	
	if ( pPlayer )
		 pPlayer->DoMuzzleFlash();
}


bool CWeaponCSBaseGun::Reload()
{
	CCSPlayer *pPlayer = GetPlayerOwner();
	if ( !pPlayer )
		return false;

	if ( GetReserveAmmoCount( AMMO_POSITION_PRIMARY ) <= 0 )
		return false;

	pPlayer->SetFOV( pPlayer, pPlayer->GetDefaultFOV(), 0.0f );

	int iResult = DefaultReload( GetMaxClip1(), GetMaxClip2(), ACT_VM_RELOAD );
	if ( !iResult )
		return false;

#if IRONSIGHT
	m_iIronSightMode = IronSight_should_approach_unsighted;
#endif //IRONSIGHT

	pPlayer->SetAnimation( PLAYER_RELOAD );

	if ((iResult) && (pPlayer->GetFOV() != pPlayer->GetDefaultFOV()))
	{
		pPlayer->SetFOV( pPlayer, pPlayer->GetDefaultFOV() );
	}

	m_flAccuracy = 0.2;
	pPlayer->m_iShotsFired = 0;
	m_bDelayFire = false;

	pPlayer->SetShieldDrawnState( false );
	return true;
}

void CWeaponCSBaseGun::WeaponIdle()
{
	if (m_flTimeWeaponIdle > gpGlobals->curtime)
		return;

	// only idle if the slid isn't back
	if ( m_iClip1 != 0 )
	{
		SetWeaponIdleTime( gpGlobals->curtime + GetCSWpnData().m_flIdleInterval );
		SendWeaponAnim( ACT_VM_IDLE );
	}
}

bool CWeaponCSBaseGun::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	/* re-deploying the weapon is punishment enough for canceling a silencer attach/detach before completion
	if ( (GetActivity() == ACT_VM_ATTACH_SILENCER && m_bSilencerOn == false) ||
		 (GetActivity() == ACT_VM_DETACH_SILENCER && m_bSilencerOn == true) )
	{
		m_flDoneSwitchingSilencer = gpGlobals->curtime;
		m_flNextSecondaryAttack = gpGlobals->curtime;
		m_flNextPrimaryAttack = gpGlobals->curtime;
	}*/

	// not sure we want to fully support animation cancelling
	if ( m_bInReload && !m_bReloadVisuallyComplete )
	{
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime;
	}
	return BaseClass::Holster( pSwitchingTo );
}
