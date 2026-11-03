/*
cl_font.c - bare bones engine font manager
Copyright (C) 2023 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "filesystem.h"
#include "client.h"
#include "qfont.h"
#include "swaplib.h"
#include "utflib.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../3rdparty/mainui/font/stb_truetype.h"

le_struct_begin( charinfo_swap )
	le_struct_field( charinfo, startoffset )
	le_struct_field( charinfo, charwidth )
le_struct_end();

le_struct_begin( qfont_swap )
	le_struct_field( qfont_t, width )
	le_struct_field( qfont_t, height )
	le_struct_field( qfont_t, rowcount )
	le_struct_field( qfont_t, rowheight )
	le_struct_array_child( qfont_t, fontinfo, charinfo_swap, NUM_GLYPHS )
le_struct_end();

qboolean CL_FixedFont( cl_font_t *font )
{
	return font && font->valid && font->type == FONT_FIXED;
}

static int CL_LoadFontTexture( const char *fontname, uint texFlags, int *width )
{
	if( !g_fsapi.FileExists( fontname, false ))
		return 0;

	int tex = ref.dllFuncs.GL_LoadTexture( fontname, NULL, 0, texFlags );
	if( !tex )
		return 0;

	int font_width = REF_GET_PARM( PARM_TEX_WIDTH, tex );
	if( !font_width )
	{
		ref.dllFuncs.GL_FreeTexture( tex );
		return 0;
	}

	*width = font_width;
	return tex;
}

static int CL_FontRenderMode( convar_t *fontrender )
{
	switch((int)fontrender->value )
	{
	case 0:
		return kRenderTransAdd;
	case 1:
		return kRenderTransAlpha;
	case 2:
		return kRenderTransTexture;
	default:
		Cvar_DirectSet( fontrender, fontrender->def_string );
	}

	return kRenderTransTexture;
}

void CL_SetFontRendermode( cl_font_t *font )
{
	ref.dllFuncs.GL_SetRenderMode( CL_FontRenderMode( font->rendermode ));
}

void CL_SetFontColor( cl_font_t *font, const rgba_t color )
{
	ref.dllFuncs.Color4ub( color[0], color[1], color[2], color[3] );
}

qboolean Con_LoadFixedWidthFont( const char *fontname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags )
{
	int font_width;

	if( !rendermode )
		return false;

	if( font->valid )
		return true; // already loaded

	font->hFontTexture = CL_LoadFontTexture( fontname, texFlags, &font_width );
	if( !font->hFontTexture )
		return false;

	font->type = FONT_FIXED;
	font->valid = true;
	font->scale = scale;
	font->rendermode = rendermode;
	font->charHeight = Q_rint( font_width / 16 * scale );

	for( int i = 0; i < ARRAYSIZE( font->fontRc ); i++ )
	{
		font->fontRc[i].left   = ( i * font_width / 16 ) % font_width;
		font->fontRc[i].right  = font->fontRc[i].left + font_width / 16;
		font->fontRc[i].top    = ( i / 16 ) * ( font_width / 16 );
		font->fontRc[i].bottom = font->fontRc[i].top + font_width / 16;

		font->charWidths[i] = Q_rint( font_width / 16 * scale );
	}

	return true;
}

qboolean Con_LoadVariableWidthFont( const char *fontname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags )
{
	fs_offset_t length;
	qfont_t src;
	int font_width;

	if( !rendermode )
		return false;

	if( font->valid )
		return true;

	byte *pfile = g_fsapi.LoadFile( fontname, &length, false );
	if( !pfile )
		return false;

	if( length < sizeof( src ))
	{
		Mem_Free( pfile );
		return false;
	}

	memcpy( &src, pfile, sizeof( src ));
	le_struct_swap( qfont_swap, &src );
	Mem_Free( pfile );

	font->hFontTexture = CL_LoadFontTexture( fontname, texFlags, &font_width );
	if( !font->hFontTexture )
		return false;

	font->type = FONT_VARIABLE;
	font->valid = true;
	font->scale = scale ? scale : 1.0f;
	font->rendermode = rendermode;
	font->charHeight = Q_rint( src.rowheight * scale );

	for( int i = 0; i < ARRAYSIZE( font->fontRc ); i++ )
	{
		const charinfo *ci = &src.fontinfo[i];

		font->fontRc[i].left   = (word)ci->startoffset % font_width;
		font->fontRc[i].right  = font->fontRc[i].left + ci->charwidth;
		font->fontRc[i].top    = (word)ci->startoffset / font_width;
		font->fontRc[i].bottom = font->fontRc[i].top + src.rowheight;

		font->charWidths[i] = Q_rint( src.fontinfo[i].charwidth * scale );
	}

	return true;
}

static uint32_t CL_TTFCodepointForByte( int ch, const char *charset )
{
	if( ch < 0x80 )
		return ch;

	qboolean cp1251 = !Q_stricmp( charset, "cp1251" );

	for( uint32_t u = 0x80; u < 0x2200; u++ )
	{
		uint32_t mapped = cp1251 ? Q_UnicodeToCP1251( u ) : Q_UnicodeToCP1252( u );
		if( mapped == (uint32_t)ch )
			return u;
	}

	return (uint32_t)ch;
}

qboolean Con_LoadTTFFont( const char *fontname, const char *texname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags )
{
	fs_offset_t length;
	byte *data;
	stbtt_fontinfo info;
	int ascent, descent, lineGap;
	float pixelHeight;
	float sf;
	int ascentPx, descentPx, cellW, cellH;
	int atlasW, atlasH;
	byte *atlas;
	rgbdata_t pic;
	const char *charset;

	if( !rendermode )
		return false;

	if( font->valid )
		return true;

	data = g_fsapi.LoadFile( fontname, &length, false );
	if( !data )
		return false;

	if( !stbtt_InitFont( &info, data, stbtt_GetFontOffsetForIndex( data, 0 )))
	{
		Mem_Free( data );
		return false;
	}

	stbtt_GetFontVMetrics( &info, &ascent, &descent, &lineGap );

	pixelHeight = 18.0f * ( scale > 0.0f ? scale : 1.0f );
	if( pixelHeight < 8.0f ) pixelHeight = 8.0f;
	if( pixelHeight > 64.0f ) pixelHeight = 64.0f;

	sf = stbtt_ScaleForPixelHeight( &info, pixelHeight );
	ascentPx = Q_rint( ascent * sf );
	descentPx = Q_rint( -descent * sf );
	cellW = Q_rint( pixelHeight * 2.0f ) + 4;
	cellH = ascentPx + descentPx + 4;
	atlasW = cellW * 16;
	atlasH = cellH * 16;

	atlas = Mem_Calloc( atlasW * atlasH * 4 );
	charset = Cvar_VariableString( "con_charset" );

	for( int i = 0; i < 256; i++ )
	{
		int cellX = ( i % 16 ) * cellW;
		int cellY = ( i / 16 ) * cellH;
		int advance = 0, lsb = 0;
		int gw = 0, gh = 0, xoff = 0, yoff = 0;
		int right;

		font->fontRc[i].left   = cellX;
		font->fontRc[i].right  = cellX + 1;
		font->fontRc[i].top    = cellY;
		font->fontRc[i].bottom = cellY + cellH;
		font->charWidths[i] = 0;

		if( i < 32 )
			continue;

		uint32_t cp = CL_TTFCodepointForByte( i, charset );

		stbtt_GetCodepointHMetrics( &info, cp, &advance, &lsb );

		int advancePx = Q_rint( advance * sf );
		if( advancePx < 0 ) advancePx = 0;
		if( advancePx > 255 ) advancePx = 255;
		font->charWidths[i] = (byte)advancePx;

		if( i == ' ' )
			continue;

		unsigned char *bitmap = stbtt_GetCodepointBitmap( &info, 0, sf, cp, &gw, &gh, &xoff, &yoff );
		if( !bitmap )
			continue;

		int gx = cellX + 2 + xoff;
		int gy = cellY + 2 + ascentPx + yoff;

		for( int y = 0; y < gh; y++ )
		{
			int dy = gy + y;
			if( dy < cellY || dy >= cellY + cellH )
				continue;

			for( int x = 0; x < gw; x++ )
			{
				int dx = gx + x;
				if( dx < cellX || dx >= cellX + cellW )
					continue;

				byte a = bitmap[y * gw + x];
				if( !a )
					continue;

				byte *dst = atlas + ( dy * atlasW + dx ) * 4;
				dst[0] = dst[1] = dst[2] = 255;
				if( a > dst[3] ) dst[3] = a;
			}
		}

		right = gx - cellX + gw;
		if( right < 1 ) right = 1;
		if( right > cellW ) right = cellW;
		font->fontRc[i].right = cellX + right;

		stbtt_FreeBitmap( bitmap, NULL );
	}

	memset( &pic, 0, sizeof( pic ));
	pic.width = atlasW;
	pic.height = atlasH;
	pic.depth = 4;
	pic.type = PF_RGBA_32;
	pic.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA;
	pic.buffer = atlas;
	pic.size = atlasW * atlasH * 4;

	font->hFontTexture = ref.dllFuncs.GL_LoadTextureFromBuffer( texname, &pic, texFlags|TF_FONT, false );
	Mem_Free( atlas );
	Mem_Free( data );

	if( !font->hFontTexture )
		return false;

	font->type = FONT_VARIABLE;
	font->valid = true;
	font->scale = 1.0f;
	font->rendermode = rendermode;
	font->charHeight = cellH;

	return true;
}

void CL_FreeFont( cl_font_t *font )
{
	if( !font || !font->valid )
		return;

	ref.dllFuncs.GL_FreeTexture( font->hFontTexture );
	memset( font, 0, sizeof( *font ));
}

static int CL_CalcTabStop( const cl_font_t *font, int x )
{
	int space = font->charWidths['0'];
	int tab   = space * 6; // 6 spaces
	int stop  = tab - x % tab;

	if( stop < space )
		return tab * 2 - x % tab; // select next

	return stop;
}

int CL_DrawCharacter( float x, float y, int number, const rgba_t color, cl_font_t *font, int flags )
{
	if( !font || !font->valid || y < -font->charHeight )
		return 0;

	// check if printable
	if( number <= 32 )
	{
		if( number == ' ' )
			return font->charWidths[' '];
		else if( number == '\t' )
			return CL_CalcTabStop( font, x );
		return 0;
	}

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		number = Con_UtfProcessChar( number & 255 );
	else number &= 255;

	if( !number || !font->charWidths[number])
		return 0;

	int texw, texh;
	R_GetTextureParms( &texw, &texh, font->hFontTexture );
	if( !texw || !texh )
		return font->charWidths[number];

	wrect_t *rc = &font->fontRc[number];
	float half = 0.5f;

	if( font->scale <= 1.f || !REF_GET_PARM( PARM_TEX_FILTERING, font->hFontTexture ))
		half = 0;

	float s1 = ((float)rc->left + half ) / texw;
	float t1 = ((float)rc->top + half ) / texh;
	float s2 = ((float)rc->right - half ) / texw;
	float t2 = ((float)rc->bottom - half ) / texh;
	float w = ( rc->right - rc->left ) * font->scale;
	float h = ( rc->bottom - rc->top ) * font->scale;

	if( FBitSet( flags, FONT_DRAW_HUD ))
		SPR_AdjustSize( &x, &y, &w, &h );

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	if( !FBitSet( flags, FONT_DRAW_NOCOLOR ))
		CL_SetFontColor( font, color );

	ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, font->hFontTexture );

	return font->charWidths[number];
}

int CL_DrawString( float x, float y, const char *s, const rgba_t color, cl_font_t *font, int flags )
{
	int draw_len = 0;

	if( !font || !font->valid )
		return 0;

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		Con_UtfProcessChar( 0 ); // clear utf state

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	CL_SetFontColor( font, color );

	SetBits( flags, FONT_DRAW_NOCOLOR | FONT_DRAW_NORENDERMODE );

	while( *s )
	{
		if( *s == '\n' )
		{
			s++;

			if( !*s )
				break;

			// some client functions ignore newlines
			if( !FBitSet( flags, FONT_DRAW_NOLF ))
			{
				draw_len = 0;
				y += font->charHeight;
			}

			if( FBitSet( flags, FONT_DRAW_RESETCOLORONLF ))
				 CL_SetFontColor( font, color );
			continue;
		}

		if( IsColorString( s ))
		{
			// don't copy alpha
			if( !FBitSet( flags, FONT_DRAW_FORCECOL ))
				CL_SetFontColor( font, g_color_table[ColorIndex(*( s + 1 ))] );

			s += 2;
			continue;
		}

		// skip setting rendermode, it was changed for this string already
		draw_len += CL_DrawCharacter( x + draw_len, y, (byte)*s, NULL, font, flags );

		s++;
	}

	return draw_len;
}

int CL_DrawStringf( cl_font_t *font, float x, float y, const rgba_t color, int flags, const char *fmt, ... )
{
	va_list va;
	char buf[MAX_VA_STRING];

	va_start( va, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	return CL_DrawString( x, y, buf, color, font, flags );
}

void CL_DrawCharacterLen( cl_font_t *font, int number, int *width, int *height )
{
	if( !font || !font->valid ) return;
	if( width )
	{
		if( number == '\t' )
			*width = CL_CalcTabStop( font, 0 ); // at least return max tabstop
		else *width = font->charWidths[number & 255];
	}
	if( height ) *height = font->charHeight;
}

void CL_DrawStringLen( cl_font_t *font, const char *s, int *width, int *height, int flags )
{
	int draw_len = 0;

	if( !font || !font->valid )
		return;

	if( height )
		*height = font->charHeight;

	if( width )
		*width = 0;

	if( COM_StringEmptyOrNULL( s ))
		return;

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		Con_UtfProcessChar( 0 ); // reset utf state

	while( *s )
	{
		int number;

		if( *s == '\n' )
		{
			// BUG: no check for end string here
			// but high chances somebody's relying on this
			s++;
			draw_len = 0;
			if( !FBitSet( flags, FONT_DRAW_NOLF ))
			{
				if( height )
					*height += font->charHeight;
			}
			continue;
		}
		else if( *s == '\t' )
		{
			draw_len += CL_CalcTabStop( font, 0 ); // at least return max tabstop
			s++;
			continue;
		}

		if( IsColorString( s ))
		{
			s += 2;
			continue;
		}

		if( FBitSet( flags, FONT_DRAW_UTF8 ))
			number = Con_UtfProcessChar( (byte)*s );
		else number = (byte)*s;

		if( number )
		{
			draw_len += font->charWidths[number];

			if( width )
			{
				if( draw_len > *width )
					*width = draw_len;
			}
		}

		s++;
	}
}
