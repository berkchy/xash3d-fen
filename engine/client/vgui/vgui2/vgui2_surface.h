// vgui2_surface.h - state for ISurface, the drawing half of VGUI2.
//
// The class itself is generated: tools/gen_vgui2_interfaces.py emits
// CISurface in vgui2_interfaces.decl.inc from Valve's headers, with the data
// members this needs added from EXTRA_MEMBERS in that same script. Declaring
// a second class here - even a matching one - meant two classes of the same
// name in one translation unit, and a matching declaration is exactly the
// thing that goes quietly wrong when a signature is wrong.
//
// The one thing worth writing by hand is the texture record: it is not part
// of the interface, and the generated declaration refers to it by name.
#pragma once

#include <vgui/ISurface.h>

#include "vgui2_font.h"

namespace vgui2 {

// Textures VGUI2 can hold by id. Fixed block, no STL: the engine binary is
// not linked against the C++ runtime.
const int kMaxTextures = 1024;

// Popups VGUI2 can track. The scoreboard and the loading dialog are one
// each; the rest of the interface's window bookkeeping is for a desktop
// build this engine is not.
const int kMaxPopups = 16;

// One texture as the surface tracks it: the renderer handle, the size the
// renderer reported, and the sub rectangle a control draws out of a sheet.
struct SurfaceTexture
{
	int texture;
	int width;
	int height;
	int subX;
	int subY;
	unsigned int subWidth;
	unsigned int subHeight;
	char name[128];
};

} // namespace vgui2
