#pragma once

#include <jni.h>
#include <sstream>
#include <fstream>
#include <string>
#include <GLES3/gl32.h>

#include <future>
#include <map>

#include <android/asset_manager.h>
#include <sstream>
#include <unistd.h>

#include "android_native_app_glue.h"

#include "glutils.h"

#define SETUP_FOR_JAVA_CALL \
    JNIEnv * env = 0; \
    JNIEnv ** envptr = &env; \
    JavaVM * jniiptr = gApp->activity->vm; \
    jniiptr->AttachCurrentThread( (JNIEnv**)&env, 0 ); \
    env = (*envptr);

enum IntentFlags
{
	FLAG_ACTIVITY_CLEAR_TASK = 32768,
	FLAG_ACTIVITY_NEW_TASK = 268435456,
	FLAG_ACTIVITY_CLEAR_TOP = 67108864,
	FLAG_ACTIVITY_REORDER_TO_FRONT = 131072
};

enum PendingIntentFlags
{
	FLAG_CANCEL_CURRENT = 268435456,
	FLAG_IMMUTABLE = 67108864,
	FLAG_UPDATE_CURRENT = 134217728,
};

enum EDeviceManufacturer : int
{
	DEVICE_MANUFACTURER_UNKNOWN,
	DEVICE_MANUFACTURER_META,
	DEVICE_MANUFACTURER_PICO,
};

float GetHmdBatteryLevel();

bool IsHmdBatteryCharging();

void FinishAndRemoveTask();

void FinishActivity();

bool GetExtrasKey( const std::string &sKey, std::string &sOutValue );

std::string GetDeviceProductName();

bool BIsWiFiConnected();

std::string GetCurrentLocale();

std::string GetVersionNumber();

void DumpObjectClassProperties( jobject objToDump );

int GetDeviceSDKVersion();

std::string GetDeviceManufacturerRaw();
EDeviceManufacturer GetDeviceManufacturer();

class TraceRAII {
public:
    TraceRAII(const std::string &sTraceName);

    ~TraceRAII();
};

#define DO_TRACE(x) \
 TraceRAII ANDROID_TRACE_##x = TraceRAII( #x )