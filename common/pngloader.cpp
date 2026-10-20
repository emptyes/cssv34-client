//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#include "pngloader.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
//#include "vstdlib/strtools.h"

#include <setjmp.h>

// clang3 on OSX folks the attribute into the prototype, causing a compile failure
// filed radar bug 10397783
#if ( __clang_major__ == 3 )
extern void longjmp( jmp_buf, int ) __attribute__((noreturn));
#endif

#include "libpng/png.h"
#include "tier0/dbg.h"

//
// struct to encapsulate png data in memory that we are walking through
//
struct PngDataStream_t
{
	PngDataStream_t( const byte *pchData, int cbData )
	{
		m_pPNGData = pchData;
		m_cbPNGData = cbData > 0 ? (size_t)cbData : 0;
		m_iPNGData = 0;
	}

	const byte *m_pPNGData; // the png data
	size_t m_cbPNGData; // length of the data
	size_t m_iPNGData; // offset for next read
};


// helper function to nibble some more png data from our memory buffer
void ReadPNGData( png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead )
{
	PngDataStream_t *pData = (PngDataStream_t *)png_get_io_ptr( png_ptr );
	if ( !pData || !pData->m_pPNGData || pData->m_iPNGData > pData->m_cbPNGData ||
		byteCountToRead > pData->m_cbPNGData - pData->m_iPNGData )
	{
		png_error( png_ptr, "Unexpected end of PNG data" );
		return;
	}

	Q_memcpy( outBytes, pData->m_pPNGData + pData->m_iPNGData, byteCountToRead );
	pData->m_iPNGData += byteCountToRead;
}

// called when we failed to load a png file, we just bail to the previously set exit
static void PNGErrorFunction(png_structp png_ptr, png_const_charp msg) 
{
	Warning( "PNG load error %s\n", msg );
	longjmp( png_jmpbuf(png_ptr), 1 );
}


// we had a warning lets log it
static void PNGWarningFunction(png_structp png_ptr, png_const_charp msg) 
{
	Warning( "PNG load error %s\n", msg );
}


// Get dimensions out of PNG header
bool GetPNGDimensions( const byte *pubPNGData, int cubPNGData, uint32 &width, uint32 &height )
{
	if ( !pubPNGData || cubPNGData < 8 )
		return false;

	png_const_bytep pngData = (png_const_bytep)pubPNGData;
	if (png_sig_cmp( pngData, 0, 8))
        return false;   /* bad signature */

	PngDataStream_t pngDataStream( pubPNGData, cubPNGData ); // keeps a local copy of the data we a reading

	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;

    png_ptr = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, &PNGErrorFunction, &PNGWarningFunction );
    if (!png_ptr)
        return false;   /* out of memory */

    info_ptr = png_create_info_struct( png_ptr );
    if ( !info_ptr ) 
	{
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return false;   /* out of memory */
    }

    /* setjmp() must be called in every function that calls a PNG-reading
     * libpng function */
    if ( setjmp( png_jmpbuf(png_ptr) ) ) 
	{
        png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
        return false;
    }

	png_set_read_fn( png_ptr, (void *)&pngDataStream, ReadPNGData );
    png_read_info( png_ptr, info_ptr );  /* read all PNG info up to image data */

	width = png_get_image_width( png_ptr, info_ptr );
	height = png_get_image_height( png_ptr, info_ptr );
	png_destroy_read_struct( &png_ptr, &info_ptr, NULL );

	return true;
}


// given a png formatted memory buffer return a raw rgba buffer
bool ConvertPNGToRGBA( const byte *pubPNGData, int cubPNGData, CUtlBuffer &bufOutput, int &width, int &height )
{
	if ( !pubPNGData || cubPNGData < 8 )
		return false;

	png_const_bytep pngData = (png_const_bytep)pubPNGData;
	if (png_sig_cmp( pngData, 0, 8))
        return false;   /* bad signature */

	PngDataStream_t pngDataStream( pubPNGData, cubPNGData ); // keeps a local copy of the data we a reading

	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;

    /* could pass pointers to user-defined error handlers instead of NULLs: */

    png_ptr = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, &PNGErrorFunction, &PNGWarningFunction );
    if (!png_ptr)
        return false;   /* out of memory */

    info_ptr = png_create_info_struct( png_ptr );
    if ( !info_ptr ) 
	{
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return false;   /* out of memory */
    }

    /* setjmp() must be called in every function that calls a PNG-reading
     * libpng function */

    if ( setjmp( png_jmpbuf(png_ptr) ) ) 
	{
        png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
        return false;
    }

	png_set_read_fn( png_ptr, (void *)&pngDataStream, ReadPNGData );
    png_read_info( png_ptr, info_ptr );  /* read all PNG info up to image data */


    /* alternatively, could make separate calls to png_get_image_width(),
     * etc., but want bit_depth and color_type for later [don't care about
     * compression_type and filter_type => NULLs] */

	int bit_depth;
	int color_type;
	uint32 png_width;
	uint32 png_height;

	png_get_IHDR( png_ptr, info_ptr, &png_width, &png_height, &bit_depth, &color_type, NULL, NULL, NULL );

	if ( !png_width || !png_height || png_width > INT_MAX / 4 ||
		png_height > (png_uint_32)INT_MAX / ( png_width * 4 ) )
		png_error( png_ptr, "PNG dimensions exceed output buffer limits" );

	if ( color_type == PNG_COLOR_TYPE_PALETTE ||
		( color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8 ) )
		png_set_expand( png_ptr );
	const bool hasTransparency = png_get_valid( png_ptr, info_ptr, PNG_INFO_tRNS ) != 0;
	if ( hasTransparency )
		png_set_tRNS_to_alpha( png_ptr );
	if ( bit_depth == 16 )
		png_set_strip_16( png_ptr );
	if ( color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA )
		png_set_gray_to_rgb( png_ptr );
	if ( !( color_type & PNG_COLOR_MASK_ALPHA ) && !hasTransparency )
		png_set_filler( png_ptr, 0xff, PNG_FILLER_AFTER );

	const int passes = png_set_interlace_handling( png_ptr );
	png_read_update_info( png_ptr, info_ptr );

	const png_size_t rowbytes = png_get_rowbytes( png_ptr, info_ptr );
	if ( png_get_channels( png_ptr, info_ptr ) != 4 || rowbytes != (size_t)png_width * 4 ||
		rowbytes > (size_t)INT_MAX / png_height )
		png_error( png_ptr, "Invalid PNG output layout" );

	const int outputBytes = (int)( rowbytes * png_height );
	bufOutput.EnsureCapacity( outputBytes );
	bufOutput.SeekPut( CUtlBuffer::SEEK_HEAD, outputBytes );
	if ( passes > 1 )
		Q_memset( bufOutput.Base(), 0, outputBytes );
	for ( int pass = 0; pass < passes; ++pass )
	{
		for ( png_uint_32 row = 0; row < png_height; ++row )
			png_read_row( png_ptr, (png_bytep)bufOutput.Base() + (size_t)row * rowbytes, NULL );
	}
	png_read_end( png_ptr, NULL );
	png_destroy_read_struct( &png_ptr, &info_ptr, NULL );

	width = (int)png_width;
	height = (int)png_height;
	return true;
}
