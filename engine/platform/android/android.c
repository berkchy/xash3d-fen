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

void Android_Init( void )
{
	memset( &jni, 0, sizeof( jni ));

#if XASH_SDL
	jni.env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jni.activity = (jobject)SDL_AndroidGetActivity();
	jni.actcls = (*jni.env)->GetObjectClass( jni.env, jni.activity );
	jni.loadAndroidID = (*jni.env)->GetMethodID( jni.env, jni.actcls, "loadAndroidID", "()Ljava/lang/String;" );
	jni.getAndroidID = (*jni.env)->GetMethodID( jni.env, jni.actcls, "getAndroidID", "()Ljava/lang/String;" );
	jni.saveAndroidID = (*jni.env)->GetMethodID( jni.env, jni.actcls, "saveAndroidID", "(Ljava/lang/String;)V" );
	// Optional: an activity without them is still fine, MOTD just stays textual.
	jni.showMOTD = (*jni.env)->GetMethodID( jni.env, jni.actcls, "showMOTD", "([B)Z" );
	jni.isMOTDDialogActive = (*jni.env)->GetMethodID( jni.env, jni.actcls, "isMOTDDialogActive", "()Z" );
#endif // !XASH_SDL
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
