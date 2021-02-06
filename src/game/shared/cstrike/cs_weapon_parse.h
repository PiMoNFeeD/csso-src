//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef CS_WEAPON_PARSE_H
#define CS_WEAPON_PARSE_H
#ifdef _WIN32
#pragma once
#endif


#include "weapon_parse.h"
#include "networkvar.h"


//--------------------------------------------------------------------------------------------------------
enum CSWeaponType
{
	WEAPONTYPE_KNIFE=0,	
	WEAPONTYPE_PISTOL,
	WEAPONTYPE_SUBMACHINEGUN,
	WEAPONTYPE_RIFLE,
	WEAPONTYPE_SHOTGUN,
	WEAPONTYPE_SNIPER_RIFLE,
	WEAPONTYPE_MACHINEGUN,
	WEAPONTYPE_C4,
	WEAPONTYPE_GRENADE,
	WEAPONTYPE_EQUIPMENT,
	WEAPONTYPE_STACKABLEITEM,
	WEAPONTYPE_UNKNOWN

};


//--------------------------------------------------------------------------------------------------------
enum CSWeaponID
{
	WEAPON_NONE = 0,

	WEAPON_P250,
	WEAPON_GLOCK,
	WEAPON_SSG08,
	WEAPON_HEGRENADE,
	WEAPON_XM1014,
	WEAPON_C4,
	WEAPON_MAC10,
	WEAPON_AUG,
	WEAPON_SMOKEGRENADE,
	WEAPON_ELITE,
	WEAPON_FIVESEVEN,
	WEAPON_UMP45,
	WEAPON_SCAR20,

	WEAPON_GALILAR,
	WEAPON_FAMAS,
	WEAPON_USP,
	WEAPON_AWP,
	WEAPON_MP5SD,
	WEAPON_M249,
	WEAPON_NOVA,
	WEAPON_M4A1,
	WEAPON_MP9,
	WEAPON_G3SG1,
	WEAPON_FLASHBANG,
	WEAPON_DEAGLE,
	WEAPON_SG556,
	WEAPON_AK47,
	WEAPON_KNIFE,
	WEAPON_KNIFE_T,
	WEAPON_KNIFE_CSS,
	WEAPON_KNIFE_KARAMBIT,
	WEAPON_KNIFE_FLIP,
	WEAPON_KNIFE_BAYONET,
	WEAPON_KNIFE_M9_BAYONET,
	WEAPON_KNIFE_BUTTERFLY,
	WEAPON_KNIFE_GUT,
	WEAPON_KNIFE_TACTICAL,
	WEAPON_KNIFE_FALCHION,
	WEAPON_KNIFE_SURVIVAL_BOWIE,
	WEAPON_KNIFE_CANIS,
	WEAPON_KNIFE_CORD,
	WEAPON_KNIFE_GYPSY,
	WEAPON_KNIFE_OUTDOOR,
	WEAPON_KNIFE_SKELETON,
	WEAPON_KNIFE_STILETTO,
	WEAPON_KNIFE_URSUS,
	WEAPON_KNIFE_WIDOWMAKER,
	WEAPON_KNIFE_PUSH,
	WEAPON_P90,

	WEAPON_HKP2000,
	WEAPON_TEC9,
	WEAPON_M4A4,
	WEAPON_REVOLVER,
	WEAPON_CZ75,
	WEAPON_MAG7,
	WEAPON_SAWEDOFF,
	WEAPON_NEGEV,
	WEAPON_MP7,
	WEAPON_BIZON,
	WEAPON_DECOY,
	WEAPON_MOLOTOV,
	WEAPON_INCGRENADE,
	WEAPON_TASER,

	WEAPON_SHIELDGUN,	// BOTPORT: Is this still needed?

	WEAPON_KEVLAR,
	WEAPON_ASSAULTSUIT,
	WEAPON_NVG,

	WEAPON_HEALTHSHOT,

	WEAPON_MAX,		// number of weapons weapon index
};

#define MAX_EQUIPMENT (WEAPON_MAX - WEAPON_KEVLAR)

void PrepareEquipmentInfo( void );

//--------------------------------------------------------------------------------------------------------
const char * WeaponClassAsString( CSWeaponType weaponType );

//--------------------------------------------------------------------------------------------------------
CSWeaponType WeaponClassFromString( const char* weaponType );

//--------------------------------------------------------------------------------------------------------
CSWeaponType WeaponClassFromWeaponID( CSWeaponID weaponID );

//--------------------------------------------------------------------------------------------------------
const char * WeaponIdAsString( CSWeaponID weaponID );

//--------------------------------------------------------------------------------------------------------
CSWeaponID WeaponIdFromString( const char *szWeaponName );


//--------------------------------------------------------------------------------------------------------
class CCSWeaponInfo : public FileWeaponInfo_t
{
public:
	DECLARE_CLASS_GAMEROOT( CCSWeaponInfo, FileWeaponInfo_t );
	
	CCSWeaponInfo();
	
	virtual void Parse( ::KeyValues *pKeyValuesData, const char *szWeaponName );

	int GetRealWeaponPrice( void ) { return m_iWeaponPrice; }


public:

	float m_flMaxSpeed;			// How fast the player can run while this is his primary weapon.

	CSWeaponType m_WeaponType;

	bool	m_bFullAuto;		// is this a fully automatic weapon?

	int m_iTeam;				// Which team can have this weapon. TEAM_UNASSIGNED if both can have it.
	float m_flBotAudibleRange;	// How far away a bot can hear this weapon.
	float m_flArmorRatio;

	int	  m_iCrosshairMinDistance;
	int	  m_iCrosshairDeltaDistance;
	
	bool  m_bCanUseWithShield;
	
	char m_WrongTeamMsg[32];	// Reference to a string describing the error if someone tries to buy
								// this weapon but they're on the wrong team to have it.
								// Zero-length if no specific message for this weapon.

	char m_szAnimExtension[16];
	char m_szUIAnimExtension[16];
	char m_szShieldViewModel[64];

	char m_szAddonModel[MAX_WEAPON_STRING];		// If this is set, it is used as the addon model. Otherwise, szWorldModel is used.
	char m_szMagModel[MAX_WEAPON_STRING];		// Magazine model for dropping magazines
	char m_szDroppedModel[MAX_WEAPON_STRING];	// Alternate dropped model, if different from the szWorldModel the player holds
	char m_szAddonLocation[MAX_WEAPON_STRING];	//If this is set, the weapon will look for an attachment location with this name. Otherwize the default is used based on weapon type.

	float m_flAddonScale;

	// grenade throw parameters
	float	m_fThrowVelocity;
	
	// Parameters for FX_FireBullets:
	float	m_iPenetration;
	int		m_iDamage;
	float	m_flRange;
	float	m_flRangeModifier;
	int		m_iBullets;
	float	m_flCycleTime;
	float	m_flCycleTimeAlt;

	// variables for new accuracy model
	float m_fSpread[2];
	float m_fInaccuracyCrouch[2];
	float m_fInaccuracyStand[2];
	float m_fInaccuracyJump[2];
	float m_fInaccuracyLand[2];
	float m_fInaccuracyLadder[2];
	float m_fInaccuracyImpulseFire[2];
	float m_fInaccuracyMove[2];
	float m_fRecoveryTimeStand;
	float m_fRecoveryTimeCrouch;
	float m_fInaccuracyReload;
	float m_fInaccuracyAltSwitch;

	// Delay until the next idle animation after shooting.
	float	m_flTimeToIdleAfterFire;
	float	m_flIdleInterval;

	// eject brass variables
	char	m_szEjectBrassEffect[MAX_WEAPON_STRING];

	// tracer variables
	int		m_iTracerFrequency[2];
	char	m_szTracerEffect[MAX_WEAPON_STRING];

	// heat variables
	float	m_flHeatPerShot;
	char	m_szHeatEffect[MAX_WEAPON_STRING];

	// muzzle flashes
	char	m_szMuzzleFlash1stPerson[MAX_WEAPON_STRING];
	char	m_szMuzzleFlash1stPersonAlt[MAX_WEAPON_STRING];
	char	m_szMuzzleFlash3rdPerson[MAX_WEAPON_STRING];
	char	m_szMuzzleFlash3rdPersonAlt[MAX_WEAPON_STRING];

	// ironsight
	bool	m_bIronsightCapable;
	float	m_flIronsightSpeedUp;
	float	m_flIronsightSpeedDown;
	float	m_flIronsightLooseness;
	Vector	m_vecIronsightEyePos;
	QAngle	m_angIronsightPivotAngle;
	float	m_flIronsightFOV;
	float	m_flIronsightPivotForward;
	char	m_szIronsightDotMaterial[MAX_WEAPON_STRING];

	// buymenu
	char	m_szBuyMenuAnim[MAX_WEAPON_STRING];
	char	m_szBuyMenuAnimT[MAX_WEAPON_STRING];

	struct RecoilOffset
	{
		float	fAngle;
		float	fMagnitude;
	};
	RecoilOffset	m_recoilTable[2][64];
   
	int		GetKillAward( void ) const;
	int		GetWeaponPrice( void ) const;
	int		GetDefaultPrice( void );
	int		GetPrevousPrice( void );
	void	GenerateRecoilTable();
	void	GetRecoilOffsets( int iMode, int iIndex, float& fAngle, float &fMagnitude ) const;
	void	SetWeaponPrice( int iPrice ) { m_iWeaponPrice = iPrice; }
	void	SetDefaultPrice( int iPrice ) { m_iDefaultPrice = iPrice; }
	void	SetPreviousPrice( int iPrice ) { m_iPreviousPrice = iPrice; }
    
private:

	int m_iWeaponPrice;
	int m_iDefaultPrice;
	int m_iPreviousPrice;

	int m_iKillAward;

};


#endif // CS_WEAPON_PARSE_H
