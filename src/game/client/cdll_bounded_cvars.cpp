//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "c_baseplayer.h"
#include "cdll_bounded_cvars.h"
#include "convar_serverbounded.h"
#include "tier0/icommandline.h"
#include "v34_interp.h"


bool g_bForceCLPredictOff = false;

// ------------------------------------------------------------------------------------------ //
// cl_predict.
// ------------------------------------------------------------------------------------------ //

class CBoundedCvar_Predict : public ConVar_ServerBounded
{
public:
	CBoundedCvar_Predict() :
	  ConVar_ServerBounded( "cl_predict", 
		  "1.0", 
#if defined(DOD_DLL) || defined(CSTRIKE_DLL)
		  FCVAR_USERINFO | FCVAR_CHEAT,
#else
		  FCVAR_USERINFO | FCVAR_NOT_CONNECTED, 
#endif
		  "Perform client side prediction." )
	  {
	  }

	  virtual float GetFloat() const
	  {
		  // Used temporarily for CS kill cam.
		  if ( g_bForceCLPredictOff )
			  return 0;

		  static const ConVar *pClientPredict = g_pCVar->FindVar( "sv_client_predict" );
		  if ( pClientPredict && pClientPredict->GetInt() != -1 )
		  {
			  // Ok, the server wants to control this value.
			  return pClientPredict->GetFloat();
		  }
		  else
		  {
			  return GetBaseFloatValue();
		  }
	  }
};

static CBoundedCvar_Predict cl_predict_var;
ConVar_ServerBounded *cl_predict = &cl_predict_var;



// ------------------------------------------------------------------------------------------ //
// cl_interp_ratio.
// ------------------------------------------------------------------------------------------ //

class CBoundedCvar_InterpRatio : public ConVar_ServerBounded
{
public:
	CBoundedCvar_InterpRatio() :
	  ConVar_ServerBounded( "cl_interp_ratio", 
		  "2.0", 
		  FCVAR_USERINFO | FCVAR_ARCHIVE /* | FCVAR_NOT_CONNECTED*/,
		  "Sets the interpolation amount (final amount is cl_interp_ratio / cl_updaterate, as v34 servers compute it for lag compensation)." )
	  {
	  }

	  virtual float GetFloat() const
	  {
		  static const ConVar *pMin = g_pCVar->FindVar( "sv_client_min_interp_ratio" );
		  static const ConVar *pMax = g_pCVar->FindVar( "sv_client_max_interp_ratio" );
		  if ( pMin && pMax && pMin->GetFloat() != -1 )
		  {
			  return clamp( GetBaseFloatValue(), pMin->GetFloat(), pMax->GetFloat() );
		  }
		  else
		  {
			  return GetBaseFloatValue();
		  }
	  }
};

static CBoundedCvar_InterpRatio cl_interp_ratio_var;
ConVar_ServerBounded *cl_interp_ratio = &cl_interp_ratio_var;


// ------------------------------------------------------------------------------------------ //
// cl_interp
// ------------------------------------------------------------------------------------------ //

class CBoundedCvar_Interp : public ConVar_ServerBounded
{
public:
	CBoundedCvar_Interp() :
	  ConVar_ServerBounded( "cl_interp", 
		  "0.1", 
		  FCVAR_USERINFO | FCVAR_ARCHIVE /* | FCVAR_NOT_CONNECTED*/,
		  "Not used: v34 servers ignore it, the interpolation amount is cl_interp_ratio / cl_updaterate. Kept for configs and server cvar queries.", true, 0.0f, true, 0.5f )
	  {
	  }

	  virtual float GetFloat() const
	  {
		  static const ConVar *pUpdateRate = g_pCVar->FindVar( "cl_updaterate" );
		  static const ConVar *pMin = g_pCVar->FindVar( "sv_client_min_interp_ratio" );
		  if ( pUpdateRate && pMin && pMin->GetFloat() != -1 )
		  {
			  return MAX( GetBaseFloatValue(), pMin->GetFloat() / pUpdateRate->GetFloat() );
		  }
		  else
		  {
			  return GetBaseFloatValue();
		  }
	  }
};

static CBoundedCvar_Interp cl_interp_var;
ConVar_ServerBounded *cl_interp = &cl_interp_var;

// v34 servers don't send sv_client_min_interp_ratio, so cl_interp_ratio 1-2 / cl_updaterate 100 (10-20 ms) is allowed.
// That is too little to smoothly watch another player: the camera jerks back for a frame. While spectating
// we don't shoot, so a bigger interpolation costs nothing.
static ConVar cl_spec_interp( "cl_spec_interp", "0.1", FCVAR_ARCHIVE, "Minimum interpolation amount while spectating (dead/spectator), 0 = same as alive.", true, 0.0f, true, 0.5f );

static float GetSpectatorInterpAmount( float flInterp )
{
	if ( cl_spec_interp.GetFloat() <= flInterp )
		return flInterp;

	C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pLocalPlayer || pLocalPlayer->GetObserverMode() == OBS_MODE_NONE )
		return flInterp;

	return cl_spec_interp.GetFloat();
}

float GetClientInterpAmount()
{
	// Same lerp time the v34 server uses for lag compensation (see v34_interp.h).
	// The update rate is clamped here explicitly: cl_updaterate lives in the engine and
	// is reached through a plain ConVar pointer, so GetFloat() would return the raw
	// value, not the one bounded by the server's sv_min/maxupdaterate.
	static const ConVar *pUpdateRate = g_pCVar->FindVar( "cl_updaterate" );
	static const ConVar *pMinUpdateRate = g_pCVar->FindVar( "sv_minupdaterate" );
	static const ConVar *pMaxUpdateRate = g_pCVar->FindVar( "sv_maxupdaterate" );
	if ( pUpdateRate )
	{
		int nUpdateRate = pUpdateRate->GetInt();
		if ( pMinUpdateRate && pMaxUpdateRate )
			nUpdateRate = clamp( nUpdateRate, (int)pMinUpdateRate->GetFloat(), (int)pMaxUpdateRate->GetFloat() );

		return GetSpectatorInterpAmount( V34_GetLerpTime( cl_interp_ratio->GetFloat(), nUpdateRate ) );
	}
	else
	{
		if (!CommandLine()->FindParm("-hushasserts"))
		{
			AssertMsgOnce( false, "GetInterpolationAmount: can't get cl_updaterate cvar." );
		}
	
		return 0.1;
	}
}

