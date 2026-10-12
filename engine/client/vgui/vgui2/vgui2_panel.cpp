// vgui2_panel.cpp - IPanel, the panel tree VGUI2 panels are parented to.
//
// A VPANEL is an integer handle, not a pointer: the client library allocates
// its own vgui2::Panel objects and calls SetParent/GetChild on them, and the
// engine is what has to know where they sit on screen. So the tree lives
// here, keyed by handle, and the client library's object is reached through
// GetPanel when something has to be painted.
//
// Handles are indexes into a vector, and a freed index is reused. VPANEL is
// unsigned and 0 is a valid handle in Valve's code, so validity is a
// generation counter rather than "non-zero" - a stale handle held by a panel
// that outlived its target must not address whatever panel took the slot.
#include "vgui2_panel.h"

#include <IClientVGUI.h>
#include <IEngineVGui.h>
#include <vgui/IPanel.h>
#include <vgui_controls/Panel.h>

#include "vgui2_interfaces.decl.inc"

#include <algorithm>
#include <stdint.h>

namespace vgui2 {



// Handles are index+1 so that a default-constructed VPANEL of 0 names no
// panel, which keeps a client that passes 0 by mistake from writing into the
// first slot it happens to be holding.
CVGui2PanelManager *CVGui2PanelManager::s_instance = nullptr;

CVGui2PanelManager::CVGui2PanelManager()
{
	s_instance = this;
	m_free.clear();
}

CVGui2PanelManager::~CVGui2PanelManager()
{
	if ( s_instance == this )
		s_instance = nullptr;
}

CVGui2PanelManager *CVGui2PanelManager::Instance()
{
	return s_instance;
}

CVGui2PanelManager::Entry *CVGui2PanelManager::Get( VPANEL handle )
{
	if ( !handle || handle > m_entries.size() )
		return nullptr;

	Entry &entry = m_entries[handle - 1];
	if ( !entry.alive || entry.generation != (handle >> 16) )
		return nullptr;
	return &entry;
}

// Reuses a freed slot when there is one. The generation is bumped either way
// so an old handle held by the client library cannot resolve to the panel
// that inherits the index.
VPANEL CVGui2PanelManager::Allocate()
{
	VPANEL handle;

	if ( !m_free.empty() )
	{
		handle = m_free.back();
		m_free.pop_back();
	}
	else
	{
		m_entries.push_back( Entry() );
		handle = (VPANEL)m_entries.size() << 16;
	}

	Entry &entry = m_entries[(handle & 0xffff) - 1];
	entry.generation++;
	handle = (entry.generation << 16) | ((handle & 0xffff));
	entry.alive = true;
	entry.parent = 0;
	entry.children.clear();
	return handle;
}

void CVGui2PanelManager::Free( VPANEL handle )
{
	Entry *entry = Get( handle );
	if ( !entry )
		return;

	// Detach from the parent first: the client library deletes panels from
	// the inside out, and a child still listed in a dead parent would make
	// the next GetChild walk read freed memory.
	VPANEL parent = entry->parent;
	if ( Entry *parentEntry = Get( parent ) )
	{
		auto it = std::find( parentEntry->children.begin(), parentEntry->children.end(), handle );
		if ( it != parentEntry->children.end() )
			parentEntry->children.erase( it );
	}

	entry->alive = false;
	entry->parent = 0;
	entry->children.clear();
	entry->client = nullptr;
	entry->panel = nullptr;
	m_free.push_back( handle );
}


// ---------------------------------------------------------------------------
// CIPanel - the interface the client library actually calls.
//
// Every method here is a lookup in the tree and, where the panel has a
// client-side object, a call on it. A handle that is not live falls straight
// out: the client library can and does keep handles past DeletePanel while
// it tears a window down.
// ---------------------------------------------------------------------------

using Entry = CVGui2PanelManager::Entry;

namespace {

Entry *panel( VPANEL handle )
{
	CVGui2PanelManager *manager = CVGui2PanelManager::Instance();
	return manager ? manager->Get( handle ) : nullptr;
}

// Absolute position is the sum up the parent chain. Panels are parented to
// the embedded panel or to the desktop, both of which sit at the origin, so
// this terminates at a panel with no parent rather than needing to know what
// the root is.
void abs_pos( Entry *entry, int *outX, int *outY )
{
	int x = 0, y = 0;

	while ( entry )
	{
		x += entry->x;
		y += entry->y;

		if ( entry->parent == 0 )
			break;

		Entry *parent = panel( entry->parent );
		if ( !parent || parent == entry )
			break;

		entry = parent;
	}

	if ( outX )
		*outX = x;
	if ( outY )
		*outY = y;
}

// Intersection of a panel with its parents' clip boxes. Returns false when
// the panel is clipped away entirely, which is the answer that matters for
// painting: a clipped subtree should not be walked at all.
bool clip_rect( Entry *entry, int *x0, int *y0, int *x1, int *y1 )
{
	if ( !entry )
		return false;

	int ax, ay;
	abs_pos( entry, &ax, &ay );

	int left = ax + entry->inset[0];
	int top = ay + entry->inset[1];
	int right = ax + entry->wide - entry->inset[2];
	int bottom = ay + entry->tall - entry->inset[3];

	Entry *current = entry;
	while ( current )
	{
		if ( current->parent )
		{
			Entry *parent = panel( current->parent );
			if ( !parent )
				break;

			int px, py;
			abs_pos( parent, &px, &py );
			left = std::max( left, px + parent->inset[0] );
			top = std::max( top, py + parent->inset[1] );
			right = std::min( right, px + parent->wide - parent->inset[2] );
			bottom = std::min( bottom, py + parent->tall - parent->inset[3] );

			current = parent;
		}
		else
			break;
	}

	if ( x0 ) *x0 = left;
	if ( y0 ) *y0 = top;
	if ( x1 ) *x1 = right;
	if ( y1 ) *y1 = bottom;

	return left < right && top < bottom;
}

} // namespace

// --- Geometry --------------------------------------------------------------

void CIPanel::SetPos( VPANEL h, int x, int y )
{
	if ( Entry *entry = panel( h ) )
	{
		entry->x = x;
		entry->y = y;
	}
}

void CIPanel::GetPos( VPANEL h, int &x, int &y )
{
	if ( Entry *entry = panel( h ) )
	{
		x = entry->x;
		y = entry->y;
	}
}

void CIPanel::GetAbsPos( VPANEL h, int &x, int &y )
{
	if ( Entry *entry = panel( h ) )
		abs_pos( entry, &x, &y );
}

void CIPanel::SetSize( VPANEL h, int wide, int tall )
{
	Entry *entry = panel( h );
	if ( !entry )
		return;

	// A minimum size set by a scheme wins over the caller, which is how
	// controls keep their art intact when a window tries to shrink them.
	wide = std::max( wide, entry->minWide );
	tall = std::max( tall, entry->minTall );

	if ( wide == entry->wide && tall == entry->tall )
		return;

	entry->wide = wide;
	entry->tall = tall;
	OnSizeChanged( h, wide, tall );
}

void CIPanel::GetSize( VPANEL h, int &wide, int &tall )
{
	if ( Entry *entry = panel( h ) )
	{
		wide = entry->wide;
		tall = entry->tall;
	}
}

void CIPanel::SetMinimumSize( VPANEL h, int wide, int tall )
{
	if ( Entry *entry = panel( h ) )
	{
		entry->minWide = wide;
		entry->minTall = tall;
	}
}

void CIPanel::GetMinimumSize( VPANEL h, int &wide, int &tall )
{
	if ( Entry *entry = panel( h ) )
	{
		wide = entry->minWide;
		tall = entry->minTall;
	}
}

void CIPanel::OnSizeChanged( VPANEL h, int wide, int tall )
{
	Entry *entry = panel( h );
	if ( !entry || !entry->client )
		return;

	entry->client->OnSizeChanged( wide, tall );

	// Children are positioned by their parent, so a resize has to reach the
	// whole subtree. One level is not enough: a label inside a frame inside
	// a dialog has to move too.
	for ( VPANEL child : entry->children )
		OnSizeChanged( child, wide, tall );
}

void CIPanel::SetInset( VPANEL h, int left, int top, int right, int bottom )
{
	if ( Entry *entry = panel( h ) )
	{
		entry->inset[0] = left;
		entry->inset[1] = top;
		entry->inset[2] = right;
		entry->inset[3] = bottom;
	}
}

void CIPanel::GetInset( VPANEL h, int &left, int &top, int &right, int &bottom )
{
	if ( Entry *entry = panel( h ) )
	{
		left = entry->inset[0];
		top = entry->inset[1];
		right = entry->inset[2];
		bottom = entry->inset[3];
	}
}

void CIPanel::GetClipRect( VPANEL h, int &x0, int &y0, int &x1, int &y1 )
{
	if ( !panel( h ) )
	{
		x0 = y0 = x1 = y1 = 0;
		return;
	}
	clip_rect( panel( h ), &x0, &y0, &x1, &y1 );
}


// --- Tree ------------------------------------------------------------------

void CIPanel::Init( VPANEL h, IClientPanel *clientPanel )
{
	Entry *entry = panel( h );
	if ( !entry )
		return;

	entry->client = clientPanel;

	// IClientPanel::GetPanel() is the client's own Panel subclass, which is
	// the vtable VGUI2 expects to call Paint() and friends through. Its
	// GetVPanel() would only give the handle back to us.
	if ( clientPanel )
		entry->panel = clientPanel->GetPanel();
}

void CIPanel::SetParent( VPANEL h, VPANEL newParent )
{
	Entry *entry = panel( h );
	if ( !entry )
		return;

	if ( entry->parent )
	{
		if ( Entry *old = panel( entry->parent ) )
		{
			auto it = std::find( old->children.begin(), old->children.end(), h );
			if ( it != old->children.end() )
				old->children.erase( it );
		}
	}

	entry->parent = newParent;

	if ( newParent )
	{
		if ( Entry *parent = panel( newParent ) )
		{
			parent->children.push_back( h );
			OnChildAdded( newParent, h );
		}
	}
}

VPANEL CIPanel::GetParent( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->parent : 0;
}

int CIPanel::GetChildCount( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? (int)entry->children.size() : 0;
}

VPANEL CIPanel::GetChild( VPANEL h, int index )
{
	Entry *entry = panel( h );
	if ( !entry || index < 0 || index >= (int)entry->children.size() )
		return 0;
	return entry->children[index];
}

void CIPanel::OnChildAdded( VPANEL h, VPANEL child )
{
	Entry *entry = panel( h );
	if ( entry && entry->client )
		entry->client->OnChildAdded( child );
}

void CIPanel::DeletePanel( VPANEL h )
{
	CVGui2PanelManager *manager = CVGui2PanelManager::Instance();
	if ( !manager || !manager->Get( h ) )
		return;

	// Depth first: a child must be gone before the parent that lists it.
	std::vector<VPANEL> stack{ h };
	while ( !stack.empty() )
	{
		VPANEL current = stack.back();
		stack.pop_back();

		if ( Entry *entry = manager->Get( current ) )
		{
			for ( VPANEL child : entry->children )
				stack.push_back( child );
		}

		manager->Free( current );
	}
}

// --- Visibility, order, state ---------------------------------------------

void CIPanel::SetVisible( VPANEL h, bool state )
{
	if ( Entry *entry = panel( h ) )
		entry->visible = state;
}

bool CIPanel::IsVisible( VPANEL h )
{
	Entry *entry = panel( h );
	return entry && entry->visible;
}


void CIPanel::SetZPos( VPANEL h, int z )
{
	if ( Entry *entry = panel( h ) )
		entry->zpos = z;
}

int CIPanel::GetZPos( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->zpos : 0;
}



// Bring to front by re-inserting last: children are painted in list order,
// so the list order is the paint order and no separate sort is needed.
void CIPanel::MoveToFront( VPANEL h )
{
	Entry *entry = panel( h );
	if ( !entry || !entry->parent )
		return;

	Entry *parent = panel( entry->parent );
	if ( !parent )
		return;

	auto it = std::find( parent->children.begin(), parent->children.end(), h );
	if ( it != parent->children.end() )
		parent->children.erase( it );
	parent->children.push_back( h );
}

void CIPanel::MoveToBack( VPANEL h )
{
	Entry *entry = panel( h );
	if ( !entry || !entry->parent )
		return;

	Entry *parent = panel( entry->parent );
	if ( !parent )
		return;

	auto it = std::find( parent->children.begin(), parent->children.end(), h );
	if ( it != parent->children.end() )
		parent->children.erase( it );
	parent->children.insert( parent->children.begin(), h );
}

bool CIPanel::HasParent( VPANEL h, VPANEL potentialParent )
{
	Entry *entry = panel( h );

	while ( entry && entry->parent )
	{
		if ( entry->parent == potentialParent )
			return true;

		// A cycle would spin here forever, and a cycle in a panel tree
		// means something upstream already went wrong.
		Entry *parent = panel( entry->parent );
		if ( !parent || parent == entry )
			return false;
		entry = parent;
	}
	return false;
}



void CIPanel::SetKeyBoardInputEnabled( VPANEL h, bool state )
{
	if ( Entry *entry = panel( h ) )
		entry->keyboardInput = state;
}

bool CIPanel::IsKeyBoardInputEnabled( VPANEL h )
{
	Entry *entry = panel( h );
	return entry && entry->keyboardInput;
}

void CIPanel::SetMouseInputEnabled( VPANEL h, bool state )
{
	if ( Entry *entry = panel( h ) )
		entry->mouseInput = state;
}

bool CIPanel::IsMouseInputEnabled( VPANEL h )
{
	Entry *entry = panel( h );
	return entry && entry->mouseInput;
}


bool CIPanel::IsProportional( VPANEL h )
{
	Entry *entry = panel( h );
	return entry && entry->proportional;
}


SurfacePlat *CIPanel::Plat( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? (SurfacePlat *)(uintptr_t)entry->plat : nullptr;
}

void CIPanel::SetPlat( VPANEL h, SurfacePlat *plat )
{
	if ( Entry *entry = panel( h ) )
		entry->plat = (VPANEL)(uintptr_t)plat;
}

IClientPanel *CIPanel::Client( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->client : nullptr;
}

const char *CIPanel::GetName( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->name.c_str() : "";
}

const char *CIPanel::GetClassName( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->className.c_str() : "";
}

const char *CIPanel::GetModuleName( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->moduleName.c_str() : "";
}

// --- Identity --------------------------------------------------------------

Panel *CIPanel::GetPanel( VPANEL h, const char *destinationModule )
{
	Entry *entry = panel( h );
	if ( !entry )
		return nullptr;

	// The client library owns these objects; the engine only hands the
	// pointer back. destinationModule is the module a message is being
	// routed to, which for a single-module build is the same panel.
	return entry->panel;
}

// --- Paint -----------------------------------------------------------------

void CIPanel::PaintTraverse( VPANEL h, bool forceRepaint, bool allowForce )
{
	Entry *entry = panel( h );
	if ( !entry || !entry->visible )
		return;

	// Clipped to nothing: skip the subtree rather than walking into it.
	int x0, y0, x1, y1;
	if ( !clip_rect( entry, &x0, &y0, &x1, &y1 ) )
		return;

	if ( entry->panel )
		entry->panel->Paint();

	// Children last, in list order: MoveToFront and MoveToBack edit this
	// same list, so paint order follows the order the client library asked
	// for without a second structure to keep in step.
	for ( VPANEL child : entry->children )
		PaintTraverse( child, forceRepaint, allowForce );
}

VPANEL CIPanel::IsWithinTraverse( VPANEL h, int x, int y, bool traversePopups )
{
	Entry *entry = panel( h );
	if ( !entry || !entry->visible )
		return 0;

	int x0, y0, x1, y1;
	if ( !clip_rect( entry, &x0, &y0, &x1, &y1 ) )
		return 0;

	int ax, ay;
	abs_pos( entry, &ax, &ay );

	bool inside = x >= ax + entry->inset[0] && x < ax + entry->wide - entry->inset[2]
		&& y >= ay + entry->inset[1] && y < ay + entry->tall - entry->inset[3];

	VPANEL hit = inside ? h : 0;

	// Back to front: the last child painted is the top one, so the first
	// hit while walking backwards is the one the user is pointing at.
	for ( int i = (int)entry->children.size() - 1; i >= 0; i-- )
	{
		VPANEL child = IsWithinTraverse( entry->children[i], x, y, traversePopups );
		if ( child )
			hit = child;
	}

	return hit;
}

void CIPanel::Repaint( VPANEL h )
{
	Entry *entry = panel( h );
	if ( !entry || !entry->panel )
		return;

	// A repaint request is meaningless while the panel is not visible, and
	// the client library asks for one on every control it creates.
	if ( !entry->visible )
		return;

	entry->panel->Paint();
}

void CIPanel::PerformApplySchemeSettings( VPANEL h )
{
	Entry *entry = panel( h );
	if ( entry && entry->panel )
		// ApplySchemeSettings is not public on Panel; SetScheme re-runs the
	// scheme for this panel, which is the public path to the same place.
	// No tag to apply: the client library sets the scheme on the panel
	// itself through Panel::SetScheme, and this walk exists to push the
	// already-set scheme down a subtree that was just parented.
	entry->panel->OnThink();

	for ( VPANEL child : entry ? entry->children : std::vector<VPANEL>() )
		PerformApplySchemeSettings( child );
}

HScheme CIPanel::GetScheme( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->scheme : 0;
}

// --- Messages and focus ----------------------------------------------------

void CIPanel::SendMessage( VPANEL h, KeyValues *params, VPANEL ifromPanel )
{
	Entry *entry = panel( h );
	if ( entry && entry->panel )
		entry->panel->OnMessage( params, ifromPanel );
}

void CIPanel::Think( VPANEL h )
{
	Entry *entry = panel( h );
	if ( entry && entry->panel )
		// Panel::Think is private in the GS headers; OnThink is the hook the
	// panel itself overrides, and it is what per-frame work runs from.
	entry->panel->OnThink();

	for ( VPANEL child : entry ? entry->children : std::vector<VPANEL>() )
		Think( child );
}

bool CIPanel::RequestInfo( VPANEL h, KeyValues *outputData )
{
	Entry *entry = panel( h );
	if ( !entry || !entry->panel )
		return false;

	entry->panel->RequestInfo( outputData );
	return true;
}

void CIPanel::InternalFocusChanged( VPANEL h, bool lost )
{
	if ( Entry *entry = panel( h ) )
	{
		if ( lost )
			entry->keyFocus = 0;
	}
}

VPANEL CIPanel::GetCurrentKeyFocus( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->keyFocus : 0;
}

void CIPanel::RequestFocus( VPANEL h, int direction )
{
	Entry *entry = panel( h );
	if ( !entry )
		return;

	// One focus at a time: the old holder has to hear about it or it keeps
	// painting a caret into a panel nobody is typing into.
	if ( entry->keyFocus && entry->keyFocus != h )
		InternalFocusChanged( entry->keyFocus, true );

	entry->keyFocus = h;
}

bool CIPanel::RequestFocusPrev( VPANEL h, VPANEL existingPanel )
{
	VPANEL current = GetCurrentKeyFocus( h );
	if ( current == existingPanel )
	{
		RequestFocus( h, -1 );
		return true;
	}
	return false;
}

bool CIPanel::RequestFocusNext( VPANEL h, VPANEL existingPanel )
{
	VPANEL current = GetCurrentKeyFocus( h );
	if ( current == existingPanel )
	{
		RequestFocus( h, 1 );
		return true;
	}
	return false;
}

// --- Popup and enabled state ----------------------------------------------

bool CIPanel::IsPopup( VPANEL h )
{
	Entry *entry = panel( h );
	return entry && entry->popup;
}

void CIPanel::SetPopup( VPANEL h, bool state )
{
	if ( Entry *entry = panel( h ) )
		entry->popup = state;
}

bool CIPanel::Render_GetPopupVisible( VPANEL h )
{
	Entry *entry = panel( h );
	return entry && entry->popupVisible;
}

void CIPanel::Render_SetPopupVisible( VPANEL h, bool state )
{
	if ( Entry *entry = panel( h ) )
		entry->popupVisible = state;
}

bool CIPanel::IsEnabled( VPANEL h )
{
	Entry *entry = panel( h );
	return entry && entry->enabled;
}

void CIPanel::SetEnabled( VPANEL h, bool state )
{
	if ( Entry *entry = panel( h ) )
		entry->enabled = state;
}

int CIPanel::GetTabPosition( VPANEL h )
{
	Entry *entry = panel( h );
	return entry ? entry->tabPosition : 0;
}

} // namespace vgui2
