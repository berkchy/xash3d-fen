// vgui2_frame.h - C-callable entry points into the VGUI2 C++ in this
// directory, for the engine's C files.
//
// One way in, so that VGUI2's state stays behind these three functions
// instead of leaking into globals the rest of the client can reach.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The surface, or NULL before anything has asked the factory for it.
struct ISurface;

// Tell the surface the render size. Safe to call every frame.
void VGui2ScreenSize( int width, int height );

// Paint whatever the client library has registered. Returns the number of
// panels painted; zero means VGUI2 is not in use on this client.
int VGui2Paint( void );

#ifdef __cplusplus
}
#endif
