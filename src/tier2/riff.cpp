//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// Avoid these warnings:
#pragma warning(disable : 4512) // warning C4512: 'InFileRIFF' : assignment operator could not be generated
#pragma warning(disable : 4514) // warning C4514: 'RIFFName' : unreferenced inline function has been removed

#include "riff.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include "tier0/dbg.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if 0
//-----------------------------------------------------------------------------
// Purpose: Test code that implements the interface on stdio
//-----------------------------------------------------------------------------
class StdIOReadBinary : public IFileReadBinary
{
public:
	intp open( const char *pFileName )
	{
		return (intp)fopen( pFileName, "rb" );
	}

	int read( void *pOutput, int size, intp file )
	{
		FILE *fp = (FILE *)file;

		return fread( pOutput, size, 1, fp );
	}

	void seek( intp file, int pos )
	{
		fseek( (FILE *)file, pos, SEEK_SET );
	}

	unsigned int tell( intp file )
	{
		return ftell( (FILE *)file );
	}

	unsigned int size( intp file )
	{
		FILE *fp = (FILE *)file;
		if ( !fp )
			return 0;

		unsigned int pos = ftell( fp );
		fseek( fp, 0, SEEK_END );
		unsigned int size = ftell( fp );

		fseek( fp, pos, SEEK_SET );
		return size;
	}

	void close( intp file )
	{
		FILE *fp = (FILE *)file;

		fclose( fp );
	}
};
#endif


#define RIFF_ID MAKEID('R','I','F','F')


//-----------------------------------------------------------------------------
// Purpose: Opens a RIFF file using the given I/O mechanism
// Input  : *pFileName 
//			&io - I/O interface
//-----------------------------------------------------------------------------
InFileRIFF::InFileRIFF( const char *pFileName, IFileReadBinary &io )
	: m_io(io), m_file(0), m_riffName(0), m_riffSize(0)
{
	m_file = m_io.open( pFileName );
	if ( !m_file )
		return;

	const unsigned int fileSize = m_io.size( m_file );
	if ( fileSize < 12 || fileSize > INT_MAX )
		return;

	int header[3];
	if ( m_io.read( header, sizeof(header), m_file ) != sizeof(header) )
		return;
	if ( LittleLong( header[0] ) != RIFF_ID )
	{
		printf( "Not a RIFF File [%s]\n", pFileName );
		return;
	}
	m_riffName = LittleLong( header[2] );
	m_riffSize = fileSize - 12;
}


//-----------------------------------------------------------------------------
// Purpose: Close the file
//-----------------------------------------------------------------------------
InFileRIFF::~InFileRIFF( void )
{
	m_io.close( m_file );
}


//-----------------------------------------------------------------------------
// Purpose: read a 4-byte int out of the stream
// Output : int = read value, default is zero
//-----------------------------------------------------------------------------
int InFileRIFF::ReadInt( void )
{
	int value = 0;
	if ( ReadData( &value, sizeof(value) ) != sizeof(value) )
		return 0;
	return LittleLong( value );
}

//-----------------------------------------------------------------------------
// Purpose: Read a block of binary data
// Input  : *pOutput - pointer to destination memory
//			dataSize - size of block to read
// Output : int - number of bytes read
//-----------------------------------------------------------------------------
int InFileRIFF::ReadData( void *pOutput, int dataSize )
{
	if ( !m_file || !pOutput || dataSize <= 0 )
		return 0;
	const unsigned int position = m_io.tell( m_file );
	const unsigned int fileSize = m_io.size( m_file );
	if ( position > fileSize || (unsigned int)dataSize > fileSize - position )
		return 0;
	return m_io.read( pOutput, dataSize, m_file );
}


//-----------------------------------------------------------------------------
// Purpose: Gets the file position
// Output : int (bytes from start of file)
//-----------------------------------------------------------------------------
int InFileRIFF::PositionGet( void )
{
	if ( !m_file )
		return -1;
	const unsigned int position = m_io.tell( m_file );
	return position <= INT_MAX ? (int)position : -1;
}


//-----------------------------------------------------------------------------
// Purpose: Seek to file position
// Input  : position - bytes from start of file
//-----------------------------------------------------------------------------
void InFileRIFF::PositionSet( int position )
{
	if ( m_file && position >= 0 && (unsigned int)position <= m_io.size( m_file ) )
		m_io.seek( m_file, position );
}

//-----------------------------------------------------------------------------
// Purpose: Used to write a RIFF format file
//-----------------------------------------------------------------------------
OutFileRIFF::OutFileRIFF( const char *pFileName, IFileWriteBinary &io ) : m_io( io )
{
	m_file = m_io.create( pFileName );

	if ( !m_file )
		return;

	int riff = RIFF_ID;
	m_io.write( &riff, 4, m_file );

	m_riffSize = 0;
	m_nNamePos = m_io.tell( m_file );

	// Save room for the size and name now
	WriteInt( 0 );

	// Write out the name
	WriteInt( RIFF_WAVE );

	m_bUseIncorrectLISETLength = false;
	m_nLISETSize = 0;
}

OutFileRIFF::~OutFileRIFF( void )
{
	if ( !IsValid() )
		return;

	unsigned int size = m_io.tell( m_file ) -8;
	m_io.seek( m_file, m_nNamePos );

	if ( m_bUseIncorrectLISETLength )
	{
		size = m_nLISETSize - 8;
	}

	WriteInt( size );
	m_io.close( m_file );
}

void OutFileRIFF::HasLISETData( int position )
{
	m_bUseIncorrectLISETLength = true;
	m_nLISETSize = position;
}

bool OutFileRIFF::WriteInt( int number )
{
	if ( !IsValid() )
		return false;

	m_io.write( &number, sizeof( int ), m_file );
	return true;
}

bool OutFileRIFF::WriteData( void *pOutput, int dataSize )
{
	if ( !IsValid() )
		return false;

	m_io.write( pOutput, dataSize, m_file );
	return true;
}

int OutFileRIFF::PositionGet( void )
{
	if ( !IsValid() )
		return 0;

	return m_io.tell( m_file );
}

void OutFileRIFF::PositionSet( int position )
{
	if ( !IsValid() )
		return;

	m_io.seek( m_file, position );
}


//-----------------------------------------------------------------------------
// Purpose: Create an iterator for the given file
// Input  : &riff - riff file
//			size - size of file or sub-chunk
//-----------------------------------------------------------------------------
IterateRIFF::IterateRIFF( InFileRIFF &riff, int size )
	: m_riff(riff), m_start(0), m_size(size), m_chunkName(0), m_chunkSize(-1), m_chunkPosition(0)
{
	if ( !m_riff.RIFFSize() || size < 0 )
		return;
	m_start = riff.PositionGet();
	const int fileEnd = 12 + (int)m_riff.RIFFSize();
	if ( m_start < 12 || m_start > fileEnd || size > fileEnd - m_start )
		return;
	ChunkSetup();
}


//-----------------------------------------------------------------------------
// Purpose: Set up a sub-chunk iterator
// Input  : &parent - parent iterator
//-----------------------------------------------------------------------------
IterateRIFF::IterateRIFF( IterateRIFF &parent )
	: m_riff(parent.m_riff), m_start(0), m_size(0), m_chunkName(0), m_chunkSize(-1), m_chunkPosition(0)
{
	if ( !parent.ChunkAvailable() )
		return;
	m_start = parent.m_chunkPosition + 8;
	m_size = parent.m_chunkSize;
	ChunkSetup();
}

//-----------------------------------------------------------------------------
// Purpose: Parse the chunk at the current file position 
//			This object will iterate over the sub-chunks of this chunk.
//			This makes for easy hierarchical parsing
//-----------------------------------------------------------------------------
void IterateRIFF::ChunkSetup( void )
{
	ChunkClear();
	m_chunkPosition = m_riff.PositionGet();
	if ( m_chunkPosition < m_start || m_chunkPosition - m_start > m_size ||
		m_size - ( m_chunkPosition - m_start ) < 8 )
		return;
	int header[2];
	if ( m_riff.ReadData( header, sizeof(header) ) != sizeof(header) )
		return;
	const int size = LittleLong( header[1] );
	if ( size < 0 || size >= 0x10000000 || size > m_size - ( m_chunkPosition - m_start ) - 8 )
		return;
	m_chunkName = LittleLong( header[0] );
	m_chunkSize = size;
}

//-----------------------------------------------------------------------------
// Purpose: clear chunk setup, ChunkAvailable will return false
//-----------------------------------------------------------------------------
void IterateRIFF::ChunkClear( void )
{
	m_chunkName = 0;
	m_chunkSize = -1;
}

//-----------------------------------------------------------------------------
// Purpose: If there are chunks left to read beyond this one, return true
//-----------------------------------------------------------------------------
bool IterateRIFF::ChunkAvailable( void )
{
	return m_chunkSize >= 0 && m_chunkSize < 0x10000000;
}


//-----------------------------------------------------------------------------
// Purpose: Go to the next chunk in the file, return true if there is one.
//-----------------------------------------------------------------------------
bool IterateRIFF::ChunkNext( void )
{
	if ( !ChunkAvailable() )
		return false;
	const int remaining = m_size - ( m_chunkPosition - m_start ) - 8;
	const int padding = m_chunkSize & 1;
	if ( m_chunkSize >= remaining - padding )
	{
		ChunkClear();
		return false;
	}
	const int nextPos = m_chunkPosition + 8 + m_chunkSize + padding;
	m_riff.PositionSet( nextPos );
	ChunkSetup();
	return ChunkAvailable();
}


//-----------------------------------------------------------------------------
// Purpose: get the chunk FOURCC as an int
// Output : unsigned int
//-----------------------------------------------------------------------------
unsigned int IterateRIFF::ChunkName( void )
{
	return m_chunkName;
}


//-----------------------------------------------------------------------------
// Purpose: get the size of this chunk
// Output : unsigned int
//-----------------------------------------------------------------------------
unsigned int IterateRIFF::ChunkSize( void )
{
	return m_chunkSize;
}

//-----------------------------------------------------------------------------
// Purpose: Read the entire chunk into a buffer
// Input  : *pOutput - dest buffer
// Output : int bytes read
//-----------------------------------------------------------------------------
int IterateRIFF::ChunkRead( void *pOutput )
{
	return ChunkReadPartial( pOutput, m_chunkSize );
}


//-----------------------------------------------------------------------------
// Purpose: Read a partial chunk (updates file position for subsequent partial reads).
// Input  : *pOutput - dest buffer
//			dataSize - partial size
// Output : int - bytes read
//-----------------------------------------------------------------------------
int IterateRIFF::ChunkReadPartial( void *pOutput, int dataSize )
{
	if ( !ChunkAvailable() || !pOutput || dataSize <= 0 )
		return 0;
	const int position = m_riff.PositionGet();
	const int start = m_chunkPosition + 8;
	if ( position < start || position - start > m_chunkSize )
		return 0;
	const int remaining = m_chunkSize - ( position - start );
	if ( dataSize > remaining )
		dataSize = remaining;
	return m_riff.ReadData( pOutput, dataSize );
}


//-----------------------------------------------------------------------------
// Purpose: Read a 4-byte int
// Output : int - read int
//-----------------------------------------------------------------------------
int IterateRIFF::ChunkReadInt( void )
{
	int value = 0;
	if ( ChunkReadPartial( &value, sizeof(value) ) != sizeof(value) )
		return 0;
	return LittleLong( value );
}

//-----------------------------------------------------------------------------
// Purpose: Used to iterate over an InFileRIFF
//-----------------------------------------------------------------------------
IterateOutputRIFF::IterateOutputRIFF( OutFileRIFF &riff )
: m_riff( riff )
{
	if ( !m_riff.IsValid() )
		return;

	m_start = m_riff.PositionGet();
	m_chunkPosition = m_start;
	m_chunkStart = -1;
}

IterateOutputRIFF::IterateOutputRIFF( IterateOutputRIFF &parent )
	: m_riff(parent.m_riff)
{
	m_start = parent.ChunkFilePosition();
	m_chunkPosition = m_start;
	m_chunkStart = -1;
}

void IterateOutputRIFF::ChunkWrite( unsigned int chunkname, void *pOutput, int size )
{
	m_chunkPosition = m_riff.PositionGet();

	m_chunkName = chunkname;
	m_chunkSize = size;

	m_riff.WriteInt( chunkname );
	m_riff.WriteInt( size );
	m_riff.WriteData( pOutput, size );

	m_chunkPosition = m_riff.PositionGet();
	
	m_chunkPosition += m_chunkPosition & 1;

	m_riff.PositionSet( m_chunkPosition );

	m_chunkStart = -1;
}

void IterateOutputRIFF::ChunkWriteInt( int number )
{
	m_riff.WriteInt( number );
}

void IterateOutputRIFF::ChunkWriteData( void *pOutput, int size )
{
	m_riff.WriteData( pOutput, size );
}

void IterateOutputRIFF::ChunkFinish( void )
{
	Assert( m_chunkStart != -1 );

	m_chunkPosition = m_riff.PositionGet();
	
	int size = m_chunkPosition - m_chunkStart - 8;

	m_chunkPosition += m_chunkPosition & 1;

	m_riff.PositionSet( m_chunkStart + sizeof( int ) );

	m_riff.WriteInt( size );

	m_riff.PositionSet( m_chunkPosition );

	m_chunkStart = -1;
}

void IterateOutputRIFF::ChunkStart( unsigned int chunkname )
{
	Assert( m_chunkStart == -1 );

	m_chunkStart = m_riff.PositionGet();

	m_riff.WriteInt( chunkname );
	m_riff.WriteInt( 0 );
}

void IterateOutputRIFF::ChunkSetPosition( int position )
{
	m_riff.PositionSet( position );
}

unsigned int IterateOutputRIFF::ChunkGetPosition( void )
{
	return m_riff.PositionGet();
}

void IterateOutputRIFF::CopyChunkData( IterateRIFF& input )
{
	if (  input.ChunkSize() > 0 )
	{
		char *buffer = new char[ input.ChunkSize() ];
		Assert( buffer );

		input.ChunkRead( buffer );

		// Don't copy/write the name or size, just the data itself
		ChunkWriteData( buffer, input.ChunkSize() );

		delete[] buffer;
	}
}

void IterateOutputRIFF::SetLISETData( int position )
{
	m_riff.HasLISETData( position );
}