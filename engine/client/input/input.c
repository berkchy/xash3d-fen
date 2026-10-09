/*
input.c - win32 input devices
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#if XASH_SDL == 2
#include <SDL.h>
#elif XASH_SDL == 3
#include <SDL3/SDL.h>
#endif

#include "common.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "cursor_type.h"
#include "platform/platform.h"

static qboolean	in_mouseactive;				// false when not focus app
static qboolean	in_mouseinitialized;
static struct
{
	int x, y;
} in_lastvalidpos;
static qboolean	in_mouse_savedpos;
static int in_mstate = 0;
static struct inputstate_s
{
	float lastpitch, lastyaw;
} inputstate;

CVAR_DEFINE_AUTO( m_pitch, "0.022", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "mouse pitch value" );
CVAR_DEFINE_AUTO( m_yaw, "0.022", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "mouse yaw value" );
CVAR_DEFINE_AUTO( m_ignore, DEFAULT_M_IGNORE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "ignore mouse events" );
static CVAR_DEFINE_AUTO( look_filter, "0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "filter look events making it smoother" );
static CVAR_DEFINE_AUTO( m_rawinput, "1", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "enable mouse raw input" );

static CVAR_DEFINE_AUTO( cl_forwardspeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default forward move speed" );
static CVAR_DEFINE_AUTO( cl_backspeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default back move speed"  );
static CVAR_DEFINE_AUTO( cl_sidespeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default side move speed"  );

static CVAR_DEFINE_AUTO( m_grab_debug, "0", FCVAR_PRIVILEGED, "show debug messages on mouse state change" );
CVAR_DEFINE_AUTO( touch_enable, DEFAULT_TOUCH_ENABLE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "enable touch controls" );

CVAR_DEFINE_AUTO( bhop_assist, "1", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "bunny hop assist, release jump at the right height (0=off, 1=on)" );
CVAR_DEFINE_AUTO( bhop_ground_dist, "37.7", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "height above ground at which bunny hop releases jump" );
CVAR_DEFINE_AUTO( bhop_debug, "0", FCVAR_ARCHIVE, "print ground distance and +gs state once a second" );

// state for +gs, see IN_GroundStrafe
static struct
{
	qboolean enabled;          // +gs is held
	qboolean was_enabled;      // was enabled on the previous move
	qboolean was_on_ground;    // were we touching the ground last move
	qboolean armed;            // may we still tap duck on the next landing
	qboolean release_duck;     // duck was tapped, drop it once it has been sent
	int         duck_cmd_seq;  // outgoing_sequence the tap was stamped on
} gs_state;

static void IN_GroundStrafe_f( void );
static void IN_GroundStrafeEnd_f( void );

/*
================
IN_CollectInputDevices

Returns a bit mask representing connected devices or, at least, enabled
================
*/
uint IN_CollectInputDevices( void )
{
	uint ret = 0;

	if( !m_ignore.value ) // no way to check is mouse connected, so use cvar only
		ret |= INPUT_DEVICE_MOUSE;

	if( touch_enable.value )
		ret |= INPUT_DEVICE_TOUCH;

	if( Joy_IsActive() ) // connected or enabled
		ret |= INPUT_DEVICE_JOYSTICK;

	Con_Reportf( "Connected devices: %s%s%s%s\n",
		FBitSet( ret, INPUT_DEVICE_MOUSE )    ? "mouse " : "",
		FBitSet( ret, INPUT_DEVICE_TOUCH )    ? "touch " : "",
		FBitSet( ret, INPUT_DEVICE_JOYSTICK ) ? "joy " : "",
		FBitSet( ret, INPUT_DEVICE_VR )       ? "vr " : "");

	return ret;
}

/*
=================
IN_LockInputDevices

tries to lock any possibilty to connect another input device after
player is connected to the server
=================
*/
void IN_LockInputDevices( qboolean lock )
{
	extern convar_t joy_enable; // private to input system

	if( lock )
	{
		SetBits( m_ignore.flags, FCVAR_READ_ONLY );
		SetBits( joy_enable.flags, FCVAR_READ_ONLY );
		SetBits( touch_enable.flags, FCVAR_READ_ONLY );
	}
	else
	{
		ClearBits( m_ignore.flags, FCVAR_READ_ONLY );
		ClearBits( joy_enable.flags, FCVAR_READ_ONLY );
		ClearBits( touch_enable.flags, FCVAR_READ_ONLY );
	}
}


/*
===========
IN_StartupMouse
===========
*/
static void IN_StartupMouse( void )
{
	Cvar_RegisterVariable( &m_ignore );

	Cvar_RegisterVariable( &m_pitch );
	Cvar_RegisterVariable( &m_yaw );
	Cvar_RegisterVariable( &look_filter );
	Cvar_RegisterVariable( &m_rawinput );
	Cvar_RegisterVariable( &m_grab_debug );
	Cvar_RegisterVariable( &touch_enable );

	// You can use -nomouse argument to prevent using mouse from client
	// -noenginemouse will disable all mouse input
	if( Sys_CheckParm(  "-noenginemouse" )) return;

	in_mouseinitialized = true;
}

/*
===========
IN_MouseSavePos

Save mouse pos before state change e.g. changelevel
===========
*/
void IN_MouseSavePos( void )
{
	if( !in_mouseactive )
		return;

	Platform_GetMousePos( &in_lastvalidpos.x, &in_lastvalidpos.y );
	in_mouse_savedpos = true;
}

/*
===========
IN_MouseRestorePos

Restore right position for background
===========
*/
void IN_MouseRestorePos( void )
{
	if( !in_mouse_savedpos )
		return;

	Platform_SetMousePos( in_lastvalidpos.x, in_lastvalidpos.y );

	in_mouse_savedpos = false;
}

/*
===========
IN_ToggleClientMouse

Called when key_dest is changed
===========
*/
void IN_ToggleClientMouse( int newstate, int oldstate )
{
	if( newstate == oldstate )
		return;

	// since SetCursorType controls cursor visibility
	// execute it first, and then check mouse grab state
	if( newstate == key_menu || newstate == key_console )
	{
		Platform_SetCursorType( dc_arrow );

#if XASH_USE_EVDEV
		Evdev_SetGrab( false );
#endif
	}
	else
	{
		Platform_SetCursorType( dc_none );

#if XASH_USE_EVDEV
		Evdev_SetGrab( true );
#endif
	}

	// don't leave the user without cursor if they enabled m_ignore
	if( m_ignore.value )
		return;

	if( oldstate == key_game )
	{
		IN_DeactivateMouse();
	}
	else if( newstate == key_game )
	{
		IN_ActivateMouse();
	}
}

void IN_SetRelativeMouseMode( qboolean set )
{
	static qboolean s_bRawInput;
	qboolean verbose = m_grab_debug.value ? true : false;

	if( set && !s_bRawInput )
	{
#if XASH_SDL >= 2
		SDL_GetRelativeMouseState( NULL, NULL );
#if XASH_SDL == 2
		SDL_SetRelativeMouseMode( SDL_TRUE );
#else // XASH_SDL != 2
		SDL_SetWindowRelativeMouseMode( host.hWnd, true );
#endif // XASH_SDL != 2
#endif // XASH_SDL >= 2
		s_bRawInput = true;
		if( verbose )
			Con_Printf( "%s: true\n", __func__ );
	}
	else if( !set && s_bRawInput )
	{
#if XASH_SDL >= 2
		SDL_GetRelativeMouseState( NULL, NULL );
#if XASH_SDL == 2
		SDL_SetRelativeMouseMode( SDL_FALSE );
#else // XASH_SDL != 2
		SDL_SetWindowRelativeMouseMode( host.hWnd, false );
#endif // XASH_SDL != 2
#endif // XASH_SDL >= 2

		s_bRawInput = false;
		if( verbose )
			Con_Printf( "%s: false\n", __func__ );
	}
}

void IN_SetMouseGrab( qboolean set )
{
	static qboolean s_bMouseGrab;
	qboolean verbose = m_grab_debug.value ? true : false;

	if( set && !s_bMouseGrab )
	{
		Platform_SetMouseGrab( true );

		s_bMouseGrab = true;
		if( verbose )
			Con_Printf( "%s: true\n", __func__ );
	}
	else if( !set && s_bMouseGrab )
	{
		Platform_SetMouseGrab( false );

		s_bMouseGrab = false;
		if( verbose )
			Con_Printf( "%s: false\n", __func__ );
	}
}

static void IN_CheckMouseState( qboolean active )
{
	qboolean use_raw_input;

#if XASH_WIN32
	use_raw_input = ( m_rawinput.value && clgame.client_dll_uses_sdl ) || clgame.dllFuncs.pfnLookEvent != NULL;
#else
	use_raw_input = true; // always use SDL code
#endif

	if( m_ignore.value )
		active = false;

	if( active && use_raw_input && !host.mouse_visible && cls.state == ca_active )
		IN_SetRelativeMouseMode( true );
	else
		IN_SetRelativeMouseMode( false );

	if( active && !host.mouse_visible && cls.state == ca_active )
		IN_SetMouseGrab( true );
	else
		IN_SetMouseGrab( false );
}

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
void IN_ActivateMouse( void )
{
	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( true );
	if( clgame.dllFuncs.IN_ActivateMouse )
		clgame.dllFuncs.IN_ActivateMouse();
	in_mouseactive = true;
}

/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
void IN_DeactivateMouse( void )
{
	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( false );
	if( clgame.dllFuncs.IN_DeactivateMouse )
		clgame.dllFuncs.IN_DeactivateMouse();
	in_mouseactive = false;
}



/*
================
IN_MouseMove
================
*/
static void IN_MouseMove( void )
{
	if( !in_mouseinitialized )
		return;

	if( Touch_WantVisibleCursor( ))
	{
		// touch emulation overrides all input
		Touch_KeyEvent( 0, 0 );
		return;
	}

	// find mouse movement
	int x, y;
	Platform_GetMousePos( &x, &y );

	VGui_MouseMove( x, y );

	// if the menu is visible, move the menu cursor
	UI_MouseMove( x, y );
}

/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent( int key, int down )
{
	if( !in_mouseinitialized )
		return;

	if( down )
		SetBits( in_mstate, BIT( key ));
	else ClearBits( in_mstate, BIT( key ));

	// touch emulation overrides all input
	if( Touch_WantVisibleCursor( ))
	{
		Touch_KeyEvent( K_MOUSE1 + key, down );
	}
	else if( cls.key_dest == key_game )
	{
		// perform button actions
		VGui_MouseEvent( K_MOUSE1 + key, down );

		// don't do Key_Event here
		// client may override IN_MouseEvent
		// but by default it calls back to Key_Event anyway
		if( in_mouseactive )
			clgame.dllFuncs.IN_MouseEvent( in_mstate );
	}
	else
	{
		// perform button actions
		Key_Event( K_MOUSE1 + key, down );
	}
}

/*
==============
IN_MWheelEvent

direction is negative for wheel down, otherwise wheel up
==============
*/
void IN_MWheelEvent( int y )
{
	int b = y > 0 ? K_MWHEELUP : K_MWHEELDOWN;

	VGui_MWheelEvent( y );

	Key_Event( b, true );
	Key_Event( b, false );
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void )
{
	IN_DeactivateMouse( );

#if XASH_USE_EVDEV
	Evdev_Shutdown();
#endif

	Touch_Shutdown();

	Cmd_RemoveCommand( "+gs" );
	Cmd_RemoveCommand( "-gs" );
}


/*
===========
IN_Init
===========
*/
void IN_Init( void )
{
	Cvar_RegisterVariable( &cl_forwardspeed );
	Cvar_RegisterVariable( &cl_backspeed );
	Cvar_RegisterVariable( &cl_sidespeed );
	Cvar_RegisterVariable( &bhop_assist );
	Cvar_RegisterVariable( &bhop_ground_dist );
	Cvar_RegisterVariable( &bhop_debug );

	Cmd_AddCommand( "+gs", IN_GroundStrafe_f, "ground strafe, tap duck on landing" );
	Cmd_AddCommand( "-gs", IN_GroundStrafeEnd_f, "stop ground strafe" );

	if( !Host_IsDedicated() )
	{
		IN_StartupMouse( );

		IN_GyroInit();

		OSK_Init();

		Joy_Init(); // common joystick support init

		Touch_Init();

#if XASH_USE_EVDEV
		Evdev_Init();
#endif
	}
}

/*
================
IN_JoyMove

Common function for engine joystick movement

	-1 < forwardmove < 1,	-1 < sidemove < 1

================
*/

#define F (1U << 0)	// Forward
#define B (1U << 1)	// Back
#define L (1U << 2)	// Left
#define R (1U << 3)	// Right
#define T (1U << 4)	// Forward stop
#define S (1U << 5)	// Side stop
static void IN_JoyAppendMove( usercmd_t *cmd, float forwardmove, float sidemove )
{
	static uint moveflags = T | S;

	if( forwardmove ) cmd->forwardmove  = forwardmove * cl_forwardspeed.value;
	if( sidemove ) cmd->sidemove  = sidemove * cl_sidespeed.value;

	if( forwardmove )
	{
		moveflags &= ~T;
	}
	else if( !( moveflags & T ) )
	{
		Cmd_ExecuteString( "-back" );
		Cmd_ExecuteString( "-forward" );
		moveflags |= T;
	}

	if( sidemove )
	{
		moveflags &= ~S;
	}
	else if( !( moveflags & S ) )
	{
		Cmd_ExecuteString( "-moveleft" );
		Cmd_ExecuteString( "-moveright" );
		moveflags |= S;
	}

	if ( forwardmove > 0.7f && !( moveflags & F ))
	{
		moveflags |= F;
		Cmd_ExecuteString( "+forward" );
	}
	else if ( forwardmove < 0.7f && ( moveflags & F ))
	{
		moveflags &= ~F;
		Cmd_ExecuteString( "-forward" );
	}

	if ( forwardmove < -0.7f && !( moveflags & B ))
	{
		moveflags |= B;
		Cmd_ExecuteString( "+back" );
	}
	else if ( forwardmove > -0.7f && ( moveflags & B ))
	{
		moveflags &= ~B;
		Cmd_ExecuteString( "-back" );
	}

	if ( sidemove > 0.9f && !( moveflags & R ))
	{
		moveflags |= R;
		Cmd_ExecuteString( "+moveright" );
	}
	else if ( sidemove < 0.9f && ( moveflags & R ))
	{
		moveflags &= ~R;
		Cmd_ExecuteString( "-moveright" );
	}

	if ( sidemove < -0.9f && !( moveflags & L ))
	{
		moveflags |= L;
		Cmd_ExecuteString( "+moveleft" );
	}
	else if ( sidemove > -0.9f && ( moveflags & L ))
	{
		moveflags &= ~L;
		Cmd_ExecuteString( "-moveleft" );
	}
}

static void IN_CollectInput( float *forward, float *side, float *pitch, float *yaw, qboolean includeMouse )
{
	if( includeMouse )
	{
		float x, y;
		Platform_MouseMove( &x, &y );
		*pitch += y * m_pitch.value;
		*yaw   -= x * m_yaw.value;

#if XASH_USE_EVDEV
		IN_EvdevMove( yaw, pitch );
#endif
	}

	IN_GyroFinalizeMove( forward, side, pitch, yaw );
	Joy_FinalizeMove( forward, side, pitch, yaw );
	Touch_GetMove( forward, side, pitch, yaw );

	if( look_filter.value )
	{
		*pitch = ( inputstate.lastpitch + *pitch ) / 2;
		*yaw   = ( inputstate.lastyaw   + *yaw ) / 2;
		inputstate.lastpitch = *pitch;
		inputstate.lastyaw   = *yaw;
	}

}

/*
================
IN_FootOffset

How far the local player's feet are below the origin, in the hull they are
currently using.

Picked by measuring rather than by index, because the hull numbering here is
not GoldSrc's. This engine fills hull 0 with the standing box and hull 1 with
the ducking one and leaves 2 and 3 zeroed, so the old hardcoded 2/3 read a
zero vector, bottom came out 0, and IN_GroundDistance bailed out with -1 on
every frame - which is "no floor", so neither assist ever ran. The engine log
shows it plainly:

  CL: hull0, player_mins: -16 -16 -36
  CL: hull1, player_mins: -16 -16 -18
  CL: hull2, player_mins: 0 0 0

The standing hull is the tallest one the game described and the ducking hull
the shallowest, so measure both rather than trusting a constant. Returns <= 0
when the game has described nothing usable.
*/
static float IN_FootOffset( qboolean ducking )
{
	float best = 0.0f;
	int hull;

	for( hull = 0; hull < MAX_MAP_HULLS; hull++ )
	{
		const float bottom = -host.player_mins[hull][2];
		const float top = host.player_maxs[hull][2];

		// Skip hulls the game never filled in, and anything that is not
		// shaped like a player box.
		if( bottom <= 1.0f || bottom >= MAX_GROUND_DIST )
			continue;
		if( top <= 0.0f || host.player_maxs[hull][0] <= 0.0f )
			continue;

		if( ducking )
		{
			if( best <= 0.0f || bottom < best )
				best = bottom;
		}
		else if( bottom > best )
		{
			best = bottom;
		}
	}

	return best;
}

/*
=============
IN_GroundDistance

How far the floor is below the local player's feet, traced straight down.
Returns -1 when no floor was found within MAX_GROUND_DIST.

The trace is a ray (hull 0), not the box CL_TraceLine() uses. The box is 32
units wide and axis aligned, so on a slope its corners meet the surface before
its centre does: the trace either stops short of the feet or reports the start
already solid, and IN_GroundDistance answered -1. -1 means "no floor", so
IN_IsOnGround said airborne and +gs never fired on anything but flat ground. A
ray only ever has to meet the floor directly underneath, which is what standing
on a slope actually is.

The foot offset comes from the hull the game reported rather than a hardcoded
36/18 - see IN_FootOffset for why the index GoldSrc uses does not exist here.
The trace starts a unit above the feet so the result reads 0 exactly on the
floor, which is what IN_IsOnGround and the bhop release height key off.

This is the primary measure rather than cl.local.onground. That is written by
the prediction pass, which runs *after* this hook, so at high tickrates there
are frames with nothing to predict and it reads back as -1 (airborne) even
while standing still. It is consulted as a second opinion below, never on its
own: a predicted "on the ground" with no floor in reach is ignored.
===============
*/
#define MAX_GROUND_DIST 64.0f

static float IN_GroundDistance( qboolean ducking )
{
	vec3_t start, end;

	const float bottom = IN_FootOffset( ducking );

	if( bottom <= 0.0f || bottom >= MAX_GROUND_DIST )
		return -1.0f;

	VectorCopy( cl.simorg, start );
	start[2] -= bottom - 1.0f;
	VectorCopy( start, end );
	end[2] -= MAX_GROUND_DIST;

	// -1 for ignore_pe matches CL_TraceLine(); PM_STUDIO_IGNORE is what keeps
	// the local player from being reported as its own floor.
	const pmtrace_t *tr = clgame.pmove->PM_TraceLine( start, end, PM_STUDIO_IGNORE, 0, -1 );

	if( !tr || tr->fraction >= 1.0f || tr->allsolid || tr->startsolid )
		return -1.0f;

	// distance from the feet to the floor
	return tr->fraction * MAX_GROUND_DIST - 1.0f;
}

// close enough to the floor that the next step down would put us on it
static qboolean IN_IsOnGround( float ground_dist )
{
	if( ground_dist >= 0.0f && ground_dist <= 1.0f )
		return true;

	// A slope can leave the feet a little clear of the surface for a frame
	// while the trace says airborne, and the predicted flag already has them
	// down. It only ever breaks the tie, never invents ground the trace did
	// not see.
	return ground_dist < 0.0f && cl.local.onground != -1 && cl.local.waterlevel < 2;
}

/*
================
IN_GroundStrafe

"+gs" taps duck for a single move when the player lands, which is the timing
GoldSrc wants to get the best acceleration out of a jump. Pressing it while
already airborne only arms it, the first landing afterwards is left alone.
================
*/
static void IN_GroundStrafe( usercmd_t *cmd, float ground_dist )
{
	const qboolean on_ground = IN_IsOnGround( ground_dist );

	if( !cmd || !gs_state.enabled )
	{
		// let the state follow the ground while the feature is off, so that
		// pressing +gs does not look like a landing
		gs_state.was_on_ground = on_ground;
		gs_state.was_enabled = false;
		gs_state.armed = false;
		gs_state.release_duck = false;
		return;
	}

	// the very first move after +gs was pressed
	const qboolean activated_this_move = !gs_state.was_enabled;

	if( !gs_state.was_enabled )
	{
		// ignore mid-air activation, only arm immediately if +gs was pressed on ground
		gs_state.armed = on_ground;
	}

	if( gs_state.release_duck )
	{
		// Only one command per tick actually reaches the server, and it is
		// whichever one was generated last before the send. At high frame
		// rates a one-move tap is therefore almost always thrown away before
		// it is ever transmitted, which is why +gs appeared to do nothing.
		// Hold duck until the command we stamped has gone out.
		if( cls.netchan.outgoing_sequence != gs_state.duck_cmd_seq )
		{
			cmd->buttons &= ~IN_DUCK;
			gs_state.release_duck = false;
		}
		else
		{
			cmd->buttons |= IN_DUCK;
		}
	}

	if( activated_this_move && on_ground )
	{
		cmd->buttons |= IN_DUCK;
		gs_state.release_duck = true;
		gs_state.duck_cmd_seq = cls.netchan.outgoing_sequence;
		gs_state.armed = true;
	}
	else if( on_ground && !gs_state.was_on_ground )
	{
		if( gs_state.armed )
		{
			cmd->buttons |= IN_DUCK;
			gs_state.release_duck = true;
			gs_state.duck_cmd_seq = cls.netchan.outgoing_sequence;
		}
		else
		{
			// first landing after enabling in air should not trigger the tap
			gs_state.armed = true;
		}
	}
	else if( on_ground )
	{
		gs_state.armed = true;
	}
	else if( !gs_state.release_duck )
	{
		cmd->buttons &= ~IN_DUCK;
	}

	gs_state.was_enabled = true;
	gs_state.was_on_ground = on_ground;
}

/*
================
IN_BunnyHop

Hold jump while airborne, but let go of it once we are high enough above the
floor, so the next jump is timed on landing instead of floating.
================
*/
static void IN_BunnyHop( usercmd_t *cmd, float ground_dist )
{
	if( !cmd || !(cmd->buttons & IN_JUMP) )
		return;

	if( IN_IsOnGround( ground_dist ))
		return;

	// nothing within reach of the floor, no idea how high we are - don't touch
	// jump, guessing here is what makes the player unable to jump at all
	if( ground_dist < 0.0f )
		return;

	const qboolean assist = bhop_assist.value >= 0.5f;
	float limit = bhop_ground_dist.value;

	if( limit < 4.0f ) limit = 4.0f;
	if( limit > MAX_GROUND_DIST ) limit = MAX_GROUND_DIST;

	if( assist && ground_dist <= limit )
		return;

	// kept identical to the client implementation, which clears jump in both
	// the assist and the no-assist branch
	cmd->buttons &= ~IN_JUMP;
}

static void IN_GroundStrafe_f( void )
{
	gs_state.enabled = true;
}

static void IN_GroundStrafeEnd_f( void )
{
	gs_state.enabled = false;
}

/*
================
IN_EngineAppendMove

Called from cl_main.c after generating command in client
================
*/
void IN_EngineAppendMove( float frametime, usercmd_t *cmd, qboolean active )
{
	if( cls.key_dest != key_game || cl.paused || cl.intermission )
		return;

	if( active )
	{
		// A client that exports LookEvent owns the view angles and the move
		// axes: IN_Commands already handed it this frame's input, and its own
		// CL_CreateMove has already run by the time we get here, so adding ours
		// would apply all of it twice. The assists below read none of that -
		// they only toggle duck and jump on the command and trace the world -
		// so they are not skipped with it. They used to be, which is why +gs
		// and the bunny hop did nothing on any client built that way.
		if( !clgame.dllFuncs.pfnLookEvent )
		{
			float forward = 0, side = 0, pitch = 0, yaw = 0;
			float sensitivity = 1;//( (float)cl.local.scr_fov / (float)90.0f );

			IN_CollectInput( &forward, &side, &pitch, &yaw, false );

			IN_JoyAppendMove( cmd, forward, side );

			if( pitch || yaw )
			{
				cmd->viewangles[YAW]   += yaw * sensitivity;
				cmd->viewangles[PITCH] += pitch * sensitivity;
				cmd->viewangles[PITCH] = bound( -90, cmd->viewangles[PITCH], 90 );
				VectorCopy( cmd->viewangles, cl.viewangles );
			}
		}

		// one trace per move, shared by both assists. The duck state comes
		// from the command the client built, i.e. before GroundStrafe
		// touches it, so the trace starts at the right height.
		// Swimming is left alone: with no floor in reach the trace says
		// nothing either, but in shallow water it would steal +jump.
		if( cl.local.waterlevel < 2 )
		{
			const float ground_dist = IN_GroundDistance( cmd->buttons & IN_DUCK );

			IN_GroundStrafe( cmd, ground_dist );
			IN_BunnyHop( cmd, ground_dist );

			if( bhop_debug.value > 0.5f )
			{
				static double next_report;
				if( host.realtime >= next_report )
				{
					next_report = host.realtime + 1.0;
					Con_Printf( "bhop: dist=%.1f ground=%d pred_onground=%d jump=%d duck=%d gs=%d armed=%d\n",
						ground_dist, IN_IsOnGround( ground_dist ), cl.local.onground != -1,
						!!(cmd->buttons & IN_JUMP), !!(cmd->buttons & IN_DUCK),
						gs_state.enabled, gs_state.armed );
				}
			}
		}
	}
	else
	{
		// dropped out of the game, forget the +gs state so the next spawn
		// doesn't look like a landing
		gs_state.was_enabled = false;
		gs_state.armed = false;
		gs_state.release_duck = false;
		gs_state.was_on_ground = false;
	}
}

static void IN_Commands( void )
{
#if XASH_USE_EVDEV
	IN_EvdevFrame();
#endif

	if( clgame.dllFuncs.pfnLookEvent )
	{
		float forward = 0, side = 0, pitch = 0, yaw = 0;

		IN_CollectInput( &forward, &side, &pitch, &yaw, in_mouseinitialized && !m_ignore.value );

		if( cls.key_dest == key_game )
		{
			clgame.dllFuncs.pfnLookEvent( yaw, pitch );
			clgame.dllFuncs.pfnMoveEvent( forward, side );
		}
	}

	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( in_mouseactive );
}

/*
==================
Host_InputFrame

Called every frame, even if not generating commands
==================
*/
void Host_InputFrame( void )
{
	IN_Commands();

	IN_MouseMove();
}
