// vgui2_draw.h - the engine drawing calls VGUI2 needs, as plain C.
//
// ISurface is C++ but everything it actually draws goes through the
// renderer: textures by file name, one textured quad, one translucent
// rectangle, text from the engine's own font. Putting those four things
// behind plain C functions keeps the C++ side free of engine headers, which
// is what lets it be syntax-checked without a configured build tree, and
// keeps ref.dllFuncs from being reached into from two different places.
//
// Implemented in vgui2_draw.c, which is a C file and is picked up by the
// engine's own client glob.
#pragma once

#include <stddef.h>

// The implementations are C (dll_int/cl_game.c), so the declarations here
// need C linkage or the C++ side mangles them and the link fails on seven
// unresolved externals that all look like a missing file.
#ifdef __cplusplus
extern "C" {
#endif

// Valve's GoldSource ISurface has no scissor rectangle at all - this build
// of it has no EnableScissor, no SetScissorRect - so there is nothing to
// push down here. Clipping is done by not drawing what falls outside.
//
// Opens the 2D drawing path. The renderer needs it before any of the draw
// calls below do anything; V_PostRender already does it once per frame, so
// this is only for callers that draw outside that.
void VGUI2_Enter2D( void );

// Translucent filled rectangle in screen space.
void VGUI2_FillRect( int x0, int y0, int x1, int y1, int r, int g, int b, int a );

// One textured quad. uv are texture coordinates in 0..1.
void VGUI2_DrawTexture( int texture, float x, float y, float w, float h,
	float s1, float t1, float s2, float t2 );

// Does the game have this file? The scheme manager asks before it records a
// scheme, so a missing .res does not become an empty scheme.
int VGUI2_FileExists( const char *path );

// Texture by name, loaded on first use. Returns 0 when the file is missing,
// which VGUI2 treats as "nothing to draw" rather than an error.
int VGUI2_FindTexture( const char *name );

// Width and height of a texture; 0 by 0 for an unknown one.
void VGUI2_TextureSize( int texture, int *width, int *height );

// Text through the engine's console fonts. slot is 0..2 as the engine counts
// them; size is the requested pixel height, used only to scale the console
// font. Returns the advance of the last character drawn.
int VGUI2_DrawText( int slot, float x, float y, const char *text, int r, int g, int b, int a );
void VGUI2_TextSize( int slot, const char *text, int *width, int *height );

#ifdef __cplusplus
}
#endif
