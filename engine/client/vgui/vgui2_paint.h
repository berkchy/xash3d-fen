// vgui2_paint.h - engine-side hooks for GoldSource VGUI2, as plain C.
//
// The C++ in this directory cannot include client.h, so the two things that
// have to happen per frame - tell the surface how big the screen is, and
// paint what the client library has asked for - are declared here and
// implemented in dll_int/cl_game.c.
//
// This header sits next to vgui_draw.h rather than inside vgui2/ so that the
// client target, whose include path has client/vgui but not
// client/vgui/vgui2, can pick it up.
#pragma once

#include <stddef.h>

// Called once per frame before painting, with the render size in pixels.
// The surface caches it rather than asking, because VGUI2 asks for screen
// and workspace bounds constantly and the answer never changes mid-frame.
void VGUI2_SetScreenSize( int width, int height );

// Paint the client library's windows, if it has any. Returns how many panels
// were painted, which is zero on a client that never brought VGUI2 up - the
// common case on a build where libvgui2client.so is not installed, and the
// reason this is cheap to call unconditionally.
int VGUI2_Paint( void );
