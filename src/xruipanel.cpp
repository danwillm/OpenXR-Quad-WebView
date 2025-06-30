#include "xruipanel.h"

#include "check.h"
#include "log.h"

#include "glm/gtc/type_ptr.hpp"

#include "glm/gtc/matrix_inverse.hpp"

#include "log.h"
#include "glm/gtx/vector_angle.inl"

#define DEBUGPANEL
#define ROTATEPANEL

PanelPositionerHUD::PanelPositionerHUD(const glm::mat4 &matOffsetPosition) {
    m_matOffsetPosition = matOffsetPosition;
}

glm::mat4 PanelPositionerHUD::GetMatrix(const glm::mat4 &matHmdPosition) {
    return matHmdPosition * m_matOffsetPosition;
}

PanelPositionerSlowTurnFromHead::PanelPositionerSlowTurnFromHead(float fDistanceForwardFromHead) :
        m_fDistanceForwardFromHead(fDistanceForwardFromHead) {
}

glm::mat4 PanelPositionerSlowTurnFromHead::GetMatrix(const glm::mat4 &matHmdPosition) {
    glm::vec3 lastHmdPosition = m_matHMDPositionOfLastUpdate[3];
    glm::vec3 currentHmdPosition = matHmdPosition[3];

    glm::mat3 lastHmdRotation = m_matHMDPositionOfLastUpdate;
    glm::mat3 currentHmdRotation = matHmdPosition;

    {
        bool bPanelCanUpdateWhileMoving =
                m_bIsPanelMoving && std::abs(m_targetRotation.w - m_lastRotation.w) > glm::radians(10.f);
        if (!m_bIsPanelMoving || bPanelCanUpdateWhileMoving) { //work out if we need to move the panel
            glm::vec3 vecForward = glm::vec3(1.f, 0.f, 0.f);

            glm::vec3 lastTransformedForward = vecForward * lastHmdRotation;
            glm::vec3 currentTransformedForward = vecForward * currentHmdRotation;

            float angleDifference = glm::orientedAngle(lastTransformedForward, currentTransformedForward,
                                                       {0.f, 1.f, 0.f});

            if ((std::abs(angleDifference) > glm::radians(40.f)) || bPanelCanUpdateWhileMoving) {
                float fTargetAngle = glm::orientedAngle(vecForward, currentTransformedForward, {0.f, 1.f, 0.f});
                m_targetRotation = glm::angleAxis(fTargetAngle, glm::vec3(0.f, -1.f, 0.f));
                m_matHMDPositionOfLastUpdate = matHmdPosition;
            }
            m_bIsPanelMoving = true;
        }
    }

    auto now = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float, std::milli>(now - m_lastUpdate).count();

    if (std::abs(m_targetRotation.w - m_lastRotation.w) < 0.0001f) {
        m_bIsPanelMoving = false;
        m_lastRotation = m_targetRotation;
    }

    float fCoeff = std::exp(-deltaTime * 0.005f);

    m_lastRotation = glm::slerp(m_targetRotation, m_lastRotation, fCoeff);

    m_lastUpdate = now;

    glm::mat4 result = glm::mat4_cast(m_lastRotation);

    glm::vec3 offset = glm::vec3(0.f, 0.f, -m_fDistanceForwardFromHead);
    glm::vec3 offsetFromHead = currentHmdPosition * m_lastRotation + offset;
    result = glm::translate(result, offsetFromHead);

    return result;
}


PanelPositionerPoint::PanelPositionerPoint(glm::vec3 vPoint) {
    m_matPoint = glm::mat4(1.0f);
    m_matPoint = glm::translate(m_matPoint, vPoint);
}

glm::mat4 PanelPositionerPoint::GetMatrix(const glm::mat4 &matHmdPosition) {
    return m_matPoint;
}

static uint64_t GetCurrentTimeUS() {
    struct timespec tsp;
    clock_gettime(CLOCK_MONOTONIC_RAW, &tsp);
    return (uint64_t) tsp.tv_sec * 1000000LL + tsp.tv_nsec / 1000;
}

XrUIPanel::XrUIPanel(
        PanelConfig panelConfig,
        std::shared_ptr<WebView> pWebView,
        std::unique_ptr<IPanelPositioner> pPanelPositioner) :
        m_panelConfig(panelConfig),
        m_pWebView(std::move(pWebView)),
        m_pPanelPositioner(std::move(pPanelPositioner)) {
    m_ulPanelFrameTimeUS = static_cast<uint32_t>(1000000.f / panelConfig.fRefreshRate);
    Log("[XRUIPanel] Refresh rate: %.2f, frame time: %i", panelConfig.fRefreshRate, m_ulPanelFrameTimeUS);
}

bool XrUIPanel::Init(const XRQContext &xrqContext) {
    Log("[XRUIPanel] Initializing UI panel swapchains...");

    XRQSwapchainInfo panelInfo = {
            .width = static_cast<uint32_t>(m_panelConfig.unTextureWidth),
            .height = static_cast<uint32_t>(m_panelConfig.unTextureHeight),
            .recommendedFormat = GL_SRGB8_ALPHA8,
            .sampleCount = xrqContext.vViewConfigViews[0].recommendedSwapchainSampleCount,
    };
    if (!XRQCreateSwapchain(xrqContext, panelInfo, m_panelSwapchain)) {
        Log(LogError, "[XRUIPanel] Failed to create swapchain for panel!");

        return false;
    }

    XRQCreateBasicQuadLayer(xrqContext, m_panelSwapchain, m_panelLayerQuad);

    m_panelLayerQuad.layerFlags =
            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
    m_panelLayerQuad.size = {
            .width = m_panelConfig.fWidthMeters,
            .height = -m_panelConfig.fHeightMeters,
    };

    return true;
}

void XrUIPanel::Focused() {
    Log("[XRUIPanel] Panel was focused");
    m_pWebView->RequestResume();
}

XrCompositionLayerBaseHeader *XrUIPanel::RenderFrame(XRQContext &xrqContext) {
    uint64_t timeNowUS = GetCurrentTimeUS();
    if (timeNowUS - m_ulLastRenderTimeUS > m_ulPanelFrameTimeUS) {
        m_pWebView->RequestDraw();
        m_ulLastRenderTimeUS = timeNowUS;
    }

    XrMatrix4x4f matPanelPosition;
    {
        XrSpaceLocation viewSpaceLocation = {
                .type = XR_TYPE_SPACE_LOCATION,
                .next = nullptr,
        };
        XRQLocateReferenceSpaceAtFrameTime(xrqContext, XR_REFERENCE_SPACE_TYPE_VIEW, viewSpaceLocation);

        XrMatrix4x4f matHmdPosition;
        XrMatrix4x4f_CreateFromRigidTransform(&matHmdPosition, &viewSpaceLocation.pose);

        XrMatrix4x4f matPanelPositionerPosition;
        {
            glm::mat4 mat = m_pPanelPositioner->GetMatrix(glm::make_mat4(matHmdPosition.m));
            memcpy(matPanelPositionerPosition.m, glm::value_ptr(mat), sizeof(XrMatrix4x4f));
        }

        matPanelPosition = matPanelPositionerPosition;
    }

    XrPosef panelPose;
    XrQuaternionf_CreateFromMatrix4x4f(&panelPose.orientation, &matPanelPosition);

    XrMatrix4x4f_GetTranslation(&panelPose.position, &matPanelPosition);

    m_panelLayerQuad.pose = panelPose;

#ifdef ROTATEPANEL
    XrQuaternionf quatRot{};
    XrVector3f vecAxis = {0.f, 1.f, 0.f};
    XrQuaternionf_CreateFromAxisAngle(&quatRot, &vecAxis, XrDegreestoRadians(180.f));

    XrQuaternionf quatResult{};
    XrQuaternionf_Multiply(&quatResult, &m_panelLayerQuad.pose.orientation, &quatRot);

    m_panelLayerQuad.pose.orientation = quatResult;
#endif

    XRQAcquireSwapchainImageRAII acquiredSwapchain(m_panelSwapchain);
    GLuint swapchainTexture = m_panelSwapchain.images[acquiredSwapchain.GetAcquiredImageIndex()].image;

#ifndef DEBUGPANEL
    m_pWebView->CopyContentsToTexture( swapchainTexture );
#else
    m_pWebView->CopyDebugContentsToTexture(swapchainTexture);
#endif
    return (XrCompositionLayerBaseHeader *) &m_panelLayerQuad;
}

void XrUIPanel::UnFocused() {
    Log("[XRUIPanel] Panel was unfocused");
    m_pWebView->RequestPause();
}

const PanelConfig &XrUIPanel::GetPanelConfig() {
    return m_panelConfig;
}

XrUIPanel::~XrUIPanel() {
    Log("[XRUIPanel] destroying UI panel...");
    m_pWebView = nullptr;
}