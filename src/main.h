#pragma once

#include <atomic>

#include <android/native_window.h>

struct app_state
{
    std::atomic<bool> bAppRunning = false;

    ANativeWindow *pNativeWindow = nullptr;

    int nPxHeight;
    int nPxWidth;
};