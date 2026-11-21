//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Helper methods + classes for sound
//
//===========================================================================//

#include "tier2/soundutils.h"
#include "tier2/riff.h"
#include "tier2/waveformat.h"
#include "tier2/tier2.h"
#include "filesystem.h"

#ifdef IS_WINDOWS_PC

#include <windows.h> // WAVEFORMATEX, WAVEFORMAT and ADPCM WAVEFORMAT!!!
#include <mmreg.h>

#else

#ifdef _X360
#include "xbox/xbox_win32stubs.h" // WAVEFORMATEX, WAVEFORMAT and ADPCM WAVEFORMAT!!!
#endif

#endif

//-----------------------------------------------------------------------------
// RIFF reader/writers that use the file system
//-----------------------------------------------------------------------------
class CFSIOReadBinary : public IFileReadBinary
{
public:
	// inherited from IFileReadBinary
	virtual intp open( const char *pFileName );
	virtual int read( void *pOutput, int size, intp file );
	virtual void seek( intp file, int pos );
	virtual unsigned int tell( intp file );
	virtual unsigned int size( intp file );
	virtual void close( intp file );
};

class CFSIOWriteBinary : public IFileWriteBinary
{
public:
	virtual intp create( const char *pFileName );
	virtual int write( void *pData, int size, intp file );
	virtual void close( intp file );
	virtual void seek( intp file, int pos );
	virtual unsigned int tell( intp file );
};


//-----------------------------------------------------------------------------
// Singletons
//-----------------------------------------------------------------------------
static CFSIOReadBinary s_FSIoIn;
static CFSIOWriteBinary s_FSIoOut;

IFileReadBinary *g_pFSIOReadBinary = &s_FSIoIn;
IFileWriteBinary *g_pFSIOWriteBinary = &s_FSIoOut;


//-----------------------------------------------------------------------------
// RIFF reader that use the file system
//-----------------------------------------------------------------------------
intp CFSIOReadBinary::open( const char *pFileName )
{
	return (intp)g_pFullFileSystem->Open( pFileName, "rb" );
}

int CFSIOReadBinary::read( void *pOutput, int size, intp file )
{
	if ( !file )
		return 0;

	return g_pFullFileSystem->Read( pOutput, size, (FileHandle_t)file );
}

void CFSIOReadBinary::seek( intp file, int pos )
{
	if ( !file )
		return;

	g_pFullFileSystem->Seek( (FileHandle_t)file, pos, FILESYSTEM_SEEK_HEAD );
}

unsigned int CFSIOReadBinary::tell( intp file )
{
	if ( !file )
		return 0;

	return g_pFullFileSystem->Tell( (FileHandle_t)file );
}

unsigned int CFSIOReadBinary::size( intp file )
{
	if ( !file )
		return 0;

	return g_pFullFileSystem->Size( (FileHandle_t)file );
}

void CFSIOReadBinary::close( intp file )
{
	if ( !file )
		return;

	g_pFullFileSystem->Close( (FileHandle_t)file );
}


//-----------------------------------------------------------------------------
// RIFF writer that use the file system
//-----------------------------------------------------------------------------
intp CFSIOWriteBinary::create( const char *pFileName )
{
	g_pFullFileSystem->SetFileWritable( pFileName, true );
	return (intp)g_pFullFileSystem->Open( pFileName, "wb" );
}

int CFSIOWriteBinary::write( void *pData, int size, intp file )
{
	return g_pFullFileSystem->Write( pData, size, (FileHandle_t)file );
}

void CFSIOWriteBinary::close( intp file )
{
	g_pFullFileSystem->Close( (FileHandle_t)file );
}

void CFSIOWriteBinary::seek( intp file, int pos )
{
	g_pFullFileSystem->Seek( (FileHandle_t)file, pos, FILESYSTEM_SEEK_HEAD );
}

unsigned int CFSIOWriteBinary::tell( intp file )
{
	return g_pFullFileSystem->Tell( (FileHandle_t)file );
}


#ifndef POSIX
//-----------------------------------------------------------------------------
// Returns the duration of a wav file
//-----------------------------------------------------------------------------
float GetWavSoundDuration( const char *pWavFile )
{
	InFileRIFF riff( pWavFile, *g_pFSIOReadBinary );
	if ( riff.RIFFName() != RIFF_WAVE )
		return 0.0f;

	char formatBuffer[1024];
	WaveFormatInfo_t info;
	bool found = false;
	int dataSize = 0;
	IterateRIFF walk( riff, riff.RIFFSize() );
	for ( ; walk.ChunkAvailable(); walk.ChunkNext() )
	{
		if ( walk.ChunkName() == WAVE_FMT )
		{
			unsigned int size = walk.ChunkSize();
			if ( size < 16 || size > sizeof( formatBuffer ) || walk.ChunkRead( formatBuffer ) != static_cast<int>( size ) ||
				 !WaveFormatParse( formatBuffer, static_cast<int>( size ), info ) )
				return 0.0f;
			found = true;
		}
		else if ( walk.ChunkName() == WAVE_DATA )
		{
			if ( walk.ChunkSize() > static_cast<unsigned int>( INT_MAX - dataSize ) )
				return 0.0f;
			dataSize += walk.ChunkSize();
		}
	}
	if ( !found )
		return 0.0f;
	return static_cast<float>( WaveFormatSampleCount( info, dataSize ) ) / info.rate;
}
#endif
