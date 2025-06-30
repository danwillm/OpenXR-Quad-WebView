#include <cstdlib>
#include "android.h"

#include "android_native_app_glue.h"

#include "check.h"
#include "log.h"

#include <utility>
#include <vector>

#include <android/asset_manager_jni.h>
#include <android/trace.h>

extern android_app *gApp;

static jobject GetServiceFromSystemService(JNIEnv *env, const char *serviceName) {
    jclass activityClass = env->FindClass("android/content/Context");
    jmethodID MethodGetSystemService = env->GetMethodID(activityClass, "getSystemService",
                                                        "(Ljava/lang/String;)Ljava/lang/Object;");

    jstring wifiServiceName = env->NewStringUTF(serviceName);
    jobject wifiObject = env->CallObjectMethod(gApp->activity->clazz, MethodGetSystemService, wifiServiceName);

    return wifiObject;
}

float GetHmdBatteryLevel() {
    SETUP_FOR_JAVA_CALL

    jobject batteryManagerObject = GetServiceFromSystemService(env, "batterymanager");
    jclass batteryManagerClass = env->FindClass("android/os/BatteryManager");

    jmethodID batteryManager_getIntProperty = env->GetMethodID(batteryManagerClass, "getIntProperty", "(I)I");

    jint batteryLevel = env->CallIntMethod(batteryManagerObject, batteryManager_getIntProperty, 4);

    return (float) batteryLevel / 100.f;
}

bool IsHmdBatteryCharging() {
    SETUP_FOR_JAVA_CALL

    jobject batteryManagerObject = GetServiceFromSystemService(env, "batterymanager");
    jclass batteryManagerClass = env->FindClass("android/os/BatteryManager");

    jmethodID batteryManager_isCharging = env->GetMethodID(batteryManagerClass, "isCharging", "()Z");
    jboolean bIsBatteryCharging = env->CallBooleanMethod(batteryManagerObject, batteryManager_isCharging);

    return bIsBatteryCharging;
}

std::string jstringToString(JNIEnv *env, jstring jStr) {
    if (!jStr) {
        return "";
    }

    const jclass stringClass = env->GetObjectClass(jStr);
    const jmethodID getBytes = env->GetMethodID(stringClass, "getBytes", "(Ljava/lang/String;)[B");
    const jbyteArray stringJbytes = (jbyteArray) env->CallObjectMethod(jStr, getBytes, env->NewStringUTF("UTF-8"));

    size_t length = (size_t) env->GetArrayLength(stringJbytes);
    jbyte *pBytes = env->GetByteArrayElements(stringJbytes, NULL);

    std::string ret = std::string((char *) pBytes, length);
    env->ReleaseByteArrayElements(stringJbytes, pBytes, JNI_ABORT);

    return ret;
}


void FinishAndRemoveTask() {
    SETUP_FOR_JAVA_CALL

    jclass cActivity = env->GetObjectClass(gApp->activity->clazz);
    jmethodID mFinishAndRemoveTask = env->GetMethodID(cActivity, "finishAndRemoveTask", "()V");

    env->CallVoidMethod(cActivity, mFinishAndRemoveTask);
}

void FinishActivity() {
    SETUP_FOR_JAVA_CALL

    jclass cActivity = env->GetObjectClass(gApp->activity->clazz);
    jmethodID mFinishAndRemoveTask = env->GetMethodID(cActivity, "finish", "()V");

    env->CallVoidMethod(cActivity, mFinishAndRemoveTask);
}

bool GetExtrasKey(const std::string &sKey, std::string &sOutValue) {
    SETUP_FOR_JAVA_CALL

    jclass IntentClass = env->FindClass("android/content/Intent");
    jclass ActivityClass = env->GetObjectClass(gApp->activity->clazz);

    jmethodID getIntentMethod = env->GetMethodID(ActivityClass, "getIntent", "()Landroid/content/Intent;");
    jobject intent = env->CallObjectMethod(gApp->activity->clazz, getIntentMethod);

    jmethodID getExtrasMethod = env->GetMethodID(IntentClass, "getExtras", "()Landroid/os/Bundle;");
    jobject extrasBundle = env->CallObjectMethod(intent, getExtrasMethod);

    if (!extrasBundle) {
        Log(LogError, "[Android] No extras bundle was present");
        return false;
    }

    jclass BundleClass = env->FindClass("android/os/Bundle");

    jstring key = env->NewStringUTF(sKey.c_str());

    jmethodID getStringMethod = env->GetMethodID(BundleClass, "getString", "(Ljava/lang/String;)Ljava/lang/String;");
    jstring value = (jstring) env->CallObjectMethod(extrasBundle, getStringMethod, key);

    std::string result = jstringToString(env, value);
    if (!result.empty()) {
        sOutValue = result;
    }

    return true;
}

std::string GetDeviceProductName() {
    SETUP_FOR_JAVA_CALL

    jclass Class_Build = env->FindClass("android/os/Build");
    jfieldID Field_MODEL = env->GetStaticFieldID(Class_Build, "PRODUCT", "Ljava/lang/String;");

    jstring jsModel = (jstring) env->GetStaticObjectField(Class_Build, Field_MODEL);
    return jstringToString(env, jsModel);
}

std::string GetDeviceManufacturerRaw() {
    SETUP_FOR_JAVA_CALL

    jclass Class_Build = env->FindClass("android/os/Build");
    jfieldID Field_MODEL = env->GetStaticFieldID(Class_Build, "MANUFACTURER", "Ljava/lang/String;");

    jstring jsManufacturer = (jstring) env->GetStaticObjectField(Class_Build, Field_MODEL);

    return jstringToString(env, jsManufacturer);
}

EDeviceManufacturer GetDeviceManufacturer() {
    std::string sManufacturer = GetDeviceManufacturerRaw();

    if (sManufacturer == "Oculus" || sManufacturer == "Meta") {
        return DEVICE_MANUFACTURER_META;
    }
    if (sManufacturer == "Pico") {
        return DEVICE_MANUFACTURER_PICO;
    }

    return DEVICE_MANUFACTURER_UNKNOWN;
}

bool BIsWiFiConnected() {
    SETUP_FOR_JAVA_CALL

    jobject connectivityManager = GetServiceFromSystemService(env, "connectivity");
    if (!connectivityManager) {
        Log(LogError, "[Android] BIsWiFiConnected failed to get connectivity manager");
        return false;
    }

    jclass cConnectivityManager = env->FindClass("android/net/ConnectivityManager");
    if (!cConnectivityManager) {
        Log(LogError, "[Android] BIsWiFiConnected failed to get connectivity manager class");
        return false;
    }

    jmethodID mActiveGetNetworkInfo = env->GetMethodID(cConnectivityManager, "getActiveNetwork", "()Landroid/net/Network;");
    if (!mActiveGetNetworkInfo) {
        Log(LogError, "[Android] BIsWiFiConnected failed to method getActiveNetwork");
        return false;
    }

    jobject activeNetwork = env->CallObjectMethod(connectivityManager, mActiveGetNetworkInfo);
    if (!activeNetwork) {
        Log(LogError, "[Android] BIsWiFiConnected failed to Network object");
        return false;
    }

    bool bIsConnected = false;
    if (activeNetwork) {
        bIsConnected = true;
    }

    return bIsConnected;
}

std::string GetCurrentLocale() {
    SETUP_FOR_JAVA_CALL

    jclass cLocale = env->FindClass("java/util/Locale");

    jmethodID mGetDefault = env->GetStaticMethodID(cLocale, "getDefault", "()Ljava/util/Locale;");
    jobject locale = env->CallStaticObjectMethod(cLocale, mGetDefault);

    jmethodID mGetLanguage = env->GetMethodID(cLocale, "getLanguage", "()Ljava/lang/String;");
    jstring jsLocale = (jstring) env->CallObjectMethod(locale, mGetLanguage);

    return jstringToString(env, jsLocale);
}


std::string GetVersionNumber() {
    SETUP_FOR_JAVA_CALL

    jobject activity = gApp->activity->clazz;
    jclass activityClass = env->GetObjectClass(activity);
    jclass PackageManagerClass = env->FindClass("android/content/pm/PackageManager");
    jmethodID gpm = env->GetMethodID(activityClass, "getPackageManager", "()Landroid/content/pm/PackageManager;");
    jobject packman = env->CallObjectMethod(activity, gpm);

    jmethodID getPackageNameID = env->GetMethodID(activityClass, "getPackageName", "()Ljava/lang/String;");
    jobject packageName = env->CallObjectMethod(activity, getPackageNameID);
    jmethodID getPackageInfoID = env->GetMethodID(PackageManagerClass, "getPackageInfo", "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
    jclass PackageInfoClass = env->FindClass("android/content/pm/PackageInfo");
    jobject PackageInfo = env->CallObjectMethod(packman, getPackageInfoID, packageName, 0);
    jfieldID versionNameField = env->GetFieldID(PackageInfoClass, "versionName", "Ljava/lang/String;");
    jfieldID versionCodeField = env->GetFieldID(PackageInfoClass, "versionCode", "I");
    std::string sRet = "Unknown";

    if (versionNameField) {
        jstring PackageName = (jstring) env->GetObjectField(PackageInfo, versionNameField);
        if (PackageName) {
            const char *sApplicationID = env->GetStringUTFChars(PackageName, nullptr);
            sRet = sApplicationID;
            env->ReleaseStringUTFChars(PackageName, sApplicationID);
        }
    }

    if (versionCodeField) {
        int versionCode = env->GetIntField(PackageInfo, versionCodeField);
        char buffername[128] = {0};
        snprintf(buffername, sizeof(buffername) - 1, ".%d", versionCode % 500000);
        sRet += buffername;
    }

    env->ExceptionClear();
    return sRet;
}

int GetDeviceSDKVersion() {
    SETUP_FOR_JAVA_CALL

    jclass Class_BuildVERSION = env->FindClass("android/os/Build$VERSION");
    jfieldID Field_SDK_INT = env->GetStaticFieldID(Class_BuildVERSION, "SDK_INT", "I");

    jint jnSDK = env->GetStaticIntField(Class_BuildVERSION, Field_SDK_INT);
    return jnSDK;
}

TraceRAII::TraceRAII(const std::string &sTraceName) {
    ATrace_beginSection(sTraceName.c_str());
}

TraceRAII::~TraceRAII() {
    ATrace_endSection();
}