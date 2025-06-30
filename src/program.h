#pragma once

#include "android_native_app_glue.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include "main.h"

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

#include "xrq.h"
#include "xruipanel.h"

class Program {
public:
    Program(android_app *pApp, app_state *pAppState);

    bool BInit();

    void Tick();

    ~Program();

private:
    android_app *m_pApp;
    app_state *m_pAppState;

    XRQContext m_xrqContext;

    XRQSwapchain m_projectionSwapchains[2];
    XRQSwapchain m_quadSwapchain;

    std::unique_ptr<XrUIPanel> m_pUIPanel;
    GLuint m_framebuffer;
    std::array<XrCompositionLayerProjectionView, 2> m_vProjectionViews{};
    XrCompositionLayerProjection m_layerProjection{};

};