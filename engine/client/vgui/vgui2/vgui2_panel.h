// vgui2_panel.h - the panel tree behind IPanel.
#pragma once

#include <vgui/IPanel.h>

namespace vgui2 {

// Panels per panel. The scoreboard's busiest frame holds a label per column
// and a row per player; the loading dialog holds a handful.
const int kMaxChildren = 256;
const int kMaxName = 64;

// Panels alive at once. The scoreboard and the loading dialog are nowhere
// near this; past it the manager refuses to hand out another handle rather
// than grow.
const int kCapacity = 512;

// Handle layout: index in the low half, generation in the high half.
const VPANEL kHandleStep = 0x10000;

// One record per live panel. Kept in a vector and handed out by handle, so
// the client library can hold a VPANEL across frames without a pointer that
// could dangle.
class CVGui2PanelManager
{
public:
	struct Entry
	{
		int x = 0, y = 0;
		int wide = 0, tall = 0;
		int minWide = 0, minTall = 0;
		int zpos = 0;
		int renderZPos = 0;
		int inset[4] = { 0, 0, 0, 0 };
		int tabPosition = 0;
		bool visible = true;
		bool popup = false;
		bool enabled = true;
		bool autoDelete = true;
		bool keyboardInput = true;
		bool mouseInput = true;
		bool proportional = false;
		bool layoutEnabled = false;
		bool allowScrollToChild = false;
		bool popupVisible = false;
		bool alive = false;
		unsigned short generation = 0;

		VPANEL parent = 0;
		VPANEL modal = 0;
		VPANEL context = 0;
		VPANEL plat = 0;
		VPANEL keyFocus = 0;
		VPANEL popupPanel = 0;
		VPANEL focusPanel = 0;
		VPANEL topmostFocusable = 0;
		VPANEL desktop = 0;
		VPANEL clipToDesktop = 0;
		VPANEL clipToDesktopChild = 0;
		HScheme scheme = 0;

		IClientPanel *client = nullptr;
		Panel *panel = nullptr;

		// Fixed capacity, no STL. The engine binary is not linked against
		// the C++ runtime - it is a C program that dlopens a C++ client dll,
		// and pulling libc++ in here would put a second copy of it in the
		// process. std::vector alone is enough to do that: growing one
		// references std::length_error, which the linker then wants.
		// A frame with more children than this is not a thing that happens.
		VPANEL children[kMaxChildren];
		int childCount = 0;

		char name[64];
		char className[64];
		char moduleName[64];
	};

	static CVGui2PanelManager *Instance();

	// One per process. VGUI2 is initialised from the client library's
	// InitInterfacesList call, which has no place to hand an instance in.
	static CVGui2PanelManager *s_instance;

	CVGui2PanelManager();
	~CVGui2PanelManager();

	Entry *Get( VPANEL handle );
	VPANEL Allocate();
	void Free( VPANEL handle );
private:
	// Entry is large, so the storage is a fixed block allocated once: the
	// same reason there is no vector.
	Entry *m_entries = nullptr;
	int m_capacity = 0;
	int m_count = 0;

	VPANEL m_free[64];
	int m_freeCount = 0;
	CVGui2PanelManager( const CVGui2PanelManager & );
	CVGui2PanelManager &operator=( const CVGui2PanelManager & );
};

} // namespace vgui2
