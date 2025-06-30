cmake_minimum_required(VERSION 3.22.1)

project(mynativeapp)

set(CMAKE_CXX_STANDARD 20)

if (CMAKE_ANDROID_NDK)
    file(STRINGS "${CMAKE_ANDROID_NDK}/source.properties" NDK_PROPERTIES)
    foreach (_line ${NDK_PROPERTIES})
        if ("${_line}" MATCHES
                "Pkg.Revision = ([0-9]+)[.]([0-9]+)[.]([0-9]+)"
        )
            set(NDK_MAJOR_VERSION ${CMAKE_MATCH_1})
        endif ()
    endforeach ()
else ()
    message(FATAL_ERROR "Please set CMAKE_ANDROID_NDK to your NDK root!")
endif ()

if (NDK_MAJOR_VERSION)
    message(STATUS "Building using NDK major version ${NDK_MAJOR_VERSION}")
else ()
    message(
            FATAL_ERROR
            "Could not parse the major version from ${CMAKE_ANDROID_NDK}/source.properties"
    )
endif ()

find_library(ANDROID_LIBRARY NAMES android)
find_library(ANDROID_LOG_LIBRARY NAMES log)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(OpenGLES COMPONENTS V3 V2)
find_package(EGL)
if (OPENGLES_FOUND AND EGL_FOUND)
    set(XR_USE_GRAPHICS_API_OPENGL_ES TRUE)
    add_definitions(-DXR_USE_GRAPHICS_API_OPENGL_ES)
     message(STATUS "Enabling OpenGLES support")
elseif (BUILD_ALL_EXTENSIONS)
    message(FATAL_ERROR "OpenGLES not found")
endif ()

if (ANDROID)
    add_definitions(-DXR_USE_PLATFORM_ANDROID)
    add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR)
endif ()

if (ANDROID_PLATFORM_LEVEL LESS 24)
    message(FATAL_ERROR "Vulkan disabled due to incompatibility: need to target at least API 24")
endif ()

add_subdirectory(lib/OpenXR-SDK)
add_subdirectory(lib/glm)

add_library(openxr_webview SHARED src/main.cpp src/program.cpp src/log.cpp src/xrq.cpp src/xruipanel.cpp src/webview.cpp src/android.cpp src/glutils.cpp src/android_native_app_glue.cpp)

target_link_libraries(openxr_webview PRIVATE ${ANDROID_LIBRARY} ${ANDROID_LOG_LIBRARY} EGL GLESv3 glm openxr_loader)