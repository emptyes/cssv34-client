//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
//
// text_message.cpp
//
// implementation of CHudTextMessage class
//
// this class routes messages through titles.txt for localisation
//
#include "cbase.h"
#include "text_message.h"
#include "client_textmessage.h"
#include "vgui_controls/Controls.h"
#include "vgui/ILocalize.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Implements IHudTextMessage
//-----------------------------------------------------------------------------
class CHudTextMessage : public IHudTextMessage
{
public:
	virtual char *LocaliseTextString( const char *msg, char *dst_buffer, int buffer_size );
	virtual char *BufferedLocaliseTextString( const char *msg );
	virtual char *LookupString( const char *msg_name, int *msg_dest = NULL );
};

// Singleton
static CHudTextMessage g_HudTextMessage;
IHudTextMessage *hudtextmessage = &g_HudTextMessage;

//-----------------------------------------------------------------------------
// Purpose:  Searches through the string for any msg names (indicated by a '#')
// any found are looked up in titles.txt and the new message substituted
// the new value is pushed into dst_buffer
// Input  : *msg - 
//			*dst_buffer - 
//			buffer_size - 
// Output : char
//-----------------------------------------------------------------------------
char *CHudTextMessage::LocaliseTextString( const char *msg, char *dst_buffer, int buffer_size )
{
	// msg comes from the server (ShowMenu): every write below is bounded by
	// dst_buffer, a message name longer than word_buf is not looked up
	if ( !dst_buffer || buffer_size <= 0 )
		return dst_buffer;

	dst_buffer[0] = 0;
	if ( !msg )
		return dst_buffer;

	char *dst = dst_buffer;
	char *dst_end = dst_buffer + buffer_size - 1;	// room for the terminator

	const char *src = msg;
	while ( *src != 0 && dst < dst_end )
	{
		if ( *src == '#' )
		{
			// cut msg name out of string
			char word_buf[255];
			int word_len = 0;
			const char *word_end = src + 1;
			while ( *word_end >= 'A' && *word_end <= 'z' )
			{
				if ( word_len < (int)sizeof( word_buf ) - 1 )
					word_buf[ word_len ] = *word_end;
				word_len++;
				word_end++;
			}

			// lookup msg name in titles.txt
			client_textmessage_t *clmsg = NULL;
			if ( word_len > 0 && word_len < (int)sizeof( word_buf ) )
			{
				word_buf[ word_len ] = 0;
				clmsg = TextMessageGet( word_buf );
			}

			if ( !clmsg || !clmsg->pMessage )
			{
				// not a message name: copy the '#' as is
				*dst++ = *src++;
				continue;
			}

			// Does titles.txt want to lookup into cstrike_<language>.txt?
			wchar_t *pLocalizedStr;
			if ( clmsg->pMessage[0] == '#' && ( ( pLocalizedStr = g_pVGuiLocalize->Find( clmsg->pMessage ) ) != NULL ) )
			{
				g_pVGuiLocalize->ConvertUnicodeToANSI( pLocalizedStr, dst, dst_end - dst + 1 );
				dst += V_strlen( dst );
			}
			else
			{
				// copy string into message over the msg name
				for ( const char *wsrc = clmsg->pMessage; *wsrc != 0 && dst < dst_end; wsrc++ )
				{
					*dst++ = *wsrc;
				}
			}

			src = word_end;
		}
		else
		{
			*dst++ = *src++;
		}
	}

	*dst = 0;
	return dst_buffer;
}

//-----------------------------------------------------------------------------
// Purpose: As above, but with a local static buffer
// Input  : *msg - 
// Output : char
//-----------------------------------------------------------------------------
char *CHudTextMessage::BufferedLocaliseTextString( const char *msg )
{
	static char dst_buffer[1024];
	return LocaliseTextString( msg, dst_buffer, 1024 );
}

//-----------------------------------------------------------------------------
// Purpose:  Simplified version of LocaliseTextString;  assumes string is only one word
// Input  : *msg - 
//			*msg_dest - 
// Output : char
//-----------------------------------------------------------------------------
char *CHudTextMessage::LookupString( const char *msg, int *msg_dest )
{
	if ( !msg )
		return "";

	// '#' character indicates this is a reference to a string in titles.txt, and not the string itself
	if ( msg[0] == '#' ) 
	{
		// this is a message name, so look up the real message
		client_textmessage_t *clmsg = TextMessageGet( msg+1 );

		if ( !clmsg || !(clmsg->pMessage) )
			return (char*)msg; // lookup failed, so return the original string
		
		if ( msg_dest )
		{
			// check to see if titles.txt info overrides msg destination
			// if clmsg->effect is less than 0, then clmsg->effect holds -1 * message_destination
			if ( clmsg->effect < 0 )  // 
				*msg_dest = -clmsg->effect;
		}

		return (char*)clmsg->pMessage;
	}
	else
	{  // nothing special about this message, so just return the same string
		return (char*)msg;
	}
}