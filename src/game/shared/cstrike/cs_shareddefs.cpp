//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "cs_shareddefs.h"

const float CS_PLAYER_SPEED_RUN			= 260.0f;
const float CS_PLAYER_SPEED_VIP			= 227.0f;
//const float CS_PLAYER_SPEED_WALK		= 100.0f;
const float CS_PLAYER_SPEED_SHIELD		= 160.0f;
const float CS_PLAYER_SPEED_STOPPED		=   1.0f;
const float CS_PLAYER_SPEED_HAS_HOSTAGE	= 200.0f;
const float CS_PLAYER_SPEED_OBSERVER	= 900.0f;

const float CS_PLAYER_SPEED_DUCK_MODIFIER	= 0.34f;
const float CS_PLAYER_SPEED_WALK_MODIFIER	= 0.52f;
const float CS_PLAYER_SPEED_CLIMB_MODIFIER	= 0.34f;

const float CS_PLAYER_DUCK_SPEED_IDEAL = 8.0f;


CCSClassInfo g_ClassInfos[] =
{
	{ "None", "" },

	{ "Phoenix",		"Phoenix"		},
	{ "Leet Crew",		"Leet"			},
	{ "Separatist",		"Separatist"	},
	{ "Balkan",			"Balkan"		},
	{ "Professional",	"Professional"	},
	{ "Anarchist",		"Anarchist"		},
	{ "Pirate",			"Pirate"		},

	{ "Seal Team 6",	"ST6"			},
	{ "GSG-9",			"GSG9"			},
	{ "SAS",			"SAS"			},
	{ "GIGN",			"GIGN"			},
	{ "FBI",			"FBI"			},
	{ "IDF",			"IDF"			},
	{ "SWAT",			"SWAT"			}
};

const CCSClassInfo* GetCSClassInfo( int i )
{
	Assert( i >= 0 && i < ARRAYSIZE( g_ClassInfos ) );
	return &g_ClassInfos[i];
}

static PlayerGloves s_playerGloves[MAX_GLOVES+1] =
{
	{ NULL, NULL },

	{ "models/weapons/v_models/arms/glove_bloodhound/v_glove_bloodhound.mdl",				"models/weapons/w_models/arms/w_glove_bloodhound.mdl"				},
	{ "models/weapons/v_models/arms/glove_bloodhound/v_glove_bloodhound_brokenfang.mdl",	"models/weapons/w_models/arms/w_glove_bloodhound_brokenfang.mdl"	},
	{ "models/weapons/v_models/arms/glove_bloodhound/v_glove_bloodhound_hydra.mdl",			"models/weapons/w_models/arms/w_glove_bloodhound_hydra.mdl"			},
	{ "models/weapons/v_models/arms/glove_fingerless/v_glove_fingerless.mdl",				"models/weapons/w_models/arms/w_glove_fingerless.mdl"				},
	{ "models/weapons/v_models/arms/glove_fullfinger/v_glove_fullfinger.mdl",				"models/weapons/w_models/arms/w_glove_fullfinger.mdl"				},
	{ "models/weapons/v_models/arms/glove_handwrap_leathery/v_glove_handwrap_leathery.mdl",	"models/weapons/w_models/arms/w_glove_handwrap_leathery.mdl"		},
	{ "models/weapons/v_models/arms/glove_hardknuckle/v_glove_hardknuckle.mdl",				"models/weapons/w_models/arms/w_glove_hardknuckle.mdl"				},
	{ "models/weapons/v_models/arms/glove_hardknuckle/v_glove_hardknuckle_black.mdl",		"models/weapons/w_models/arms/w_glove_hardknuckle_black.mdl"		},
	{ "models/weapons/v_models/arms/glove_hardknuckle/v_glove_hardknuckle_blue.mdl",		"models/weapons/w_models/arms/w_glove_hardknuckle_blue.mdl"			},
	{ "models/weapons/v_models/arms/glove_motorcycle/v_glove_motorcycle.mdl",				"models/weapons/w_models/arms/w_glove_motorcycle.mdl"				},
	{ "models/weapons/v_models/arms/glove_slick/v_glove_slick.mdl",							"models/weapons/w_models/arms/w_glove_slick.mdl"					},
	{ "models/weapons/v_models/arms/glove_specialist/v_glove_specialist.mdl",				"models/weapons/w_models/arms/w_glove_specialist.mdl"				},
	{ "models/weapons/v_models/arms/glove_sporty/v_glove_sporty.mdl",						"models/weapons/w_models/arms/w_glove_sporty.mdl"					},
	{ "models/weapons/v_models/arms/glove_sas_old/v_glove_sas_old.mdl",						"models/weapons/w_models/arms/w_glove_sas_old.mdl"					},
	{ "models/weapons/v_models/arms/glove_fbi_old/v_glove_fbi_old.mdl",						"models/weapons/w_models/arms/w_glove_fbi_old.mdl"					},
	{ "models/weapons/v_models/arms/glove_phoenix_old/v_glove_phoenix_old.mdl",				"models/weapons/w_models/arms/w_glove_phoenix_old.mdl"				},
};

const PlayerGloves* GetGlovesInfo( int i )
{
	Assert( i >= 0 && i < ARRAYSIZE( s_playerGloves ) );
	return &s_playerGloves[i];
}

const PlayerViewmodelArmConfig *GetPlayerViewmodelArmConfigForPlayerModel( const char* szPlayerModel )
{
	if ( szPlayerModel != NULL )
	{
		for ( int i=0; i<ARRAYSIZE(s_playerViewmodelArmConfigs); i++ )
		{
			if ( V_stristr( szPlayerModel, s_playerViewmodelArmConfigs[i].szPlayerModelSearchSubStr ) )
				return &s_playerViewmodelArmConfigs[i];
		}
	}

	AssertMsg1( false, "Could not determine viewmodel config for character model: %s", szPlayerModel );
	return &s_playerViewmodelArmConfigs[0];
}

CCSAgentInfo g_AgentInfosCT[MAX_AGENTS_CT + 1] =
{
	{ "", "", 0, 0 },
	// Shattered Web
	{ "models/player/custom_player/legacy/ctm_fbi_variantf.mdl",	"FBI",			CS_CLASS_FBI,			false	},
	{ "models/player/custom_player/legacy/ctm_fbi_variantg.mdl",	"FBI",			CS_CLASS_FBI,			false	},
	{ "models/player/custom_player/legacy/ctm_fbi_varianth.mdl",	"FBI",			CS_CLASS_FBI,			false	},
	{ "models/player/custom_player/legacy/ctm_fbi_variantb.mdl",	"FBI_Epic",		CS_CLASS_FBI,			true	},
	{ "models/player/custom_player/legacy/ctm_sas_variantf.mdl",	"SAS",			CS_CLASS_SAS,			false	},
	{ "models/player/custom_player/legacy/ctm_st6_variantk.mdl",	"GSG9",			CS_CLASS_SEAL_TEAM_6,	false	},
	{ "models/player/custom_player/legacy/ctm_st6_variante.mdl",	"ST6",			CS_CLASS_SEAL_TEAM_6,	false	},
	{ "models/player/custom_player/legacy/ctm_st6_variantg.mdl",	"ST6",			CS_CLASS_SEAL_TEAM_6,	false	},
	{ "models/player/custom_player/legacy/ctm_st6_variantm.mdl",	"ST6",			CS_CLASS_SEAL_TEAM_6,	false	},
	{ "models/player/custom_player/legacy/ctm_st6_varianti.mdl",	"ST6_Epic",		CS_CLASS_SEAL_TEAM_6,	false	},
	// Broken Fang
	{ "models/player/custom_player/legacy/ctm_swat_variantj.mdl",	"SWAT",			CS_CLASS_SWAT,			false	},
	{ "models/player/custom_player/legacy/ctm_swat_varianth.mdl",	"SWAT",			CS_CLASS_SWAT,			false	},
	{ "models/player/custom_player/legacy/ctm_st6_variantj.mdl",	"ST6",			CS_CLASS_SEAL_TEAM_6,	false	},
	{ "models/player/custom_player/legacy/ctm_swat_variantg.mdl",	"SWAT",			CS_CLASS_SWAT,			false	},
	{ "models/player/custom_player/legacy/ctm_swat_varianti.mdl",	"SWAT",			CS_CLASS_SWAT,			false	},
	{ "models/player/custom_player/legacy/ctm_swat_variantf.mdl",	"SWAT_Fem",		CS_CLASS_SWAT,			true	},
	{ "models/player/custom_player/legacy/ctm_st6_variantl.mdl",	"ST6",			CS_CLASS_SEAL_TEAM_6,	false	},
	{ "models/player/custom_player/legacy/ctm_swat_variante.mdl",	"SWAT_Epic",	CS_CLASS_SWAT,			true	},
	// what?
	{ "models/player/ctm_sas_old.mdl",								"SAS",			CS_CLASS_SAS,			false	},
	{ "models/player/ctm_fbi_old.mdl",								"FBI",			CS_CLASS_FBI,			false	}
};

const CCSAgentInfo* GetCSAgentInfoCT( int i )
{
	Assert( i >= 0 && i < ARRAYSIZE( g_AgentInfosCT ) );
	return &g_AgentInfosCT[i];
}

CCSAgentInfo g_AgentInfosT[MAX_AGENTS_T + 1] =
{
	{ "", "", 0, 0 },
	// Shattered Web
	{ "models/player/custom_player/legacy/tm_leet_variantg.mdl",		"Leet",				CS_CLASS_L337_KREW,				false	},
	{ "models/player/custom_player/legacy/tm_leet_varianth.mdl",		"Leet",				CS_CLASS_L337_KREW,				false	},
	{ "models/player/custom_player/legacy/tm_leet_varianti.mdl",		"Leet",				CS_CLASS_L337_KREW,				false	},
	{ "models/player/custom_player/legacy/tm_leet_variantf.mdl",		"Leet_Epic",		CS_CLASS_L337_KREW,				false	},
	{ "models/player/custom_player/legacy/tm_phoenix_varianth.mdl",		"Phoenix",			CS_CLASS_PHOENIX_CONNNECTION,	false	},
	{ "models/player/custom_player/legacy/tm_phoenix_variantf.mdl",		"Phoenix",			CS_CLASS_PHOENIX_CONNNECTION,	false	},
	{ "models/player/custom_player/legacy/tm_phoenix_variantg.mdl",		"Phoenix",			CS_CLASS_PHOENIX_CONNNECTION,	false	},
	{ "models/player/custom_player/legacy/tm_balkan_variantf.mdl",		"Balkan",			CS_CLASS_BALKAN,				false	},
	{ "models/player/custom_player/legacy/tm_balkan_varianti.mdl",		"Balkan",			CS_CLASS_BALKAN,				false	},
	{ "models/player/custom_player/legacy/tm_balkan_variantg.mdl",		"Balkan",			CS_CLASS_BALKAN,				false	},
	{ "models/player/custom_player/legacy/tm_balkan_variantj.mdl",		"Balkan",			CS_CLASS_BALKAN,				false	},
	{ "models/player/custom_player/legacy/tm_balkan_varianth.mdl",		"Balkan_Epic",		CS_CLASS_BALKAN,				false	},
	// Broken Fang
	{ "models/player/custom_player/legacy/tm_balkan_variantl.mdl",		"Balkan",			CS_CLASS_BALKAN,				false	},
	{ "models/player/custom_player/legacy/tm_phoenix_varianti.mdl",		"Phoenix",			CS_CLASS_PHOENIX_CONNNECTION,	false	},
	{ "models/player/custom_player/legacy/tm_professional_varj.mdl",	"Professional_Fem",	CS_CLASS_PROFESSIONAL,			true	},
	{ "models/player/custom_player/legacy/tm_professional_varh.mdl",	"Professional",		CS_CLASS_PROFESSIONAL,			false	},
	{ "models/player/custom_player/legacy/tm_balkan_variantk.mdl",		"Balkan",			CS_CLASS_BALKAN,				false	},
	{ "models/player/custom_player/legacy/tm_professional_varg.mdl",	"Professional_Fem",	CS_CLASS_PROFESSIONAL,			true	},
	{ "models/player/custom_player/legacy/tm_professional_vari.mdl",	"Professional",		CS_CLASS_PROFESSIONAL,			false	},
	{ "models/player/custom_player/legacy/tm_professional_varf.mdl",	"Professional_Epic",CS_CLASS_PROFESSIONAL,			false	},
	{ "models/player/custom_player/legacy/tm_professional_varf1.mdl",	"Professional_Epic",CS_CLASS_PROFESSIONAL,			false	},
	{ "models/player/custom_player/legacy/tm_professional_varf2.mdl",	"Professional_Epic",CS_CLASS_PROFESSIONAL,			false	},
	{ "models/player/custom_player/legacy/tm_professional_varf3.mdl",	"Professional_Epic",CS_CLASS_PROFESSIONAL,			false	},
	{ "models/player/custom_player/legacy/tm_professional_varf4.mdl",	"Professional_Epic",CS_CLASS_PROFESSIONAL,			false	},
	// what?
	{ "models/player/tm_leet_old.mdl",									"Leet",				CS_CLASS_L337_KREW,				false	},
	{ "models/player/tm_phoenix_old.mdl",								"Phoenix",			CS_CLASS_PHOENIX_CONNNECTION,	false	}
};

const CCSAgentInfo* GetCSAgentInfoT( int i )
{
	Assert( i >= 0 && i < ARRAYSIZE( g_AgentInfosT ) );
	return &g_AgentInfosT[i];
}

const char *pszWinPanelCategoryHeaders[] =
{
	"",
	"#winpanel_topdamage",
	"#winpanel_topheadshots",
	"#winpanel_kills"
};

// todo: rewrite this because it's TOO MASSIVE!
const char* TPhoenixPlayerModelStrings[] =
{
	"models/player/custom_player/legacy/tm_phoenix.mdl",
	"models/player/custom_player/legacy/tm_phoenix_varianta.mdl",
	"models/player/custom_player/legacy/tm_phoenix_variantb.mdl",
	"models/player/custom_player/legacy/tm_phoenix_variantc.mdl",
	"models/player/custom_player/legacy/tm_phoenix_variantd.mdl",
};
const char* TLeetPlayerModelStrings[] =
{
	"models/player/custom_player/legacy/tm_leet_variantA.mdl",
	"models/player/custom_player/legacy/tm_leet_variantB.mdl",
	"models/player/custom_player/legacy/tm_leet_variantC.mdl",
	"models/player/custom_player/legacy/tm_leet_variantD.mdl",
	"models/player/custom_player/legacy/tm_leet_variantE.mdl",

};
const char *TSeparatistPlayerModelStrings[] =
{
	"models/player/custom_player/legacy/tm_separatist.mdl",
	"models/player/custom_player/legacy/tm_separatist_varianta.mdl",
	"models/player/custom_player/legacy/tm_separatist_variantb.mdl",
	"models/player/custom_player/legacy/tm_separatist_variantc.mdl",
	"models/player/custom_player/legacy/tm_separatist_variantd.mdl",
};
const char* TBalkanPlayerModelStrings[] =
{
	"models/player/tm_balkan_varianta.mdl",
	"models/player/tm_balkan_variantb.mdl",
	"models/player/tm_balkan_variantc.mdl",
	"models/player/tm_balkan_variantd.mdl",
	"models/player/tm_balkan_variante.mdl",
};
const char* TProfessionalPlayerModelStrings[] =
{
	"models/player/tm_professional.mdl",
	"models/player/tm_professional_var1.mdl",
	"models/player/tm_professional_var2.mdl",
	"models/player/tm_professional_var3.mdl",
	"models/player/tm_professional_var4.mdl",
};
const char* TAnarchistPlayerModelStrings[] =
{
	"models/player/tm_anarchist.mdl",
	"models/player/tm_anarchist_varianta.mdl",
	"models/player/tm_anarchist_variantb.mdl",
	"models/player/tm_anarchist_variantc.mdl",
	"models/player/tm_anarchist_variantd.mdl",
};
const char* TPiratePlayerModelStrings[] =
{
	"models/player/tm_pirate.mdl",
	"models/player/tm_pirate_varianta.mdl",
	"models/player/tm_pirate_variantb.mdl",
	"models/player/tm_pirate_variantc.mdl",
	"models/player/tm_pirate_variantd.mdl",
};
CUtlVectorInitialized< const char * > TPhoenixPlayerModels( TPhoenixPlayerModelStrings, ARRAYSIZE( TPhoenixPlayerModelStrings ) );
CUtlVectorInitialized< const char * > TLeetPlayerModels( TLeetPlayerModelStrings, ARRAYSIZE( TLeetPlayerModelStrings ) );
CUtlVectorInitialized< const char * > TSeparatistPlayerModels( TSeparatistPlayerModelStrings, ARRAYSIZE( TSeparatistPlayerModelStrings ) );
CUtlVectorInitialized< const char * > TBalkanPlayerModels( TBalkanPlayerModelStrings, ARRAYSIZE( TBalkanPlayerModelStrings ) );
CUtlVectorInitialized< const char * > TProfessionalPlayerModels( TProfessionalPlayerModelStrings, ARRAYSIZE( TProfessionalPlayerModelStrings ) );
CUtlVectorInitialized< const char * > TAnarchistPlayerModels( TAnarchistPlayerModelStrings, ARRAYSIZE( TAnarchistPlayerModelStrings ) );
CUtlVectorInitialized< const char * > TPiratePlayerModels( TPiratePlayerModelStrings, ARRAYSIZE( TPiratePlayerModelStrings ) );

const char* CTST6PlayerModelStrings[] =
{
	"models/player/ctm_st6.mdl",
	"models/player/ctm_st6_varianta.mdl",
	"models/player/ctm_st6_variantb.mdl",
	"models/player/ctm_st6_variantc.mdl",
	"models/player/ctm_st6_variantd.mdl",
};
const char* CTGSG9PlayerModelStrings[] =
{
	"models/player/ctm_gsg9.mdl",
	"models/player/ctm_gsg9_varianta.mdl",
	"models/player/ctm_gsg9_variantb.mdl",
	"models/player/ctm_gsg9_variantc.mdl",
	"models/player/ctm_gsg9_variantd.mdl",
};
const char* CTSASPlayerModelStrings[] =
{
	"models/player/custom_player/legacy/ctm_sas.mdl",
};
const char* CTGIGNPlayerModelStrings[] =
{
	"models/player/ctm_gign.mdl",
	"models/player/ctm_gign_varianta.mdl",
	"models/player/ctm_gign_variantb.mdl",
	"models/player/ctm_gign_variantc.mdl",
	"models/player/ctm_gign_variantd.mdl",
};
const char* CTFBIPlayerModelStrings[] =
{
	"models/player/custom_player/legacy/ctm_fbi.mdl",
	"models/player/custom_player/legacy/ctm_fbi_varianta.mdl",
	"models/player/custom_player/legacy/ctm_fbi_variantc.mdl",
	"models/player/custom_player/legacy/ctm_fbi_variantd.mdl",
	"models/player/custom_player/legacy/ctm_fbi_variante.mdl",
};
const char* CTIDFPlayerModelStrings[] =
{
	"models/player/ctm_idf.mdl",
	"models/player/ctm_idf_variantb.mdl",
	"models/player/ctm_idf_variantc.mdl",
	"models/player/ctm_idf_variantd.mdl",
	"models/player/ctm_idf_variante.mdl",
	"models/player/ctm_idf_variantf.mdl",
};
const char* CTSWATPlayerModelStrings[] =
{
	"models/player/ctm_swat.mdl",
	"models/player/ctm_swat_varianta.mdl",
	"models/player/ctm_swat_variantb.mdl",
	"models/player/ctm_swat_variantc.mdl",
	"models/player/ctm_swat_variantd.mdl",
};
CUtlVectorInitialized< const char * > CTST6PlayerModels( CTST6PlayerModelStrings, ARRAYSIZE( CTST6PlayerModelStrings ) );
CUtlVectorInitialized< const char * > CTGSG9PlayerModels( CTGSG9PlayerModelStrings, ARRAYSIZE( CTGSG9PlayerModelStrings ) );
CUtlVectorInitialized< const char * > CTSASPlayerModels( CTSASPlayerModelStrings, ARRAYSIZE( CTSASPlayerModelStrings ) );
CUtlVectorInitialized< const char * > CTGIGNPlayerModels( CTGIGNPlayerModelStrings, ARRAYSIZE( CTGIGNPlayerModelStrings ) );
CUtlVectorInitialized< const char * > CTFBIPlayerModels( CTFBIPlayerModelStrings, ARRAYSIZE( CTFBIPlayerModelStrings ) );
CUtlVectorInitialized< const char * > CTIDFPlayerModels( CTIDFPlayerModelStrings, ARRAYSIZE( CTIDFPlayerModelStrings ) );
CUtlVectorInitialized< const char * > CTSWATPlayerModels( CTSWATPlayerModelStrings, ARRAYSIZE( CTSWATPlayerModelStrings ) );

// any new knives? add them here
const char *KnivesEntitiesStrings[] =
{
	"weapon_knife",
	"weapon_knife_t",
	"weapon_knife_css",
	"weapon_knife_karambit",
	"weapon_knife_flip",
	"weapon_knife_bayonet",
	"weapon_knife_m9_bayonet",
	"weapon_knife_butterfly",
	"weapon_knife_gut",
	"weapon_knife_tactical",
	"weapon_knife_falchion",
	"weapon_knife_survival_bowie",
	"weapon_knife_canis",
	"weapon_knife_cord",
	"weapon_knife_gypsy_jackknife",
	"weapon_knife_outdoor",
	"weapon_knife_skeleton",
	"weapon_knife_stiletto",
	"weapon_knife_ursus",
	"weapon_knife_widowmaker",
};
CUtlVectorInitialized< const char * > KnivesEntities( KnivesEntitiesStrings, ARRAYSIZE( KnivesEntitiesStrings ) );