// vgui2_interfaces.cpp - engine half of Valve's GoldSource VGUI2.
//
// See vgui2_interfaces.h for why this exists. The classes themselves live in
// vgui2_interfaces.gen.cpp, which tools/gen_vgui2_interfaces.py writes from
// the headers; only the includes and the factory live here.
#include <vgui/ILocalize.h>
#include <vgui/ISystem.h>
#include <vgui/IVGui.h>
#include <vgui/IInputInternal.h>
#include <vgui/ISurface.h>
#include <vgui/IPanel.h>

#include "vgui2_interfaces.gen.inc"
