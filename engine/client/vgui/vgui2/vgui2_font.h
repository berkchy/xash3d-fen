// vgui2_font.h - VGUI2 HFont, mapped onto the engine's own fonts.
//
// VGUI2 asks for a font by name and draws text through ISurface. It has no
// idea how the engine rasterises glyphs, so the mapping is the whole of it:
// one VGUI2 font name becomes one cl_font_t, and every text call turns into
// CL_DrawString / CL_DrawStringLen.
//
// The fonts are the engine's, not Valve's BMF files. The console font is
// already loaded, already has an atlas and already draws at every resolution
// the phone uses; loading a separate rasteriser for VGUI2 would mean a second
// glyph cache for the same characters.
#pragma once

#include <vgui/ISurface.h>

namespace vgui2 {

// VGUI2 font ids are small integers handed out to the client library. 0 is
// what an unset HFont is, so ids start at 1.
const int kMaxFonts = 16;
const int kInvalidFont = 0;

// Schemes name fonts; CS asks for "Arial", "Tahoma" and the Consolas-family
// names. Map what the panel library actually asks for onto what the engine
// has, and let everything else fall back to the default rather than failing:
// a missing font should cost a typeface, not a screen.
struct FontEntry
{
	char name[64];
	int engineFont;
	int tall;
	bool valid;
};

class CVGui2FontSet
{
public:
	// Returns the VGUI2 font id, or kInvalidFont when the table is full.
	int Create( const char *name, int tall );
	bool Add( int fontId, const char *name, int tall );

	// The engine font behind a VGUI2 id, or the default when the id is one
	// the client library made up.
	int EngineFont( int fontId ) const;
	int Tall( int fontId ) const;

private:
	FontEntry m_fonts[kMaxFonts];
	int m_count = 0;
};

} // namespace vgui2
