// vgui2_surface.cpp - ISurface, the drawing half of engine-side VGUI2.
//
// Everything here ends up in one of four engine calls (vgui2_draw.h): a
// translucent rectangle, a textured quad, a texture lookup and a run of
// text. The texture ids VGUI2 hands out are indices into m_textures rather
// than renderer handles, because the renderer numbers its own and the two
// would drift the first time something is released.
//
// No engine headers are included: the C++ stays checkable without a
// configured build tree, and ref.dllFuncs is touched in one place only.
#include "vgui2_surface.h"

#include "vgui2_draw.h"
#include "vgui2_interfaces.decl.inc"

#include <string.h>

namespace vgui2 {

CISurface::CISurface()
{
	memset( m_textures, 0, sizeof( m_textures ) );
	memset( m_freeTextures, 0, sizeof( m_freeTextures ) );
	memset( m_popups, 0, sizeof( m_popups ) );

	m_textureCount = 0;
	m_freeTextureCount = 0;
	m_popupCount = 0;

	m_currentTexture = 0;
	m_font = 0;
	m_cursorVisible = true;
	m_hasFocus = true;
	m_proportionalBase[0] = 640;
	m_proportionalBase[1] = 480;
	m_screenSize[0] = 0;
	m_screenSize[1] = 0;

	DrawSetColor( 255, 255, 255, 255 );
	DrawSetTextColor( 255, 255, 255, 255 );
	DrawSetTextPos( 0, 0 );
}

SurfaceTexture *CISurface::Find( int id )
{
	if( id <= 0 || id > m_textureCount )
		return NULL;

	return &m_textures[id - 1];
}

// --- state -----------------------------------------------------------------

void CISurface::DrawSetColor( int r, int g, int b, int a )
{
	m_drawColor[0] = r;
	m_drawColor[1] = g;
	m_drawColor[2] = b;
	m_drawColor[3] = a;
}

void CISurface::DrawSetColor( Color col )
{
	int r, g, b, a;
	col.GetColor( r, g, b, a );
	DrawSetColor( r, g, b, a );
}

// --- rectangles ------------------------------------------------------------

void CISurface::DrawFilledRect( int x0, int y0, int x1, int y1 )
{
	VGUI2_FillRect( x0, y0, x1, y1, m_drawColor[0], m_drawColor[1], m_drawColor[2], m_drawColor[3] );
}

void CISurface::DrawOutlinedRect( int x0, int y0, int x1, int y1 )
{
	// Four one-pixel bars rather than an outline call: the renderer has no
	// such call, and VGUI2 asks for outlines in colours that vary per side.
	VGUI2_FillRect( x0, y0, x1 - 1, y0 + 1, m_drawColor[0], m_drawColor[1], m_drawColor[2], m_drawColor[3] );
	VGUI2_FillRect( x0, y1 - 1, x1, y1, m_drawColor[0], m_drawColor[1], m_drawColor[2], m_drawColor[3] );
	VGUI2_FillRect( x0, y0 + 1, x0 + 1, y1 - 1, m_drawColor[0], m_drawColor[1], m_drawColor[2], m_drawColor[3] );
	VGUI2_FillRect( x1 - 1, y0 + 1, x1, y1 - 1, m_drawColor[0], m_drawColor[1], m_drawColor[2], m_drawColor[3] );
}

// --- textures --------------------------------------------------------------

int CISurface::CreateNewTextureID( bool procedural )
{
	int id;

	if( m_freeTextureCount > 0 )
		id = m_freeTextures[--m_freeTextureCount];
	else if( m_textureCount < kMaxTextures )
		id = ++m_textureCount;
	else
		return 0;

	SurfaceTexture &entry = m_textures[id - 1];
	entry.texture = 0;
	entry.width = 0;
	entry.height = 0;
	entry.subX = 0;
	entry.subY = 0;
	entry.subWidth = 0;
	entry.subHeight = 0;
	entry.name[0] = '\0';

	return id;
}

bool CISurface::IsTextureIDValid( int id )
{
	return Find( id ) != NULL;
}

void CISurface::DrawSetTexture( int id )
{
	if( !IsTextureIDValid( id ) )
	{
		m_currentTexture = 0;
		return;
	}

	SurfaceTexture &entry = m_textures[id - 1];

	// The name is the cache key. VGUI2 creates the id once and names the
	// file afterwards, so a lookup that misses loads and re-looks-up.
	if( !entry.texture && entry.name[0] )
		entry.texture = VGUI2_FindTexture( entry.name );

	m_currentTexture = entry.texture;
}

void CISurface::DrawGetTextureSize( int id, int &wide, int &tall )
{
	if( !IsTextureIDValid( id ) )
	{
		wide = tall = 0;
		return;
	}

	SurfaceTexture &entry = m_textures[id - 1];
	if( !entry.texture && entry.name[0] )
	{
		entry.texture = VGUI2_FindTexture( entry.name );
		if( entry.texture )
			VGUI2_TextureSize( entry.texture, &entry.width, &entry.height );
	}

	wide = entry.width;
	tall = entry.height;
}

void CISurface::DrawSetTextureFile( int id, const char *filename, int hardwareFilter, bool forceReload )
{
	if( !IsTextureIDValid( id ) || !filename )
		return;

	SurfaceTexture &entry = m_textures[id - 1];

	int i = 0;
	for( ; filename[i] && i < (int)sizeof( entry.name ) - 1; i++ )
		entry.name[i] = filename[i];
	entry.name[i] = '\0';

	entry.texture = VGUI2_FindTexture( entry.name );
	if( entry.texture )
		VGUI2_TextureSize( entry.texture, &entry.width, &entry.height );
}

void CISurface::DrawSetTextureRGBA( int id, const unsigned char *rgba, int wide, int tall, int hardwareFilter, bool forceReload )
{
	// A raw RGBA buffer would need a texture create through the renderer,
	// which this engine's 2D path does not expose. Recorded so
	// DrawGetTextureSize can still answer with the right numbers.
	if( !IsTextureIDValid( id ) )
		return;

	SurfaceTexture &entry = m_textures[id - 1];
	entry.width = wide;
	entry.height = tall;
	entry.subWidth = (unsigned int)wide;
	entry.subHeight = (unsigned int)tall;
}

void CISurface::DrawTexturedRect( int x0, int y0, int x1, int y1 )
{
	if( !m_currentTexture )
		return;

	VGUI2_DrawTexture( m_currentTexture, (float)x0, (float)y0,
		(float)(x1 - x0), (float)(y1 - y0), 0.0f, 0.0f, 1.0f, 1.0f );
}

// --- fonts -----------------------------------------------------------------

HFont CISurface::CreateFont()
{
	// One font, sized as the scheme asked for. The client library creates
	// a font per control and then adds glyph sets to it, so the id is
	// cheap: what matters is that AddGlyphSetToFont and GetFontTall agree
	// with what CreateFont handed out.
	return m_fonts.Create( "", 0 );
}

void CISurface::DrawSetTextFont( HFont font )
{
	m_font = font;
}


int CISurface::GetFontTall( HFont font )
{
	return m_fonts.Tall( font );
}

// --- text ------------------------------------------------------------------

void CISurface::DrawSetTextColor( int r, int g, int b, int a )
{
	m_textColor[0] = r;
	m_textColor[1] = g;
	m_textColor[2] = b;
	m_textColor[3] = a;
}

void CISurface::DrawSetTextColor( Color col )
{
	int r, g, b, a;
	col.GetColor( r, g, b, a );
	DrawSetTextColor( r, g, b, a );
}

void CISurface::DrawSetTextPos( int x, int y )
{
	m_textPos[0] = x;
	m_textPos[1] = y;
}

void CISurface::DrawGetTextPos( int &x, int &y )
{
	x = m_textPos[0];
	y = m_textPos[1];
}

void CISurface::DrawPrintText( const wchar_t *text, int textLen )
{
	if( !text || textLen <= 0 )
		return;

	// The engine's console font is 8-bit, so anything outside Latin-1 has
	// no glyph to draw. Replacing those with '?' keeps the advance correct
	// rather than letting the text end early and the rest of the line
	// land on top of it.
	char buffer[256];
	int count = textLen < 255 ? textLen : 255;

	for( int i = 0; i < count; i++ )
	{
		unsigned int ch = (unsigned int)text[i];
		buffer[i] = ( ch > 0 && ch < 256 ) ? (char)ch : '?';
	}
	buffer[count] = '\0';

	VGUI2_DrawText( m_fonts.EngineFont( m_font ), (float)m_textPos[0], (float)m_textPos[1],
		buffer, m_textColor[0], m_textColor[1], m_textColor[2], m_textColor[3] );
}

void CISurface::GetTextSize( HFont font, const wchar_t *text, int &wide, int &tall )
{
	wide = 0;
	tall = m_fonts.Tall( font );

	if( !text )
		return;

	char buffer[256];
	int count = 0;

	while( text[count] && count < 255 )
	{
		unsigned int ch = (unsigned int)text[count];
		buffer[count] = ( ch > 0 && ch < 256 ) ? (char)ch : '?';
		count++;
	}
	buffer[count] = '\0';

	int width = 0, height = 0;
	VGUI2_TextSize( m_fonts.EngineFont( font ), buffer, &width, &height );

	wide = width;
	if( !tall )
		tall = height;
}

// --- screen ----------------------------------------------------------------

void CISurface::GetScreenSize( int &wide, int &tall )
{
	wide = m_screenSize[0];
	tall = m_screenSize[1];
}

} // namespace vgui2
