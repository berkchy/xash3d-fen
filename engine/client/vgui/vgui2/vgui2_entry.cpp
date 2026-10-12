// vgui2_entry.cpp - C entry point into the C++ VGUI2 interfaces.
//
// Sys_GetNativeObject is C and cannot know about a C++ function pointer, so
// the lookup it does is wrapped here.
#include "vgui2_factory.h"

extern "C" void *VGui2_GetFactory( void )
{
	return (void *)VGui2_CreateInterface;
}
