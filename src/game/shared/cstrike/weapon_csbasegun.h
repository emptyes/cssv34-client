//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef WEAPON_CSBASE_GUN_H
#define WEAPON_CSBASE_GUN_H
#ifdef _WIN32
#pragma once
#endif


#include "weapon_csbase.h"


// This is the base class for pistols and rifles.
#if defined( CLIENT_DLL )

	#define CWeaponCSBaseGun C_WeaponCSBaseGun

#else
#endif


class CWeaponCSBaseGun : public CWeaponCSBase
{
public:
	
	DECLARE_CLASS( CWeaponCSBaseGun, CWeaponCSBase );
	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();
	
	CWeaponCSBaseGun();

	virtual void PrimaryAttack();
	virtual void Spawn();
	virtual bool Deploy();
	virtual bool Reload();
	virtual void WeaponIdle();

	// Derived classes call this to fire a bullet.
	// flSpread < 0: the spread is GetInaccuracy() taken before this shot updates m_flAccuracy,
	// as the v34 weapons computed it before calling CSBaseGunFire. Weapons that change their
	// accuracy before the call pass the spread computed beforehand.
	bool CSBaseGunFire( float flCycleTime, CSWeaponMode weaponMode, float flSpread = -1.0f );

	// Usually plays the shot sound. Guns with silencers can play different sounds.
	virtual void DoFireEffects();
	virtual void ItemPostFrame();

protected: 
	float m_zoomFullyActiveTime;

private:

	CWeaponCSBaseGun( const CWeaponCSBaseGun & );
};


#endif // WEAPON_CSBASE_GUN_H
