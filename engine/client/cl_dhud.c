/*
cl_dhud.c - director HUD messages (SVC_DIRECTOR / DRC_CMD_MESSAGE)

AMXX's set_dhudmessage() sends these; Steam CS renders them bolder and
bigger than a plain HudText. The client DLL cannot tell them apart, so the
engine consumes the command itself: parses the parameters, runs the message
lifecycle (fade in/out, flicker, typewriter) and draws the text twice per
frame with a credits font scaled 1.2x.
*/

#include "common.h"
#include "client.h"
#include "ref_common.h"


#define DRC_CMD_MESSAGE	6
#define MAX_DHUD_MESSAGES	8


typedef struct
{
	qboolean active;
	byte effect;
	byte r, g, b;
	float x, y;
	float fadein, fadeout, holdtime, fxtime;
	double start;
	char text[256];
} dhud_message_t;

static dhud_message_t s_dhud[MAX_DHUD_MESSAGES];
static cl_font_t s_scaled_font;
static qboolean s_scaled_valid;

static void CL_DHUD_BuildscaledFont( void )
{
	static float builtScale = -1.0f;

	if( s_scaled_valid && s_scaled_font.hFontTexture == cls.creditsFont.hFontTexture
		&& builtScale == dhud_scale.value )
		return;

	s_scaled_font = cls.creditsFont;

	if( !s_scaled_font.valid )
	{
		memset( &s_scaled_font, 0, sizeof( s_scaled_font ));
		return;
	}

	s_scaled_font.scale *= dhud_scale.value;
	s_scaled_font.charHeight = (int)( s_scaled_font.charHeight * dhud_scale.value );

	for( int i = 0; i < ARRAYSIZE( s_scaled_font.charWidths ); i++ )
		s_scaled_font.charWidths[i] = (byte)bound( 0, (int)( s_scaled_font.charWidths[i] * dhud_scale.value ), 255 );

	s_scaled_valid = true;
	builtScale = dhud_scale.value;
}

void CL_DirectorMessage_Store( const byte *pbuf, int iSize )
{
	sizebuf_t tmp;
	dhud_message_t *msg;
	int slot = -1;
	int i;

	if( iSize <= 0 || pbuf[0] != DRC_CMD_MESSAGE )
		return;

	for( i = 0; i < MAX_DHUD_MESSAGES; i++ )
	{
		if( !s_dhud[i].active )
		{
			slot = i;
			break;
		}

		if( slot == -1 || s_dhud[i].start < s_dhud[slot].start )
			slot = i;
	}

	msg = &s_dhud[slot];
	memset( msg, 0, sizeof( *msg ));

	MSG_Init( &tmp, "dhud", (byte *)pbuf, iSize );

	MSG_ReadByte( &tmp ); // DRC_CMD_MESSAGE

	msg->effect = (byte)MSG_ReadByte( &tmp );

	int color = MSG_ReadLong( &tmp );

	msg->r = (byte)(( color >> 16 ) & 0xFF );
	msg->g = (byte)(( color >> 8 ) & 0xFF );
	msg->b = (byte)( color & 0xFF );

	msg->x = MSG_ReadFloat( &tmp );
	msg->y = MSG_ReadFloat( &tmp );
	msg->fadein = MSG_ReadFloat( &tmp );
	msg->fadeout = MSG_ReadFloat( &tmp );
	msg->holdtime = MSG_ReadFloat( &tmp );
	msg->fxtime = MSG_ReadFloat( &tmp );

	Q_strncpy( msg->text, MSG_ReadString( &tmp ), sizeof( msg->text ));

	msg->start = host.realtime;
	msg->active = true;
}

static int CL_DHUD_Utf8Len( const char *s )
{
	int count = 0;

	Con_UtfProcessChar( 0 );

	while( *s )
	{
		int uch = Con_UtfProcessChar( (byte)*s );

		if( uch ) count++;

		s++;
	}

	return count;
}

static int CL_DHUD_LineCodepoints( const char *s, int len )
{
	int count = 0;

	Con_UtfProcessChar( 0 );

	for( int i = 0; i < len; )
	{
		int uch = Con_UtfProcessChar( (byte)s[i] );
		byte c = (byte)s[i];

		if( uch ) count++;

		i++;
		if( ( c & 0xE0 ) == 0xC0 && i < len ) i += 1;
		else if( ( c & 0xF0 ) == 0xE0 && i + 1 < len ) i += 2;
		else if( ( c & 0xF8 ) == 0xF0 && i + 2 < len ) i += 3;
	}

	return count;
}

// copy up to codepointLimit codepoints of src (len bytes) into dst
static int CL_DHUD_Prefix( const char *src, int len, char *dst, int dstSize, int codepointLimit )
{
	int written = 0;

	while( len > 0 && written < dstSize - 1 )
	{
		byte c = *src;
		int seq = 1;

		if( ( c & 0xE0 ) == 0xC0 ) seq = 2;
		else if( ( c & 0xF0 ) == 0xE0 ) seq = 3;
		else if( ( c & 0xF8 ) == 0xF0 ) seq = 4;

		if( codepointLimit <= 0 ) break;
		codepointLimit--;

		for( int j = 0; j < seq && len > 0 && written < dstSize - 1; j++ )
		{
			dst[written++] = *src++;
			len--;
		}
	}

	dst[written] = '\0';
	return written;
}

static float CL_DHUD_MessageAlpha( const dhud_message_t *msg )
{
	double elapsed = host.realtime - msg->start;

	if( msg->effect == 2 )
	{
		int len = CL_DHUD_Utf8Len( msg->text );
		double after = (double)msg->fadein * len + msg->holdtime;

		if( elapsed > after && msg->fadeout > 0.0f )
			return 1.0f - (float)(( elapsed - after ) / msg->fadeout );

		return 1.0f;
	}

	if( msg->fadein > 0.0f && elapsed < msg->fadein )
		return (float)( elapsed / msg->fadein );

	{
		double holdEnd = msg->fadein + msg->holdtime;

		if( elapsed > holdEnd && msg->fadeout > 0.0f )
			return 1.0f - (float)(( elapsed - holdEnd ) / msg->fadeout );
	}

	return 1.0f;
}

static qboolean CL_DHUD_MessageExpired( const dhud_message_t *msg )
{
	double elapsed = host.realtime - msg->start;

	if( msg->effect == 2 )
	{
		int len = CL_DHUD_Utf8Len( msg->text );

		return elapsed >= (double)msg->fadein * len + msg->holdtime + msg->fadeout;
	}

	return elapsed >= msg->fadein + msg->holdtime + msg->fadeout;
}

static int CL_DHUD_XPos( float xfrac, int sw, int width )
{
	int xpos;

	if( xfrac < 0.0f )
		return ( sw - width ) / 2;

	xpos = (int)( xfrac * (float)sw );

	if( xpos + width > sw ) xpos = sw - width;
	if( xpos < 0 ) xpos = 0;

	return xpos;
}

static int CL_DHUD_LineWidth( cl_font_t *font, const char *text )
{
	int width = 0;
	const char *s = text;

	Con_UtfProcessChar( 0 );

	while( *s )
	{
		int uch = Con_UtfProcessChar( (byte)*s );

		s++;

		if( !uch ) continue;

		width += font->charWidths[uch];
		if( dhud_spacing.value != 0.0f )
			width += (int)dhud_spacing.value;
	}

	// drop the trailing spacer
	{
		float sp = dhud_spacing.value;

		if( sp > 0.0f && width >= (int)sp )
			width -= (int)sp;
	}

	return width;
}

static void CL_DHUD_DrawLine( const char *line, int lineLen, int codepointBudget,
	int sw, float xfrac, int y, const rgba_t color, cl_font_t *font )
{
	static char buf[256];
	const char *draw = line;
	int width = 0, xpos, stripAdvance;

	if( codepointBudget >= 0 )
	{
		CL_DHUD_Prefix( line, lineLen, buf, sizeof( buf ), codepointBudget );
		draw = buf;
	}
	else
	{
		memcpy( buf, line, lineLen );
		buf[lineLen] = '\0';
		draw = buf;
	}

	width = CL_DHUD_LineWidth( font, draw );
	xpos = CL_DHUD_XPos( xfrac, sw, width );

	Con_UtfProcessChar( 0 );

	for( const char *s = draw; *s; s++ )
	{
		int uch = Con_UtfProcessChar( (byte)*s );

		if( !uch ) continue;

		stripAdvance = font->charWidths[uch] + (int)dhud_spacing.value;

		// triple strike with 1px horizontal offsets for a bold look
		CL_DrawCharacter( xpos, y, uch, color, font, FONT_DRAW_HUD );
		CL_DrawCharacter( xpos + 1, y, uch, color, font, FONT_DRAW_HUD );
		CL_DrawCharacter( xpos + 2, y, uch, color, font, FONT_DRAW_HUD );

		xpos += stripAdvance;
	}
}

void CL_DrawDirectorMessages( void )
{
	for( int i = 0; i < MAX_DHUD_MESSAGES; i++ )
	{
		dhud_message_t *msg = &s_dhud[i];
		cl_font_t *font;
		const char *p;
		int budget = -1;

		if( !msg->active ) continue;

		if( CL_DHUD_MessageExpired( msg ))
		{
			msg->active = false;
			continue;
		}

		CL_DHUD_BuildscaledFont();

		font = s_scaled_font.valid ? &s_scaled_font : Con_GetCurFont();

		{
			float a = CL_DHUD_MessageAlpha( msg );

			if( a <= 0.0f ) continue;

			rgba_t color;
			int alpha = bound( 0, (int)( a * 255.0f ), 255 );

			Vector4Set( color, msg->r, msg->g, msg->b, alpha );

			if( msg->effect == 1 && ( rand() % 100 ) < 10 )
				Vector4Set( color, msg->r, msg->g, msg->b, bound( 0, (int)( a * 60.0f ), 255 ));

			int sw = refState.width, sh = refState.height;
			int lineH = font ? font->charHeight : 16;

			int lines = 1;

			for( const char *l = msg->text; *l; l++ ) if( *l == '\n' ) lines++;

			int height = lines * lineH;
			int y;

			if( msg->y < 0.0f ) y = ( sh - height ) / 2;
			else y = (int)( msg->y * (float)sh );

			if( y + height > sh ) y = sh - height;
			if( y < 0 ) y = 0;

			if( msg->effect == 2 )
			{
				double elapsed = host.realtime - msg->start;

				if( msg->fadein > 0.0f ) budget = (int)( elapsed / msg->fadein );

				if( budget < 0 ) budget = 0;
			}

			p = msg->text;

			while( 1 )
			{
				const char *eol = strchr( p, '\n' );
				int lineLen = eol ? (int)( eol - p ) : (int)strlen( p );
				int lineCps = CL_DHUD_LineCodepoints( p, lineLen );

				CL_DHUD_DrawLine( p, lineLen,
					budget >= 0 ? ( budget < lineCps ? budget : -1 ) : -1,
					sw, msg->x, y, color, font );

				if( budget >= 0 )
				{
					budget -= lineCps;
					if( budget < 0 ) budget = 0;
				}

				if( !eol ) break;

				y += lineH;
				p = eol + 1;
			}
		}
	}
}
