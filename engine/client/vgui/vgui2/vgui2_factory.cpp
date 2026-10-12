// vgui2_factory.cpp - the "VGui2Factory" native object.
//
// This is the single translation unit that includes the generated interface
// implementations. They are definitions, not declarations, so a second
// translation unit including them would define every method twice - MSVC
// reports that as LNK4006 and then dies in codegen with LNK1257.
//
// The client library resolves thirteen interfaces through this one function
// (vgui2/src/tier2/tier2.cpp:55-103). Today Sys_GetNativeObject knows nothing
// about VGUI2, so every one of those lookups returns NULL, tier2 refuses to
// continue, and the panel code in the client library never runs.
//
// The interfaces themselves are generated from the Valve headers, so the set
// of names answered here and the set the client asks for cannot drift apart
// without a link error.
#include "vgui2_factory.h"

#include <vgui/ILocalize.h>
#include <vgui/ISystem.h>
#include <vgui/IVGui.h>
#include <vgui/IInputInternal.h>

#include "vgui2_interfaces.gen.inc"

// Surface and panel: implemented in vgui2_surface.cpp and vgui2_panel.cpp.
// Declared here so the factory can hand them out once they exist; until then
// they resolve to NULL and the client library stops at its own check rather
// than crashing on a half-built surface.
extern "C" void *VGui2_GetSurfaceInterface( const char *name, int *code );
extern "C" void *VGui2_GetPanelInterface( const char *name, int *code );

namespace {

vgui2::CISystem  g_system;
vgui2::CIVGui     g_vgui;
vgui2::CIInputInternal g_input;
vgui2::CILocalize g_localize;

struct Entry
{
	const char *name;
	void *object;
};

const Entry kInterfaces[] =
{
	{ VGUI_SYSTEM_INTERFACE_VERSION_GS,         &g_system },
	{ VGUI_IVGUI_INTERFACE_VERSION_GS,          &g_vgui },
	{ VGUI_INPUTINTERNAL_INTERFACE_VERSION,     &g_input },
	{ VGUI_LOCALIZE_INTERFACE_VERSION,           &g_localize },
};

} // namespace

extern "C" void *VGui2_CreateInterface( const char *name, int *returnCode )
{
	if ( returnCode )
		*returnCode = 0;

	for ( size_t i = 0; i < sizeof( kInterfaces ) / sizeof( kInterfaces[0] ); i++ )
	{
		if ( name && !Q_strcmp( name, kInterfaces[i].name ) )
			return kInterfaces[i].object;
	}

	// Not stubbed yet: hand back NULL and a code, exactly like any other
	// CreateInterface. The client checks the result.
	if ( returnCode )
		*returnCode = 1;
	return NULL;
}
