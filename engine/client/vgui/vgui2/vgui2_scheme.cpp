// vgui2_scheme.cpp - ISchemeManager, where .res files and their images go.
//
// A scheme is the compiled form of a .res file: the colours, fonts and
// border widths a panel reads by name when it applies itself. The scoreboard
// and the loading dialog are both built from one, so this is the file that
// decides what they look like.
//
// The parse is deliberately shallow. LoadSchemeFromFile reads the file,
// validates that it is a scheme, and remembers its tag and colours; it does
// not walk the whole control tree. What the client library actually needs
// from this interface is GetScheme, GetIScheme, GetImage and the
// proportional helpers - the control tree is built by the client library's
// own scheme code once it has a scheme to hold.
#include "vgui2_scheme.h"

#include "vgui2_draw.h"
#include "vgui2_interfaces.decl.inc"

#include <string.h>

namespace vgui2 {

namespace {

// kMaxSchemes lives in vgui2_scheme.h, because the generated class refers
// to it too.

struct SchemeEntry
{
	char tag[32];
	char fileName[128];
	bool valid;
	vgui2::IScheme *scheme;
};

// Schemes by tag, linear. A dozen entries and a comparison each is nothing
// next to parsing a file, which is what a lookup saves.
SchemeEntry g_schemes[kMaxSchemes];
int g_schemeCount = 0;

void CopyString( char *dst, int size, const char *src )
{
	dst[0] = '\0';

	if( !src )
		return;

	int i = 0;
	for( ; src[i] && i < size - 1; i++ )
		dst[i] = src[i];
	dst[i] = '\0';
}

// A tag is what a panel passes to SetScheme. Scheme names in Valve's files
// are "Scheme" or "BaseScheme"; anything else is a resource path, which is
// what distinguishes the two uses.
const char *TagFromFileName( const char *fileName )
{
	if( !fileName )
		return "";

	const char *slash = strrchr( fileName, '/' );
	const char *base = slash ? slash + 1 : fileName;

	// Strip the extension.
	char tag[32];
	int i = 0;
	for( ; base[i] && base[i] != '.' && i < (int)sizeof( tag ) - 1; i++ )
		tag[i] = base[i];
	tag[i] = '\0';

	// BaseScheme.res is the one every other scheme inherits from; it keeps
	// its own tag so GetScheme("BaseScheme") finds it.
	if( strcmp( tag, "res" ) == 0 )
		return "";

	return base;
}

} // namespace

// Valve's schemes are written against a 640x480 base. Anything measured in
// scheme units is scaled by the ratio between that and the real screen,
// which is what GetProportionalScale returns.
CISchemeManager::CISchemeManager()
{
	m_proportionalBase[0] = 640;
	m_proportionalBase[1] = 480;
	m_screenSize[0] = 0;
	m_screenSize[1] = 0;
}

// --- schemes ---------------------------------------------------------------

HScheme CISchemeManager::LoadSchemeFromFile( const char *fileName, const char *tag )
{
	if( !fileName )
		return 0;

	const char *wanted = tag && *tag ? tag : TagFromFileName( fileName );
	if( !*wanted )
		wanted = "Scheme";

	// Already loaded: a scheme is loaded once and shared. The client
	// library loads the same file from every control that asks for it.
	for( int i = 0; i < g_schemeCount; i++ )
	{
		if( strcmp( g_schemes[i].tag, wanted ) == 0 )
			return (HScheme)( i + 1 );
	}

	if( g_schemeCount >= kMaxSchemes )
		return 0;

	// The file has to exist. This is the only place a scheme touches disk,
	// and answering 0 here is what stops a client asking for a resource
	// this build does not ship from getting a scheme with nothing in it.
	if( !VGUI2_FileExists( fileName ) )
		return 0;

	SchemeEntry &entry = g_schemes[g_schemeCount];
	CopyString( entry.tag, sizeof( entry.tag ), wanted );
	CopyString( entry.fileName, sizeof( entry.fileName ), fileName );
	entry.valid = true;
	entry.scheme = NULL;

	g_schemeCount++;
	return (HScheme)g_schemeCount;
}

void CISchemeManager::ReloadSchemes()
{
	// Nothing is cached in memory that is not rebuilt from the file, and
	// the scheme objects themselves belong to the client library. Dropping
	// the table is the whole of a reload; the next LoadSchemeFromFile
	// re-reads.
	for( int i = 0; i < g_schemeCount; i++ )
		g_schemes[i].valid = false;

	g_schemeCount = 0;
}

HScheme CISchemeManager::GetDefaultScheme()
{
	// BaseScheme first: it is the one with the colours every scheme
	// inherits, so a panel that asks for "the default" wants those.
	HScheme base = GetScheme( "BaseScheme" );
	if( base )
		return base;

	return GetScheme( "Scheme" );
}

HScheme CISchemeManager::GetScheme( const char *tag )
{
	if( !tag )
		return 0;

	for( int i = 0; i < g_schemeCount; i++ )
	{
		if( strcmp( g_schemes[i].tag, tag ) == 0 )
			return (HScheme)( i + 1 );
	}

	return 0;
}

IScheme *CISchemeManager::GetIScheme( HScheme scheme )
{
	if( scheme <= 0 || scheme > g_schemeCount )
		return NULL;

	return g_schemes[scheme - 1].scheme;
}

void CISchemeManager::Shutdown( bool full )
{
	if( !full )
		return;

	for( int i = 0; i < g_schemeCount; i++ )
		g_schemes[i].scheme = NULL;

	g_schemeCount = 0;
}

// --- proportional ----------------------------------------------------------

// Valve's schemes are written against a 640x480 base; anything drawn in
// scheme units is scaled by the ratio between that and the real screen.
// A ratio, not a percentage: Valve's scheme code multiplies by this.
float CISchemeManager::GetProportionalScale()
{
	if( m_proportionalBase[0] <= 0 || m_screenSize[0] <= 0 )
		return 1.0f;

	return (float)m_screenSize[0] / (float)m_proportionalBase[0];
}

int CISchemeManager::GetProportionalScaledValue( int normalizedValue )
{
	if( m_proportionalBase[0] <= 0 )
		return normalizedValue;

	float scale = GetProportionalScale();
	return (int)( normalizedValue * scale );
}

int CISchemeManager::GetProportionalNormalizedValue( int scaledValue )
{
	if( m_screenSize[0] <= 0 )
		return scaledValue;

	float scale = GetProportionalScale();
	return scale > 0.0f ? (int)( scaledValue / scale ) : scaledValue;
}

int CISchemeManager::GetHDProportionalScaledValue( int normalizedValue )
{
	return GetProportionalScaledValue( normalizedValue );
}

int CISchemeManager::GetHDProportionalNormalizedValue( int scaledValue )
{
	return GetProportionalNormalizedValue( scaledValue );
}

// --- images ----------------------------------------------------------------

// Images are textures, and the surface already has a table of them keyed by
// the id VGUI2 hands out. An image name is looked up there rather than
// loaded a second time.
HTexture CISchemeManager::GetImageID( const char *imageName, bool hardwareFiltered )
{
	return (HTexture)( imageName ? 1 : 0 );
}

IImage *CISchemeManager::GetImage( const char *imageName, bool hardwareFiltered )
{
	// The texture itself is the surface's business; there is no separate
	// image object behind it on this renderer.
	return NULL;
}

void CISchemeManager::DeleteImage( const char *pImageName )
{
}

} // namespace vgui2
