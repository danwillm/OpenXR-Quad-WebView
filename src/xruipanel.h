#pragma once

#include <array>

#include "xrq.h"
#include "xrmath.h"

#include "webview.h"
#include "glm/detail/type_quat.hpp"

class IPanelPositioner
{
public:
    virtual glm::mat4 GetMatrix( const glm::mat4 &matHmdPosition ) = 0;

    virtual ~IPanelPositioner() = default;
};

class PanelPositionerHUD : public IPanelPositioner
{
public:
    PanelPositionerHUD( const glm::mat4 &matOffsetPosition );

    glm::mat4 GetMatrix( const glm::mat4 &matHmdPosition ) override;

private:
    glm::mat4 m_matOffsetPosition{1.f};
};

class PanelPositionerSlowTurnFromHead : public IPanelPositioner
{
public:
    explicit PanelPositionerSlowTurnFromHead( float fDistanceForwardFromHead );

    glm::mat4 GetMatrix( const glm::mat4 &matHmdPosition ) override;

private:
    glm::mat4 m_matHMDPositionOfLastUpdate{1.f};

    float m_fDistanceForwardFromHead;

    glm::quat m_lastRotation{1.f, 0.f, 0.f, 0.f};
    glm::quat m_targetRotation{1.f, 0.f, 0.f, 0.f};

    bool m_bIsPanelMoving = false;

    std::chrono::time_point<std::chrono::steady_clock> m_lastUpdate;
};

class PanelPositionerPoint : public IPanelPositioner
{
public:
    explicit PanelPositionerPoint( glm::vec3 vPoint );

    glm::mat4 GetMatrix( const glm::mat4 &matHmdPosition ) override;

private:
    glm::mat4 m_panelPosition{1.f};
    glm::mat4 m_matPoint{1.f};
};


struct XrUIPanelHandInteractionState
{
	bool bRayIntersects = false;
	XrVector2f vecIntersection{};
	XrVector3f vecWorldIntersection{};
	bool bIsSelecting = false;
};

class XrUIPanel
{
public:
	XrUIPanel( PanelConfig panelConfig,
			   std::shared_ptr<WebView> pWebView,
			   std::unique_ptr<IPanelPositioner> pPanelPositioner );

	bool Init( const XRQContext &xrqContext );

	void Focused();

	XrCompositionLayerBaseHeader *
	RenderFrame( XRQContext &xrqContext );

	void UnFocused();

	const PanelConfig &GetPanelConfig();

	void DisableInput() { m_bInputDisabled = true; }

	void EnableInput() { m_bInputDisabled = false; }

	~XrUIPanel();

	std::shared_ptr<WebView> m_pWebView;

private:
	std::unique_ptr<IPanelPositioner> m_pPanelPositioner;

	XRQSwapchain m_panelSwapchain{};
	XrCompositionLayerQuad m_panelLayerQuad{};

	PanelConfig m_panelConfig;
	uint32_t m_ulPanelFrameTimeUS = 16000;

	uint64_t m_ulLastRenderTimeUS = 0;

	bool m_bLastMouseState = false;
	bool m_bInputDisabled = false;
};