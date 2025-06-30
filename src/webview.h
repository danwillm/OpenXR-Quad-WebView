#pragma once

#include <string>
#include "android_native_app_glue.h"

#include "glutils.h"

#include <thread>
#include <queue>

struct WebViewInfo
{
	jobject canvas = nullptr;
	jobject bitmap = nullptr;
	jobject webView = nullptr;
	jobjectArray messageChannels = nullptr;
	jobject looper = nullptr;

	int32_t nWidth = 0;
	int32_t nHeight = 0;

	std::string sBaseUrl;
};

struct WebViewInput
{
	std::string sInput;
};

struct WebViewOutput
{
	std::string sOutput;
};

class WebView: public std::enable_shared_from_this<WebView> {
public:
	static std::shared_ptr<WebView> Create( int32_t nWidth, int32_t nHeight, std::string sBaseUrl );

	std::shared_ptr<WebView> GetPtr() { return shared_from_this(); }
	std::weak_ptr<WebView> GetWeakPtr() { return weak_from_this(); }

	//assumes that texture is the same size as the webview
	void CopyContentsToTexture( GLuint texture );

    void CopyDebugContentsToTexture( GLuint texture );

	void RequestDraw();

	void RequestPause();

	void RequestResume();

	static void OnAppClose();

	~WebView();

private:

	WebView( int32_t nWidth, int32_t nHeight, std::string sBaseUrl );

	WebView() = delete;

	void WebViewThread();

	void UIThread_SetupWebView();

	void UIThread_InitializeMessageChannels();

	void UIThread_Draw();

	void UIThread_PauseWebView();

	void UIThread_ResumeWebView();

	WebViewInfo m_webViewInfo;

    uint8_t *m_bufferbytes = nullptr;
	jobject m_buffer = nullptr;

	jclass m_WVTcWebView = nullptr;
	jclass m_WVTcCanvas = nullptr;
	jclass m_WVTcBitmap = nullptr;
	jclass m_WVTcByteBuffer = nullptr;

	jmethodID m_WVTmWebviewDraw = nullptr;
	jmethodID m_WVTmCanvasDrawColor = nullptr;
	jmethodID m_WVTmBitmapCopyPixelsToBuffer = nullptr;
	jmethodID m_WVTmBufferRewind = nullptr;

	jobject m_WVToPorterDuffClear = nullptr;

	std::queue<WebViewInput> m_inputQueue;
	std::queue<WebViewOutput> m_outputQueue;

	std::thread m_webViewThread;
	std::atomic<bool> m_bIsRunning = false;

	std::atomic<bool> m_bIsWebViewSetup = false;

	std::atomic<bool> m_bIsWebviewMessagesChannelsInitialized = false;

	std::atomic<bool> m_bIsDrawing = false;

	std::mutex m_mutWebView;
	std::mutex m_mutPixelBuffer;

	std::mutex m_mutMessageQueueMutex;
	std::vector<std::string> m_vMessagesQueuedForWebViewReady{};
};