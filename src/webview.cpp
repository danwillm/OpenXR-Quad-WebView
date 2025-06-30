#include <byteswap.h>
#include "webview.h"

#include "android.h"
#include "check.h"

#include <utility>

extern android_app *gApp;

static jobject s_contentView = nullptr;

enum JColor
{
	JCOLOR_TRANSPARENT = 0,
	JCOLOR_BLACK = -16777216,
	JCOLOR_GREEN = -16711936,
};

void WebView::UIThread_SetupWebView()
{
	SETUP_FOR_JAVA_CALL

	m_WVTcWebView = (jclass) env->NewGlobalRef( env->FindClass( "android/webkit/WebView" ) );
	m_WVTcCanvas = (jclass) env->NewGlobalRef(  env->FindClass( "android/graphics/Canvas" ) );
	m_WVTcBitmap = (jclass) env->NewGlobalRef( env->FindClass( "android/graphics/Bitmap" ) );
	m_WVTcByteBuffer = (jclass) env->NewGlobalRef( env->FindClass( "java/nio/ByteBuffer" ) );

	//methods don't need ot be made global refs
	m_WVTmWebviewDraw = env->GetMethodID( m_WVTcWebView, "draw", "(Landroid/graphics/Canvas;)V" );
	m_WVTmCanvasDrawColor = env->GetMethodID( m_WVTcCanvas, "drawColor", "(ILandroid/graphics/PorterDuff$Mode;)V" );
	m_WVTmBitmapCopyPixelsToBuffer = env->GetMethodID( m_WVTcBitmap, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V" );
	m_WVTmBufferRewind = env->GetMethodID( m_WVTcByteBuffer, "rewind", "()Ljava/nio/Buffer;" );

	//create webview
	jclass cActivity = env->FindClass( "android/app/Activity" );
	jmethodID mGetApplicationContext = env->GetMethodID( cActivity, "getApplicationContext","()Landroid/content/Context;" );
	jobject context = env->CallObjectMethod(gApp->activity->clazz, mGetApplicationContext );

	jmethodID mWebView = env->GetMethodID( m_WVTcWebView, "<init>", "(Landroid/content/Context;)V" );
	jobject webview = env->NewObject( m_WVTcWebView, mWebView, context );
	m_webViewInfo.webView = env->NewGlobalRef( webview );

	//general webview configuration
	{
		jclass cWebChromeClient = env->FindClass( "android/webkit/WebChromeClient" );
		jmethodID mWebChromeClient = env->GetMethodID( cWebChromeClient, "<init>", "()V" );
		jobject webChromeClient = env->NewObject( cWebChromeClient, mWebChromeClient );

		jmethodID mSetWebChromeClient = env->GetMethodID( m_WVTcWebView, "setWebChromeClient", "(Landroid/webkit/WebChromeClient;)V" );
		env->CallVoidMethod( webview, mSetWebChromeClient, webChromeClient );

		jmethodID mSetMeausredDimension = env->GetMethodID( m_WVTcWebView, "setMeasuredDimension", "(II)V" );
		env->CallVoidMethod( webview, mSetMeausredDimension, m_webViewInfo.nWidth, m_webViewInfo.nHeight );

		jclass cPorterDuffMode = env->FindClass( "android/graphics/PorterDuff$Mode" );
		jfieldID fPorterDuffClear = env->GetStaticFieldID( cPorterDuffMode, "CLEAR", "Landroid/graphics/PorterDuff$Mode;" );
		m_WVToPorterDuffClear = env->NewGlobalRef( env->GetStaticObjectField( cPorterDuffMode, fPorterDuffClear ));

		jmethodID mSetBackgroundColor = env->GetMethodID( m_WVTcWebView, "setBackgroundColor", "(I)V" );
		env->CallVoidMethod( webview, mSetBackgroundColor, (jint) JCOLOR_TRANSPARENT );
	}

	//webview settings initialization
	{
		jmethodID mGetSettings = env->GetMethodID( m_WVTcWebView, "getSettings", "()Landroid/webkit/WebSettings;" );
		jobject webSettings = env->CallObjectMethod( webview, mGetSettings );

		jclass cWebSettings = env->FindClass( "android/webkit/WebSettings" );

		jmethodID useWideViewport = env->GetMethodID( cWebSettings, "setUseWideViewPort", "(Z)V" );
		env->CallVoidMethod( webSettings, useWideViewport, JNI_TRUE );

		jmethodID setLoadWithOverviewMode = env->GetMethodID( cWebSettings, "setLoadWithOverviewMode", "(Z)V" );
		env->CallVoidMethod( webSettings, setLoadWithOverviewMode, JNI_TRUE );

		jmethodID setMediaPlaybackRequiresUserGesture = env->GetMethodID( cWebSettings,"setMediaPlaybackRequiresUserGesture","(Z)V" );
		env->CallVoidMethod( webSettings, setMediaPlaybackRequiresUserGesture, JNI_FALSE );

		jmethodID mSetLoadsImagesAutomatically = env->GetMethodID( cWebSettings, "setLoadsImagesAutomatically","(Z)V" );
		env->CallVoidMethod( webSettings, mSetLoadsImagesAutomatically, JNI_TRUE );

		jmethodID mSetJavaScriptEnabled = env->GetMethodID( cWebSettings, "setJavaScriptEnabled", "(Z)V" );
		env->CallVoidMethod( webSettings, mSetJavaScriptEnabled, JNI_TRUE );
	}

	std::scoped_lock<std::mutex> lock( m_mutWebView );

	//Load WebView
	{
		Log( "[WebView] Initializing webview with URL: %s", m_webViewInfo.sBaseUrl.c_str());

		jmethodID mLoadUrl = env->GetMethodID( m_WVTcWebView, "loadUrl", "(Ljava/lang/String;)V" );
		jstring jsUrl = env->NewStringUTF( m_webViewInfo.sBaseUrl.c_str());
		env->CallVoidMethod( webview, mLoadUrl, jsUrl );
	}

	//Create message channels
	{
		jmethodID mCreateWebMessageChannel = env->GetMethodID( m_WVTcWebView, "createWebMessageChannel","()[Landroid/webkit/WebMessagePort;" );
		jobjectArray messageChannels = (jobjectArray) env->CallObjectMethod( webview, mCreateWebMessageChannel );
		m_webViewInfo.messageChannels = (jobjectArray) env->NewGlobalRef( messageChannels );

		jclass cHandler = env->FindClass( "android/os/Handler" );
		jmethodID mHandler = env->GetMethodID( cHandler, "<init>", "(Landroid/os/Looper;)V" );
		jobject handler = env->NewObject( cHandler, mHandler, m_webViewInfo.looper );
		handler = env->NewGlobalRef( handler );

		jobject mc0 = env->GetObjectArrayElement( messageChannels, 0 ); // MC1 is handed over to javascript.
		jclass cWebMessagePort = env->FindClass( "android/webkit/WebMessagePort" );
		jmethodID mSetWebMessageCallback = env->GetMethodID( cWebMessagePort, "setWebMessageCallback","(Landroid/webkit/WebMessagePort$WebMessageCallback;Landroid/os/Handler;)V" );
		env->CallVoidMethod( mc0, mSetWebMessageCallback, (jobject) 0, handler );
	}

	//Setup bitmap and canvas
	{
		jclass cBitmapConfig = env->FindClass( "android/graphics/Bitmap$Config" );

		jstring jsBitmapMode = env->NewStringUTF( "ARGB_8888" );
		jmethodID mValueOf = env->GetStaticMethodID( cBitmapConfig, "valueOf","(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;" );
		jobject bitmapConfig = env->CallStaticObjectMethod( cBitmapConfig, mValueOf, jsBitmapMode );

		jmethodID mCreateBitmap = env->GetStaticMethodID( m_WVTcBitmap, "createBitmap","(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;" );
		jobject bitmap = env->CallStaticObjectMethod( m_WVTcBitmap, mCreateBitmap, m_webViewInfo.nWidth, m_webViewInfo.nHeight, bitmapConfig );
		m_webViewInfo.bitmap = env->NewGlobalRef( bitmap );

		jmethodID cCanvas = env->GetMethodID( m_WVTcCanvas, "<init>", "(Landroid/graphics/Bitmap;)V" );
		jobject canvas = env->NewObject( m_WVTcCanvas, cCanvas, bitmap );
		m_webViewInfo.canvas = env->NewGlobalRef( canvas );
	}

	//add webview to view
	{
		//Create relative layout to add webview to
		jclass cRelativeLayoutLayoutParams = env->FindClass( "android/widget/RelativeLayout$LayoutParams" );
		jmethodID mRelativeLayoutLayoutParams = env->GetMethodID( cRelativeLayoutLayoutParams, "<init>", "(II)V" );
		jobject RelativeLayoutParamsObject = env->NewObject( cRelativeLayoutLayoutParams, mRelativeLayoutLayoutParams, m_webViewInfo.nWidth, m_webViewInfo.nHeight );

		jclass cRelativeLayout = env->FindClass( "android/widget/RelativeLayout" );
		jmethodID mRelativeLayout = env->GetMethodID( cRelativeLayout, "<init>","(Landroid/content/Context;)V" );
		jobject relativeLayout = env->NewObject( cRelativeLayout, mRelativeLayout, context );

		jmethodID mAddView = env->GetMethodID( cRelativeLayout, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V" );
		env->CallVoidMethod( relativeLayout, mAddView, webview, RelativeLayoutParamsObject );

		//Create main content view which will contain all the webviews
		if ( !s_contentView )
		{
			jmethodID mGetWindow = env->GetMethodID( cActivity, "getWindow", "()Landroid/view/Window;" );
			jobject window = env->CallObjectMethod(gApp->activity->clazz, mGetWindow );

			jclass cWindow = env->FindClass( "android/view/Window" );
			jmethodID mSetFlags = env->GetMethodID( cWindow, "setFlags", "(II)V" );
			env->CallVoidMethod( window, mSetFlags, 16777216, 16777216 );

			jmethodID setLayerType = env->GetMethodID( cRelativeLayout, "setLayerType","(ILandroid/graphics/Paint;)V" );
			env->CallVoidMethod( relativeLayout, setLayerType, 2, (jobject) 0 );

			jmethodID setContentViewMethod = env->GetMethodID( cActivity, "setContentView", "(Landroid/view/View;)V" );
			env->CallVoidMethod(gApp->activity->clazz, setContentViewMethod, relativeLayout );

			s_contentView = env->NewGlobalRef( relativeLayout );
		}
		else
		{
			env->CallVoidMethod( s_contentView, mAddView, relativeLayout, RelativeLayoutParamsObject );
		}
	}

	m_bIsWebViewSetup = true;
	Log( "[WebView] WebView completed setup!" );
}

WebView::WebView( int32_t nWidth, int32_t nHeight, std::string sBaseUrl )
{
	m_webViewInfo = {
			.nWidth = nWidth,
			.nHeight = nHeight,
			.sBaseUrl = std::move( sBaseUrl ),
	};

	SETUP_FOR_JAVA_CALL

	m_bufferbytes = (uint8_t *) malloc( m_webViewInfo.nWidth * m_webViewInfo.nHeight * 4 );
	m_buffer = env->NewDirectByteBuffer( m_bufferbytes, m_webViewInfo.nWidth * m_webViewInfo.nHeight * 4 );
	m_buffer = env->NewGlobalRef( m_buffer );

	m_bIsRunning = true;
	m_webViewThread = std::thread( &WebView::WebViewThread, this );
}

std::shared_ptr<WebView> WebView::Create( int32_t nWidth, int32_t nHeight, std::string sBaseUrl )
{
	return std::shared_ptr<WebView>(new WebView( nWidth, nHeight, sBaseUrl ) );
}

void WebView::UIThread_InitializeMessageChannels()
{
	std::scoped_lock<std::mutex> lock( m_mutWebView );

	if ( !m_bIsWebViewSetup || m_bIsWebviewMessagesChannelsInitialized )
	{
		return;
	}

	SETUP_FOR_JAVA_CALL
	jmethodID mGetProgress = env->GetMethodID( m_WVTcWebView, "getProgress", "()I" );
	int nProgress = env->CallIntMethod( m_webViewInfo.webView, mGetProgress );

	if ( nProgress != 100 )
	{
		return;
	}

	jobject mc1 = env->GetObjectArrayElement( m_webViewInfo.messageChannels, 1 );

	jclass cWebMessagePort = env->FindClass( "android/webkit/WebMessagePort" );
	jobjectArray jvUseWebPorts = env->NewObjectArray( 1, cWebMessagePort, mc1 );

	jstring jsStr = env->NewStringUTF( "" );
	jclass cWebMessage = env->FindClass( "android/webkit/WebMessage" );
	jmethodID mWebMessage = env->GetMethodID( cWebMessage, "<init>","(Ljava/lang/String;[Landroid/webkit/WebMessagePort;)V" );
	jobject webMessage = env->NewObject( cWebMessage, mWebMessage, jsStr, jvUseWebPorts );

	jclass cUri = env->FindClass( "android/net/Uri" );
	jfieldID fEMPTY = env->GetStaticFieldID( cUri, "EMPTY", "Landroid/net/Uri;" );
	jobject emptyUri = env->GetStaticObjectField( cUri, fEMPTY );

	jmethodID mPostWebMessage = env->GetMethodID( m_WVTcWebView, "postWebMessage", "(Landroid/webkit/WebMessage;Landroid/net/Uri;)V" );
	env->CallVoidMethod( m_webViewInfo.webView, mPostWebMessage, webMessage, emptyUri );

	Log( "[WebView] WebView initialized message channels!" );
	m_bIsWebviewMessagesChannelsInitialized = true;
}

void WebView::WebViewThread()
{
	pthread_setname_np( pthread_self(), "SVLWebViewThread" );

	SETUP_FOR_JAVA_CALL

	jobject messageQueue;

	//get the message queue
	{
		jclass cLooper = env->FindClass( "android/os/Looper" );
		jmethodID mMyLooper = env->GetStaticMethodID( cLooper, "myLooper", "()Landroid/os/Looper;" );
		jobject thisLooper = env->CallStaticObjectMethod( cLooper, mMyLooper );

		std::scoped_lock<std::mutex> lock( m_mutWebView );

		if ( !thisLooper )
		{
			jmethodID prepareMethod = env->GetStaticMethodID( cLooper, "prepare", "()V" );
			env->CallStaticVoidMethod( cLooper, prepareMethod );
			thisLooper = env->CallStaticObjectMethod( cLooper, mMyLooper );
		}
		m_webViewInfo.looper = env->NewGlobalRef( thisLooper );

		jmethodID getQueueMethod = env->GetMethodID( cLooper, "getQueue", "()Landroid/os/MessageQueue;" );
		messageQueue = env->CallObjectMethod( m_webViewInfo.looper, getQueueMethod );
	}

	jclass cMessageQueue = env->FindClass( "android/os/MessageQueue" );
	jmethodID nextMethod = env->GetMethodID( cMessageQueue, "next", "()Landroid/os/Message;" );
	jclass cMessage = env->FindClass( "android/os/Message" );
	jfieldID fObj = env->GetFieldID( cMessage, "obj", "Ljava/lang/Object;" );
	jclass PairClass = env->FindClass( "android/util/Pair" );
	jfieldID fFirst = env->GetFieldID( PairClass, "first", "Ljava/lang/Object;" );

	Log("[WebView] Attempting to setup webview...");
	gApp->uiThreadCallbackHandler->post([pWeak = GetWeakPtr()]()
										 {
											 if ( auto pWebView = pWeak.lock() )
											 {
												 pWebView->UIThread_SetupWebView();
											 }
										 } );

	Log("[WebView] Waiting for webview to setup...");
	while ( m_bIsRunning && !m_bIsWebViewSetup )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( 1 ));
	}

	Log( "[WebView] WebView finished setup. Waiting for message channels to initialize..." );

	while ( m_bIsRunning && !m_bIsWebviewMessagesChannelsInitialized )
	{

		gApp->uiThreadCallbackHandler->post([pWeak = GetWeakPtr()]()
											 {
												 if ( auto pWebView = pWeak.lock() )
												 {
													 pWebView->UIThread_InitializeMessageChannels();
												 }
											 } );

		std::this_thread::sleep_for( std::chrono::milliseconds( 5 ));
	}
	Log( "[WebView] WebView Message channels initialized! Sending any queued messages..." );

	Log( "[WebView] WebView finished setup and is running" );

	char sFromJSBuffer[128];
	while ( m_bIsRunning )
	{
		jobject message = env->CallObjectMethod( messageQueue, nextMethod );
		if ( !message )
		{
			std::this_thread::yield();
			continue;
		}

		jobject messageObject = env->GetObjectField( message, fObj );
		const char *name;
		jstring strObj;
		jclass innerClass;

		// Check Object Type
		{
			innerClass = env->GetObjectClass( messageObject );
			jmethodID mid = env->GetMethodID( innerClass, "getClass", "()Ljava/lang/Class;" );
			jobject clsObj = env->CallObjectMethod( messageObject, mid );
			jclass clazzz = env->GetObjectClass( clsObj );
			mid = env->GetMethodID( clazzz, "getName", "()Ljava/lang/String;" );
			strObj = (jstring) env->CallObjectMethod( clsObj, mid );
			name = env->GetStringUTFChars( strObj, 0 );
		}

		//z5 - quest, U4 - pico
		if ( strcmp( name, "z5" ) == 0 || strcmp( name, "U4" ) == 0 )
		{
			jfieldID fString = env->GetFieldID( innerClass, "a", "[B" );
			jbyteArray jvMessageBytes = (jbyteArray) env->GetObjectField( messageObject, fString );
			int nMessageLength = env->GetArrayLength( jvMessageBytes );

			jboolean jbIsCopy = 0;
			jbyte *jpBuffer = env->GetByteArrayElements( jvMessageBytes, &jbIsCopy );

			if ( nMessageLength >= 6 )
			{
				const char *sDescription = (const char *) jpBuffer + 6;
				char csMessage[ nMessageLength - 5];
				memcpy( csMessage, sDescription, nMessageLength - 6 );
				csMessage[ nMessageLength - 6 ] = 0;
				snprintf( sFromJSBuffer, sizeof(sFromJSBuffer) - 1, "WebMessage: %s\n", csMessage );

				std::string sMessage( csMessage );
				std::string sMailboxName = sMessage.substr( 0, sMessage.find( ' ' ));
				std::string sMessageData = sMessage.substr( sMessage.find( ' ' ) + 1 );
			}
		}
		else
		{
			// MessagePayload is a org.chromium.content_public.browser.MessagePayload
			jobject messagePayload = env->GetObjectField((jobject) messageObject, fFirst );

			jclass cMessagePayload = env->GetObjectClass( messagePayload );

			// Get field "b" which is the web message payload.
			// If you are using binary sockets, it will be in `c` and be a byte array.
			jfieldID fMessagePayload = env->GetFieldID( cMessagePayload, "b", "Ljava/lang/String;" );
			jstring strObjDescr = (jstring) env->GetObjectField( messagePayload, fMessagePayload );

			const char *csMessage = env->GetStringUTFChars( strObjDescr, 0 );
			snprintf( sFromJSBuffer, sizeof( sFromJSBuffer ) - 1, "WebMessage: %s\n", csMessage );

			std::string sMessage( csMessage );
			std::string sMailboxName = sMessage.substr( 0, sMessage.find( ' ' ));
			std::string sMessageData = sMessage.substr( sMessage.find( ' ' ) + 1 );


			env->ReleaseStringUTFChars( strObjDescr, csMessage );
		}

		std::this_thread::yield();
	}
}

void WebView::UIThread_Draw()
{
	if ( !m_bIsWebviewMessagesChannelsInitialized || !m_bIsRunning )
	{
		return;
	}

	m_bIsDrawing = true;

	SETUP_FOR_JAVA_CALL

	{
		std::scoped_lock<std::mutex> lock( m_mutWebView );

		env->CallVoidMethod( m_webViewInfo.canvas, m_WVTmCanvasDrawColor, (jint) JCOLOR_TRANSPARENT, m_WVToPorterDuffClear );

		env->CallVoidMethod( m_webViewInfo.webView, m_WVTmWebviewDraw, m_webViewInfo.canvas );

		{
			std::scoped_lock<std::mutex> lock2( m_mutPixelBuffer );
			env->CallVoidMethod( m_webViewInfo.bitmap, m_WVTmBitmapCopyPixelsToBuffer, m_buffer );
		}

		env->CallObjectMethod( m_buffer, m_WVTmBufferRewind );
	}

	m_bIsDrawing = false;
}

void WebView::RequestDraw()
{
	if ( m_bIsDrawing )
	{
		return;
	}

	gApp->uiThreadCallbackHandler->post([pWeak = GetWeakPtr()]()
										 {
											 if ( auto pWebView = pWeak.lock() )
											 {
												 pWebView->UIThread_Draw();
											 }
										 } );
}

void WebView::UIThread_PauseWebView()
{
	if ( !m_bIsWebViewSetup )
	{
		Log( LogError, "[WebView] Cannot pause webview as it's not setup yet!" );
		return;
	}

	SETUP_FOR_JAVA_CALL

	jmethodID mOnPause = env->GetMethodID( m_WVTcWebView, "onPause", "()V" );
	env->CallVoidMethod( m_webViewInfo.webView, mOnPause );

	jmethodID mSetVisibility = env->GetMethodID( m_WVTcWebView, "setVisibility", "(I)V" );
	env->CallVoidMethod( m_webViewInfo.webView, mSetVisibility, 8 ); //GONE
}

void WebView::RequestPause()
{
	gApp->uiThreadCallbackHandler->post([pWeak = GetWeakPtr()]()
										 {
											 if ( auto pWebView = pWeak.lock() )
											 {
												 pWebView->UIThread_PauseWebView();
											 }
										 } );
}

void WebView::UIThread_ResumeWebView()
{
	if ( !m_bIsWebViewSetup )
	{
		Log( LogError, "[WebView] Cannot resume webview as it's not setup yet!" );
		return;
	}

	SETUP_FOR_JAVA_CALL

	jmethodID mOnPause = env->GetMethodID( m_WVTcWebView, "onResume", "()V" );
	env->CallVoidMethod( m_webViewInfo.webView, mOnPause );

	jmethodID mSetVisibility = env->GetMethodID( m_WVTcWebView, "setVisibility", "(I)V" );
	env->CallVoidMethod( m_webViewInfo.webView, mSetVisibility, 0 ); //VISIBLE
}

void WebView::RequestResume()
{
	gApp->uiThreadCallbackHandler->post([pWeak = GetWeakPtr()]()
										 {
											 if ( auto pWebView = pWeak.lock())
											 {
												 pWebView->UIThread_ResumeWebView();
											 }
										 } );
}


void WebView::CopyContentsToTexture( GLuint texture )
{
	if ( !m_bIsWebviewMessagesChannelsInitialized )
	{
		return;
	}

	{
		std::scoped_lock<std::mutex> lock( m_mutPixelBuffer );
		GL_CHECK( glBindTexture( GL_TEXTURE_2D, texture ));
		GL_CHECK( glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, m_webViewInfo.nWidth, m_webViewInfo.nHeight, GL_RGBA,
								   GL_UNSIGNED_BYTE, m_bufferbytes ));
		GL_CHECK( glBindTexture( GL_TEXTURE_2D, 0 ));
	}
}

void WebView::CopyDebugContentsToTexture(GLuint texture) {
    if ( !m_bIsWebviewMessagesChannelsInitialized )
    {
        return;
    }

    {
        std::scoped_lock<std::mutex> lock( m_mutPixelBuffer );
        GL_CHECK( glBindTexture( GL_TEXTURE_2D, texture ));

		for(unsigned int i = 0; i < m_webViewInfo.nWidth * m_webViewInfo.nHeight; i++) {
			m_bufferbytes[i * 4] = 255;
			m_bufferbytes[i * 4 + 1] = 0;
			m_bufferbytes[i * 4 + 2] = 255;
			m_bufferbytes[i * 4 + 3] = 255;
		}

        GL_CHECK( glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, m_webViewInfo.nWidth, m_webViewInfo.nHeight, GL_RGBA,
                                   GL_UNSIGNED_BYTE, m_bufferbytes ));

        GL_CHECK( glBindTexture( GL_TEXTURE_2D, 0 ));
    }
}

WebView::~WebView()
{
	Log( "[WebView] Closing webview" );

	m_bIsWebviewMessagesChannelsInitialized = false;

	SETUP_FOR_JAVA_CALL

	{
		std::scoped_lock<std::mutex> lock( m_mutWebView );

		if ( m_bIsRunning.exchange( false ))
		{
			if ( m_webViewInfo.looper )
			{
				jclass cLooper = env->FindClass( "android/os/Looper" );

				jmethodID mQuit = env->GetMethodID( cLooper, "quit", "()V" );
				env->CallVoidMethod( m_webViewInfo.looper, mQuit );
			}

			m_webViewThread.join();
			Log( "[WebView] Joined WebView thread" );
		}

		env->DeleteGlobalRef( m_WVToPorterDuffClear );

		env->DeleteGlobalRef( m_webViewInfo.bitmap );
		env->DeleteGlobalRef( m_webViewInfo.canvas );
		env->DeleteGlobalRef( m_webViewInfo.webView );
		env->DeleteGlobalRef( m_webViewInfo.looper );
		env->DeleteGlobalRef( m_buffer );

		env->DeleteGlobalRef( m_WVTcWebView );
		env->DeleteGlobalRef( m_WVTcCanvas );
		env->DeleteGlobalRef( m_WVTcBitmap );
		env->DeleteGlobalRef( m_WVTcByteBuffer );

		free( m_bufferbytes );
	}

	Log( "[WebView] Finished closing webview" );
}

void WebView::OnAppClose()
{
	Log( "[WebView] Clearing root view" );

	SETUP_FOR_JAVA_CALL
	env->DeleteGlobalRef(s_contentView);
	s_contentView = nullptr;

	Log( "[WebView] Completed on app close steps" );
}