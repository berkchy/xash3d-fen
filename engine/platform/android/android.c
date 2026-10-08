/*
android_nosdl.c - android backend
Copyright (C) 2016-2019 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "platform/platform.h"
#include "input.h"
#include "client.h"
#include "sound.h"
#include "errno.h"
#include <pthread.h>
#include <sys/prctl.h>

#include <android/log.h>
#include <jni.h>
#if XASH_SDL
#include <SDL.h>
#endif // XASH_SDL

struct jnimethods_s
{
	JNIEnv *env;
	jobject activity;
	jclass actcls;
	jmethodID loadAndroidID;
	jmethodID getAndroidID;
	jmethodID saveAndroidID;
	jmethodID showMOTD;
	jmethodID isMOTDDialogActive;
	jmethodID getKeyboardHeightPerMille;
} jni;

/*
========================
Android_ShowMOTD

Render an HTML MOTD in a sandboxed WebView dialog owned by the activity.
The payload goes over as a raw byte array on purpose: a server string that is
not valid modified-UTF8 must never abort NewStringUTF, and Java decodes it with
replacement characters. Returns false when the dialog could not be shown, which
is the client's cue to fall back to its own text renderer.
========================
*/
qboolean Android_ShowMOTD( const char *html )
{
	size_t len;
	jbyteArray jbytes;
	jboolean shown;

	if( !jni.env || !jni.activity || !jni.showMOTD )
		return false;

	len = Q_strlen( html );
	jbytes = (*jni.env)->NewByteArray( jni.env, (jsize)len );

	if( !jbytes )
		return false;

	(*jni.env)->SetByteArrayRegion( jni.env, jbytes, 0, (jsize)len, (const jbyte *)html );
	shown = (*jni.env)->CallBooleanMethod( jni.env, jni.activity, jni.showMOTD, jbytes );
	(*jni.env)->DeleteLocalRef( jni.env, jbytes );

	return shown ? true : false;
}

/*
========================
Android_IsMOTDDialogActive

Whether a MOTD dialog is on screen right now.
========================
*/
qboolean Android_IsMOTDDialogActive( void )
{
	if( !jni.env || !jni.activity || !jni.isMOTDDialogActive )
		return false;

	return (*jni.env)->CallBooleanMethod( jni.env, jni.activity, jni.isMOTDDialogActive ) ? true : false;
}

/*
========================
Android_GetMOTDAPI

The MOTD entry points handed to client dlls through
Sys_GetNativeObject("MOTDAPI"). A named lookup instead of a slot in
cl_enginefunc_t: the client dll copies that struct wholesale, so growing it
breaks every engine APK that is older than the dll.
========================
*/
android_motdapi_t *Android_GetMOTDAPI( void )
{
	static android_motdapi_t motdapi =
	{
		sizeof( android_motdapi_t ),
		CL_ShowMOTD,
		CL_IsMOTDDialogActive
	};

	return &motdapi;
}

/*
========================
Android_GetMethodID

GetMethodID leaves a pending NoSuchMethodError behind when the method is
missing, and every later JNI call then trips ART's assert and aborts the
process. That happens whenever libxash.so is newer than the activity class it
runs in. Clear the exception and report the miss instead.
========================
*/
static jmethodID Android_GetMethodID( const char *name, const char *sig )
{
	jmethodID id;

	if( !jni.env || !jni.actcls )
		return NULL;

	id = (*jni.env)->GetMethodID( jni.env, jni.actcls, name, sig );

	if( (*jni.env)->ExceptionCheck( jni.env ) )
	{
		(*jni.env)->ExceptionDescribe( jni.env );
		(*jni.env)->ExceptionClear( jni.env );
		Con_Printf( S_WARN "activity has no %s%s - that part stays disabled\n", name, sig );
		return NULL;
	}

	return id;
}

void Android_Init( void )
{
	memset( &jni, 0, sizeof( jni ));

#if XASH_SDL
	jni.env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jni.activity = (jobject)SDL_AndroidGetActivity();

	if( !jni.env || !jni.activity )
	{
		Con_Printf( S_ERROR "no JNI environment, platform calls are unavailable\n" );
		return;
	}

	jni.actcls = (*jni.env)->GetObjectClass( jni.env, jni.activity );

	if( !jni.actcls || (*jni.env)->ExceptionCheck( jni.env ) )
	{
		(*jni.env)->ExceptionClear( jni.env );
		Con_Printf( S_ERROR "activity class unavailable, platform calls are disabled\n" );
		return;
	}

	jni.loadAndroidID = Android_GetMethodID( "loadAndroidID", "()Ljava/lang/String;" );
	jni.getAndroidID = Android_GetMethodID( "getAndroidID", "()Ljava/lang/String;" );
	jni.saveAndroidID = Android_GetMethodID( "saveAndroidID", "(Ljava/lang/String;)V" );
	// Optional: an activity without them is still fine, MOTD just stays textual.
	//
	// These names must also be listed in android/app/proguard-rules.pro. R8
	// renames any member missing from that keep rule, and a renamed method is
	// invisible to GetMethodID - the lookup then returns NULL, Android_Init
	// only warns about it, and the HTML MOTD silently stays plain text.
	jni.showMOTD = Android_GetMethodID( "showMOTD", "([B)Z" );
	jni.isMOTDDialogActive = Android_GetMethodID( "isMOTDDialogActive", "()Z" );
	jni.getKeyboardHeightPerMille = Android_GetMethodID( "getKeyboardHeightPerMille", "()I" );
#endif // !XASH_SDL
}

/*
========================
Android_GetKeyboardHeight

How many pixels at the bottom of the framebuffer the soft keyboard covers, 0
when it is hidden or when the activity is too old to report it.

The activity answers in per-mille of its own window height rather than in
pixels: a fullscreen SDL window is never resized for the keyboard, so the
window tells the engine nothing by itself, and the engine may well render at a
different height than the window has pixels.
========================
*/
int Android_GetKeyboardHeight( void )
{
	int		per_mille;

	if( !jni.env || !jni.activity || !jni.getKeyboardHeightPerMille )
		return 0;

	per_mille = (*jni.env)->CallIntMethod( jni.env, jni.activity, jni.getKeyboardHeightPerMille );

	if( (*jni.env)->ExceptionCheck( jni.env ) )
	{
		(*jni.env)->ExceptionClear( jni.env );
		return 0;
	}

	if( per_mille <= 0 || per_mille >= 1000 )
		return 0;

	return refState.height * per_mille / 1000;
}

/*
========================
Android_GetNativeObject
========================
*/

void *Android_GetNativeObject( const char *name )
{
	if( !strcasecmp( name, "JNIEnv" ) )
	{
		return (void *)jni.env;
	}
	else if( !strcasecmp( name, "ActivityClass" ) )
	{
		return (void *)jni.actcls;
	}
	else if( !strcasecmp( name, "MOTDAPI" ) )
	{
		// Present on every platform: the calls themselves report "no dialog"
		// when there is none. An engine without this name gives the client dll
		// NULL and it keeps its text MOTD.
		return Android_GetMOTDAPI();
	}

	return NULL;
}

/*
========================
Android_GetAndroidID
========================
*/
const char *Android_GetAndroidID( void )
{
	static char id[32];

	if( !COM_StringEmpty( id ))
		return id;

	jstring resultJNIStr = (*jni.env)->CallObjectMethod( jni.env, jni.activity, jni.getAndroidID );
	const char *resultCStr = (*jni.env)->GetStringUTFChars( jni.env, resultJNIStr, NULL );
	Q_strncpy( id, resultCStr, sizeof( id ) );
	(*jni.env)->ReleaseStringUTFChars( jni.env, resultJNIStr, resultCStr );
	(*jni.env)->DeleteLocalRef( jni.env, resultJNIStr );

	return id;
}

/*
========================
Android_LoadID
========================
*/
const char *Android_LoadID( void )
{
	static char id[32];
	jstring resultJNIStr = (*jni.env)->CallObjectMethod( jni.env, jni.activity, jni.loadAndroidID );
	const char *resultCStr = (*jni.env)->GetStringUTFChars( jni.env, resultJNIStr, NULL );
	Q_strncpy( id, resultCStr, sizeof( id ) );
	(*jni.env)->ReleaseStringUTFChars( jni.env, resultJNIStr, resultCStr );
	(*jni.env)->DeleteLocalRef( jni.env, resultJNIStr );

	return id;
}

/*
========================
Android_SaveID
========================
*/
void Android_SaveID( const char *id )
{
	jstring JStr = (*jni.env)->NewStringUTF( jni.env, id );
	(*jni.env)->CallVoidMethod( jni.env, jni.activity, jni.saveAndroidID, JStr );
	(*jni.env)->DeleteLocalRef( jni.env, JStr );
}

/*
========================
Android_ShellExecute
========================
*/
void Platform_ShellExecute( const char *path, const char *parms )
{
#if XASH_SDL
	SDL_OpenURL( path );
#endif // XASH_SDL
}
