#include "xrq.h"

#include <vector>

#include "check.h"
#include "xrmath.h"
#include "android_native_app_glue.h"
#include <unistd.h>

#include "log.h"

extern EGLDisplay egl_display;
extern EGLContext egl_context;
extern EGLConfig egl_config;

extern android_app * gApp;

static const std::set<std::string> k_internalExtensions = {
		XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
		XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
		XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,
		XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
		XR_FB_FACE_TRACKING2_EXTENSION_NAME,
		XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME,
		XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,
		XR_FB_SWAPCHAIN_UPDATE_STATE_OPENGL_ES_EXTENSION_NAME,
		XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME,
		XR_FB_COLOR_SPACE_EXTENSION_NAME,
		XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME,
		XR_EXT_HAND_TRACKING_EXTENSION_NAME,
		XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME,
		XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME,
};

static PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
static PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR;
static PFN_xrCreateEyeTrackerFB xrCreateEyeTrackerFB;
static PFN_xrCreateFaceTracker2FB xrCreateFaceTracker2FB;
static PFN_xrEnumerateDisplayRefreshRatesFB xrEnumerateDisplayRefreshRatesFB;
static PFN_xrGetEyeGazesFB xrGetEyeGazesFB;
static PFN_xrGetFaceExpressionWeights2FB xrGetFaceExpressionWeights2FB;
static PFN_xrConvertTimespecTimeToTimeKHR xrConvertTimespecTimeToTimeKHR;
static PFN_xrRequestDisplayRefreshRateFB xrRequestDisplayRefreshRateEXT;
static PFN_xrGetSwapchainStateFB xrGetSwapchainStateFB;
static PFN_xrUpdateSwapchainFB xrUpdateSwapchainFB;
static PFN_xrEnumerateColorSpacesFB xrEnumerateColorSpacesFB;
static PFN_xrSetColorSpaceFB xrSetColorSpaceFB;
static PFN_xrSetAndroidApplicationThreadKHR xrSetAndroidApplicationThreadKHR;
static PFN_xrCreateHandTrackerEXT xrCreateHandTrackerEXT;
static PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT;
static PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT;

static bool XRQSetAvailableExtensions( XRQContext &out_context,
									   const std::set<std::string> &requested_extensions )
{
	uint32_t extension_count = 0;
	QUALIFY_XR( out_context, xrEnumerateInstanceExtensionProperties( nullptr, 0, &extension_count,
																	 nullptr ));

	std::vector<XrExtensionProperties> extensions_properties( extension_count,
															  {.type = XR_TYPE_EXTENSION_PROPERTIES, .next = nullptr} );
	QUALIFY_XR( out_context,
				xrEnumerateInstanceExtensionProperties( nullptr, extension_count, &extension_count,
														extensions_properties.data()));

	std::set<std::string> missingExtensions = requested_extensions;
	for ( const XrExtensionProperties &extension_properties: extensions_properties )
	{
		std::string extension_name = extension_properties.extensionName;
		out_context.setAvailableExtensions.emplace( extension_name );

		if ( requested_extensions.contains( extension_name ))
		{
			Log( "[XRQ] Extension %s is available and was requested", extension_name.c_str());
			out_context.setEnabledExtensions.emplace( extension_name );
			missingExtensions.erase( extension_name );
		}
	}

	if ( !missingExtensions.empty())
	{
		for ( const std::string &sExtensionName: missingExtensions )
		{
			Log( LogWarning, "[XRQ] Requested extension %s was not available!", sExtensionName.c_str());
		}
	}

	return true;
}

bool XRQIsExtensionAvailable( const XRQContext &context, const std::string &extension_name )
{
	return context.setEnabledExtensions.contains( extension_name );
}

template<class T>
bool XRQGetExtensionFromProcAddr( const XRQContext &context, const std::string &proc_addr, T &out )
{
	QUALIFY_XR( context, xrGetInstanceProcAddr( context.instance, proc_addr.c_str(),
												(PFN_xrVoidFunction *) &out ));

	return true;
}

bool XRQCreateXRInstance( const XRQApp &app, XRQContext &outContext )
{
	{
		XRQGetExtensionFromProcAddr( outContext, "xrInitializeLoaderKHR", xrInitializeLoaderKHR );

		XrLoaderInitInfoAndroidKHR init_data = {
				.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
				.applicationVM = gApp->activity->vm,
				.applicationContext = gApp->activity->clazz,
		};
		QUALIFY_XR( outContext, xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR *) &init_data ));

		Log( "[XRQ] OpenXR Loader initialized successfully" );
	}

	{
		std::set<std::string> requestedExtensions = app.requestedExtensions;
		requestedExtensions.insert( k_internalExtensions.begin(), k_internalExtensions.end());

		XRQSetAvailableExtensions( outContext, requestedExtensions );

		Log( "[XRQ] Enabled extensions:" );
		std::vector<const char *> vecRequestedExtensions = {};
		for ( const std::string &extension: outContext.setEnabledExtensions )
		{
			Log( "[XRQ] \t %s", extension.c_str());
			vecRequestedExtensions.emplace_back( extension.c_str());
		}

		XrInstanceCreateInfoAndroidKHR createInfoAndroidKhr = {
				.type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR,
				.next = nullptr,
				.applicationVM = gApp->activity->vm,
				.applicationActivity = gApp->activity->clazz,
		};
		XrInstanceCreateInfo instance_create_info = {
				.type = XR_TYPE_INSTANCE_CREATE_INFO,
				.next = &createInfoAndroidKhr,
				.createFlags = 0,
				.applicationInfo = {
						.applicationVersion = app.unAppVersion,
						.engineVersion = app.unEngineVersion,
						.apiVersion = XR_MAKE_VERSION(1,0,34),
				},
				.enabledApiLayerCount = 0,
				.enabledApiLayerNames = nullptr,
				.enabledExtensionCount = static_cast<uint32_t>(vecRequestedExtensions.size()),
				.enabledExtensionNames = vecRequestedExtensions.data()
		};
		app.sAppName.copy( instance_create_info.applicationInfo.applicationName, app.sAppName.size());
		app.sEngineName.copy( instance_create_info.applicationInfo.engineName, app.sEngineName.size());

		QUALIFY_XR( outContext, xrCreateInstance( &instance_create_info, &outContext.instance ));

		Log( "[XRQ] Created OpenXR Instance successfully" );
	}

	{
		XrSystemGetInfo systemGetInfo = {
				.type = XR_TYPE_SYSTEM_GET_INFO,
				.next = nullptr,
				.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY
		};
		QUALIFY_XR( outContext,
					xrGetSystem( outContext.instance, &systemGetInfo, &outContext.systemId ));

		XrSystemHandTrackingPropertiesEXT handTrackingPropertiesExt = {
				.type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
				.next = nullptr,
		};
		XrSystemFaceTrackingProperties2FB faceTrackingProperties2Fb = {
				.type = XR_TYPE_SYSTEM_FACE_TRACKING_PROPERTIES2_FB,
				.next = &handTrackingPropertiesExt,
		};
		XrSystemEyeTrackingPropertiesFB socialEyeTrackingProperties = {
				.type = XR_TYPE_SYSTEM_EYE_TRACKING_PROPERTIES_FB,
				.next = &faceTrackingProperties2Fb,
		};
		XrSystemEyeGazeInteractionPropertiesEXT eyeGazeInteractionPropertiesExt = {
				.type = XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT,
				.next = &socialEyeTrackingProperties,
		};
		XrSystemProperties systemProperties = {
				.type = XR_TYPE_SYSTEM_PROPERTIES,
				.next = &eyeGazeInteractionPropertiesExt,
		};
		QUALIFY_XR( outContext, xrGetSystemProperties( outContext.instance, outContext.systemId,
													   &systemProperties ));

		outContext.bIsEyeGazeInteractionSupported = eyeGazeInteractionPropertiesExt.supportsEyeGazeInteraction;
		Log( "[XRQ] Eye gaze interaction supported: %s", outContext.bIsEyeGazeInteractionSupported ? "Yes" : "No" );

		outContext.bIsSocialEyeTrackingSupported = socialEyeTrackingProperties.supportsEyeTracking;
		Log( "[XRQ] FB Eye tracking Social supported: %s", outContext.bIsSocialEyeTrackingSupported ? "Yes" : "No" );

		outContext.bIsFaceTrackingSupported = faceTrackingProperties2Fb.supportsVisualFaceTracking;
		Log( "[XRQ] Face tracking supported: %s", outContext.bIsFaceTrackingSupported ? "Yes" : "No" );

		outContext.bIsHandTrackingSupported = handTrackingPropertiesExt.supportsHandTracking;
		Log( "[XRQ] Hand tracking supported: %s", outContext.bIsHandTrackingSupported ? "Yes" : "No" );
	}

	{
		XRQGetExtensionFromProcAddr( outContext, "xrGetOpenGLESGraphicsRequirementsKHR",
									 xrGetOpenGLESGraphicsRequirementsKHR );

		if ( XRQIsExtensionAvailable( outContext, XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME ))
		{
			XRQGetExtensionFromProcAddr( outContext, "xrConvertTimespecTimeToTimeKHR", xrConvertTimespecTimeToTimeKHR );
		}

		if ( XRQIsExtensionAvailable( outContext, XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME ))
		{
			XRQGetExtensionFromProcAddr( outContext, "xrGetSwapchainStateFB", xrGetSwapchainStateFB );
			XRQGetExtensionFromProcAddr( outContext, "xrUpdateSwapchainFB", xrUpdateSwapchainFB );
		}

		if ( XRQIsExtensionAvailable( outContext, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME ))
		{
			XRQGetExtensionFromProcAddr( outContext, "xrEnumerateDisplayRefreshRatesFB",
										 xrEnumerateDisplayRefreshRatesFB );
			XRQGetExtensionFromProcAddr( outContext, "xrRequestDisplayRefreshRateFB", xrRequestDisplayRefreshRateEXT );
		}

		if( XRQIsExtensionAvailable( outContext, XR_FB_COLOR_SPACE_EXTENSION_NAME ))
		{
			XRQGetExtensionFromProcAddr( outContext, "xrEnumerateColorSpacesFB", xrEnumerateColorSpacesFB );
			XRQGetExtensionFromProcAddr( outContext, "xrSetColorSpaceFB", xrSetColorSpaceFB );
		}

		if( XRQIsExtensionAvailable( outContext, XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME ))
		{
			XRQGetExtensionFromProcAddr( outContext, "xrSetAndroidApplicationThreadKHR", xrSetAndroidApplicationThreadKHR );
		}
		if ( outContext.bIsSocialEyeTrackingSupported )
		{
			XRQGetExtensionFromProcAddr( outContext, "xrCreateEyeTrackerFB", xrCreateEyeTrackerFB );
			XRQGetExtensionFromProcAddr( outContext, "xrGetEyeGazesFB", xrGetEyeGazesFB );
		}

		if ( outContext.bIsFaceTrackingSupported )
		{
			XRQGetExtensionFromProcAddr( outContext, "xrCreateFaceTracker2FB", xrCreateFaceTracker2FB );
			XRQGetExtensionFromProcAddr( outContext, "xrGetFaceExpressionWeights2FB", xrGetFaceExpressionWeights2FB );
		}

		if( outContext.bIsHandTrackingSupported )
		{
			XRQGetExtensionFromProcAddr( outContext, "xrCreateHandTrackerEXT", xrCreateHandTrackerEXT );
			XRQGetExtensionFromProcAddr( outContext, "xrLocateHandJointsEXT", xrLocateHandJointsEXT );
			XRQGetExtensionFromProcAddr( outContext, "xrDestroyHandTrackerEXT", xrDestroyHandTrackerEXT );
		}
	}

	return true;
}

bool XRQSetReferencePlaySpace( XRQContext &out_context, const XrReferenceSpaceType space_type )
{
	out_context.playSpace = space_type;

	XrReferenceSpaceCreateInfo play_space_create_info = {
			.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
			.next = nullptr,
			.referenceSpaceType = out_context.playSpace,
			.poseInReferenceSpace = k_identityPose,
	};

	out_context.mapReferenceSpaceSpaces[ out_context.playSpace ] = XR_NULL_HANDLE;
	QUALIFY_XR( out_context, xrCreateReferenceSpace( out_context.session, &play_space_create_info,
													 &out_context.mapReferenceSpaceSpaces[ out_context.playSpace ] ));

	return true;
}

bool XRQGetXRViewConfiguration( const XrViewConfigurationType view_type, XRQContext &out_context )
{
	out_context.viewType = view_type;

	QUALIFY_XR( out_context,
				xrEnumerateViewConfigurationViews( out_context.instance, out_context.systemId,
												   view_type, 0, &out_context.unViewCount,
												   nullptr ));

	out_context.vViewConfigViews = {
			out_context.unViewCount,
			{.type = XR_TYPE_VIEW_CONFIGURATION_VIEW, .next = nullptr}};
	QUALIFY_XR( out_context,
				xrEnumerateViewConfigurationViews( out_context.instance, out_context.systemId,
												   view_type, out_context.unViewCount,
												   &out_context.unViewCount,
												   out_context.vViewConfigViews.data()));

	out_context.vCurrentFrameViews = {out_context.unViewCount, {.type = XR_TYPE_VIEW, .next = nullptr}};

	return true;
}

bool XRQGetSupportedSwapchainFormat( const XRQContext &context, const int64_t requested_format,
									 int64_t &out_color_format )
{
	uint32_t swapchain_format_count;
	QUALIFY_XR( context,
				xrEnumerateSwapchainFormats( context.session, 0, &swapchain_format_count,
											 nullptr ));

	std::vector<int64_t> swapchain_formats( swapchain_format_count );
	QUALIFY_XR( context,
				xrEnumerateSwapchainFormats( context.session, swapchain_format_count,
											 &swapchain_format_count, swapchain_formats.data()));

	for ( const int64_t format: swapchain_formats )
	{
		if ( format == requested_format )
		{
			out_color_format = format;
			return true;
		}
	}

	return false;
}

bool
XRQCreateSwapchain( const XRQContext &context, const XRQSwapchainInfo &xrqSwapchainInfo, XRQSwapchain &outSwapchain )
{
	int64_t nColorFormat = -1;
	if ( !XRQGetSupportedSwapchainFormat( context, xrqSwapchainInfo.recommendedFormat,
										  nColorFormat ))
	{
		Log( LogError, "[XRQ] XRQCreateSwapchain could not find supported color format" );
		return false;
	}

	XrSwapchainCreateInfo swapchainCreateInfo = {
			.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
			.next = nullptr,
			.createFlags = 0,
			.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
			.format = nColorFormat,
			.sampleCount = xrqSwapchainInfo.sampleCount,
			.width = xrqSwapchainInfo.width,
			.height = xrqSwapchainInfo.height,
			.faceCount = 1,
			.arraySize = 1,
			.mipCount = 1,
	};

	QUALIFY_XR( context, xrCreateSwapchain( context.session, &swapchainCreateInfo,
											&outSwapchain.swapchain ));

	QUALIFY_XR( context,
				xrEnumerateSwapchainImages( outSwapchain.swapchain, 0, &outSwapchain.imageCount,
											nullptr ));

	//allocate a vector with the number of swapchain images we need
	outSwapchain.images = {
			outSwapchain.imageCount,
			{.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR, .next = nullptr}};
	QUALIFY_XR( context,
				xrEnumerateSwapchainImages( outSwapchain.swapchain,
											outSwapchain.images.size(),
											&outSwapchain.imageCount,
											(XrSwapchainImageBaseHeader *) outSwapchain.images.data()));

	outSwapchain.width = (int32_t) xrqSwapchainInfo.width;
	outSwapchain.height = (int32_t) xrqSwapchainInfo.height;

	Log( "[XRQ] XRQCreateSwapchain created swapchain successfully" );

	XrSwapchainStateSamplerOpenGLESFB samplerOpenGlesFB = {
			.type = XR_TYPE_SWAPCHAIN_STATE_SAMPLER_OPENGL_ES_FB,
			.next = nullptr,
	};
	if ( XRQGetSwapchainSamplerStateGLES( context, outSwapchain, samplerOpenGlesFB ))
	{
		Log( "[XRQ] New swapchain Info - magFilter: %#04x, minFilter: %#04x, maxAnisotropy: %.3f",
				samplerOpenGlesFB.magFilter, samplerOpenGlesFB.minFilter, samplerOpenGlesFB.maxAnisotropy );

		outSwapchain.samplerOpenGlesFB = samplerOpenGlesFB;
	}
	else
	{
		Log( LogWarning, "[XRQ] XRQCreateSwapchain could not get swapchain state for created swapchain" );
		outSwapchain.samplerOpenGlesFB = {};
	}
	return true;
}

bool XRQGetSwapchainSamplerStateGLES( const XRQContext &context, XRQSwapchain &swapchain,
									  XrSwapchainStateSamplerOpenGLESFB &samplerState )
{
	if ( !XRQIsExtensionAvailable( context, XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME ) ||
		 !XRQIsExtensionAvailable( context, XR_FB_SWAPCHAIN_UPDATE_STATE_OPENGL_ES_EXTENSION_NAME ))
	{
		Log( LogWarning,
				"[XRQ] XRQGetSwapchainSamplerStateGLES: XR_FB_swapchain_update_state extension or derivatives are not available" );
		return false;
	}

	QUALIFY_XR( context, xrGetSwapchainStateFB( swapchain.swapchain, (XrSwapchainStateBaseHeaderFB *) &samplerState ));
	swapchain.samplerOpenGlesFB = samplerState;

	return true;
}

bool XRQUpdateSwapchainSamplerStateGLES( const XRQContext &context, const XRQSwapchain &swapchain,
										 const XrSwapchainStateSamplerOpenGLESFB &samplerState )
{
	if ( !XRQIsExtensionAvailable( context, XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME ) ||
		 !XRQIsExtensionAvailable( context, XR_FB_SWAPCHAIN_UPDATE_STATE_OPENGL_ES_EXTENSION_NAME ))
	{
		Log( LogWarning,
				"[XRQ] XRQUpdateSwapchainSamplerStateGLES: XR_FB_swapchain_update_state extension or derivatives are not available" );
		return false;
	}

	QUALIFY_XR( context, xrUpdateSwapchainFB( swapchain.swapchain, (XrSwapchainStateBaseHeaderFB *) &samplerState ));
	return true;
}

bool XRQCreateBasicQuadLayer( const XRQContext &context, const XRQSwapchain &xrqSwapchain, XrReferenceSpaceType spaceType, XrCompositionLayerQuad &outQuadLayer )
{
	XrSpace space;
	XRQGetReferenceSpace( context, spaceType, space );

	outQuadLayer = {
			.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
			.next = nullptr,
			.space = space,
			.eyeVisibility = XR_EYE_VISIBILITY_BOTH,
			.subImage = {
					.swapchain = xrqSwapchain.swapchain,
					.imageRect = {
							.offset = {
									.x = 0,
									.y = 0
							},
							.extent = {
									.width = xrqSwapchain.width,
									.height = xrqSwapchain.height
							}
					},
					.imageArrayIndex = 0,
			},
	};

	return true;
}

bool XRQCreateBasicQuadLayer( const XRQContext &context, const XRQSwapchain &xrqSwapchain,
							  XrCompositionLayerQuad &outQuadLayer )
{
	return XRQCreateBasicQuadLayer( context, xrqSwapchain, context.playSpace, outQuadLayer );
}

bool
XRQCreateProjectionViewLayer( const XRQContext &context, const XRQSwapchain &xrqSwapchain,
							  XrCompositionLayerProjectionView &outProjectionView )
{
	outProjectionView = {
			.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			.next = nullptr,
			.subImage = {
					.swapchain = xrqSwapchain.swapchain,
					.imageRect = {
							.offset = {
									.x = 0,
									.y = 0
							},
							.extent = {
									.width = xrqSwapchain.width,
									.height = xrqSwapchain.height
							}
					},
					.imageArrayIndex = 0,
			}
	};

	return true;
}

XRQSwapchain::~XRQSwapchain()
{
	if ( swapchain == XR_NULL_HANDLE )
	{
		Log( LogWarning, "[XRQ] ~XRQSwapchain: swapchain was already XR_NULL_HANDLE!" );
		return;
	}

	xrDestroySwapchain( swapchain );
	swapchain = XR_NULL_HANDLE;

	Log( "[XRQ] ~XRQSwapchain: Swapchain destroyed." );
}

static bool XRQCreateReferenceSpace( XRQContext &context, XrReferenceSpaceType spaceType )
{
	if ( context.mapReferenceSpaceSpaces.contains( spaceType ))
	{
		Log( "[XRQ] XRQCreateReferenceSpace: Not creating a new reference space as it was already created" );
		return true;
	}
	context.mapReferenceSpaceSpaces[ spaceType ] = XR_NULL_PATH;

	XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = {
			.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
			.next = nullptr,
			.referenceSpaceType = spaceType,
			.poseInReferenceSpace = k_identityPose,
	};
	QUALIFY_XR( context, xrCreateReferenceSpace( context.session, &referenceSpaceCreateInfo,
												 &context.mapReferenceSpaceSpaces[ spaceType ] ));

	return true;
}


static bool SetupFaceTracking( XRQContext &context )
{
	if ( !context.bIsFaceTrackingSupported )
	{
		return false;
	}

	std::vector<XrFaceTrackingDataSource2FB> vFaceTrackingSources = {
			XR_FACE_TRACKING_DATA_SOURCE2_VISUAL_FB
	};
	XrFaceTrackerCreateInfo2FB faceTrackerCreateInfo2Fb = {
			.type = XR_TYPE_FACE_TRACKER_CREATE_INFO2_FB,
			.next = nullptr,
			.faceExpressionSet = XR_FACE_EXPRESSION_SET2_DEFAULT_FB,
			.requestedDataSourceCount = (uint32_t)vFaceTrackingSources.size(),
			.requestedDataSources = vFaceTrackingSources.data(),
	};

	QUALIFY_XR( context, xrCreateFaceTracker2FB( context.session, &faceTrackerCreateInfo2Fb, &context.faceTracker ));

	return true;
}

static bool SetupSocialEyeTracking( XRQContext &context )
{
	if ( !context.bIsSocialEyeTrackingSupported )
	{
		return false;
	}

	XrEyeTrackerCreateInfoFB eyeTrackerCreateInfoFb = {
			.type = XR_TYPE_EYE_TRACKER_CREATE_INFO_FB,
			.next = nullptr
	};
	QUALIFY_XR( context, xrCreateEyeTrackerFB( context.session, &eyeTrackerCreateInfoFb, &context.eyeTracker ));
	return true;
}

bool XRQCreateHandTrackers( XRQContext& context )
{
	if( !context.bIsHandTrackingSupported )
	{
		Log( LogError, "[XRQ] Could not setup hand tracking as hand tracking was not supported" );
		return false;
	}

	std::lock_guard<std::mutex> lock( context.mutHandTrackerMutex );

	if( context.bIsHandTrackingSetup )
	{
		Log( LogWarning, "[XRQ] Hand tracking was already setup, not re-creating.");
		return true;
	}

	Log( "[XRQ] Creating hand trackers as hand tracking was supported" );

	for( int i = 0; i < 2; i++ )
	{
		XrHandTrackingDataSourceEXT source = XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT;
		XrHandTrackingDataSourceInfoEXT handTrackingDataSourceInfo = {
			.type = XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT,
			.next = nullptr,
			.requestedDataSourceCount = 1,
			.requestedDataSources = &source,
		};

		XrHandTrackerCreateInfoEXT handTrackerCreateInfoExt = {
				.type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
				.next = &handTrackingDataSourceInfo,
				.hand = ( XrHandEXT ) ( i + 1 ),
				.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT,
		};
		QUALIFY_XR( context, xrCreateHandTrackerEXT(context.session, &handTrackerCreateInfoExt, &context.handTracker[ i ]) );
	}

	context.bIsHandTrackingSetup = true;

	return true;
}

bool XRQCreateXRSession( XRQContext &outContext )
{
	if ( !XRQGetXRViewConfiguration( XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, outContext ))
	{
		Log( LogError, "[XRQ] XRQCreateXRSession: Failed to get view configurations" );
		return false;
	}

	if ( !XRQIsExtensionAvailable( outContext, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME ))
	{
		Log( LogError, "[XRQ] XRQCreateXRSession: OpenGLES OpenXR Extension is not available" );

		return false;
	}

	XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {
			.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR,
			.next = nullptr,
	};
	QUALIFY_XR( outContext, xrGetOpenGLESGraphicsRequirementsKHR( outContext.instance, outContext.systemId,
																  &graphicsRequirements ));

	XrGraphicsBindingOpenGLESAndroidKHR graphics_binding = {
			.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
			.display = egl_display,
			.config = egl_config,
			.context = egl_context,
	};
	XrSessionCreateInfo session_create_info = {
			.type = XR_TYPE_SESSION_CREATE_INFO,
			.next = &graphics_binding,
			.systemId = outContext.systemId,
	};

	QUALIFY_XR( outContext, xrCreateSession( outContext.instance, &session_create_info, &outContext.session ));

	for ( const XrReferenceSpaceType referenceSpaceType: {
			XR_REFERENCE_SPACE_TYPE_STAGE,
			XR_REFERENCE_SPACE_TYPE_VIEW,
			XR_REFERENCE_SPACE_TYPE_LOCAL
	} )
	{
		XRQCreateReferenceSpace( outContext, referenceSpaceType );
	}

	if ( !SetupSocialEyeTracking( outContext ))
	{
		Log( LogWarning, "[XRQ] XRQCreateXRSession: Unable to setup social eye tracking" );
	}

	if ( !SetupFaceTracking( outContext ))
	{
		Log( LogWarning, "[XRQ] XRQCreateXRSession: Unable to setup face tracking" );
	}

	Log( "[XRQ] Created session successfully" );

	return true;
}

static XrEventDataBaseHeader *TryReadNextEvent( XRQContext &context )
{
	auto pBaseHeader = reinterpret_cast<XrEventDataBaseHeader *>(&context.lastEventDataBuffer);
	*pBaseHeader = {XR_TYPE_EVENT_DATA_BUFFER};
	const XrResult result = xrPollEvent( context.instance, &context.lastEventDataBuffer );

	switch ( result )
	{
		case XR_SUCCESS:
		{
			if ( pBaseHeader->type == XR_TYPE_EVENT_DATA_EVENTS_LOST )
			{
				auto eventsLost = reinterpret_cast<const XrEventDataEventsLost *>(pBaseHeader);
				Log( LogError, "[XRQ] %d OpenXR Events lost before this event in queue!",
						eventsLost->lostEventCount );
			}

			return pBaseHeader;
		}

		case XR_EVENT_UNAVAILABLE:
		{
			return nullptr;
		}

		default:
		{
			Log( LogError, "[XRQ] Unknown xrPollEvent return: %i", result );
			return nullptr;
		}
	}
}

static void SendEventCallback( XRQContext &context, XrqEvent event, XrqEventData data )
{
	if ( context.eventCallback )
	{
		context.eventCallback( event, data );
	}
}

static bool HandleSessionStateChangedEvent( XRQContext &context, const XrEventDataBaseHeader *pEvent )
{
	auto *sessionStateEvent = (XrEventDataSessionStateChanged *) pEvent;
	context.sessionState = sessionStateEvent->state;

	switch ( sessionStateEvent->state )
	{
		case XR_SESSION_STATE_IDLE:
		{
			Log( "[XRQ] Session transition to XR_SESSION_STATE_IDLE" );

			break;
		}

		case XR_SESSION_STATE_READY:
		{
			Log( "[XRQ] Session transition to XR_SESSION_STATE_READY" );

			XrSessionBeginInfo session_begin_info = {
					.type = XR_TYPE_SESSION_BEGIN_INFO,
					.primaryViewConfigurationType = context.viewType,
			};
			QUALIFY_XR( context, xrBeginSession( context.session, &session_begin_info ));

			context.bIsSessionRunning = true;
			context.bAppShouldSubmitFrames = true;

			SendEventCallback( context, XRQ_SESSION_STATE_READY, {} );

			break;
		}


		case XR_SESSION_STATE_FOCUSED:
		{
			Log( "[XRQ] Session transition to XR_SESSION_STATE_FOCUSED" );

			context.bAppShouldSubmitFrames = true;
			context.bIsSessionFocused = true;

			SendEventCallback( context, XRQ_SESSION_STATE_FOCUSED, {} );
			break;
		}

		case XR_SESSION_STATE_VISIBLE:
		{
			Log( "[XRQ] Session transition to XR_SESSION_STATE_VISIBLE" );

			context.bAppShouldSubmitFrames = true;
			context.bIsSessionFocused = false;

			SendEventCallback( context, XRQ_SESSION_STATE_VISIBLE, {} );

			break;
		}

		case XR_SESSION_STATE_SYNCHRONIZED:
		{
			Log( "[XRQ] Session transition to XR_SESSION_STATE_SYNCHRONIZED" );

			context.bAppShouldSubmitFrames = true;

			break;
		}

		case XR_SESSION_STATE_STOPPING:
		{
			Log( "[XRQ] Session transition to XR_SESSION_STATE_STOPPING" );

			if ( context.bIsSessionRunning )
			{
				QUALIFY_XR( context, xrEndSession( context.session ));
				context.bIsSessionRunning = false;
			}

			SendEventCallback( context, XRQ_SESSION_STATE_STOPPING, {} );

			context.bAppShouldSubmitFrames = false;

			break;
		}

		case XR_SESSION_STATE_LOSS_PENDING:
		{
			Log( "[XRQ] Session transition to XR_SESSION_STATE_LOSS_PENDING" );
			QUALIFY_XR( context, xrDestroySession( context.session ));

			break;
		}

		case XR_SESSION_STATE_EXITING:
		{
			Log( "[XRQ] Session transition to XR_SESSION_STATE_EXITING" );

			SendEventCallback( context, XRQ_SHUTDOWN, {} );
			break;
		}

		default:
		{
			Log( LogWarning, "[XRQ] Unhandled OpenXR Session state: %i", sessionStateEvent->state );

			break;
		}
	}

	return true;
}

bool XRQHandleEvents( XRQContext &context )
{
	while ( const XrEventDataBaseHeader *pEvent = TryReadNextEvent( context ))
	{
		switch ( pEvent->type )
		{
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			{
				context.bAppShouldSubmitFrames = false;
				SendEventCallback( context, XRQ_SHUTDOWN, {} );

				break;
			}

			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			{
				if ( !HandleSessionStateChangedEvent( context, pEvent ))
				{
					return true;
				}

				break;
			}

			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			{
				Log( "[XRQ] Received reference space change pending event" );

				auto pReferenceSpaceChangeData = reinterpret_cast<XrEventDataReferenceSpaceChangePending &>(pEvent);
				SendEventCallback( context, XRQ_REFERENCE_SPACE_CHANGE_PENDING, pReferenceSpaceChangeData );
				break;
			}

			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
			{
				Log( "[XRQ] Received interaction profile changed event" );

				SendEventCallback( context, XRQ_INTERACTION_PROFILE_CHANGED, {} );
				break;
			}

			default:
			{
				Log( LogWarning, "[XRQ] Unhandled OpenXR Event: %i", pEvent->type );

				break;
			}
		}
	}

	return true;
}

bool XRQWaitFrame( XRQContext &context )
{
	context.currentFrameState = {.type = XR_TYPE_FRAME_STATE, .next = nullptr};
	XrFrameWaitInfo frame_wait_info = {.type = XR_TYPE_FRAME_WAIT_INFO, .next = nullptr};
	QUALIFY_XR( context,
				xrWaitFrame( context.session, &frame_wait_info, &context.currentFrameState ));

	return true;
}

bool XRQLocateViewsFrame( XRQContext &context )
{
	XrViewLocateInfo view_locate_info = {
			.type = XR_TYPE_VIEW_LOCATE_INFO,
			.next = nullptr,
			.viewConfigurationType =
			XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			.displayTime = context.currentFrameState.predictedDisplayTime,
			.space = context.mapReferenceSpaceSpaces[ context.playSpace ]
	};
	uint32_t view_count;

	XrViewState view_state = {.type = XR_TYPE_VIEW_STATE, .next = nullptr};
	QUALIFY_XR( context, xrLocateViews( context.session, &view_locate_info, &view_state,
										context.unViewCount, &view_count, context.vCurrentFrameViews.data()));

	return true;
}

bool XRQGetTimeNow( const XRQContext &context, XrTime &out_time )
{
	if ( !XRQIsExtensionAvailable( context, XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME ))
	{
		Log( "[XRQ] XR_KHR_convert_timespec_time is not available on this runtime" );
		return false;
	}

	timespec time{};
	clock_gettime( CLOCK_MONOTONIC, &time );

	QUALIFY_XR( context, xrConvertTimespecTimeToTimeKHR( context.instance, &time, &out_time ));

	return true;
}

bool
XRQLocateViewsInReferenceSpace( const XRQContext &context, XrTime time, XrSpace space, std::vector<XrView> &vOutViews )
{
	XrViewLocateInfo view_locate_info = {
			.type = XR_TYPE_VIEW_LOCATE_INFO,
			.next = nullptr,
			.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			.displayTime = time,
			.space = space,
	};
	uint32_t view_count;

	XrViewState view_state = {.type = XR_TYPE_VIEW_STATE, .next = nullptr};
	QUALIFY_XR( context, xrLocateViews( context.session, &view_locate_info, &view_state,
										vOutViews.size(), &view_count, vOutViews.data()));

	return true;
}

bool XRQLocateViewsAtTime( XRQContext &context, const XrTime time, std::vector<XrView> &vOutViews )
{
	XrSpace locateSpace;
	XRQGetReferenceSpace( context, context.playSpace, locateSpace );

	return XRQLocateViewsInReferenceSpace( context, time, locateSpace, vOutViews );
}

bool XRQSetProjectionViewsFromCurrentFrameViews( const XRQContext &context,
												 std::array<XrCompositionLayerProjectionView, 2> &projectionViews )
{
	for ( int i = 0; i < 2; i++ )
	{
		projectionViews[ i ].pose = context.vCurrentFrameViews[ i ].pose;
		projectionViews[ i ].fov = context.vCurrentFrameViews[ i ].fov;
	}

	return true;
}

bool XRQLocateReferenceSpace( XRQContext &context, XrReferenceSpaceType referenceSpaceType, XrReferenceSpaceType baseSpaceType,
							  XrTime time, XrSpaceLocation &outSpaceLocation )
{
	XrSpace locateSpace;
	if( !XRQGetReferenceSpace( context, referenceSpaceType, locateSpace ) )
	{
		Log("[XRQ] XRQLocateReferenceSpace: Failed to locate reference space type: %i", referenceSpaceType );
		return false;
	}

	XrSpace baseSpace;
	if( !XRQGetReferenceSpace( context, baseSpaceType, baseSpace ))
	{
		Log("[XRQ] XRQLocateReferenceSpace: Failed to locate base reference space type: %i", referenceSpaceType );
		return false;
	}

	QUALIFY_XR( context, xrLocateSpace(locateSpace, baseSpace, time, &outSpaceLocation ) );

	return true;
}

bool XRQLocateReferenceSpace( XRQContext &context, XrReferenceSpaceType referenceSpaceType, XrTime time,
							  XrSpaceLocation &outSpaceLocation )
{
	return XRQLocateReferenceSpace( context, referenceSpaceType, context.playSpace, time, outSpaceLocation );
}

bool XRQLocateReferenceSpaceAtFrameTime( XRQContext &context, XrReferenceSpaceType referenceSpaceType,
										 XrSpaceLocation &outSpaceLocation )
{
	return XRQLocateReferenceSpace( context, referenceSpaceType, context.currentFrameState.predictedDisplayTime,outSpaceLocation );
}

static bool XRQAcquireSwapchainImage( XrSwapchain swapchain, uint32_t &index )
{
	static const XrSwapchainImageAcquireInfo acquire_info = {
			.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
			.next = nullptr,
	};
	QUALIFY_XR_MIN( xrAcquireSwapchainImage( swapchain, &acquire_info, &index ));

	static const XrSwapchainImageWaitInfo wait_info = {
			.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
			.next = nullptr,
			.timeout = XR_INFINITE_DURATION,
	};
	QUALIFY_XR_MIN( xrWaitSwapchainImage( swapchain, &wait_info ));

	return true;
}

static bool XRQReleaseSwapchain( XrSwapchain swapchain )
{
	static const XrSwapchainImageReleaseInfo &release_info = {
			.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
			.next = nullptr,
	};

	QUALIFY_XR_MIN( xrReleaseSwapchainImage( swapchain, &release_info ));

	return true;
}

XRQAcquireSwapchainImageRAII::XRQAcquireSwapchainImageRAII( const XRQSwapchain &swapchain )
		: m_swapchain( swapchain.swapchain )
{
	XRQAcquireSwapchainImage( m_swapchain, m_unAcquiredIndex );
}

const uint32_t &XRQAcquireSwapchainImageRAII::GetAcquiredImageIndex() const
{
	return m_unAcquiredIndex;
}

XRQAcquireSwapchainImageRAII::~XRQAcquireSwapchainImageRAII()
{
	XRQReleaseSwapchain( m_swapchain );
}


XrPath XRQStringToXrPath( XrInstance instance, const std::string &path )
{
	XrPath xrPath;
	QUALIFY_XR_MIN( xrStringToPath( instance, path.c_str(), &xrPath ));

	return xrPath;
}

XrPath XRQStringToXrPath( const XRQContext &context, const std::string &path )
{
	return XRQStringToXrPath( context.instance, path );
}

bool XRQXrPathToString( XrInstance instance, const XrPath path, std::string &out_path )
{
	char buffer[XR_MAX_PATH_LENGTH];
	uint32_t written;

	QUALIFY_XR_MIN( xrPathToString( instance, path, sizeof(buffer), &written, buffer ));

	out_path = buffer;

	return true;
}

bool XRQXrPathToString( const XRQContext &context, const XrPath path, std::string &out_path )
{
	return XRQXrPathToString( context.instance, path, out_path );
}

bool XRQGetPlayAreaBoundsRect( const XRQContext &context, XrExtent2Df &out_bounds )
{
	QUALIFY_XR( context,
				xrGetReferenceSpaceBoundsRect( context.session, context.playSpace, &out_bounds ));

	return true;
}

bool XRQGetReferenceSpace( const XRQContext &context, XrReferenceSpaceType spaceType, XrSpace &outSpace )
{
	auto pos = context.mapReferenceSpaceSpaces.find( spaceType );
	if ( pos != context.mapReferenceSpaceSpaces.end())
	{
		outSpace = pos->second;
		return true;
	}

	outSpace = XR_NULL_HANDLE;
	return false;
}

bool XRQEnumerateSupportedRefreshRates( XRQContext &context,
										std::vector<float> &out_supported_refresh_rates )
{
	if ( !XRQIsExtensionAvailable( context, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME ))
	{
		Log( "[XRQ] XR_EXT_display_refresh_rate is not available on this runtime" );
		return false;
	}

	uint32_t count;
	QUALIFY_XR( context, xrEnumerateDisplayRefreshRatesFB( context.session, 0, &count, nullptr ));

	out_supported_refresh_rates.resize( count );
	QUALIFY_XR( context, xrEnumerateDisplayRefreshRatesFB( context.session, count, &count,
														   out_supported_refresh_rates.data()));

	return true;
}

bool XRQRequestRefreshRate( XRQContext &context, float refresh_rate )
{
	if ( !XRQIsExtensionAvailable( context, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME ))
	{
		Log( "[XRQ] XR_EXT_display_refresh_rate is not available on this runtime" );
		return false;
	}

	QUALIFY_XR( context, xrRequestDisplayRefreshRateEXT( context.session, refresh_rate ));

	Log( "[XRQ] XRQRequestRefreshRate: Set refresh rate to %.1fHz", refresh_rate );

	return true;
}

bool XRQRequestHighestRefreshRate( XRQContext &context )
{
	std::vector<float> vRefreshRates{};
	if ( !XRQEnumerateSupportedRefreshRates( context, vRefreshRates ))
	{
		Log( LogError, "[XRQ] XRQRequestHighestRefreshRate: Failed to enumerate refresh rates" );
		return false;
	}

	if ( vRefreshRates.empty())
	{
		Log( LogWarning, "[XRQ] XRQRequestHighestRefreshRate: no refresh rates returned from enumerate" );
		return false;
	}

	return XRQRequestRefreshRate( context, vRefreshRates.back());
}

XrPath XRQGetSubactionPathForHand( const XRQContext &context, XRQHand hand )
{
	switch ( hand )
	{
		case XRQ_HAND_LEFT:
		{
			XrPath result;
			QUALIFY_XR( context, xrStringToPath( context.instance, "/user/hand/left", &result ));

			return result;
		}

		case XRQ_HAND_RIGHT:
		{
			XrPath result;
			QUALIFY_XR( context, xrStringToPath( context.instance, "/user/hand/right", &result ));

			return result;
		}

		case XRQ_HAND_BOTH:
		case XRQ_HAND_NONE:
		{
			return XR_NULL_PATH;
		}

		default:
		{
			Log( LogError, "[XRQ] XRQGetSubactionPathForHand: Invalid hand: %i to get subaction path for", hand );
			return XR_NULL_PATH;
		}
	}
}

bool XRQGetHandSubactionPaths( const XRQContext &context, XRQHand hand, std::vector<XrPath> &outHandSubactionPaths )
{
	outHandSubactionPaths = {};
	if ( hand == XRQ_HAND_LEFT || hand == XRQ_HAND_BOTH )
	{
		outHandSubactionPaths.emplace_back( XRQGetSubactionPathForHand( context, XRQ_HAND_LEFT ));
	}
	if ( hand == XRQ_HAND_RIGHT || hand == XRQ_HAND_BOTH )
	{
		outHandSubactionPaths.emplace_back( XRQGetSubactionPathForHand( context, XRQ_HAND_RIGHT ));
	}

	return true;
}

bool XRQCreateAndRegisterActionSetForAttach( XRQContext &context, const XRQActionSetCreateInfo &actionSetCreateInfo,
											 XrActionSet &outActionSet )
{
	XrActionSetCreateInfo xrActionSetCreateInfo = {
			.type = XR_TYPE_ACTION_SET_CREATE_INFO,
			.next = nullptr,
	};
	actionSetCreateInfo.sActionSetName.copy( xrActionSetCreateInfo.actionSetName,
											 actionSetCreateInfo.sActionSetName.size());
	actionSetCreateInfo.sLocalizedActionSetName.copy( xrActionSetCreateInfo.localizedActionSetName,
													  actionSetCreateInfo.sLocalizedActionSetName.size());

	QUALIFY_XR( context, xrCreateActionSet( context.instance, &xrActionSetCreateInfo, &outActionSet ));

	context.vecActionSets.push_back( outActionSet );

	return true;
}

bool XRQCreateActionAndRegisterSuggestedBindings( XRQContext &context, const XrActionSet actionSet,
												  const XRQActionCreateInfo &actionCreateInfo,
												  XRQAction &outAction )
{
	std::vector<XrPath> vecSubactionPaths;
	XRQGetHandSubactionPaths( context, actionCreateInfo.subActionPathHand, vecSubactionPaths );

	XrActionCreateInfo xrActionCreateInfo = {
			.type = XR_TYPE_ACTION_CREATE_INFO,
			.next = nullptr,
			.actionType = actionCreateInfo.actionType,
			.countSubactionPaths = (uint32_t) vecSubactionPaths.size(),
			.subactionPaths = vecSubactionPaths.data(),
	};
	actionCreateInfo.sActionName.copy( xrActionCreateInfo.actionName, actionCreateInfo.sActionName.size());
	actionCreateInfo.sActionName.copy( xrActionCreateInfo.localizedActionName,
									   actionCreateInfo.sActionName.size());

	XrAction action;
	QUALIFY_XR( context, xrCreateAction( actionSet, &xrActionCreateInfo, &action ));

	outAction = {
			.actionSet = actionSet,
			.action = action,
			.actionType = actionCreateInfo.actionType,
			.vecSubactionPaths = vecSubactionPaths,
			.sActionName = actionCreateInfo.sActionName,
	};

	if ( actionCreateInfo.actionType == XR_ACTION_TYPE_POSE_INPUT )
	{
		if ( vecSubactionPaths.empty())
		{
			XrActionSpaceCreateInfo actionSpaceCreateInfo = {
					.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
					.next = nullptr,
					.action = action,
					.subactionPath = XR_NULL_PATH,
					.poseInActionSpace = !actionCreateInfo.poseInActionSpace.empty()
										 ? actionCreateInfo.poseInActionSpace[ 0 ] : k_identityPose,
			};

			outAction.vecActionSpaces.resize( 1 );
			QUALIFY_XR( context, xrCreateActionSpace( context.session, &actionSpaceCreateInfo,
													  &outAction.vecActionSpaces[ 0 ] ));
		}
		else
		{
			for ( int i = 0; i < vecSubactionPaths.size(); ++i )
			{
				XrActionSpaceCreateInfo actionSpaceCreateInfo = {
						.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
						.next = nullptr,
						.action = action,
						.subactionPath = vecSubactionPaths[ i ],
						.poseInActionSpace = actionCreateInfo.poseInActionSpace.size() > i
											 ? actionCreateInfo.poseInActionSpace[ i ] : k_identityPose,
				};

				XrSpace space;
				QUALIFY_XR( context, xrCreateActionSpace( context.session, &actionSpaceCreateInfo, &space ));

				outAction.vecActionSpaces.push_back( space );
			}
		}
	}

	for ( auto &[sInteractionProfile, vecBindings]: actionCreateInfo.mapInteractionProfileBindings )
	{
		XrPath interactionProfilePath = XRQStringToXrPath( context, sInteractionProfile );

		if ( !context.mapInteractionProfileBindings.contains( interactionProfilePath ))
		{
			context.mapInteractionProfileBindings[ interactionProfilePath ] = {};
		}

		for ( auto &sBinding: vecBindings )
		{
			XrPath bindingPath = XRQStringToXrPath( context, sBinding );
			XrActionSuggestedBinding suggestedBinding = {
					.action = action,
					.binding = bindingPath,
			};
			context.mapInteractionProfileBindings[ interactionProfilePath ].push_back( suggestedBinding );
		}
	}

	return true;
}

bool XRQSuggestRegisteredInteractionProfileBindings( XRQContext &context )
{
	for ( auto &[interactionProfile, vecBindings]: context.mapInteractionProfileBindings )
	{
		std::string sInteractionProfile;
		XRQXrPathToString(context, interactionProfile, sInteractionProfile);

		Log("[XRQ] Suggesting bindings for %s", sInteractionProfile.c_str());

		XrInteractionProfileSuggestedBinding interactionProfileSuggestedBinding = {
				.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
				.next = nullptr,
				.interactionProfile = interactionProfile,
				.countSuggestedBindings = (uint32_t) vecBindings.size(),
				.suggestedBindings = vecBindings.data(),
		};

		XrResult result = xrSuggestInteractionProfileBindings( context.instance, &interactionProfileSuggestedBinding );
		if ( result != XR_SUCCESS )
		{
			std::string sInteractionProfileName;
			XRQXrPathToString( context, interactionProfile, sInteractionProfileName );
			Log( LogWarning, "[XRQ] Failed to suggest interaction profile bindings for %s with error %i. Skipping.",
					sInteractionProfileName.c_str(), result );
		}
	}

	return true;
}

bool XRQRegisterActionSetForSync( XRQContext& context, const XrActionSet actionSet, XRQHand hand )
{
	XrPath subactionPath = XRQGetSubactionPathForHand(context, hand);
	XrActiveActionSet activeActionSet = {
			.actionSet = actionSet,
			.subactionPath = subactionPath,
	};
	context.vActiveActionSets.emplace_back( activeActionSet );

	return true;
}

bool XRQSyncRegisteredActiveActionSets( const XRQContext& context )
{
	XrActionsSyncInfo syncInfo = {
			.type = XR_TYPE_ACTIONS_SYNC_INFO,
			.next = nullptr,
			.countActiveActionSets = ( uint32_t ) context.vActiveActionSets.size(),
			.activeActionSets = context.vActiveActionSets.data(),
	};
	QUALIFY_XR( context, xrSyncActions( context.session, &syncInfo ));

	return true;
}

bool XRQAttachRegisteredActionSets( XRQContext &context )
{
	XrSessionActionSetsAttachInfo attachInfo = {
			.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
			.next = nullptr,
			.countActionSets = (uint32_t) context.vecActionSets.size(),
			.actionSets = context.vecActionSets.data(),
	};
	QUALIFY_XR( context, xrAttachSessionActionSets( context.session, &attachInfo ));

	return true;
}

bool XRQGetFaceTracking( XRQContext &context, XrFaceExpressionWeights2FB &outFaceExpressionWeights )
{
	XrTime timeNow;
	XRQGetTimeNow( context, timeNow );

	XrFaceExpressionInfo2FB expressionInfo2FB = {
			.type = XR_TYPE_FACE_EXPRESSION_INFO2_FB,
			.next = nullptr,
			.time = context.currentFrameState.predictedDisplayTime,
	};

	QUALIFY_XR( context,
				xrGetFaceExpressionWeights2FB( context.faceTracker, &expressionInfo2FB, &outFaceExpressionWeights ));

	return true;
}

bool XRQEnumerateColorSpaces( const XRQContext& context, std::vector<XrColorSpaceFB>& vOutColorSpaces )
{
	if( !XRQIsExtensionAvailable(context, XR_FB_COLOR_SPACE_EXTENSION_NAME) )
	{
		Log(LogError, "[XRQ] XRQSetColorSpace: Did not set color space as XR_FB_color_space was not available");

		return false;
	}

	uint32_t unColorSpaceCount;
	QUALIFY_XR( context, xrEnumerateColorSpacesFB( context.session, 0, &unColorSpaceCount, nullptr ) );

	vOutColorSpaces.resize( unColorSpaceCount );

	QUALIFY_XR( context, xrEnumerateColorSpacesFB( context.session, vOutColorSpaces.size(), &unColorSpaceCount, vOutColorSpaces.data() ) );
	return true;
}

bool XRQSetColorSpace( const XRQContext& context, XrColorSpaceFB colorSpace )
{
	if( !XRQIsExtensionAvailable(context, XR_FB_COLOR_SPACE_EXTENSION_NAME) )
	{
		Log(LogError, "[XRQ] XRQSetColorSpace: Did not set color space as XR_FB_color_space was not available");

		return false;
	}

	QUALIFY_XR( context, xrSetColorSpaceFB(context.session, colorSpace));
	return true;
}

bool XRQSetApplicationThread( const XRQContext& context, XrAndroidThreadTypeKHR threadType )
{
	if( !XRQIsExtensionAvailable( context, XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME ) )
	{
		Log( LogError, "[XRQ SetApplicationThread: Failed to set application thread as extension was not available" );
		return false;
	}

	QUALIFY_XR( context, xrSetAndroidApplicationThreadKHR( context.session, threadType, gettid() ) );
	return true;
}

bool XRQLocateHandJoints( XRQContext& context, XRQHand hand, XrTime time, XrHandJointLocationsEXT& outJointLocations )
{
	std::lock_guard<std::mutex> lock( context.mutHandTrackerMutex );

	if( !context.bIsHandTrackingSupported || !context.bIsHandTrackingSetup || !context.handTracker[ hand ] )
	{
		return false;
	}

	XrSpace baseSpace;
	XRQGetReferenceSpace( context, context.playSpace, baseSpace );

	XrHandJointsLocateInfoEXT locateInfoExt = {
			.type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
			.next = nullptr,
			.baseSpace = baseSpace,
			.time = time,
	};
	QUALIFY_XR( context, xrLocateHandJointsEXT( context.handTracker[ hand ], &locateInfoExt, &outJointLocations ) );

	if( outJointLocations.isActive )
	{
		context.vHandTrackingDataReceived[ hand ] = true;
	}

	return true;
}

bool XRQIsLocationValid( const XrSpaceLocationFlags locationFlags )
{
	return locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT &&
			locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT;
}

bool XRQIsVelocityValid( const XrSpaceVelocityFlags velocityFlags )
{
	return velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
}

bool XRQDestroyAction( XRQAction &action )
{
	if ( action.action != XR_NULL_HANDLE )
	{
		CHECK_XR_MIN( xrDestroyAction( action.action ));
		action.action = XR_NULL_HANDLE;
	}

	for ( auto &actionSpace: action.vecActionSpaces )
	{
		if ( actionSpace != XR_NULL_HANDLE )
		{
			CHECK_XR_MIN( xrDestroySpace( actionSpace ));
			actionSpace = XR_NULL_HANDLE;
		}
	}

	return true;
}

bool XRQDestroyActionSet( XrActionSet &actionSet )
{
	if ( actionSet != XR_NULL_HANDLE )
	{
		CHECK_XR_MIN( xrDestroyActionSet( actionSet ));
		actionSet = XR_NULL_HANDLE;
	}

	return true;
}

bool XRQDestroyHandTrackers( XRQContext& context )
{
	std::lock_guard<std::mutex> lock( context.mutHandTrackerMutex );

	if( context.bIsHandTrackingSetup )
	{
		Log( LogWarning, "[XRQ] Hand tracking was not setup, not destroying" );
		return true;
	}

	for( int i = 0; i < context.handTracker.size(); i++ )
	{
		if( !context.handTracker[ i ] )
		{
			continue;
		}

		if( XrResult result = xrDestroyHandTrackerEXT(context.handTracker[ i ] ) )
		{
			Log( LogError, "[XRQ] Failed to destroy hand tracker (%s): %i", i == 0 ? "left" : "right", result );
		}

		context.handTracker[ i ] = XR_NULL_HANDLE;
	}

	context.bIsHandTrackingSetup = false;

	return true;
}

bool XRQRequestExitSession( XRQContext &context )
{
	QUALIFY_XR( context, xrRequestExitSession( context.session ));

	return true;
}

bool XRQTeardown( XRQContext &context )
{
	Log( "[XRQ] XRQTeardown: bIsSessionRunning = %d", context.bIsSessionRunning );

	if ( context.session != XR_NULL_HANDLE )
	{
		CHECK_XR( context, xrDestroySession( context.session ));
		Log( "[XRQ] XRQTeardown: Destroyed session" );
		context.session = XR_NULL_HANDLE;
	}

	if ( context.instance != XR_NULL_HANDLE )
	{
		CHECK_XR( context, xrDestroyInstance( context.instance ));
		Log( "[XRQ] XRQTeardown: Destroyed instance" );
		context.instance = XR_NULL_HANDLE;
	}

	context.systemId = XR_NULL_SYSTEM_ID;
	context.setAvailableExtensions = {};
	context.setEnabledExtensions = {};
	context.vecActionSets = {};
	context.mapInteractionProfileBindings = {};
	context.vActiveActionSets = {};
	context.sUsingInteractionProfile.clear();

	return true;
}