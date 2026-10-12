// vgui2_panel.h - the panel tree behind IPanel.
#pragma once

#include <vgui/IPanel.h>

#include <string>
#include <vector>

namespace vgui2 {

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

		std::vector<VPANEL> children;
		std::string name;
		std::string className;
		std::string moduleName;
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
	std::vector<Entry> m_entries;
	std::vector<VPANEL> m_free;
	CVGui2PanelManager( const CVGui2PanelManager & );
	CVGui2PanelManager &operator=( const CVGui2PanelManager & );
};

} // namespace vgui2
