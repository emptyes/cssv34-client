#ifndef TIER2_WAVEFORMAT_H
#define TIER2_WAVEFORMAT_H
#pragma once

#include <limits.h>

struct WaveFormatInfo_t
{
	int format;
	int channels;
	int rate;
	int blockAlign;
	int samplesPerBlock;
};

inline unsigned int WaveFormatReadWord( const unsigned char *p )
{
	return p[0] | ( static_cast<unsigned int>( p[1] ) << 8 );
}

inline unsigned int WaveFormatReadDWord( const unsigned char *p )
{
	return WaveFormatReadWord( p ) | ( WaveFormatReadWord( p + 2 ) << 16 );
}

inline int WaveADPCMSampleCount( int channels, int samplesPerBlock, int length )
{
	if ( ( channels != 1 && channels != 2 ) || samplesPerBlock < 2 || samplesPerBlock > 65535 || length < 0 )
		return 0;

	int blockSize = ( samplesPerBlock - 2 ) * channels / 2 + 7 * channels;
	int remainder = length % blockSize;
	long long count = static_cast<long long>( length / blockSize ) * samplesPerBlock;
	if ( remainder >= 7 * channels )
		count += 2 + ( remainder - 7 * channels ) * 2 / channels;
	return count <= INT_MAX ? static_cast<int>( count ) : 0;
}

inline bool WaveFormatParse( const void *data, int size, WaveFormatInfo_t &info )
{
	if ( !data || size < 16 )
		return false;

	const unsigned char *p = static_cast<const unsigned char *>( data );
	WaveFormatInfo_t parsed;
	parsed.format = WaveFormatReadWord( p );
	parsed.channels = WaveFormatReadWord( p + 2 );
	unsigned int rate = WaveFormatReadDWord( p + 4 );
	parsed.blockAlign = WaveFormatReadWord( p + 12 );
	parsed.samplesPerBlock = 0;
	if ( ( parsed.channels != 1 && parsed.channels != 2 ) || rate == 0 || rate > INT_MAX )
		return false;
	parsed.rate = static_cast<int>( rate );

	if ( size == 17 || ( size >= 18 && WaveFormatReadWord( p + 16 ) > static_cast<unsigned int>( size - 18 ) ) )
		return false;

	int bits = WaveFormatReadWord( p + 14 );
	if ( parsed.format == 1 )
	{
		if ( ( bits != 8 && bits != 16 ) || parsed.blockAlign != parsed.channels * ( bits / 8 ) )
			return false;
	}
	else if ( parsed.format == 2 )
	{
		if ( size < 22 || bits != 4 )
			return false;
		unsigned int coefficients = WaveFormatReadWord( p + 20 );
		unsigned int extensionSize = WaveFormatReadWord( p + 16 );
		if ( coefficients == 0 || extensionSize < 4 + coefficients * 4 )
			return false;
		parsed.samplesPerBlock = WaveFormatReadWord( p + 18 );
		if ( parsed.samplesPerBlock < 2 || parsed.blockAlign < 7 * parsed.channels || parsed.blockAlign >= 4096 )
			return false;
		if ( parsed.samplesPerBlock != 2 + ( parsed.blockAlign - 7 * parsed.channels ) * 2 / parsed.channels )
			return false;
	}
	else
	{
		return false;
	}

	info = parsed;
	return true;
}

inline int WaveFormatSampleCount( const WaveFormatInfo_t &info, int length )
{
	if ( length < 0 )
		return 0;
	if ( info.format == 2 )
		return WaveADPCMSampleCount( info.channels, info.samplesPerBlock, length );
	return info.blockAlign > 0 ? length / info.blockAlign : 0;
}

#endif
