// vgui2_draw.c - see vgui2_draw.h.
#include "vgui2_draw.h"

#include "client.h"
#include "ref_common.h"

#include <string.h>

// The console font slots, as the engine counts them: 0 and 2 are variable
// width, 1 is fixed. Kept here so vgui2_font.cpp does not have to know.
cl_font_t *VGUI2_GetEngineFont( int slot )
{
	// Con_GetFont bounds the slot itself; anything out of range lands on
	// font 0 rather than reading past the array.
	if( slot < 0 )
		slot = 0;
	return Con_GetFont( slot );
}

int VGUI2_GetTextFlags( void )
{
	return FONT_DRAW_FORCECOL;
}

void VGUI2_Enter2D( void )
{
	ref.dllFuncs.R_Set2DMode( true );
}

void VGUI2_FillRect( int x0, int y0, int x1, int y1, int r, int g, int b, int a )
{
	if( x1 <= x0 || y1 <= y0 )
		return;

	ref.dllFuncs.FillRGBA( kRenderTransTexture, (float)x0, (float)y0,
		(float)(x1 - x0), (float)(y1 - y0), (byte)r, (byte)g, (byte)b, (byte)a );
}

void VGUI2_DrawTexture( int texture, float x, float y, float w, float h,
	float s1, float t1, float s2, float t2 )
{
	if( !texture || w <= 0.0f || h <= 0.0f )
		return;

	ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, texture );
}

int VGUI2_FindTexture( const char *name )
{
	if( !name || !*name )
		return 0;

	// TF_CLAMP is not asked for: VGUI2 sub-textures index into a larger
	// texture with explicit uv ranges, and a clamped border would smear
	// the edge texel across them.
	return ref.dllFuncs.GL_FindTexture( name );
}

void VGUI2_TextureSize( int texture, int *width, int *height )
{
	int w = 0, h = 0;

	if( texture > 0 )
		R_GetTextureParms( &w, &h, texture );

	if( width ) *width = w;
	if( height ) *height = h;
}

int VGUI2_DrawText( int slot, float x, float y, const char *text, int r, int g, int b, int a )
{
	cl_font_t *font = VGUI2_GetEngineFont( slot );

	if( !font || !text )
		return 0;

	rgba_t color;
	color[0] = (byte)r;
	color[1] = (byte)g;
	color[2] = (byte)b;
	color[3] = (byte)a;

	return CL_DrawString( x, y, text, color, font, VGUI2_GetTextFlags() );
}

void VGUI2_TextSize( int slot, const char *text, int *width, int *height )
{
	cl_font_t *font = VGUI2_GetEngineFont( slot );

	if( width ) *width = 0;
	if( height ) *height = 0;

	if( !font || !text )
		return;

	CL_DrawStringLen( font, text, width, height, VGUI2_GetTextFlags() );
}
