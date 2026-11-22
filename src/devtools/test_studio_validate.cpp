//========= cssv34-client: model validation self-test =========================//
//
// Purpose: Builds a small valid model in memory and checks that the model load
//          validation (datacache/studio_validate.h) accepts it, then breaks one
//          field at a time and checks that each broken copy is rejected. Run by
//          CI so a change to the validation can't silently start rejecting good
//          models or accepting broken ones.
//
//          Build and run (Linux, from src/):
//              g++ -std=c++17 -O2 -DPOSIX=1 -D_POSIX=1 -DLINUX=1 -D_LINUX=1 -DPLATFORM_POSIX=1 \
//                  -DGNUC -DCOMPILER_GCC=1 -DPLATFORM_64BITS=1 -DNDEBUG -Ipublic -Ipublic/tier0 \
//                  -Ipublic/tier1 -Ipublic/mathlib -Icommon -Idatacache \
//                  devtools/test_studio_validate.cpp -o test_studio_validate && ./test_studio_validate
//
//=============================================================================//

// standard headers first: the engine headers define min/max macros
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <functional>
#include <limits>
#include <algorithm>
#include <cmath>
#include "studio_validate.h"

// A model laid out as studiomdl does it: header, then the tables, then the strings.
struct TestModel
{
	std::vector<unsigned char> data;

	studiohdr_t *Hdr() { return (studiohdr_t *)data.data(); }
	template <class T> T *At( int offset ) { return (T *)( data.data() + offset ); }

	int Append( const void *p, int size )
	{
		int offset = (int)data.size();
		// keep the structures 4-byte aligned
		while ( offset & 3 )
		{
			data.push_back( 0 );
			offset++;
		}
		const unsigned char *b = (const unsigned char *)p;
		data.insert( data.end(), b, b + size );
		return offset;
	}
	int AppendZero( int size )
	{
		std::vector<unsigned char> zero( size, 0 );
		return Append( zero.data(), size );
	}
	int AppendString( const char *s ) { return Append( s, (int)strlen( s ) + 1 ); }
};

static TestModel BuildModel()
{
	TestModel m;
	m.AppendZero( sizeof( studiohdr_t ) );

	int boneOfs = m.AppendZero( sizeof( mstudiobone_t ) );
	int setOfs = m.AppendZero( sizeof( mstudiohitboxset_t ) );
	int boxOfs = m.AppendZero( sizeof( mstudiobbox_t ) );
	int texOfs = m.AppendZero( sizeof( mstudiotexture_t ) );
	int cdOfs = m.AppendZero( sizeof( int ) );
	int skinOfs = m.AppendZero( sizeof( short ) * 2 );
	int partOfs = m.AppendZero( sizeof( mstudiobodyparts_t ) );
	int modelOfs = m.AppendZero( sizeof( mstudiomodel_t ) );
	int meshOfs = m.AppendZero( sizeof( mstudiomesh_t ) );
	int attOfs = m.AppendZero( sizeof( mstudioattachment_t ) );
	int animDescOfs = m.AppendZero( sizeof( mstudioanimdesc_t ) );
	int animOfs = m.AppendZero( sizeof( mstudioanim_t ) );
	int seqOfs = m.AppendZero( sizeof( mstudioseqdesc_t ) );
	int blendOfs = m.AppendZero( sizeof( short ) );

	int boneName = m.AppendString( "root" );
	int setName = m.AppendString( "default" );
	int texName = m.AppendString( "skin" );
	int cdPath = m.AppendString( "models/test/" );
	int partName = m.AppendString( "body" );
	int attName = m.AppendString( "muzzle" );
	int surfprop = m.AppendString( "default" );
	int animName = m.AppendString( "@idle" );
	int seqName = m.AppendString( "idle" );
	int actName = m.AppendString( "ACT_IDLE" );

	studiohdr_t *h = m.Hdr();
	h->id = ( 'T' << 24 ) + ( 'S' << 16 ) + ( 'D' << 8 ) + 'I';
	h->version = STUDIO_VERSION;
	strcpy( h->name, "test.mdl" );
	h->length = (int)m.data.size();

	h->numbones = 1;
	h->boneindex = boneOfs;
	mstudiobone_t *bone = m.At<mstudiobone_t>( boneOfs );
	bone->sznameindex = boneName - boneOfs;
	bone->parent = -1;
	for ( int i = 0; i < 6; i++ )
		bone->bonecontroller[i] = -1;

	h->numhitboxsets = 1;
	h->hitboxsetindex = setOfs;
	mstudiohitboxset_t *set = m.At<mstudiohitboxset_t>( setOfs );
	set->sznameindex = setName - setOfs;
	set->numhitboxes = 1;
	set->hitboxindex = boxOfs - setOfs;

	h->numtextures = 1;
	h->textureindex = texOfs;
	m.At<mstudiotexture_t>( texOfs )->sznameindex = texName - texOfs;
	h->numcdtextures = 1;
	h->cdtextureindex = cdOfs;
	*m.At<int>( cdOfs ) = cdPath;

	h->numskinref = 1;
	h->numskinfamilies = 2;
	h->skinindex = skinOfs;

	h->numbodyparts = 1;
	h->bodypartindex = partOfs;
	mstudiobodyparts_t *part = m.At<mstudiobodyparts_t>( partOfs );
	part->sznameindex = partName - partOfs;
	part->nummodels = 1;
	part->base = 1;
	part->modelindex = modelOfs - partOfs;
	mstudiomodel_t *model = m.At<mstudiomodel_t>( modelOfs );
	strcpy( model->name, "body_ref" );
	model->nummeshes = 1;
	model->meshindex = meshOfs - modelOfs;
	model->numvertices = 3;
	mstudiomesh_t *mesh = m.At<mstudiomesh_t>( meshOfs );
	mesh->modelindex = modelOfs - meshOfs;
	mesh->numvertices = 3;
	mesh->vertexdata.numLODVertexes[0] = 3;

	h->numlocalattachments = 1;
	h->localattachmentindex = attOfs;
	m.At<mstudioattachment_t>( attOfs )->sznameindex = attName - attOfs;

	h->surfacepropindex = surfprop;

	// one animation of the root bone, one sequence playing it
	h->numlocalanim = 1;
	h->localanimindex = animDescOfs;
	mstudioanimdesc_t *desc = m.At<mstudioanimdesc_t>( animDescOfs );
	desc->sznameindex = animName - animDescOfs;
	desc->numframes = 1;
	desc->animindex = animOfs - animDescOfs;
	mstudioanim_t *anim = m.At<mstudioanim_t>( animOfs );
	anim->bone = 0;

	h->numlocalseq = 1;
	h->localseqindex = seqOfs;
	mstudioseqdesc_t *seq = m.At<mstudioseqdesc_t>( seqOfs );
	seq->szlabelindex = seqName - seqOfs;
	seq->szactivitynameindex = actName - seqOfs;
	seq->groupsize[0] = seq->groupsize[1] = 1;
	seq->paramindex[0] = seq->paramindex[1] = -1;
	seq->animindexindex = blendOfs - seqOfs;
	return m;
}

static bool Accepts( TestModel &m, std::string *pReason = nullptr )
{
	CStudioHdrValidator validator( m.Hdr(), (int)m.data.size() );
	bool ok = validator.Validate();
	if ( !ok && pReason )
		*pReason = validator.Error();
	return ok;
}

int main()
{
	int failures = 0;

	TestModel good = BuildModel();
	std::string reason;
	if ( !Accepts( good, &reason ) )
	{
		printf( "FAIL: the valid model is rejected (bad %s)\n", reason.c_str() );
		return 1;
	}
	printf( "ok: the valid model is accepted\n" );

	// Data found in real models that the validation once rejected by mistake
	struct Variant
	{
		const char *name;
		std::function<void( TestModel & )> apply;
	};
	const Variant variants[] =
	{
		// models/parachute/p_cosmos.mdl (from a server)
		{ "an animation without animated bones (bone 255)", []( TestModel &m ) { ( (mstudioanim_t *)( (byte *)m.Hdr()->pLocalAnimdesc( 0 ) + m.Hdr()->pLocalAnimdesc( 0 )->animindex ) )->bone = 255; } },
		// models/vortigaunt_gestures.mdl (standard)
		{ "a blend grid of size 0", []( TestModel &m ) { m.Hdr()->pLocalSeqdesc( 0 )->groupsize[0] = 0; } },
		// models/parachute/parachute_carbon.mdl (from a server)
		{ "a sequence referencing a missing animation", []( TestModel &m ) { *(short *)( (byte *)m.Hdr()->pLocalSeqdesc( 0 ) + m.Hdr()->pLocalSeqdesc( 0 )->animindexindex ) = 5; } },
		// models/humans/female_ss.mdl (standard animation library)
		{ "a pose parameter of the including model", []( TestModel &m ) { m.Hdr()->pLocalSeqdesc( 0 )->paramindex[0] = 3; } },
		// weapon models compiled by old studiomdl versions
		{ "a hitbox name offset relative to the header", []( TestModel &m ) { m.Hdr()->pHitboxSet( 0 )->pHitbox( 0 )->szhitboxnameindex = 1 << 20; } },
	};
	for ( const Variant &variant : variants )
	{
		TestModel model = BuildModel();
		variant.apply( model );
		if ( !Accepts( model, &reason ) )
		{
			printf( "FAIL: rejected a valid model with %s (bad %s)\n", variant.name, reason.c_str() );
			failures++;
		}
		else
		{
			printf( "ok: accepted %s\n", variant.name );
		}
	}

	struct Mutation
	{
		const char *name;
		std::function<void( TestModel & )> apply;
	};
	const Mutation mutations[] =
	{
		{ "length beyond the file", []( TestModel &m ) { m.Hdr()->length = (int)m.data.size() + 4; } },
		{ "too many bones", []( TestModel &m ) { m.Hdr()->numbones = MAXSTUDIOBONES + 1; } },
		{ "bone table outside the file", []( TestModel &m ) { m.Hdr()->boneindex = (int)m.data.size(); } },
		{ "bone name outside the file", []( TestModel &m ) { m.Hdr()->pBone( 0 )->sznameindex = 1 << 20; } },
		{ "bone parent after the bone", []( TestModel &m ) { m.Hdr()->pBone( 0 )->parent = 0; } },
		{ "procedural bone without data", []( TestModel &m ) { m.Hdr()->pBone( 0 )->proctype = STUDIO_PROC_JIGGLE; } },
		{ "hitbox on a missing bone", []( TestModel &m ) { m.Hdr()->pHitboxSet( 0 )->pHitbox( 0 )->bone = 5; } },
		{ "skin references a missing texture", []( TestModel &m ) { ( (short *)( m.data.data() + m.Hdr()->skinindex ) )[1] = 7; } },
		{ "texture path outside the file", []( TestModel &m ) { *(int *)( m.data.data() + m.Hdr()->cdtextureindex ) = -8; } },
		{ "body part with base 0", []( TestModel &m ) { m.Hdr()->pBodypart( 0 )->base = 0; } },
		{ "mesh with a wrong model back reference", []( TestModel &m ) { m.Hdr()->pBodypart( 0 )->pModel( 0 )->pMesh( 0 )->modelindex += 4; } },
		{ "mesh material beyond the skin references", []( TestModel &m ) { m.Hdr()->pBodypart( 0 )->pModel( 0 )->pMesh( 0 )->material = 1; } },
		{ "mesh vertices beyond the model", []( TestModel &m ) { m.Hdr()->pBodypart( 0 )->pModel( 0 )->pMesh( 0 )->numvertices = 4; } },
		{ "attachment on a missing bone", []( TestModel &m ) { m.Hdr()->pLocalAttachment( 0 )->localbone = 1; } },
		{ "too many bone controllers", []( TestModel &m ) { m.Hdr()->numbonecontrollers = MAXSTUDIOBONECTRLS + 1; } },
		{ "too many pose parameters", []( TestModel &m ) { m.Hdr()->numlocalposeparameters = MAXSTUDIOPOSEPARAM + 1; } },
		{ "animation data for a missing bone", []( TestModel &m ) { ( (mstudioanim_t *)( (byte *)m.Hdr()->pLocalAnimdesc( 0 ) + m.Hdr()->pLocalAnimdesc( 0 )->animindex ) )->bone = 3; } },
		{ "animation data outside the file", []( TestModel &m ) { m.Hdr()->pLocalAnimdesc( 0 )->animindex = 1 << 20; } },
		{ "blend grid outside the file", []( TestModel &m ) { m.Hdr()->pLocalSeqdesc( 0 )->groupsize[0] = 1 << 20; } },
		{ "sequence label outside the file", []( TestModel &m ) { m.Hdr()->pLocalSeqdesc( 0 )->szlabelindex = -( 1 << 20 ); } },
	};

	for ( const Mutation &mutation : mutations )
	{
		TestModel broken = BuildModel();
		mutation.apply( broken );
		if ( Accepts( broken ) )
		{
			printf( "FAIL: accepted a model with %s\n", mutation.name );
			failures++;
		}
		else
		{
			printf( "ok: rejected %s\n", mutation.name );
		}
	}

	printf( "%s: %d failures (%d valid variants, %d broken models)\n", failures ? "FAILED" : "PASSED", failures,
		(int)( sizeof( variants ) / sizeof( variants[0] ) ), (int)( sizeof( mutations ) / sizeof( mutations[0] ) ) );
	return failures ? 1 : 0;
}
