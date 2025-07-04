/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <jni.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "android_native_app_glue.h"
#include <android/log.h>
#include <android/native_activity.h>

struct android_app *gapp;

#define LOGI( ... ) __android_log_print(ANDROID_LOG_INFO, "NativeActivity", __VA_ARGS__)
#define LOGE( ... ) __android_log_print(ANDROID_LOG_ERROR, "NativeActivity", __VA_ARGS__)

/* For debug builds, always enable the debug traces in this library */

#ifndef NDEBUG
#  define LOGV( ... ) __android_log_print(ANDROID_LOG_VERBOSE, "NativeActivity", __VA_ARGS__)
#else
#  define LOGV(...)  ((void)0)
#endif


typedef struct __attribute__((packed))
{
	void (*callback)( void * );

	void *opaque;
} MainThreadCallbackProps;

static int pfd[2];
pthread_t debug_capture_thread;

static void *debug_capture_thread_fn( void *v )
{
	//struct android_app * app = (struct android_app*)v;
	ssize_t readSize;
	char buf[2048];

	while ((readSize = read( pfd[ 0 ], buf, sizeof buf - 1 )) > 0 )
	{
		if ( buf[ readSize - 1 ] == '\n' )
		{
			--readSize;
		}
		buf[ readSize ] = 0;  // add null-terminator
		__android_log_write( ANDROID_LOG_DEBUG, "steamlinkvr", buf ); // Set any log level you want
#ifdef RDALOGFNCB
		extern void RDALOGFNCB( int size, char * buf );
		RDALOGFNCB( readSize, buf );
#endif
		//if( debug_capture_hook_function ) debug_capture_hook_function( readSize, buf );
	}
	return 0;
}

static void free_saved_state( struct android_app *android_app )
{
	pthread_mutex_lock( &android_app->mutex );
	if ( android_app->savedState != NULL )
	{
		free( android_app->savedState );
		android_app->savedState = NULL;
		android_app->savedStateSize = 0;
	}
	pthread_mutex_unlock( &android_app->mutex );
}

int8_t android_app_read_cmd( struct android_app *android_app )
{
	int8_t cmd;
	if ( read( android_app->msgread, &cmd, sizeof(cmd)) == sizeof(cmd))
	{
		switch ( cmd )
		{
			case APP_CMD_SAVE_STATE: free_saved_state( android_app );
				break;
		}
		return cmd;
	}
	else
	{
		LOGE( "No data on command pipe!" );
	}
	return -1;
}

static void print_cur_config( struct android_app *android_app )
{
	//For additional debugging this can be enabled, but for now - no need for the extra space.
/*
    char lang[2], country[2];
    AConfiguration_getLanguage(android_app->config, lang);
    AConfiguration_getCountry(android_app->config, country);

    LOGV("Config: mcc=%d mnc=%d lang=%c%c cnt=%c%c orien=%d touch=%d dens=%d "
            "keys=%d nav=%d keysHid=%d navHid=%d sdk=%d size=%d long=%d "
            "modetype=%d modenight=%d",
            AConfiguration_getMcc(android_app->config),
            AConfiguration_getMnc(android_app->config),
            lang[0], lang[1], country[0], country[1],
            AConfiguration_getOrientation(android_app->config),
            AConfiguration_getTouchscreen(android_app->config),
            AConfiguration_getDensity(android_app->config),
            AConfiguration_getKeyboard(android_app->config),
            AConfiguration_getNavigation(android_app->config),
            AConfiguration_getKeysHidden(android_app->config),
            AConfiguration_getNavHidden(android_app->config),
            AConfiguration_getSdkVersion(android_app->config),
            AConfiguration_getScreenSize(android_app->config),
            AConfiguration_getScreenLong(android_app->config),
            AConfiguration_getUiModeType(android_app->config),
            AConfiguration_getUiModeNight(android_app->config));
*/
}

void android_app_pre_exec_cmd( struct android_app *android_app, int8_t cmd )
{
	switch ( cmd )
	{
		case APP_CMD_INPUT_CHANGED:
		{
			LOGV( "[AppGlue] APP_CMD_INPUT_CHANGED\n" );
			pthread_mutex_lock( &android_app->mutex );
			if ( android_app->inputQueue != NULL )
			{
				AInputQueue_detachLooper( android_app->inputQueue );
			}
			android_app->inputQueue = android_app->pendingInputQueue;
			if ( android_app->inputQueue != NULL )
			{
				LOGV( "[AppGlue] Attaching input queue to looper" );
				AInputQueue_attachLooper( android_app->inputQueue,
										  android_app->looper, LOOPER_ID_INPUT, NULL,
										  &android_app->inputPollSource );
			}
			pthread_cond_broadcast( &android_app->cond );
			pthread_mutex_unlock( &android_app->mutex );

			break;
		}

		case APP_CMD_INIT_WINDOW:
		{
			LOGV( "[AppGlue] APP_CMD_INIT_WINDOW\n" );
			pthread_mutex_lock( &android_app->mutex );
			android_app->window = android_app->pendingWindow;
			pthread_cond_broadcast( &android_app->cond );
			pthread_mutex_unlock( &android_app->mutex );
			break;
		}

		case APP_CMD_TERM_WINDOW:
		{
			LOGV( "[AppGlue] APP_CMD_TERM_WINDOW\n" );
			pthread_cond_broadcast( &android_app->cond );
			break;

		}

		case APP_CMD_RESUME:
		case APP_CMD_START:
		case APP_CMD_PAUSE:
		case APP_CMD_STOP:
		{
			LOGV( "[AppGlue] activityState=%d\n", cmd );
			pthread_mutex_lock( &android_app->mutex );
			android_app->activityState = cmd;
			pthread_cond_broadcast( &android_app->cond );
			pthread_mutex_unlock( &android_app->mutex );
			break;
		}

		case APP_CMD_CONFIG_CHANGED:
		{
			LOGV( "[AppGlue] APP_CMD_CONFIG_CHANGED\n" );
			AConfiguration_fromAssetManager( android_app->config,
											 android_app->activity->assetManager );
			print_cur_config( android_app );
			break;
		}

		case APP_CMD_DESTROY:
		{
			LOGV( "[AppGlue] APP_CMD_DESTROY\n" );
			android_app->destroyRequested = 1;

			break;
		}
	}
}

void android_app_post_exec_cmd( struct android_app *android_app, int8_t cmd )
{
	switch ( cmd )
	{
		case APP_CMD_TERM_WINDOW: LOGV( "[AppGlue] APP_CMD_TERM_WINDOW\n" );
			pthread_mutex_lock( &android_app->mutex );
			android_app->window = NULL;
			pthread_cond_broadcast( &android_app->cond );
			pthread_mutex_unlock( &android_app->mutex );
			break;

		case APP_CMD_SAVE_STATE: LOGV( "[AppGlue] APP_CMD_SAVE_STATE\n" );
			pthread_mutex_lock( &android_app->mutex );
			android_app->stateSaved = 1;
			pthread_cond_broadcast( &android_app->cond );
			pthread_mutex_unlock( &android_app->mutex );
			break;

		case APP_CMD_RESUME: free_saved_state( android_app );
			break;
	}
}

void app_dummy()
{

}

static void android_app_destroy( struct android_app *android_app )
{
	LOGV( "[AppGlue] android_app_destroy!\n" );
	free_saved_state( android_app );
	pthread_mutex_lock( &android_app->mutex );
	if ( android_app->inputQueue != NULL )
	{
		AInputQueue_detachLooper( android_app->inputQueue );
	}
	AConfiguration_delete( android_app->config );

	delete android_app->uiThreadCallbackHandler;

	android_app->destroyed = 1;
	pthread_cond_broadcast( &android_app->cond );
	pthread_mutex_unlock( &android_app->mutex );
	// Can't touch android_app object after this.
}

static void process_input( struct android_app *app, struct android_poll_source *source )
{
	AInputEvent *event = NULL;
	while ( AInputQueue_getEvent( app->inputQueue, &event ) >= 0 )
	{
		LOGV( "[AppGlue] New input event: type=%d\n", AInputEvent_getType( event ));
		if ( AInputQueue_preDispatchEvent( app->inputQueue, event ))
		{
			continue;
		}
		int32_t handled = 0;
		if ( app->onInputEvent != NULL )
		{
			handled = app->onInputEvent( app, event );
		}
		AInputQueue_finishEvent( app->inputQueue, event, handled );
	}
}

static void process_cmd( struct android_app *app, struct android_poll_source *source )
{
	int8_t cmd = android_app_read_cmd( app );
	android_app_pre_exec_cmd( app, cmd );
	if ( app->onAppCmd != NULL )
	{
		app->onAppCmd( app, cmd );
	}
	android_app_post_exec_cmd( app, cmd );
}

static void *android_app_entry( void *param )
{
	struct android_app *android_app = (struct android_app *) param;

	android_app->config = AConfiguration_new();
	AConfiguration_fromAssetManager( android_app->config, android_app->activity->assetManager );

	print_cur_config( android_app );
	android_app->cmdPollSource.id = LOOPER_ID_MAIN;
	android_app->cmdPollSource.app = android_app;
	android_app->cmdPollSource.process = process_cmd;
	android_app->inputPollSource.id = LOOPER_ID_INPUT;
	android_app->inputPollSource.app = android_app;
	android_app->inputPollSource.process = process_input;

	ALooper *looper = ALooper_prepare( ALOOPER_PREPARE_ALLOW_NON_CALLBACKS );
	ALooper_addFd( looper, android_app->msgread, LOOPER_ID_MAIN, ALOOPER_EVENT_INPUT, NULL,
				   &android_app->cmdPollSource );
	android_app->looper = looper;

	pthread_mutex_lock( &android_app->mutex );
	android_app->running = 1;
	pthread_cond_broadcast( &android_app->cond );
	pthread_mutex_unlock( &android_app->mutex );

	android_main( android_app );

	android_app_destroy( android_app );

	LOGV( "[AppGlue] native activity completed.\n" );
	return NULL;
}

// --------------------------------------------------------------------
// Native activity interaction (called from main thread)
// --------------------------------------------------------------------

static struct android_app *android_app_create( ANativeActivity *activity,
											   void *savedState, size_t savedStateSize )
{
	struct android_app *android_app = (struct android_app *) malloc( sizeof( struct android_app ));
	memset( android_app, 0, sizeof( struct android_app ));
	android_app->activity = activity;

	pthread_mutex_init( &android_app->mutex, NULL );
	pthread_cond_init( &android_app->cond, NULL );

	pthread_attr_t attr;
	pthread_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

	//Capture input
	setvbuf( stdout, 0, _IOLBF, 0 ); // make stdout line-buffered
	setvbuf( stderr, 0, _IONBF, 0 ); // make stderr unbuffered
	pipe( pfd );
	dup2( pfd[ 1 ], 1 );
	dup2( pfd[ 1 ], 2 );
	pthread_create( &debug_capture_thread, &attr, debug_capture_thread_fn, android_app );

	if ( savedState != NULL )
	{
		android_app->savedState = malloc( savedStateSize );
		android_app->savedStateSize = savedStateSize;
		memcpy( android_app->savedState, savedState, savedStateSize );
	}

	int msgpipe[2];
	if ( pipe( msgpipe ))
	{
		LOGE( "[AppGlue] could not create pipe: %s", strerror( errno ));
		return NULL;
	}
	android_app->msgread = msgpipe[ 0 ];
	android_app->msgwrite = msgpipe[ 1 ];

	android_app->uiThreadCallbackHandler = UIThreadCallbackHandler::AttachForCurrentThread();

	pthread_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	pthread_create( &android_app->thread, &attr, android_app_entry, android_app );

	// Wait for thread to start.
	pthread_mutex_lock( &android_app->mutex );
	while ( !android_app->running && !android_app->destroyed )
	{
		pthread_cond_wait( &android_app->cond, &android_app->mutex );
	}
	pthread_mutex_unlock( &android_app->mutex );

	return android_app;
}

static void android_app_write_cmd( struct android_app *android_app, int8_t cmd )
{
	if ( write( android_app->msgwrite, &cmd, sizeof(cmd)) != sizeof(cmd))
	{
		LOGE( "[AppGlue] Failure writing android_app cmd: %s\n", strerror( errno ));
	}
}

static void android_app_set_input( struct android_app *android_app, AInputQueue *inputQueue )
{
	pthread_mutex_lock( &android_app->mutex );
	android_app->pendingInputQueue = inputQueue;
	android_app_write_cmd( android_app, APP_CMD_INPUT_CHANGED );
	while ( android_app->inputQueue != android_app->pendingInputQueue && !android_app->destroyed )
	{
		pthread_cond_wait( &android_app->cond, &android_app->mutex );
	}
	pthread_mutex_unlock( &android_app->mutex );
}

static void android_app_set_window( struct android_app *android_app, ANativeWindow *window )
{
	pthread_mutex_lock( &android_app->mutex );
	if ( android_app->pendingWindow != NULL )
	{
		android_app_write_cmd( android_app, APP_CMD_TERM_WINDOW );
	}
	android_app->pendingWindow = window;
	if ( window != NULL )
	{
		android_app_write_cmd( android_app, APP_CMD_INIT_WINDOW );
	}
	while ( android_app->window != android_app->pendingWindow && !android_app->destroyed )
	{
		pthread_cond_wait( &android_app->cond, &android_app->mutex );
	}
	pthread_mutex_unlock( &android_app->mutex );
}

static void android_app_set_activity_state( struct android_app *android_app, int8_t cmd )
{
	pthread_mutex_lock( &android_app->mutex );
	android_app_write_cmd( android_app, cmd );
	while ( android_app->activityState != cmd && !android_app->destroyed )
	{
		pthread_cond_wait( &android_app->cond, &android_app->mutex );
	}
	pthread_mutex_unlock( &android_app->mutex );
}

static void android_app_free( struct android_app *android_app )
{
	pthread_mutex_lock( &android_app->mutex );
	android_app_write_cmd( android_app, APP_CMD_DESTROY );
	while ( !android_app->destroyed )
	{
		pthread_cond_wait( &android_app->cond, &android_app->mutex );
	}
	pthread_mutex_unlock( &android_app->mutex );

	close( android_app->msgread );
	close( android_app->msgwrite );
	pthread_cond_destroy( &android_app->cond );
	pthread_mutex_destroy( &android_app->mutex );
	free( android_app );
}

static void onDestroy( ANativeActivity *activity )
{
	LOGV( "[AppGlue] Destroy: %p\n", activity );
	android_app_free((struct android_app *) activity->instance );
}

static void onStart( ANativeActivity *activity )
{
	LOGV( "[AppGlue] Start: %p\n", activity );
	android_app_set_activity_state((struct android_app *) activity->instance, APP_CMD_START );
}

static void onResume( ANativeActivity *activity )
{
	LOGV( "[AppGlue] Resume: %p\n", activity );
	android_app_set_activity_state((struct android_app *) activity->instance, APP_CMD_RESUME );
}

static void *onSaveInstanceState( ANativeActivity *activity, size_t *outLen )
{
	struct android_app *android_app = (struct android_app *) activity->instance;
	void *savedState = NULL;

	LOGV( "[AppGlue] SaveInstanceState: %p\n", activity );
	pthread_mutex_lock( &android_app->mutex );
	android_app->stateSaved = 0;
	android_app_write_cmd( android_app, APP_CMD_SAVE_STATE );
	while ( !android_app->stateSaved && !android_app->destroyed )
	{
		pthread_cond_wait( &android_app->cond, &android_app->mutex );
	}

	if ( android_app->savedState != NULL )
	{
		savedState = android_app->savedState;
		*outLen = android_app->savedStateSize;
		android_app->savedState = NULL;
		android_app->savedStateSize = 0;
	}

	pthread_mutex_unlock( &android_app->mutex );

	return savedState;
}

static void onPause( ANativeActivity *activity )
{
	LOGV( "[AppGlue] Pause: %p\n", activity );
	android_app_set_activity_state((struct android_app *) activity->instance, APP_CMD_PAUSE );
}

static void onStop( ANativeActivity *activity )
{
	LOGV( "[AppGlue] Stop: %p\n", activity );
	android_app_set_activity_state((struct android_app *) activity->instance, APP_CMD_STOP );
}

static void onConfigurationChanged( ANativeActivity *activity )
{
	struct android_app *android_app = (struct android_app *) activity->instance;
	LOGV( "[AppGlue] ConfigurationChanged: %p\n", activity );
	android_app_write_cmd( android_app, APP_CMD_CONFIG_CHANGED );
}

static void onLowMemory( ANativeActivity *activity )
{
	struct android_app *android_app = (struct android_app *) activity->instance;
	LOGV( "[AppGlue] LowMemory: %p\n", activity );
	android_app_write_cmd( android_app, APP_CMD_LOW_MEMORY );
}

static void onWindowFocusChanged( ANativeActivity *activity, int focused )
{
	LOGV( "[AppGlue] WindowFocusChanged: %p -- %d\n", activity, focused );
	android_app_write_cmd((struct android_app *) activity->instance,
						  focused ? APP_CMD_GAINED_FOCUS : APP_CMD_LOST_FOCUS );
}

static void onNativeWindowCreated( ANativeActivity *activity, ANativeWindow *window )
{
	LOGV( "[AppGlue] NativeWindowCreated: %p -- %p\n", activity, window );
	android_app_set_window((struct android_app *) activity->instance, window );
}

static void onNativeWindowDestroyed( ANativeActivity *activity, ANativeWindow *window )
{
	LOGV( "[AppGlue] NativeWindowDestroyed: %p -- %p\n", activity, window );
	android_app_set_window((struct android_app *) activity->instance, NULL );
}

static void onInputQueueCreated( ANativeActivity *activity, AInputQueue *queue )
{
	LOGV( "[AppGlue] InputQueueCreated: %p -- %p\n", activity, queue );
	android_app_set_input((struct android_app *) activity->instance, queue );
}

static void onInputQueueDestroyed( ANativeActivity *activity, AInputQueue *queue )
{
	LOGV( "[AppGlue] InputQueueDestroyed: %p -- %p\n", activity, queue );
	android_app_set_input((struct android_app *) activity->instance, NULL );
}

static void onNativeWindowRedrawNeeded( ANativeActivity *activity, ANativeWindow *window )
{
	LOGV( "[AppGlue] onNativeWindowRedrawNeeded: %p -- %p\n", activity, window );
}

JNIEXPORT
void ANativeActivity_onCreate( ANativeActivity *activity, void *savedState,
							   size_t savedStateSize )
{
	LOGV( "[AppGlue] Creating: %p\n", activity );
	activity->callbacks->onDestroy = onDestroy;
	activity->callbacks->onStart = onStart;
	activity->callbacks->onResume = onResume;
	activity->callbacks->onSaveInstanceState = onSaveInstanceState;
	activity->callbacks->onPause = onPause;
	activity->callbacks->onStop = onStop;
	activity->callbacks->onConfigurationChanged = onConfigurationChanged;
	activity->callbacks->onLowMemory = onLowMemory;
	activity->callbacks->onWindowFocusChanged = onWindowFocusChanged;
	activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
	activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
	activity->callbacks->onInputQueueCreated = onInputQueueCreated;
	activity->callbacks->onInputQueueDestroyed = onInputQueueDestroyed;
	activity->callbacks->onNativeWindowRedrawNeeded = onNativeWindowRedrawNeeded;

	activity->instance = android_app_create( activity, savedState, savedStateSize );
}
