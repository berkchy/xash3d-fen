// vgui2_font.cpp - VGUI2 font names resolved against the engine's fonts.
#include "vgui2_font.h"

// No engine headers here. This file turns a scheme's font name into one of
// the engine's three console font slots and nothing more; the drawing that
// actually uses the slot is in vgui2_draw.c, which does include client.h.
#include <string.h>

namespace vgui2 {

namespace {

bool EqualNoCase( const char *a, const char *b )
{
	if ( !a || !b )
		return false;

	while ( *a && *b )
	{
		char ca = *a, cb = *b;
		if ( ca >= 'A' && ca <= 'Z' ) ca = (char)( ca - 'A' + 'a' );
		if ( cb >= 'A' && cb <= 'Z' ) cb = (char)( cb - 'A' + 'a' );
		if ( ca != cb )
			return false;
		a++;
		b++;
	}
	return *a == *b;
}

// Which engine font a scheme name maps to.
//
// The engine has three console slots and they are the only fonts it keeps
// rasterised, so the mapping is a choice between them rather than a load.
// Consolas and Courier are fixed width, Arial and Tahoma are not: the score
// board and the loading dialog both ask for proportional text, so the
// variable-width slot is the default and the fixed one is used only for a
// name that explicitly asks for it.
int EngineFontForName( const char *name )
{
	if ( EqualNoCase( name, "Consolas" ) || EqualNoCase( name, "Courier New" )
	  || EqualNoCase( name, "Courier" ) || EqualNoCase( name, "Lucida Console" ) )
		return 1;

	// Anything else - Arial, Tahoma, "Roboto Condensed", whatever a scheme
	// in the wild names - is proportional.
	return 0;
}

void CopyName( char *dst, int size, const char *src )
{
	dst[0] = '\0';

	if ( !src )
		return;

	int i = 0;
	for ( ; src[i] && i < size - 1; i++ )
		dst[i] = src[i];
	dst[i] = '\0';
}

} // namespace

int CVGui2FontSet::Create( const char *name, int tall )
{
	// Asked twice for the same name and height: hand back the same id rather
	// than a second entry. Schemes create fonts per control and the same
	// scheme gets applied more than once.
	for ( int i = 0; i < m_count; i++ )
	{
		if ( m_fonts[i].tall == tall && EqualNoCase( m_fonts[i].name, name ) )
			return i + 1;
	}

	if ( m_count >= kMaxFonts )
		return kInvalidFont;

	FontEntry &entry = m_fonts[m_count++];
	CopyName( entry.name, sizeof( entry.name ), name );
	entry.tall = tall;
	entry.engineFont = EngineFontForName( name );
	entry.valid = true;

	return m_count;
}

bool CVGui2FontSet::Add( int fontId, const char *name, int tall )
{
	if ( fontId <= 0 || fontId > kMaxFonts )
		return false;

	FontEntry &entry = m_fonts[fontId - 1];
	CopyName( entry.name, sizeof( entry.name ), name );
	entry.tall = tall;
	entry.engineFont = EngineFontForName( name );
	entry.valid = true;

	if ( fontId > m_count )
		m_count = fontId;

	return true;
}

int CVGui2FontSet::EngineFont( int fontId ) const
{
	if ( fontId <= 0 || fontId > m_count )
		return 0;

	return m_fonts[fontId - 1].engineFont;
}

int CVGui2FontSet::Tall( int fontId ) const
{
	if ( fontId <= 0 || fontId > m_count )
		return 0;

	return m_fonts[fontId - 1].tall;
}

} // namespace vgui2
