#include <atomic>
#include <stdexcept>
#include <thread>

#include <android/log.h>
#include "android_native_app_glue.h"
#include <android/native_window.h>
#include <jni.h>

#include <sys/system_properties.h>

#include "main.h"
#include "log.h"
#include "program.h"


struct android_app *gApp;
app_state g_app_state;

void app_handle_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_START: {
            Log("[AndroidActivity] APP_CMD_START");
            gApp = app;

            break;
        }

        case APP_CMD_DESTROY: {
            Log("[AndroidActivity] APP_CMD_DESTROY");
            g_app_state.pNativeWindow = nullptr;

            break;
        }

        case APP_CMD_INIT_WINDOW: {
            Log("[AndroidActivity] APP_CMD_INIT_WINDOW");
            g_app_state.pNativeWindow = app->window;

            break;
        }

        case APP_CMD_TERM_WINDOW: {
            Log("[AndroidActivity] APP_CMD_TERM_WINDOW");
            g_app_state.pNativeWindow = nullptr;

            break;
        }

        case APP_CMD_PAUSE: {
            Log("[AndroidActivity] APP_CMD_PAUSE");
            g_app_state.bAppRunning = false;

            break;
        }

        case APP_CMD_STOP: {
            Log("[AndroidActivity] APP_CMD_STOP");

            break;
        }

        case APP_CMD_RESUME: {
            Log("[AndroidActivity] APP_CMD_RESUME");
            g_app_state.bAppRunning = true;

            break;
        }

        default: {
            Log(LogWarning, "[AndroidActivity] Event not handled: %d", cmd);
            break;
        }
    }
}

void android_main(struct android_app *app) {
    JNIEnv *Env;

    JavaVM *java_vm = app->activity->vm;
    java_vm->AttachCurrentThread(&Env, nullptr);

    gApp = app;

    app->userData = nullptr;
    app->onAppCmd = app_handle_cmd;

    Program program = Program(gApp, &g_app_state);

    if (!program.BInit()) {
        Log(LogError, "[android_main] Failed to initialize openxr program. Aborting.");
        ANativeActivity_finish(app->activity);
        goto finish;
    }

    g_app_state.bAppRunning = true;

    while (app->destroyRequested == 0) {
        while (true) {
            int events;
            struct android_poll_source *source;

            const int n_ms_timeout = (!g_app_state.bAppRunning && app->destroyRequested == 0) ? -1 : 0;
            if (ALooper_pollAll(n_ms_timeout, nullptr, &events, (void **) &source) < 0) {
                break;
            }

            if (source != nullptr) {
                source->process(app, source);
            }

            if (!g_app_state.bAppRunning) {
                ANativeActivity_finish(app->activity);
            }
        }

        program.Tick();
    }

    finish:
    java_vm->DetachCurrentThread();
}