#pragma once

#include <array>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <mutex>
#include <variant>

#define EGL_EGLEXT_PROTOTYPES 1

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/native_window.h>
#include <jni.h>
#include <sys/system_properties.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_TIMESPEC

#define XR_USE_GRAPHICS_API_OPENGL_ES

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

static XrPosef k_identityPose = {
		.orientation = {.x = 0, .y = 0, .z = 0, .w = 1.0},
		.position = {.x = 0, .y = 0, .z = 0}};

static const XrVector3f k_vecForward = {0.f, 0.f, -1.f};

static const XrVector3f k_vecUp = {0.f, 1.f, 0.f};
static const XrVector3f k_vecDown = {0.f, -1.f, 0.f};

enum XrqEvent
{
	XRQ_SESSION_IDLE_OR_UNKNOWN,
	XRQ_SESSION_STATE_READY,
	XRQ_SESSION_STATE_VISIBLE,
	XRQ_SESSION_STATE_FOCUSED,
	XRQ_INTERACTION_PROFILE_CHANGED,
	XRQ_SESSION_STATE_STOPPING,
	XRQ_REFERENCE_SPACE_CHANGE_PENDING,
	XRQ_SHUTDOWN,
};
typedef std::variant<XrEventDataReferenceSpaceChangePending> XrqEventData;

enum XRQHand
{
	XRQ_HAND_LEFT,
	XRQ_HAND_RIGHT,
	XRQ_HAND_BOTH,
	XRQ_HAND_NONE,
};

struct XRQSwapchainInfo
{
	uint32_t width;
	uint32_t height;

	int64_t recommendedFormat;
	uint32_t sampleCount;
};

struct XRQSwapchain
{
	XrSwapchain swapchain = XR_NULL_HANDLE;

	XrSwapchainStateSamplerOpenGLESFB samplerOpenGlesFB;

	uint32_t imageCount = 0;
	std::vector<XrSwapchainImageOpenGLESKHR> images;

	int32_t width = 0;
	int32_t height = 0;

	~XRQSwapchain();
};

struct XRQActionSetCreateInfo
{
	std::string sActionSetName;
	std::string sLocalizedActionSetName;
	uint32_t unPriority;
};

struct XRQAction
{
	XrActionSet actionSet = XR_NULL_HANDLE;

	XrAction action = XR_NULL_HANDLE;
	XrActionType actionType;

	std::vector<XrSpace> vecActionSpaces;
	std::vector<XrPath> vecSubactionPaths;

	std::string sActionName;
};

struct XRQActionCreateInfo
{
	XrActionType actionType;
	std::string sActionName;

	XRQHand subActionPathHand;
	std::vector<XrPosef> poseInActionSpace{};

	std::map<std::string, std::vector<std::string>> mapInteractionProfileBindings;
};

struct XRQContext
{
	XrInstance instance = XR_NULL_HANDLE;
	XrSession session = XR_NULL_HANDLE;
	XrSystemId systemId = 0;

	std::set<std::string> setAvailableExtensions;
	std::set<std::string> setEnabledExtensions;

	XrReferenceSpaceType playSpace;
	std::map<XrReferenceSpaceType, XrSpace> mapReferenceSpaceSpaces;

	bool bAppShouldSubmitFrames = false;
	bool bIsSessionRunning = false;
	bool bCanUseControllers = false;
	bool bIsSessionFocused = false;

	XrSessionState sessionState;
	std::function<void( XrqEvent, XrqEventData )> eventCallback;

	XrEventDataBuffer lastEventDataBuffer;

	XrFrameState currentFrameState;

	XrViewConfigurationType viewType;
	uint32_t unViewCount;
	std::vector<XrView> vCurrentFrameViews;
	std::vector<XrViewConfigurationView> vViewConfigViews;

	bool bIsEyeGazeInteractionSupported = false;

	bool bIsSocialEyeTrackingSupported = false;
	XrEyeTrackerFB eyeTracker = XR_NULL_HANDLE;

	bool bIsFaceTrackingSupported = false;
	XrFaceTracker2FB faceTracker = XR_NULL_HANDLE;

	std::mutex mutHandTrackerMutex = {};
	bool bIsHandTrackingSupported = false;
	std::atomic< bool > bIsHandTrackingSetup = false;
	std::array< bool, 2 > vHandTrackingDataReceived = { false, false };
	std::array< XrHandTrackerEXT, 2 > handTracker = {XR_NULL_HANDLE, XR_NULL_HANDLE};

	std::vector<XrActionSet> vecActionSets;
	std::map<XrPath, std::vector<XrActionSuggestedBinding>> mapInteractionProfileBindings;

	std::vector<XrActiveActionSet> vActiveActionSets;

	std::string sUsingInteractionProfile;
};

struct XRQApp
{
	std::string sAppName;
	uint32_t unAppVersion;

	std::string sEngineName;
	uint32_t unEngineVersion;

	std::set<std::string> requestedExtensions;
};

struct XRQEyeVector
{
	XrVector3f vec;
	float fConfidence;
};

bool XRQIsExtensionAvailable( const XRQContext &context, const std::string &extension_name );

bool XRQCreateXRInstance( const XRQApp &app, XRQContext &outContext );

bool XRQSetReferencePlaySpace( XRQContext &out_context, const XrReferenceSpaceType space_type );

bool XRQCreateXRSession( XRQContext &outContext );

bool XRQCreateHandTrackers( XRQContext& context );

bool
XRQCreateSwapchain( const XRQContext &context, const XRQSwapchainInfo &xrqSwapchainInfo, XRQSwapchain &outSwapchain );

bool XRQGetSwapchainSamplerStateGLES( const XRQContext &context, XRQSwapchain &swapchain,
									  XrSwapchainStateSamplerOpenGLESFB &samplerState );

bool XRQUpdateSwapchainSamplerStateGLES( const XRQContext &context, const XRQSwapchain &swapchain,
										 const XrSwapchainStateSamplerOpenGLESFB &samplerState );

bool XRQCreateBasicQuadLayer( const XRQContext &context, const XRQSwapchain &xrqSwapchain, XrCompositionLayerQuad &outQuadLayer );
bool XRQCreateBasicQuadLayer( const XRQContext &context, const XRQSwapchain &xrqSwapchain, XrReferenceSpaceType spaceType, XrCompositionLayerQuad &outQuadLayer );

bool XRQCreateProjectionViewLayer( const XRQContext &context, const XRQSwapchain &xrqSwapchain,
								   XrCompositionLayerProjectionView &outProjectionView );

bool XRQHandleEvents( XRQContext &context );

bool XRQWaitFrame( XRQContext &context );

bool XRQGetTimeNow( const XRQContext &context, XrTime &out_time );

bool XRQLocateViewsFrame( XRQContext &context );

bool
XRQLocateViewsInReferenceSpace( const XRQContext &context, XrTime time, XrSpace space, std::vector<XrView> &vOutViews );

bool XRQLocateViewsAtTime( XRQContext &context, const XrTime time, std::vector<XrView> &vOutViews );

bool XRQSetProjectionViewsFromCurrentFrameViews( const XRQContext &context,
												 std::array<XrCompositionLayerProjectionView, 2> &projectionViews );

bool XRQLocateReferenceSpace( XRQContext &context, XrReferenceSpaceType referenceSpaceType, XrTime time,
							  XrSpaceLocation &outSpaceLocation );

bool XRQLocateReferenceSpace( XRQContext &context, XrReferenceSpaceType referenceSpaceType, XrReferenceSpaceType baseSpaceType,
							  XrTime time, XrSpaceLocation &outSpaceLocation );

bool XRQLocateReferenceSpaceAtFrameTime( XRQContext &context, XrReferenceSpaceType reference_space_type,
										 XrSpaceLocation &out_space_location );

bool XRQGetPlayAreaBoundsRect( const XRQContext &context, XrExtent2Df &out_bounds );

bool XRQGetReferenceSpace( const XRQContext &context, XrReferenceSpaceType spaceType, XrSpace &outSpace );

bool XRQEnumerateSupportedRefreshRates( XRQContext &context, std::vector<float> &out_supported_refresh_rates );

bool XRQRequestRefreshRate( XRQContext &context, float refresh_rate );

bool XRQRequestHighestRefreshRate( XRQContext &context );

struct XRQAcquireSwapchainImageRAII
{
	XRQAcquireSwapchainImageRAII( const XRQAcquireSwapchainImageRAII & ) = delete;

	XRQAcquireSwapchainImageRAII &operator=( const XRQAcquireSwapchainImageRAII & ) = delete;

	XRQAcquireSwapchainImageRAII( XRQAcquireSwapchainImageRAII && ) = delete;

	XRQAcquireSwapchainImageRAII &operator=( XRQAcquireSwapchainImageRAII && ) = delete;

	XRQAcquireSwapchainImageRAII( const XRQSwapchain &swapchain );

	const uint32_t &GetAcquiredImageIndex() const;

	~XRQAcquireSwapchainImageRAII();

private:
	uint32_t m_unAcquiredIndex = 0;
	XrSwapchain m_swapchain = XR_NULL_HANDLE;
};

XrPath XRQStringToXrPath( XrInstance instance, const std::string &path );

XrPath XRQStringToXrPath( const XRQContext &context, const std::string &path );

bool XRQXrPathToString( XrInstance instance, const XrPath path, std::string &out_path );

bool XRQXrPathToString( const XRQContext &context, const XrPath path, std::string &out_path );

XrPath XRQGetSubactionPathForHand( const XRQContext &context, XRQHand hand );

bool XRQGetHandSubactionPaths( const XRQContext &context, XRQHand hand, std::vector<XrPath> &outHandSubactionPaths );

bool XRQCreateAndRegisterActionSetForAttach( XRQContext &context, const XRQActionSetCreateInfo &actionSetCreateInfo,
											 XrActionSet &outActionSet );

bool XRQCreateActionAndRegisterSuggestedBindings( XRQContext &context, const XrActionSet actionSet,
												  const XRQActionCreateInfo &actionCreateInfo,
												  XRQAction &outAction );

bool XRQRegisterActionSetForSync( XRQContext& context, const XrActionSet actionSet, XRQHand hand );

bool XRQSyncRegisteredActiveActionSets( const XRQContext& context );

bool XRQSuggestRegisteredInteractionProfileBindings( XRQContext &context );

bool XRQAttachRegisteredActionSets( XRQContext &context );

bool XRQGetFaceTracking( XRQContext &context, XrFaceExpressionWeights2FB &outFaceExpressionWeights );

bool XRQEnumerateColorSpaces( const XRQContext& context, std::vector<XrColorSpaceFB>& vOutColorSpaces );

bool XRQSetColorSpace( const XRQContext& context, XrColorSpaceFB colorSpace );

bool XRQSetApplicationThread( const XRQContext& context, XrAndroidThreadTypeKHR threadType );
bool XRQLocateHandJoints( XRQContext& context, XRQHand hand, XrTime time, XrHandJointLocationsEXT& outJointLocations );

bool XRQIsLocationValid( const XrSpaceLocationFlags locationFlags );

bool XRQIsVelocityValid( const XrSpaceVelocityFlags velocityFlags );

bool XRQDestroyAction( XRQAction &action );

bool XRQDestroyActionSet( XrActionSet &actionSet );

bool XRQDestroyHandTrackers( XRQContext& context );

bool XRQRequestExitSession( XRQContext &context );

bool XRQTeardown( XRQContext &context );