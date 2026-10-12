// vgui2_frame.cpp - see vgui2_frame.h.
#include "vgui2_frame.h"

#include "vgui2_factory.h"
#include "vgui2_panel.h"
#include "vgui2_surface.h"

#include <vgui/ISurface.h>

#include "vgui2_interfaces.decl.inc"

namespace {

// The surface the factory hands out, remembered so the engine can paint
// without resolving the factory itself. Not a reference: the factory owns
// the object for the life of the process.
vgui2::CISurface *Surface()
{
	int code = 0;
	void *object = VGui2_CreateInterface( VGUI_SURFACE_INTERFACE_VERSION_GS, &code );
	return (vgui2::CISurface *)object;
}

} // namespace

extern "C" void VGui2ScreenSize( int width, int height )
{
	if( vgui2::CISurface *surface = Surface() )
	{
		surface->m_screenSize[0] = width;
		surface->m_screenSize[1] = height;
	}
}

extern "C" int VGui2Paint( void )
{
	vgui2::CISurface *surface = Surface();
	if( !surface )
		return 0;

	vgui2::VPANEL root = surface->GetEmbeddedPanel();
	if( root == 0 )
		return 0;

	surface->PaintTraverse( root );
	return 1;
}
