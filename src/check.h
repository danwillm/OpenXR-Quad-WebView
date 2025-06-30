#pragma once

#include <GLES3/gl32.h>
#include "log.h"
#include <string>

#define QUALIFY_XR( context, func )                                                                                             \
    do {                                                                                                                      \
        XrResult xrq_check_xr_result = func;                                                                                  \
        if (!XR_SUCCEEDED(xrq_check_xr_result)) {                                                                             \
                if(context.instance == XR_NULL_HANDLE) {                                                                      \
                    Log(LogError, "[CheckXR] Failed to call %s %s:%i with error: %i", __PRETTY_FUNCTION__, __FILE__, __LINE__, xrq_check_xr_result);          \
                    return false;                                                                                             \
                }                                                                                                             \
                                                                                                                              \
            char xrq_check_xr_error[XR_MAX_RESULT_STRING_SIZE];                                                               \
            xrResultToString(context.instance, xrq_check_xr_result, xrq_check_xr_error);                                      \
            std::string s_xrq_check_xr_error(xrq_check_xr_error);                                                             \
                                                                                                                              \
            Log(LogError, "[CheckXR] Failed to call %s with error: %s", #func, s_xrq_check_xr_error.c_str());                                  \
            return false;                                                                                                     \
        }                                                                                                                     \
    } while (false)


#define QUALIFY_XR_VOID( instance, func )                                                                                       \
    do {                                                                                                                      \
        XrResult xrq_check_xr_result = func;                                                                                  \
        if (!XR_SUCCEEDED(xrq_check_xr_result)) {                                                                             \
                if(instance == XR_NULL_HANDLE) {                                                                              \
                    Log(LogError, "[CheckXR] Failed to call %s %s:%i with error: %i", __PRETTY_FUNCTION__, __FILE__, __LINE__, xrq_check_xr_result);          \
                    return;                                                                                                   \
                }                                                                                                             \
                                                                                                                              \
            char xrq_check_xr_error[XR_MAX_RESULT_STRING_SIZE];                                                               \
            xrResultToString(instance, xrq_check_xr_result, xrq_check_xr_error);                                              \
            std::string s_xrq_check_xr_error(xrq_check_xr_error);                                                             \
                                                                                                                              \
            Log(LogError, "[CheckXR] Failed to call %s %s:%i with error: %s", #func, __FILE__, __LINE__, s_xrq_check_xr_error.c_str());     \
            return;                                                                                                           \
        }                                                                                                                     \
    } while (false)

#define QUALIFY_XR_MIN( func )                                                                                                  \
    do {                                                                                                                      \
        XrResult xrq_check_xr_result = func;                                                                                  \
        if (!XR_SUCCEEDED(xrq_check_xr_result)) {                                                                   \
            Log(LogError, "[CheckXR] Failed to call %s %s:%i with error: %i", __PRETTY_FUNCTION__, __FILE__, __LINE__, xrq_check_xr_result);          \
            return false;                                                                                                     \
        }                                                                                                                     \
    } while (false)

#define CHECK_XR( context, func )                                                                                           \
    do {                                                                                                                  \
        XrResult xrq_check_xr_result = func;                                                                              \
        if (!XR_SUCCEEDED(xrq_check_xr_result)) {                                                                         \
            if(context.instance == XR_NULL_HANDLE) {                                                                      \
                Log(LogError, "[CheckXR] Failed to call %s %s:%i with error: %i", __PRETTY_FUNCTION__, __FILE__, __LINE__, xrq_check_xr_result);          \
            } else {                                                                                                      \
                char xrq_check_xr_error[XR_MAX_RESULT_STRING_SIZE];                                                       \
                xrResultToString(context.instance, xrq_check_xr_result, xrq_check_xr_error);                              \
                std::string s_xrq_check_xr_error(xrq_check_xr_error);                                                     \
                Log( LogError, "[CheckXR] Failed to call %s with error: %s", #func, s_xrq_check_xr_error.c_str());                          \
            }                                                                                                             \
     }                                                                                                                    \
    } while (false)

#define CHECK_XR_MIN( func )                                                                                              \
    do {                                                                                                                  \
        XrResult xrq_check_xr_result = func;                                                                              \
        if (!XR_SUCCEEDED(xrq_check_xr_result)) {                                                                         \
            Log(LogError, "[CheckXR] Failed to call %s %s:%i with error: %i", __PRETTY_FUNCTION__, __FILE__, __LINE__, xrq_check_xr_result);          \
        }                                                                                                                    \
    } while (false)


static void CheckOpenGLError( const char *stmt, const char *fname, int line )
{
	GLenum err = glGetError();
	if ( err != GL_NO_ERROR )
	{
		std::string error_msg;

		switch ( err )
		{
			case GL_INVALID_OPERATION:
			{
				error_msg = "INVALID_OPERATION";
				break;
			}
			case GL_INVALID_ENUM:
			{
				error_msg = "INVALID_ENUM";
				break;
			}
			case GL_INVALID_VALUE:
			{
				error_msg = "INVALID_VALUE";
				break;
			}
			case GL_OUT_OF_MEMORY:
			{
				error_msg = "OUT_OF_MEMORY";
				break;
			}
			case GL_INVALID_FRAMEBUFFER_OPERATION:
			{
				error_msg = "INVALID_FRAMEBUFFER_OPERATION";
				break;
			}
			default:
			{
				error_msg = "UNKNOWN";
				break;
			}
		}

		Log( LogError, "[CheckGL] OpenGL error %i, %s, at %s:%i - for %s\n", err, error_msg.c_str(), fname, line,
				stmt );
	}
}

#define GL_CHECK( stmt ) stmt; \
                        CheckOpenGLError(#stmt, __FILE__, __LINE__) \

