#include "program.h"

#include <array>
#include <unordered_map>
#include <vector>

#include "android_native_app_glue.h"

#include "main.h"

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

#include "log.h"
#include "webview.h"
#include "xruipanel.h"
#include "check.h"

EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;
EGLConfig egl_config;

Program::Program(android_app *pApp, app_state *pAppState) {

}

bool Program::BInit() {

    EGLint egl_major, egl_minor;

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        Log(LogError, "InitOGLESContext: No display found!");
        return false;
    }

    if (!eglInitialize(egl_display, &egl_major, &egl_minor)) {
        Log(LogError, "InitOGLESContext: eglInitialize failed!");
        return false;
    }

    Log("InitOGLESContext: EGL Version: %s", eglQueryString(egl_display, EGL_VERSION));
    Log("InitOGLESContext: EGL Vendor: %s", eglQueryString(egl_display, EGL_VENDOR));
    Log("InitOGLESContext: EGL Extensions: %s", eglQueryString(egl_display, EGL_EXTENSIONS));

    //don't use eglChooseConfig! EGL code pushes in multisample flags, wasted on the time warped frontbuffer
    enum {
        MAX_CONFIGS = 1024
    };

#define EGL_ZBITS 16
    static EGLint const config_attribute_list[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, EGL_ZBITS,
            EGL_NONE
    };

    static const EGLint context_attribute_list[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };

    static EGLint window_attribute_list[] = {
            EGL_NONE
    };

    EGLConfig configs[MAX_CONFIGS];
    EGLint numConfigs = 0;
    if (!eglGetConfigs(egl_display, configs, MAX_CONFIGS, &numConfigs)) {
        Log(LogError, "InitOGLESContext: eglGetConfigs failed!");
        return false;
    }

    for (int i = 0; i < numConfigs; i++) {
        EGLint value = 0;

        eglGetConfigAttrib(egl_display, configs[i], EGL_RENDERABLE_TYPE, &value);
        if ((value & EGL_OPENGL_ES3_BIT) != EGL_OPENGL_ES3_BIT) {
            continue;
        }

        // Without EGL_KHR_surfaceless_context, the config needs to support both pbuffers and window surfaces.
        eglGetConfigAttrib(egl_display, configs[i], EGL_SURFACE_TYPE, &value);
        if ((value & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) != (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) {
            continue;
        }

        int j = 0;
        for (; config_attribute_list[j] != EGL_NONE; j += 2) {
            eglGetConfigAttrib(egl_display, configs[i], config_attribute_list[j], &value);
            if (value != config_attribute_list[j + 1]) {
                break;
            }
        }

        if (config_attribute_list[j] == EGL_NONE) {
            egl_config = configs[i];
            break;
        }
    }

    if (egl_config == 0) {
        Log(LogError, "InitOGLESContext: Failed to find egl config!");
        return false;
    }

    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribute_list);
    if (egl_context == EGL_NO_CONTEXT) {
        Log(LogError, "eglCreateContext failed: 0x%08X", eglGetError());
        return false;
    }

    Log("InitOGLESContext: Created context %p", egl_context);

    const EGLint surfaceAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    egl_surface = eglCreatePbufferSurface(egl_display, egl_config, surfaceAttribs);
    if (egl_surface == EGL_NO_SURFACE) {
        Log(LogError, "eglCreatePbufferSurface failed: 0x%08X", eglGetError());

        eglDestroyContext(egl_display, egl_context);
        egl_context = EGL_NO_CONTEXT;

        return false;
    }

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        Log(LogError, "InitOGLESContext: eglMakeCurrent() failed: 0x%08X", eglGetError());
        return false;
    }

    Log("GL Vendor: \"%s\"\n", glGetString(GL_VENDOR));
    Log("GL Renderer: \"%s\"\n", glGetString(GL_RENDERER));
    Log("GL Version: \"%s\"\n", glGetString(GL_VERSION));
    Log("GL Extensions: \"%s\"\n", glGetString(GL_EXTENSIONS));

    XRQApp app = {
            .sAppName = "test",
            .unAppVersion = 1,
            .sEngineName = "danwillm",
            .unEngineVersion = 1,

            .requestedExtensions = {},
    };

    if (!XRQCreateXRInstance(app, m_xrqContext)) {
        Log(LogError, "[XrProgram] Failed to create instance");
        return false;
    }

    if (!XRQCreateXRSession(m_xrqContext)) {
        Log(LogError, "[XrProgram] Failed to create session!");
        return false;
    }

    if (!XRQSetReferencePlaySpace(m_xrqContext, XR_REFERENCE_SPACE_TYPE_STAGE)) {
        Log(LogError, "[XrProgram] Failed to set play space");

        return false;
    }

    for (int i = 0; i < 2; i++) {
        XRQSwapchainInfo projectionSwapchainInfo = {
                .width = m_xrqContext.vViewConfigViews[i].recommendedImageRectWidth,
                .height = m_xrqContext.vViewConfigViews[i].recommendedImageRectHeight,
                .recommendedFormat = GL_SRGB8_ALPHA8,
                .sampleCount = m_xrqContext.vViewConfigViews[i].recommendedSwapchainSampleCount,
        };
        if (!XRQCreateSwapchain(m_xrqContext, projectionSwapchainInfo, m_projectionSwapchains[i])) {
            Log(LogError, "[XrProgram] Failed to create projection swapchain for eye: %i", i);
            return false;
        }

        XRQCreateProjectionViewLayer(m_xrqContext, m_projectionSwapchains[i],
                                     m_vProjectionViews[i]);
    }

    std::shared_ptr<WebView> pWebview = WebView::Create(
            1280, 800,
            "file:///android_asset/webview.html");

    PanelConfig panelConfig = {
            .fWidthMeters = 1.6f,
            .fHeightMeters = 1.f,
            .unTextureWidth = 1280,
            .unTextureHeight = 800,
            .fRefreshRate = 120.f
    };
    std::unique_ptr<IPanelPositioner> pPanelPositioner = std::make_unique<PanelPositionerSlowTurnFromHead>(1.5f);
    m_pUIPanel = std::make_unique<XrUIPanel>(
            panelConfig,
            std::move(pWebview),
            std::move(pPanelPositioner)
    );

    if (m_pUIPanel->Init(m_xrqContext)) {
        Log("[XrProgram] initialized animation panel");
    } else {
        Log(LogError, "[XrProgram] failed to initialize stream animation panel. Not displaying.");
    }

    m_layerProjection = {
            .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
            .next = nullptr,
            .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
            .viewCount = m_xrqContext.unViewCount,
            .views = m_vProjectionViews.data()
    };

    GL_CHECK(glGenFramebuffers(1, &m_framebuffer));

    return true;
}

void Program::Tick() {
    XRQHandleEvents(m_xrqContext);

    if (m_xrqContext.bAppShouldSubmitFrames) {
        XRQWaitFrame(m_xrqContext);

        XrFrameBeginInfo frame_begin_info = {.type = XR_TYPE_FRAME_BEGIN_INFO, .next = nullptr};
        QUALIFY_XR_VOID(m_xrqContext.instance, xrBeginFrame(m_xrqContext.session, &frame_begin_info));

        XRQLocateViewsFrame(m_xrqContext);

        for (int i = 0; i < 2; i++) {
            XRQAcquireSwapchainImageRAII constructImageIndex(m_projectionSwapchains[i]);
            GLuint unSwapchainTexture = m_projectionSwapchains[i].images[constructImageIndex.GetAcquiredImageIndex()].image;

            GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer));
            GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, unSwapchainTexture, 0));

            GL_CHECK(glClearColor(.5f, .5f, .5f, 1.f));
            GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

            GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        }
        XRQSetProjectionViewsFromCurrentFrameViews(m_xrqContext, m_vProjectionViews);
        m_layerProjection.space = m_xrqContext.mapReferenceSpaceSpaces.at(m_xrqContext.playSpace);

        std::vector<XrCompositionLayerBaseHeader *> vLayers = {
                (XrCompositionLayerBaseHeader *) &m_layerProjection,
                m_pUIPanel->RenderFrame(m_xrqContext)
        };

        XrFrameEndInfo frame_end_info = {
                .type = XR_TYPE_FRAME_END_INFO,
                .next = nullptr,
                .displayTime = m_xrqContext.currentFrameState.predictedDisplayTime,
                .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
                .layerCount = (uint32_t) vLayers.size(),
                .layers = vLayers.data(),
        };
        QUALIFY_XR_VOID(m_xrqContext.instance, xrEndFrame(m_xrqContext.session, &frame_end_info));
    }
}

Program::~Program() {

};