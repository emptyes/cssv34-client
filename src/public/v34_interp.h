//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Interpolation (lerp) time as computed by CS:S v34 servers.
//
//=============================================================================//

#ifndef V34_INTERP_H
#define V34_INTERP_H
#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// v34 server.dll (CServerGameClients::ClientSettingsChanged) rewinds players for
// lag compensation by
//
//		m_fLerpTime = cl_interp_ratio / cl_updaterate
//
// where cl_interp_ratio 0 means 1, the ratio is clamped by sv_client_min/max_interp_ratio
// when the server has them, and cl_updaterate is Q_atoi'ed and clamped by
// sv_min/maxupdaterate. cl_interp is not read at all (unlike Source 2006, which used
// cl_interp alone, and Source 2007, which used MAX( cl_interp, ratio / updaterate )).
//
// The client has to render other players with exactly this amount: anything else
// makes the server rewind them to a different position than the one the player
// shot at. Callers pass the ratio and update rate already clamped the way the
// server sees them.
//-----------------------------------------------------------------------------
inline float V34_GetLerpTime( float flInterpRatio, int nUpdateRate )
{
	if ( flInterpRatio <= 0.0f )
		flInterpRatio = 1.0f;

	if ( nUpdateRate <= 0 )
		return 0.1f; // can't happen with sv_minupdaterate > 0; same as the initial m_fLerpTime

	return flInterpRatio / (float)nUpdateRate;
}

#endif // V34_INTERP_H
