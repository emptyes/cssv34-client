//========= cssv34-client: model checker ======================================//
//
// Purpose: Checks .mdl/.vvd/.vtx files with the same code the client uses when
//          it loads a model (datacache/studio_validate.h), without the game:
//
//              check_mdl models/player/ct_urban.mdl models/weapons/*.mdl
//
//          A model reported as broken is not loaded (or not drawn) by the client.
//          Exit code: 0 if all models are fine, 1 otherwise.
//
//          Build (Linux):
//              g++ -std=c++17 -O2 -DPOSIX=1 -D_POSIX=1 -DLINUX=1 -D_LINUX=1 -DPLATFORM_POSIX=1 \
//                  -DGNUC -DCOMPILER_GCC=1 -DPLATFORM_64BITS=1 -DNDEBUG -Ipublic -Ipublic/tier0 \
//                  -Ipublic/tier1 -Ipublic/mathlib -Icommon -Idatacache \
//                  devtools/check_mdl.cpp -o check_mdl
//
//=============================================================================//

// standard headers first: the engine headers define min/max macros
#include <stdio.h>
#include <vector>
#include <string>
#include <limits>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "studio_validate.h"

// as in datacache/mdlcache.cpp
#ifndef IDSTUDIOHEADER
#define IDSTUDIOHEADER	(('T'<<24)+('S'<<16)+('D'<<8)+'I')
#endif

static bool ReadFile( const std::string &path, std::vector<unsigned char> &data )
{
	FILE *f = fopen( path.c_str(), "rb" );
	if ( !f )
		return false;
	fseek( f, 0, SEEK_END );
	long size = ftell( f );
	fseek( f, 0, SEEK_SET );
	// keep the buffer aligned for the structures and NUL padded like the engine's buffers
	data.assign( size > 0 ? size : 0, 0 );
	bool ok = size >= 0 && fread( data.data(), 1, data.size(), f ) == data.size();
	fclose( f );
	return ok;
}

static int CheckModel( const std::string &mdlPath )
{
	std::vector<unsigned char> mdl;
	if ( !ReadFile( mdlPath, mdl ) )
	{
		printf( "%s: ERROR: cannot read the file\n", mdlPath.c_str() );
		return 1;
	}
	if ( mdl.size() < sizeof( studiohdr_t ) || *(const int *)mdl.data() != IDSTUDIOHEADER )
	{
		printf( "%s: ERROR: not a model\n", mdlPath.c_str() );
		return 1;
	}
	const studiohdr_t *pHdr = (const studiohdr_t *)mdl.data();

	CStudioHdrValidator validator( pHdr, (int)mdl.size() );
	if ( !validator.Validate() )
	{
		printf( "%s: ERROR: .mdl: bad %s (%d)\n", mdlPath.c_str(), validator.Error(), validator.ErrorIndex() );
		return 1;
	}

	std::string base = mdlPath.substr( 0, mdlPath.size() - 4 );
	std::string notes;

	if ( pHdr->numbodyparts )
	{
		std::vector<unsigned char> vvd;
		if ( !ReadFile( base + ".vvd", vvd ) )
			notes += " (no .vvd)";
		else
		{
			const vertexFileHeader_t *pVvd = (const vertexFileHeader_t *)vvd.data();
			const char *pszError = NULL;
			if ( vvd.size() < sizeof( vertexFileHeader_t ) || pVvd->id != MODEL_VERTEX_FILE_ID ||
				pVvd->version != MODEL_VERTEX_FILE_VERSION || pVvd->checksum != pHdr->checksum )
			{
				printf( "%s: ERROR: .vvd: wrong header or checksum\n", mdlPath.c_str() );
				return 1;
			}
			if ( !ValidateVertexData( pVvd, (int)vvd.size(), 0, true ) )
			{
				printf( "%s: ERROR: .vvd: invalid data ranges or fixups\n", mdlPath.c_str() );
				return 1;
			}
			if ( !ValidateVertexBones( pHdr, pVvd, 0, &pszError ) )
			{
				printf( "%s: ERROR: .vvd: invalid %s\n", mdlPath.c_str(), pszError );
				return 1;
			}
		}

		// the client uses .dx90.vtx on DirectX 9
		const char *vtxExt[] = { ".dx90.vtx", ".dx80.vtx", ".sw.vtx" };
		bool bAny = false;
		for ( const char *ext : vtxExt )
		{
			std::vector<unsigned char> vtx;
			if ( !ReadFile( base + ext, vtx ) )
				continue;
			bAny = true;
			const OptimizedModel::FileHeader_t *pVtx = (const OptimizedModel::FileHeader_t *)vtx.data();
			const char *pszError = "header";
			if ( vtx.size() < sizeof( OptimizedModel::FileHeader_t ) || pVtx->version != OPTIMIZED_MODEL_FILE_VERSION ||
				pVtx->checkSum != pHdr->checksum )
			{
				printf( "%s: ERROR: %s: wrong header or checksum\n", mdlPath.c_str(), ext );
				return 1;
			}
			if ( !ValidateHardwareData( pHdr, pVtx, (int)vtx.size(), &pszError ) )
			{
				printf( "%s: ERROR: %s: bad %s\n", mdlPath.c_str(), ext, pszError );
				return 1;
			}
		}
		if ( !bAny )
			notes += " (no .vtx)";
	}

	printf( "%s: OK%s\n", mdlPath.c_str(), notes.c_str() );
	return 0;
}

int main( int argc, char **argv )
{
	if ( argc < 2 )
	{
		printf( "usage: check_mdl <file.mdl> ...\n" );
		return 2;
	}
	int failed = 0;
	for ( int i = 1; i < argc; i++ )
		failed += CheckModel( argv[i] );
	if ( argc > 2 )
		printf( "%d models checked, %d with errors\n", argc - 1, failed );
	return failed ? 1 : 0;
}
