//========= cssv34-client: model file validation ==============================//
//
// Purpose: Checks .mdl, .vvd and .vtx data before the engine uses it. Models are
//          downloaded from servers, and the engine, the studio renderer and the
//          game code access their tables through offsets and counts from the file.
//          Used by datacache (mdlcache.cpp) and by devtools/check_mdl.
//
//=============================================================================//

#ifndef STUDIO_VALIDATE_H
#define STUDIO_VALIDATE_H
#ifdef _WIN32
#pragma once
#endif

#include <string.h>
#include "studio.h"
#include "optimize.h"
#include "materialsystem/imaterialsystem.h"	// NUM_MODEL_TRANSFORMS

//-----------------------------------------------------------------------------
// Model file validation
//
// Models are downloaded from servers, and the engine, the studio renderer and the
// game code access every table of a studiohdr_t through offsets and counts from
// the file without further checks. Before a model is used, every table, string
// and index is checked against the file; a broken model is not loaded (the
// error model is used instead).
//-----------------------------------------------------------------------------
class CStudioHdrValidator
{
public:
	CStudioHdrValidator( const studiohdr_t *pHdr, int nSize ) : m_pHdr( pHdr ), m_pBase( (const byte *)pHdr ), m_nSize( nSize ), m_pszError( NULL ), m_nIndex( 0 ) {}
	// for a block of an external .ani file: offsets into the block, the model is already checked
	CStudioHdrValidator( const studiohdr_t *pHdr, const void *pBlock, int nBlockSize ) : m_pHdr( pHdr ), m_pBase( (const byte *)pBlock ), m_nSize( nBlockSize ), m_pszError( NULL ), m_nIndex( 0 ) {}

	bool Validate();
	bool ValidateAnimBlock( int nBlock );
	const char *Error() const { return m_pszError; }
	int ErrorIndex() const { return m_nIndex; }

private:
	// offset of p from the start of the file (p is always inside the file here)
	int64 Ofs( const void *p ) const { return (const byte *)p - m_pBase; }

	// count elements of nElemSize at pBase + nOffset are inside the file
	bool Range( const void *pBase, int64 nOffset, int64 nCount, int64 nElemSize )
	{
		if ( nCount < 0 || nElemSize <= 0 )
			return false;
		int64 nStart = Ofs( pBase ) + nOffset;
		return nStart >= 0 && nStart <= m_nSize && nCount <= ( m_nSize - nStart ) / nElemSize;
	}

	// a NUL terminated string at pBase + nOffset
	bool String( const void *pBase, int64 nOffset )
	{
		int64 nStart = Ofs( pBase ) + nOffset;
		if ( nStart < 0 || nStart >= m_nSize )
			return false;
		return memchr( m_pBase + nStart, 0, (size_t)( m_nSize - nStart ) ) != NULL;
	}

	// an optional string: offset 0 means none
	bool OptString( const void *pBase, int64 nOffset ) { return nOffset == 0 || String( pBase, nOffset ); }

	bool Fail( const char *pszWhat, int nIndex ) { m_pszError = pszWhat; m_nIndex = nIndex; return false; }

	bool ValidateBones();
	bool ValidateHitboxes();
	bool ValidateAnims();
	bool ValidateAnimData( const mstudioanimdesc_t *pDesc, const byte *pData );
	bool ValidateSequences();
	bool ValidateTextures();
	bool ValidateBodyParts();
	bool ValidateFlexes();
	bool ValidateMisc();

	const studiohdr_t *m_pHdr;
	const byte *m_pBase;
	int64 m_nSize;
	const char *m_pszError;
	int m_nIndex;
};

bool CStudioHdrValidator::Validate()
{
	const studiohdr_t *h = m_pHdr;
	if ( m_nSize < (int64)sizeof( studiohdr_t ) || h->length < (int)sizeof( studiohdr_t ) || h->length > m_nSize )
		return Fail( "length", h->length );
	m_nSize = h->length;	// the tables belong to the model, not to padding after it

	if ( memchr( h->name, 0, sizeof( h->name ) ) == NULL )
		return Fail( "name", 0 );

	return ValidateBones() && ValidateHitboxes() && ValidateAnims() && ValidateSequences() &&
		ValidateTextures() && ValidateBodyParts() && ValidateFlexes() && ValidateMisc();
}

bool CStudioHdrValidator::ValidateBones()
{
	const studiohdr_t *h = m_pHdr;

	// many arrays in the engine and the game have MAXSTUDIOBONES entries
	if ( h->numbones < 0 || h->numbones > MAXSTUDIOBONES || !Range( h, h->boneindex, h->numbones, sizeof( mstudiobone_t ) ) )
		return Fail( "bone table", h->numbones );
	if ( h->numbonecontrollers < 0 || h->numbonecontrollers > MAXSTUDIOBONECTRLS ||
		!Range( h, h->bonecontrollerindex, h->numbonecontrollers, sizeof( mstudiobonecontroller_t ) ) )
		return Fail( "bone controller table", h->numbonecontrollers );

	for ( int i = 0; i < h->numbones; i++ )
	{
		const mstudiobone_t *pBone = h->pBone( i );
		// parents come first: bone setup relies on it and walks parent chains
		if ( !String( pBone, pBone->sznameindex ) || pBone->parent < -1 || pBone->parent >= i ||
			!OptString( pBone, pBone->surfacepropidx ) )
			return Fail( "bone", i );
		for ( int j = 0; j < 6; j++ )
		{
			if ( pBone->bonecontroller[j] < -1 || pBone->bonecontroller[j] >= h->numbonecontrollers )
				return Fail( "bone controller reference", i );
		}

		// pProcedure() is NULL for procindex 0
		if ( pBone->proctype != 0 && pBone->procindex == 0 )
			return Fail( "procedural bone", i );

		switch ( pBone->proctype )
		{
		case 0:
			break;
		case STUDIO_PROC_AXISINTERP:
			if ( !Range( pBone, pBone->procindex, 1, sizeof( mstudioaxisinterpbone_t ) ) ||
				(unsigned)( (const mstudioaxisinterpbone_t *)pBone->pProcedure() )->control >= (unsigned)h->numbones )
				return Fail( "procedural bone", i );
			break;
		case STUDIO_PROC_QUATINTERP:
			{
				if ( !Range( pBone, pBone->procindex, 1, sizeof( mstudioquatinterpbone_t ) ) )
					return Fail( "procedural bone", i );
				const mstudioquatinterpbone_t *pProc = (const mstudioquatinterpbone_t *)pBone->pProcedure();
				if ( (unsigned)pProc->control >= (unsigned)h->numbones ||
					!Range( pProc, pProc->triggerindex, pProc->numtriggers, sizeof( mstudioquatinterpinfo_t ) ) )
					return Fail( "procedural bone", i );
			}
			break;
		case STUDIO_PROC_AIMATBONE:
		case STUDIO_PROC_AIMATATTACH:
			{
				if ( !Range( pBone, pBone->procindex, 1, sizeof( mstudioaimatbone_t ) ) )
					return Fail( "procedural bone", i );
				const mstudioaimatbone_t *pProc = (const mstudioaimatbone_t *)pBone->pProcedure();
				int nAimLimit = pBone->proctype == STUDIO_PROC_AIMATBONE ? h->numbones : h->numlocalattachments;
				if ( (unsigned)pProc->parent >= (unsigned)h->numbones || (unsigned)pProc->aim >= (unsigned)nAimLimit )
					return Fail( "procedural bone", i );
			}
			break;
		case STUDIO_PROC_JIGGLE:
			if ( !Range( pBone, pBone->procindex, 1, sizeof( mstudiojigglebone_t ) ) )
				return Fail( "procedural bone", i );
			break;
		default:
			return Fail( "procedural bone type", i );
		}
	}

	for ( int i = 0; i < h->numbonecontrollers; i++ )
	{
		const mstudiobonecontroller_t *pCtrl = h->pBonecontroller( i );
		// inputfield indexes the controller value arrays
		if ( pCtrl->bone < -1 || pCtrl->bone >= h->numbones || pCtrl->inputfield < 0 || pCtrl->inputfield >= MAXSTUDIOBONECTRLS )
			return Fail( "bone controller", i );
	}

	if ( h->bonetablebynameindex )
	{
		if ( !Range( h, h->bonetablebynameindex, h->numbones, 1 ) )
			return Fail( "bone name table", 0 );
		const byte *pTable = h->GetBoneTableSortedByName();
		for ( int i = 0; i < h->numbones; i++ )
		{
			if ( pTable[i] >= h->numbones )
				return Fail( "bone name table", i );
		}
	}
	return true;
}

bool CStudioHdrValidator::ValidateHitboxes()
{
	const studiohdr_t *h = m_pHdr;
	if ( !Range( h, h->hitboxsetindex, h->numhitboxsets, sizeof( mstudiohitboxset_t ) ) )
		return Fail( "hitbox set table", h->numhitboxsets );
	for ( int i = 0; i < h->numhitboxsets; i++ )
	{
		const mstudiohitboxset_t *pSet = h->pHitboxSet( i );
		if ( !String( pSet, pSet->sznameindex ) || !Range( pSet, pSet->hitboxindex, pSet->numhitboxes, sizeof( mstudiobbox_t ) ) )
			return Fail( "hitbox set", i );
		for ( int j = 0; j < pSet->numhitboxes; j++ )
		{
			const mstudiobbox_t *pBox = pSet->pHitbox( j );
			// szhitboxnameindex is not checked: older studiomdl versions wrote it relative to the
			// header, and the client doesn't read the name
			if ( (unsigned)pBox->bone >= (unsigned)h->numbones )
				return Fail( "hitbox", j );
		}
	}
	return true;
}

// The animation data of one block: a chain of mstudioanim_t, one per animated bone. The
// bone number indexes the bone arrays (MAXSTUDIOBONES) that the animation is written to.
bool CStudioHdrValidator::ValidateAnimData( const mstudioanimdesc_t *pDesc, const byte *pData )
{
	const mstudioanim_t *pAnim = (const mstudioanim_t *)pData;
	for ( int nNodes = 0; pAnim; nNodes++ )
	{
		if ( nNodes > MAXSTUDIOBONES || !Range( pAnim, 0, 1, sizeof( mstudioanim_t ) ) )
			return false;
		// bone 255 ends the list (an animation without animated bones); the bone setup
		// loop stops there too
		if ( pAnim->bone == 255 )
			break;
		if ( pAnim->bone >= m_pHdr->numbones )
			return false;

		// the per bone data follows the header: value pointers or raw values
		int nDataSize = 0;
		if ( pAnim->flags & STUDIO_ANIM_ANIMROT )
			nDataSize += sizeof( mstudioanim_valueptr_t );
		if ( pAnim->flags & STUDIO_ANIM_ANIMPOS )
			nDataSize += sizeof( mstudioanim_valueptr_t );
		if ( pAnim->flags & STUDIO_ANIM_RAWROT )
			nDataSize += sizeof( Quaternion48 );
		if ( pAnim->flags & STUDIO_ANIM_RAWROT2 )
			nDataSize += sizeof( Quaternion64 );
		if ( pAnim->flags & STUDIO_ANIM_RAWPOS )
			nDataSize += sizeof( Vector48 );
		if ( !Range( pAnim->pData(), 0, nDataSize, 1 ) )
			return false;

		// the RLE value streams start inside the file (their length depends on the frame)
		const mstudioanim_valueptr_t *pPtrs[2] = {
			( pAnim->flags & STUDIO_ANIM_ANIMROT ) ? pAnim->pRotV() : NULL,
			( pAnim->flags & STUDIO_ANIM_ANIMPOS ) ? pAnim->pPosV() : NULL };
		for ( int p = 0; p < 2; p++ )
		{
			if ( !pPtrs[p] )
				continue;
			for ( int c = 0; c < 3; c++ )
			{
				if ( pPtrs[p]->offset[c] > 0 && !Range( pPtrs[p], pPtrs[p]->offset[c], 2, sizeof( mstudioanimvalue_t ) ) )
					return false;
			}
		}

		if ( !pAnim->nextoffset )
			break;
		if ( pAnim->nextoffset < 0 )
			return false;	// a loop
		pAnim = (const mstudioanim_t *)( (const byte *)pAnim + pAnim->nextoffset );
	}
	return true;
}

bool CStudioHdrValidator::ValidateAnims()
{
	const studiohdr_t *h = m_pHdr;
	if ( !Range( h, h->localanimindex, h->numlocalanim, sizeof( mstudioanimdesc_t ) ) )
		return Fail( "animation table", h->numlocalanim );
	if ( h->numanimblocks && ( !String( h, h->szanimblocknameindex ) ||
		!Range( h, h->animblockindex, h->numanimblocks, sizeof( mstudioanimblock_t ) ) ) )
		return Fail( "animation block table", h->numanimblocks );
	for ( int i = 1; i < h->numanimblocks; i++ )
	{
		// the block is read from the .ani file at datastart, dataend - datastart bytes
		const mstudioanimblock_t *pBlock = h->pAnimBlock( i );
		if ( pBlock->datastart < 0 || pBlock->dataend < pBlock->datastart )
			return Fail( "animation block", i );
	}

	for ( int i = 0; i < h->numlocalanim; i++ )
	{
		const mstudioanimdesc_t *pDesc = h->pLocalAnimdesc( i );
		if ( !String( pDesc, pDesc->sznameindex ) || pDesc->numframes < 0 ||
			!Range( pDesc, pDesc->movementindex, pDesc->nummovements, sizeof( mstudiomovement_t ) ) ||
			!Range( pDesc, pDesc->localhierarchyindex, pDesc->numlocalhierarchy, sizeof( mstudiolocalhierarchy_t ) ) )
			return Fail( "animation", i );

		// animblock -1 disables the animation (old models); > 0 is in the .ani file
		if ( pDesc->animblock < -1 || pDesc->animblock >= MAX( h->numanimblocks, 1 ) )
			return Fail( "animation block", i );

		// IK rules: in the model (ikruleindex, or animblockikruleindex for block 0) or in the .ani
		// file (as mstudioanimdesc_t::pIKRule finds them)
		int nIKRuleOffset = pDesc->ikruleindex ? pDesc->ikruleindex : ( pDesc->animblock == 0 ? pDesc->animblockikruleindex : 0 );
		if ( pDesc->numikrules < 0 || ( pDesc->numikrules && nIKRuleOffset &&
			!Range( pDesc, nIKRuleOffset, pDesc->numikrules, sizeof( mstudioikrule_t ) ) ) )
			return Fail( "animation IK rules", i );

		if ( pDesc->sectionframes > 0 )
		{
			int nSections = pDesc->numframes / pDesc->sectionframes + 2;
			if ( !Range( pDesc, pDesc->sectionindex, nSections, sizeof( mstudioanimsections_t ) ) )
				return Fail( "animation sections", i );
			for ( int s = 0; s < nSections; s++ )
			{
				const mstudioanimsections_t *pSection = pDesc->pSection( s );
				if ( pSection->animblock < -1 || pSection->animblock >= MAX( h->numanimblocks, 1 ) )
					return Fail( "animation section", i );
				if ( pSection->animblock == 0 && pSection->animindex &&
					!ValidateAnimData( pDesc, (const byte *)pDesc + pSection->animindex ) )
					return Fail( "animation section data", i );
			}
		}
		else if ( pDesc->animblock == 0 && !( pDesc->flags & STUDIO_ALLZEROS ) )
		{
			if ( !ValidateAnimData( pDesc, (const byte *)pDesc + pDesc->animindex ) )
				return Fail( "animation data", i );
		}

		if ( pDesc->numikrules && nIKRuleOffset )
		{
			for ( int r = 0; r < pDesc->numikrules; r++ )
			{
				const mstudioikrule_t *pRule = (const mstudioikrule_t *)( (const byte *)pDesc + nIKRuleOffset ) + r;
				if ( (unsigned)pRule->chain >= (unsigned)h->numikchains || pRule->bone < -1 || pRule->bone >= h->numbones ||
					!OptString( pRule, pRule->szattachmentindex ) )
					return Fail( "animation IK rule", i );
			}
		}
	}
	return true;
}

// Checks the data of one block of the .ani file (m_pBase/m_nSize are the block): the
// animation data and IK rules of the animations and sections that use the block.
bool CStudioHdrValidator::ValidateAnimBlock( int nBlock )
{
	const studiohdr_t *h = m_pHdr;
	for ( int i = 0; i < h->numlocalanim; i++ )
	{
		const mstudioanimdesc_t *pDesc = h->pLocalAnimdesc( i );
		if ( pDesc->sectionframes > 0 )
		{
			int nSections = pDesc->numframes / pDesc->sectionframes + 2;
			for ( int s = 0; s < nSections; s++ )
			{
				const mstudioanimsections_t *pSection = pDesc->pSection( s );
				if ( pSection->animblock == nBlock && !ValidateAnimData( pDesc, m_pBase + pSection->animindex ) )
					return Fail( "animation section data", i );
			}
		}
		else if ( pDesc->animblock == nBlock && !( pDesc->flags & STUDIO_ALLZEROS ) )
		{
			if ( pDesc->animindex < 0 || !ValidateAnimData( pDesc, m_pBase + pDesc->animindex ) )
				return Fail( "animation data", i );
		}

		// IK rules in the block (mstudioanimdesc_t::pIKRule)
		if ( pDesc->animblock == nBlock && pDesc->numikrules && !pDesc->ikruleindex && pDesc->animblockikruleindex )
		{
			if ( pDesc->animblockikruleindex < 0 || !Range( m_pBase, pDesc->animblockikruleindex, pDesc->numikrules, sizeof( mstudioikrule_t ) ) )
				return Fail( "animation IK rules", i );
			for ( int r = 0; r < pDesc->numikrules; r++ )
			{
				const mstudioikrule_t *pRule = (const mstudioikrule_t *)( m_pBase + pDesc->animblockikruleindex ) + r;
				if ( (unsigned)pRule->chain >= (unsigned)h->numikchains || pRule->bone < -1 || pRule->bone >= h->numbones )
					return Fail( "animation IK rule", i );
			}
		}
	}
	return true;
}

bool CStudioHdrValidator::ValidateSequences()
{
	const studiohdr_t *h = m_pHdr;
	if ( !Range( h, h->localseqindex, h->numlocalseq, sizeof( mstudioseqdesc_t ) ) )
		return Fail( "sequence table", h->numlocalseq );
	if ( h->numlocalposeparameters < 0 || h->numlocalposeparameters > MAXSTUDIOPOSEPARAM ||
		!Range( h, h->localposeparamindex, h->numlocalposeparameters, sizeof( mstudioposeparamdesc_t ) ) )
		return Fail( "pose parameter table", h->numlocalposeparameters );
	for ( int i = 0; i < h->numlocalposeparameters; i++ )
	{
		if ( !String( h->pLocalPoseParameter( i ), h->pLocalPoseParameter( i )->sznameindex ) )
			return Fail( "pose parameter", i );
	}

	for ( int i = 0; i < h->numlocalseq; i++ )
	{
		const mstudioseqdesc_t *pSeq = h->pLocalSeqdesc( i );
		if ( !String( pSeq, pSeq->szlabelindex ) || !String( pSeq, pSeq->szactivitynameindex ) ||
			!Range( pSeq, pSeq->eventindex, pSeq->numevents, sizeof( mstudioevent_t ) ) )
			return Fail( "sequence", i );
		for ( int e = 0; e < pSeq->numevents; e++ )
		{
			const mstudioevent_t *pEvent = pSeq->pEvent( e );
			if ( memchr( pEvent->options, 0, sizeof( pEvent->options ) ) == NULL || !OptString( pEvent, pEvent->szeventindex ) )
				return Fail( "sequence event", i );
		}

		// the blend grid: groupsize[0] x groupsize[1] animation numbers
		// The grid can have size 0 on an axis (standard models such as models/monk_animations.mdl
		// and models/vortigaunt_gestures.mdl); mstudioseqdesc_t::anim() then reads the first entry.
		if ( pSeq->groupsize[0] < 0 || pSeq->groupsize[1] < 0 )
			return Fail( "sequence blend grid size", i );
		if ( !Range( pSeq, pSeq->animindexindex, MAX( (int64)pSeq->groupsize[0] * pSeq->groupsize[1], (int64)1 ), sizeof( short ) ) )
			return Fail( "sequence blend grid range", i );
		// the animation numbers aren't checked: pLocalAnimdesc() uses animation 0 for numbers
		// out of range (some server models reference missing animations)
		// paramindex and the layers' iPose are not checked against this model: in animation
		// libraries ($includemodel, e.g. models/humans/female_ss.mdl) they refer to the pose
		// parameters of the including model; GetSharedPoseParameter checks the mapping
		if ( pSeq->posekeyindex && !Range( pSeq, pSeq->posekeyindex, pSeq->groupsize[0] + pSeq->groupsize[1], sizeof( float ) ) )
			return Fail( "sequence pose keys", i );
		if ( pSeq->weightlistindex && !Range( pSeq, pSeq->weightlistindex, h->numbones, sizeof( float ) ) )
			return Fail( "sequence weights", i );

		// the layers' iSequence: pLocalSeqdesc() uses sequence 0 for numbers out of range
		if ( !Range( pSeq, pSeq->autolayerindex, pSeq->numautolayers, sizeof( mstudioautolayer_t ) ) )
			return Fail( "sequence layers", i );

		if ( !Range( pSeq, pSeq->iklockindex, pSeq->numiklocks, sizeof( mstudioiklock_t ) ) )
			return Fail( "sequence IK locks", i );
		for ( int l = 0; l < pSeq->numiklocks; l++ )
		{
			if ( (unsigned)pSeq->pIKLock( l )->chain >= (unsigned)h->numikchains )
				return Fail( "sequence IK lock", i );
		}

		if ( pSeq->keyvaluesize && !Range( pSeq, pSeq->keyvalueindex, pSeq->keyvaluesize, 1 ) )
			return Fail( "sequence key values", i );
		// pActivityModifier() is NULL for index 0
		if ( pSeq->numactivitymodifiers < 0 || ( pSeq->numactivitymodifiers > 0 && pSeq->activitymodifierindex == 0 ) ||
			!Range( pSeq, pSeq->activitymodifierindex, pSeq->numactivitymodifiers, sizeof( mstudioactivitymodifier_t ) ) )
			return Fail( "sequence activity modifiers", i );
		for ( int m = 0; m < pSeq->numactivitymodifiers; m++ )
		{
			if ( !String( pSeq->pActivityModifier( m ), pSeq->pActivityModifier( m )->sznameindex ) )
				return Fail( "sequence activity modifier", i );
		}

		// nodes are stored 1-based, 0 is none
		if ( pSeq->localentrynode < 0 || pSeq->localentrynode > h->numlocalnodes ||
			pSeq->localexitnode < 0 || pSeq->localexitnode > h->numlocalnodes )
			return Fail( "sequence node", i );
	}

	if ( h->numlocalnodes < 0 || h->numlocalnodes > 255 ||
		!Range( h, h->localnodenameindex, h->numlocalnodes, sizeof( int ) ) ||
		!Range( h, h->localnodeindex, (int64)h->numlocalnodes * h->numlocalnodes, 1 ) )
		return Fail( "node table", h->numlocalnodes );
	for ( int i = 0; i < h->numlocalnodes; i++ )
	{
		if ( !String( h, *( (const int *)( m_pBase + h->localnodenameindex ) + i ) ) )
			return Fail( "node name", i );
	}
	return true;
}

bool CStudioHdrValidator::ValidateTextures()
{
	const studiohdr_t *h = m_pHdr;
	if ( !Range( h, h->textureindex, h->numtextures, sizeof( mstudiotexture_t ) ) )
		return Fail( "texture table", h->numtextures );
	for ( int i = 0; i < h->numtextures; i++ )
	{
		if ( !String( h->pTexture( i ), h->pTexture( i )->sznameindex ) )
			return Fail( "texture", i );
	}

	if ( !Range( h, h->cdtextureindex, h->numcdtextures, sizeof( int ) ) )
		return Fail( "texture path table", h->numcdtextures );
	for ( int i = 0; i < h->numcdtextures; i++ )
	{
		if ( !String( h, *( (const int *)( m_pBase + h->cdtextureindex ) + i ) ) )
			return Fail( "texture path", i );
	}

	// skin families: numskinref texture numbers per family
	if ( h->numskinref < 0 || h->numskinfamilies < 0 ||
		!Range( h, h->skinindex, (int64)h->numskinref * h->numskinfamilies, sizeof( short ) ) )
		return Fail( "skin table", h->numskinfamilies );
	const short *pSkins = (const short *)( m_pBase + h->skinindex );
	for ( int64 i = 0; i < (int64)h->numskinref * h->numskinfamilies; i++ )
	{
		if ( pSkins[i] < 0 || pSkins[i] >= h->numtextures )
			return Fail( "skin", (int)i );
	}
	return true;
}

bool CStudioHdrValidator::ValidateBodyParts()
{
	const studiohdr_t *h = m_pHdr;
	if ( !Range( h, h->bodypartindex, h->numbodyparts, sizeof( mstudiobodyparts_t ) ) )
		return Fail( "body part table", h->numbodyparts );

	for ( int i = 0; i < h->numbodyparts; i++ )
	{
		const mstudiobodyparts_t *pPart = h->pBodypart( i );
		// the body group value is divided by base and taken modulo nummodels
		if ( !String( pPart, pPart->sznameindex ) || pPart->nummodels < 1 || pPart->base < 1 ||
			!Range( pPart, pPart->modelindex, pPart->nummodels, sizeof( mstudiomodel_t ) ) )
			return Fail( "body part", i );

		for ( int m = 0; m < pPart->nummodels; m++ )
		{
			const mstudiomodel_t *pModel = pPart->pModel( m );
			if ( memchr( pModel->name, 0, sizeof( pModel->name ) ) == NULL || pModel->numvertices < 0 ||
				!Range( pModel, pModel->meshindex, pModel->nummeshes, sizeof( mstudiomesh_t ) ) ||
				!Range( pModel, pModel->eyeballindex, pModel->numeyeballs, sizeof( mstudioeyeball_t ) ) )
				return Fail( "model", m );

			for ( int e = 0; e < pModel->numeyeballs; e++ )
			{
				const mstudioeyeball_t *pEye = const_cast<mstudiomodel_t *>( pModel )->pEyeball( e );
				if ( !String( pEye, pEye->sznameindex ) || (unsigned)pEye->bone >= (unsigned)h->numbones )
					return Fail( "eyeball", e );
			}

			for ( int n = 0; n < pModel->nummeshes; n++ )
			{
				const mstudiomesh_t *pMesh = pModel->pMesh( n );
				// pModel() of a mesh goes back through modelindex
				if ( (const byte *)pMesh + pMesh->modelindex != (const byte *)pModel ||
					pMesh->material < 0 || pMesh->material >= h->numskinref ||
					pMesh->numvertices < 0 || pMesh->vertexoffset < 0 ||
					(int64)pMesh->vertexoffset + pMesh->numvertices > pModel->numvertices ||
					!Range( pMesh, pMesh->flexindex, pMesh->numflexes, sizeof( mstudioflex_t ) ) )
					return Fail( "mesh", n );
				// the LOD vertex counts bound the vertices used by flexes and by the renderer
				for ( int lod = 0; lod < MAX_NUM_LODS; lod++ )
				{
					if ( pMesh->vertexdata.numLODVertexes[lod] < 0 || pMesh->vertexdata.numLODVertexes[lod] > pMesh->numvertices )
						return Fail( "mesh LOD vertices", n );
				}

				for ( int f = 0; f < pMesh->numflexes; f++ )
				{
					const mstudioflex_t *pFlex = pMesh->pFlex( f );
					int nVertAnimSize = pFlex->vertanimtype == STUDIO_VERT_ANIM_WRINKLE ? sizeof( mstudiovertanim_wrinkle_t ) : sizeof( mstudiovertanim_t );
					if ( (unsigned)pFlex->flexdesc >= (unsigned)h->numflexdesc ||
						pFlex->flexpair < 0 || pFlex->flexpair >= MAX( h->numflexdesc, 1 ) ||
						!Range( pFlex, pFlex->vertindex, pFlex->numverts, nVertAnimSize ) )
						return Fail( "mesh flex", f );
					for ( int v = 0; v < pFlex->numverts; v++ )
					{
						// the vertex number indexes the mesh's vertices
						const mstudiovertanim_t *pVert = (const mstudiovertanim_t *)( (const byte *)pFlex + pFlex->vertindex + (int64)v * nVertAnimSize );
						if ( pVert->index >= pMesh->numvertices )
							return Fail( "mesh flex vertex", f );
					}
				}
			}
		}
	}
	return true;
}

bool CStudioHdrValidator::ValidateFlexes()
{
	const studiohdr_t *h = m_pHdr;
	if ( h->numflexdesc < 0 || h->numflexdesc > MAXSTUDIOFLEXDESC ||
		!Range( h, h->flexdescindex, h->numflexdesc, sizeof( mstudioflexdesc_t ) ) )
		return Fail( "flex table", h->numflexdesc );
	for ( int i = 0; i < h->numflexdesc; i++ )
	{
		if ( !String( h->pFlexdesc( i ), h->pFlexdesc( i )->szFACSindex ) )
			return Fail( "flex", i );
	}

	if ( h->numflexcontrollers < 0 || h->numflexcontrollers > MAXSTUDIOFLEXCTRL ||
		!Range( h, h->flexcontrollerindex, h->numflexcontrollers, sizeof( mstudioflexcontroller_t ) ) )
		return Fail( "flex controller table", h->numflexcontrollers );
	for ( int i = 0; i < h->numflexcontrollers; i++ )
	{
		const mstudioflexcontroller_t *pCtrl = h->pFlexcontroller( (LocalFlexController_t)i );
		if ( !String( pCtrl, pCtrl->sztypeindex ) || !String( pCtrl, pCtrl->sznameindex ) )
			return Fail( "flex controller", i );
	}

	// the rule operands are checked when the rules run (CStudioHdr::RunFlexRules)
	if ( !Range( h, h->flexruleindex, h->numflexrules, sizeof( mstudioflexrule_t ) ) )
		return Fail( "flex rule table", h->numflexrules );
	for ( int i = 0; i < h->numflexrules; i++ )
	{
		const mstudioflexrule_t *pRule = h->pFlexRule( i );
		if ( (unsigned)pRule->flex >= (unsigned)h->numflexdesc ||
			!Range( pRule, pRule->opindex, pRule->numops, sizeof( mstudioflexop_t ) ) )
			return Fail( "flex rule", i );
	}

	if ( !Range( h, h->flexcontrolleruiindex, h->numflexcontrollerui, sizeof( mstudioflexcontrollerui_t ) ) )
		return Fail( "flex controller UI table", h->numflexcontrollerui );
	for ( int i = 0; i < h->numflexcontrollerui; i++ )
	{
		if ( !String( h->pFlexControllerUI( i ), h->pFlexControllerUI( i )->sznameindex ) )
			return Fail( "flex controller UI", i );
	}

	if ( !Range( h, h->mouthindex, h->nummouths, sizeof( mstudiomouth_t ) ) )
		return Fail( "mouth table", h->nummouths );
	for ( int i = 0; i < h->nummouths; i++ )
	{
		const mstudiomouth_t *pMouth = h->pMouth( i );
		if ( (unsigned)pMouth->bone >= (unsigned)h->numbones || (unsigned)pMouth->flexdesc >= (unsigned)h->numflexdesc )
			return Fail( "mouth", i );
	}
	return true;
}

bool CStudioHdrValidator::ValidateMisc()
{
	const studiohdr_t *h = m_pHdr;

	if ( !Range( h, h->localattachmentindex, h->numlocalattachments, sizeof( mstudioattachment_t ) ) )
		return Fail( "attachment table", h->numlocalattachments );
	for ( int i = 0; i < h->numlocalattachments; i++ )
	{
		const mstudioattachment_t *pAtt = h->pLocalAttachment( i );
		if ( !String( pAtt, pAtt->sznameindex ) || (unsigned)pAtt->localbone >= (unsigned)h->numbones )
			return Fail( "attachment", i );
	}

	if ( !Range( h, h->ikchainindex, h->numikchains, sizeof( mstudioikchain_t ) ) )
		return Fail( "IK chain table", h->numikchains );
	for ( int i = 0; i < h->numikchains; i++ )
	{
		const mstudioikchain_t *pChain = h->pIKChain( i );
		if ( !String( pChain, pChain->sznameindex ) || !Range( pChain, pChain->linkindex, pChain->numlinks, sizeof( mstudioiklink_t ) ) )
			return Fail( "IK chain", i );
		for ( int l = 0; l < pChain->numlinks; l++ )
		{
			if ( (unsigned)pChain->pLink( l )->bone >= (unsigned)h->numbones )
				return Fail( "IK link", i );
		}
	}

	if ( !Range( h, h->localikautoplaylockindex, h->numlocalikautoplaylocks, sizeof( mstudioiklock_t ) ) )
		return Fail( "IK autoplay lock table", h->numlocalikautoplaylocks );
	for ( int i = 0; i < h->numlocalikautoplaylocks; i++ )
	{
		if ( (unsigned)h->pLocalIKAutoplayLock( i )->chain >= (unsigned)h->numikchains )
			return Fail( "IK autoplay lock", i );
	}

	if ( !OptString( h, h->surfacepropindex ) )
		return Fail( "surface property", 0 );
	if ( h->keyvaluesize && !Range( h, h->keyvalueindex, h->keyvaluesize, 1 ) )
		return Fail( "key values", 0 );

	if ( !Range( h, h->includemodelindex, h->numincludemodels, sizeof( mstudiomodelgroup_t ) ) )
		return Fail( "include model table", h->numincludemodels );
	for ( int i = 0; i < h->numincludemodels; i++ )
	{
		const mstudiomodelgroup_t *pGroup = h->pModelGroup( i );
		if ( !OptString( pGroup, pGroup->szlabelindex ) || !String( pGroup, pGroup->sznameindex ) )
			return Fail( "include model", i );
	}

	if ( h->studiohdr2index )
	{
		if ( !Range( h, h->studiohdr2index, 1, sizeof( studiohdr2_t ) ) )
			return Fail( "extended header", 0 );
		const studiohdr2_t *h2 = h->pStudioHdr2();
		// srcbonetransformindex is relative to the main header (studiohdr_t::SrcBoneTransform)
		if ( !Range( h, h2->srcbonetransformindex, h2->numsrcbonetransform, sizeof( mstudiosrcbonetransform_t ) ) ||
			!OptString( h2, h2->sznameindex ) ||
			h2->illumpositionattachmentindex < 0 || h2->illumpositionattachmentindex > h->numlocalattachments )
			return Fail( "extended header", 0 );
		for ( int i = 0; i < h2->numsrcbonetransform; i++ )
		{
			const mstudiosrcbonetransform_t *pTransform = h->SrcBoneTransform( i );
			if ( !String( pTransform, pTransform->sznameindex ) )
				return Fail( "source bone transform", i );
		}

		if ( h2->linearboneindex )
		{
			if ( !Range( h2, h2->linearboneindex, 1, sizeof( mstudiolinearbone_t ) ) )
				return Fail( "linear bones", 0 );
			const mstudiolinearbone_t *pLinear = h2->pLinearBones();
			// parallel arrays with one entry per bone
			if ( pLinear->numbones != h->numbones ||
				!Range( pLinear, pLinear->flagsindex, h->numbones, sizeof( int ) ) ||
				!Range( pLinear, pLinear->parentindex, h->numbones, sizeof( int ) ) ||
				!Range( pLinear, pLinear->posindex, h->numbones, sizeof( Vector ) ) ||
				!Range( pLinear, pLinear->quatindex, h->numbones, sizeof( Quaternion ) ) ||
				!Range( pLinear, pLinear->rotindex, h->numbones, sizeof( RadianEuler ) ) ||
				!Range( pLinear, pLinear->posetoboneindex, h->numbones, sizeof( matrix3x4_t ) ) ||
				!Range( pLinear, pLinear->posscaleindex, h->numbones, sizeof( Vector ) ) ||
				!Range( pLinear, pLinear->rotscaleindex, h->numbones, sizeof( Vector ) ) ||
				!Range( pLinear, pLinear->qalignmentindex, h->numbones, sizeof( Quaternion ) ) )
				return Fail( "linear bones", 0 );
			for ( int i = 0; i < h->numbones; i++ )
			{
				int parent = pLinear->parent( i );
				if ( parent < -1 || parent >= i )
					return Fail( "linear bone parent", i );
			}
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Checks a .vtx file against its model before the studio renderer walks it: every
// table is inside the file, the counts match the model, and the vertex, index and
// bone numbers are in range (a bone state change's hardware bone indexes the
// fixed size bone matrix array of the shader API).
//-----------------------------------------------------------------------------
inline bool ValidateHardwareData( const studiohdr_t *pStudioHdr, const OptimizedModel::FileHeader_t *pVtxHdr, int nDataSize, const char **ppszError )
{
	using namespace OptimizedModel;
	const byte *pBase = (const byte *)pVtxHdr;
	const bool bV49 = pStudioHdr->version == 49;
	const int nStripGroupSize = bV49 ? sizeof( StripGroupHeader_v49_t ) : sizeof( StripGroupHeader_t );
	const int nStripSize = bV49 ? sizeof( StripHeader_v49_t ) : sizeof( StripHeader_t );

	auto Range = [pBase, nDataSize]( const void *p, int64 nOffset, int64 nCount, int64 nElemSize ) -> bool
	{
		if ( nCount < 0 || nElemSize <= 0 )
			return false;
		int64 nStart = (const byte *)p - pBase + nOffset;
		return nStart >= 0 && nStart <= nDataSize && nCount <= ( nDataSize - nStart ) / nElemSize;
	};
	auto String = [pBase, nDataSize]( const void *p, int64 nOffset ) -> bool
	{
		int64 nStart = (const byte *)p - pBase + nOffset;
		return nStart >= 0 && nStart < nDataSize && memchr( pBase + nStart, 0, (size_t)( nDataSize - nStart ) ) != NULL;
	};
#define VTX_FAIL( what ) do { *ppszError = what; return false; } while ( 0 )

	if ( nDataSize < (int)sizeof( FileHeader_t ) )
		VTX_FAIL( "header" );
	if ( pVtxHdr->numLODs < 1 || pVtxHdr->numLODs > MAX_NUM_LODS || pVtxHdr->numBodyParts != pStudioHdr->numbodyparts ||
		!Range( pVtxHdr, pVtxHdr->bodyPartOffset, pVtxHdr->numBodyParts, sizeof( BodyPartHeader_t ) ) )
		VTX_FAIL( "body parts" );

	// one material replacement list per LOD
	if ( !Range( pVtxHdr, pVtxHdr->materialReplacementListOffset, pVtxHdr->numLODs, sizeof( MaterialReplacementListHeader_t ) ) )
		VTX_FAIL( "material replacements" );
	for ( int lod = 0; lod < pVtxHdr->numLODs; lod++ )
	{
		const MaterialReplacementListHeader_t *pList = pVtxHdr->pMaterialReplacementList( lod );
		if ( !Range( pList, pList->replacementOffset, pList->numReplacements, sizeof( MaterialReplacementHeader_t ) ) )
			VTX_FAIL( "material replacements" );
		for ( int i = 0; i < pList->numReplacements; i++ )
		{
			const MaterialReplacementHeader_t *pRepl = pList->pMaterialReplacement( i );
			if ( pRepl->materialID < 0 || pRepl->materialID >= pStudioHdr->numtextures || !String( pRepl, pRepl->replacementMaterialNameOffset ) )
				VTX_FAIL( "material replacement" );
		}
	}

	for ( int bp = 0; bp < pVtxHdr->numBodyParts; bp++ )
	{
		const BodyPartHeader_t *pVtxPart = pVtxHdr->pBodyPart( bp );
		const mstudiobodyparts_t *pPart = pStudioHdr->pBodypart( bp );
		if ( pVtxPart->numModels != pPart->nummodels || !Range( pVtxPart, pVtxPart->modelOffset, pVtxPart->numModels, sizeof( ModelHeader_t ) ) )
			VTX_FAIL( "models" );

		for ( int m = 0; m < pVtxPart->numModels; m++ )
		{
			const ModelHeader_t *pVtxModel = pVtxPart->pModel( m );
			const mstudiomodel_t *pModel = pPart->pModel( m );
			if ( pVtxModel->numLODs != pVtxHdr->numLODs || !Range( pVtxModel, pVtxModel->lodOffset, pVtxModel->numLODs, sizeof( ModelLODHeader_t ) ) )
				VTX_FAIL( "LODs" );

			for ( int lod = 0; lod < pVtxModel->numLODs; lod++ )
			{
				const ModelLODHeader_t *pVtxLOD = pVtxModel->pLOD( lod );
				if ( pVtxLOD->numMeshes != pModel->nummeshes || !Range( pVtxLOD, pVtxLOD->meshOffset, pVtxLOD->numMeshes, sizeof( MeshHeader_t ) ) )
					VTX_FAIL( "meshes" );

				for ( int n = 0; n < pVtxLOD->numMeshes; n++ )
				{
					const MeshHeader_t *pVtxMesh = pVtxLOD->pMesh( n );
					const mstudiomesh_t *pMesh = pModel->pMesh( n );
					if ( !Range( pVtxMesh, pVtxMesh->stripGroupHeaderOffset, pVtxMesh->numStripGroups, nStripGroupSize ) )
						VTX_FAIL( "strip groups" );

					for ( int g = 0; g < pVtxMesh->numStripGroups; g++ )
					{
						// the accessors use the version flag set later; compute the address here
						const StripGroupHeader_t *pGroup = (const StripGroupHeader_t *)( (const byte *)pVtxMesh + pVtxMesh->stripGroupHeaderOffset + (int64)g * nStripGroupSize );
						if ( !Range( pGroup, pGroup->vertOffset, pGroup->numVerts, sizeof( Vertex_t ) ) ||
							!Range( pGroup, pGroup->indexOffset, pGroup->numIndices, sizeof( unsigned short ) ) ||
							!Range( pGroup, pGroup->stripOffset, pGroup->numStrips, nStripSize ) )
							VTX_FAIL( "strip group" );

						for ( int v = 0; v < pGroup->numVerts; v++ )
						{
							const Vertex_t *pVert = (const Vertex_t *)( (const byte *)pGroup + pGroup->vertOffset ) + v;
							if ( pVert->origMeshVertID >= pMesh->numvertices || pVert->numBones > MAX_NUM_BONES_PER_VERT )
								VTX_FAIL( "strip group vertex" );
							for ( int b = 0; b < pVert->numBones; b++ )
							{
								if ( pVert->boneWeightIndex[b] >= MAX_NUM_BONES_PER_VERT )
									VTX_FAIL( "strip group vertex" );
							}
						}
						const unsigned short *pIndices = (const unsigned short *)( (const byte *)pGroup + pGroup->indexOffset );
						for ( int i = 0; i < pGroup->numIndices; i++ )
						{
							if ( pIndices[i] >= pGroup->numVerts )
								VTX_FAIL( "strip group index" );
						}

						for ( int st = 0; st < pGroup->numStrips; st++ )
						{
							const StripHeader_t *pStrip = (const StripHeader_t *)( (const byte *)pGroup + pGroup->stripOffset + (int64)st * nStripSize );
							if ( pStrip->indexOffset < 0 || pStrip->numIndices < 0 || (int64)pStrip->indexOffset + pStrip->numIndices > pGroup->numIndices ||
								pStrip->vertOffset < 0 || pStrip->numVerts < 0 || (int64)pStrip->vertOffset + pStrip->numVerts > pGroup->numVerts ||
								!Range( pStrip, pStrip->boneStateChangeOffset, pStrip->numBoneStateChanges, sizeof( BoneStateChangeHeader_t ) ) )
								VTX_FAIL( "strip" );
							for ( int c = 0; c < pStrip->numBoneStateChanges; c++ )
							{
								const BoneStateChangeHeader_t *pChange = (const BoneStateChangeHeader_t *)( (const byte *)pStrip + pStrip->boneStateChangeOffset ) + c;
								// newBoneID < 0 is skipped by the renderer
								if ( pChange->hardwareID < 0 || pChange->hardwareID >= NUM_MODEL_TRANSFORMS || pChange->newBoneID >= pStudioHdr->numbones )
									VTX_FAIL( "bone state change" );
							}
						}
					}
				}
			}
		}
	}
#undef VTX_FAIL
	return true;
}


//-----------------------------------------------------------------------------
// Ranges and fixups of a .vvd file
//-----------------------------------------------------------------------------
inline bool ValidateVertexData( const vertexFileHeader_t *pHdr, int nDataSize, int rootLOD, bool bNeedsTangentS )
{
	if ( !pHdr || nDataSize < (int)sizeof( vertexFileHeader_t ) ||
		pHdr->numLODs < 1 || pHdr->numLODs > MAX_NUM_LODS || rootLOD < 0 || rootLOD >= pHdr->numLODs )
	{
		return false;
	}

	for ( int lod = 0; lod < pHdr->numLODs; ++lod )
	{
		if ( pHdr->numLODVertexes[lod] < 0 ||
			( lod > 0 && pHdr->numLODVertexes[lod] > pHdr->numLODVertexes[lod - 1] ) )
		{
			return false;
		}
	}

	if ( pHdr->vertexDataStart < (int)sizeof( vertexFileHeader_t ) ||
		pHdr->vertexDataStart > nDataSize || ( pHdr->vertexDataStart & 3 ) ||
		pHdr->numLODVertexes[0] > ( nDataSize - pHdr->vertexDataStart ) / (int)sizeof( mstudiovertex_t ) )
	{
		return false;
	}

	if ( bNeedsTangentS &&
		( pHdr->tangentDataStart < (int)sizeof( vertexFileHeader_t ) ||
		  pHdr->tangentDataStart > nDataSize || ( pHdr->tangentDataStart & 3 ) ||
		  pHdr->numLODVertexes[0] > ( nDataSize - pHdr->tangentDataStart ) / (int)sizeof( Vector4D ) ) )
	{
		return false;
	}

	uint64 vertexCount = (uint64)pHdr->numLODVertexes[rootLOD] + 1;
	uint64 cacheLength = (uint64)pHdr->vertexDataStart +
		vertexCount * ( sizeof( mstudiovertex_t ) + ( bNeedsTangentS ? sizeof( Vector4D ) : 0 ) );
	if ( cacheLength > INT_MAX || pHdr->numFixups < 0 )
	{
		return false;
	}
	if ( !pHdr->numFixups )
	{
		return true;
	}

	if ( pHdr->fixupTableStart < (int)sizeof( vertexFileHeader_t ) ||
		pHdr->fixupTableStart > nDataSize || ( pHdr->fixupTableStart & 3 ) ||
		pHdr->numFixups > ( nDataSize - pHdr->fixupTableStart ) / (int)sizeof( vertexFileFixup_t ) )
	{
		return false;
	}

	const vertexFileFixup_t *pFixups = (const vertexFileFixup_t *)( (const byte *)pHdr + pHdr->fixupTableStart );
	int target = 0;
	for ( int i = 0; i < pHdr->numFixups; ++i )
	{
		const vertexFileFixup_t &fixup = pFixups[i];
		if ( fixup.lod < 0 || fixup.lod >= MAX_NUM_LODS ||
			fixup.sourceVertexID < 0 || fixup.sourceVertexID > pHdr->numLODVertexes[0] ||
			fixup.numVertexes < 0 || fixup.numVertexes > pHdr->numLODVertexes[0] - fixup.sourceVertexID )
		{
			return false;
		}
		if ( fixup.lod >= rootLOD )
		{
			if ( fixup.numVertexes > pHdr->numLODVertexes[rootLOD] - target )
			{
				return false;
			}
			target += fixup.numVertexes;
		}
	}
	return target == pHdr->numLODVertexes[rootLOD];
}

//-----------------------------------------------------------------------------
// The bones of every vertex index the bone matrix arrays when the model is skinned,
// and the model's vertex ranges (byte offsets into this data) must be inside it.
// Call after ValidateVertexData.
//-----------------------------------------------------------------------------
inline bool ValidateVertexBones( const studiohdr_t *pStudioHdr, const vertexFileHeader_t *pVvdHdr, int rootLOD, const char **ppszError )
{
	const mstudiovertex_t *pVerts = (const mstudiovertex_t *)( (const byte *)pVvdHdr + pVvdHdr->vertexDataStart );
	for ( int i = 0; i < pVvdHdr->numLODVertexes[0]; i++ )
	{
		const mstudioboneweight_t &weights = pVerts[i].m_BoneWeights;
		if ( weights.numbones > MAX_NUM_BONES_PER_VERT )
		{
			*ppszError = "bone weights";
			return false;
		}
		for ( int b = 0; b < weights.numbones; b++ )
		{
			if ( weights.bone[b] < 0 || weights.bone[b] >= pStudioHdr->numbones )
			{
				*ppszError = "bone weights";
				return false;
			}
		}
	}

	if ( rootLOD != 0 )
		return true;	// the offsets are adjusted for the root LOD later

	for ( int bp = 0; bp < pStudioHdr->numbodyparts; bp++ )
	{
		const mstudiobodyparts_t *pPart = pStudioHdr->pBodypart( bp );
		for ( int m = 0; m < pPart->nummodels; m++ )
		{
			const mstudiomodel_t *pModel = pPart->pModel( m );
			if ( pModel->numvertices == 0 )
				continue;
			int64 nFirst = pModel->vertexindex / (int)sizeof( mstudiovertex_t );
			if ( pModel->vertexindex < 0 || pModel->vertexindex % (int)sizeof( mstudiovertex_t ) ||
				nFirst + pModel->numvertices > pVvdHdr->numLODVertexes[0] )
			{
				*ppszError = "model vertex range";
				return false;
			}
		}
	}
	return true;
}

#endif // STUDIO_VALIDATE_H
