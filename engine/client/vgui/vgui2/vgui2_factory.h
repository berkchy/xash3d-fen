// vgui2_factory.h - engine-side entry points for GoldSource VGUI2.
#pragma once

// Resolves one of the thirteen interfaces the client library asks for.
// Named the way Valve names them so the client's lookup strings match, e.g.
// "VGUI_Surface026". Unknown names return NULL with *returnCode set to 1.
extern "C" void *VGui2_CreateInterface( const char *name, int *returnCode );
