#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHISwapChain.h"
#include "Engine/Graphics/RHI/RHIDeferredCommandList.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#include <glad/glad.h>

#if defined( SW_PLATFORM_WINDOWS )
	// WGL system header — kept local (not in PlatformHeaders) to avoid clashing with glad.
	#include <gl/GL.h>
	#define WGL_CONTEXT_MAJOR_VERSION_ARB	 0x2091
	#define WGL_CONTEXT_MINOR_VERSION_ARB	 0x2092
	#define WGL_CONTEXT_PROFILE_MASK_ARB	 0x9126
	#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC( WINAPI* )( HDC hDC, HGLRC hShareContext, const int32* pAttribList );
#elif defined( SW_PLATFORM_LINUX )
	#define GLX_GLXEXT_LEGACY
	#include <GL/glx.h>
	#include "Core/Common/X11MacroUndef.h"
	#define GLX_CONTEXT_MAJOR_VERSION_ARB	 0x2091
	#define GLX_CONTEXT_MINOR_VERSION_ARB	 0x2092
	#define GLX_CONTEXT_PROFILE_MASK_ARB	 0x9126
	#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef GLXContext ( *PFNGLXCREATECONTEXTATTRIBSARBPROC )( Display*, GLXFBConfig, GLXContext, int32, const int32* );
#endif

namespace sw
{
	namespace
	{
		struct OpenGLRHIDeviceInternal
		{
#if defined( SW_PLATFORM_LINUX )
			static inline thread_local int32 t_glxXError{ 0 };

			static int32 glxXErrorHandler( Display*, XErrorEvent* )
			{
				t_glxXError = 1;
				return 0;
			}

			struct GlxXErrorScope
			{
				Display*	  _pDpy{ nullptr };
				XErrorHandler _prev{ nullptr };

				explicit GlxXErrorScope( Display* pDpy )
					: _pDpy{ pDpy }
					, _prev{ XSetErrorHandler( &OpenGLRHIDeviceInternal::glxXErrorHandler ) }
				{
					OpenGLRHIDeviceInternal::t_glxXError = 0;
				}

				~GlxXErrorScope()
				{
					if ( _pDpy != nullptr )
						XSync( _pDpy, 0 );
					XSetErrorHandler( _prev );
				}

				bool failed()
				{
					if ( _pDpy != nullptr )
						XSync( _pDpy, 0 );
					const bool b						 = OpenGLRHIDeviceInternal::t_glxXError != 0;
					OpenGLRHIDeviceInternal::t_glxXError = 0;
					return b;
				}
			};
#endif

			static void applyVsyncInterval( void* pHdc, void* pHrc, bool vsync )
			{
				(void)pHdc;
#if defined( SW_PLATFORM_WINDOWS )
				(void)pHrc;
				using PFNWGLSWAPINTERVALEXTPROC						  = BOOL( WINAPI* )( int32 );
				static PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>( wglGetProcAddress( "wglSwapIntervalEXT" ) );
				if ( s_wglSwapIntervalEXT != nullptr )
					s_wglSwapIntervalEXT( vsync ? 1 : 0 );
#elif defined( SW_PLATFORM_LINUX )
				(void)pHrc;
				using PFNGLXSWAPINTERVALEXTPROC = void ( * )( Display*, GLXDrawable, int32 );
				static PFNGLXSWAPINTERVALEXTPROC s_glXSwapIntervalEXT =
					reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>( glXGetProcAddressARB( (const GLubyte*)"glXSwapIntervalEXT" ) );
				if ( s_glXSwapIntervalEXT != nullptr && pHdc != nullptr )
				{
					Display* pDpy = static_cast<Display*>( pHdc );
					s_glXSwapIntervalEXT( pDpy, glXGetCurrentDrawable(), vsync ? 1 : 0 );
				}
#elif defined( SW_PLATFORM_MACOS )
				(void)pHdc;
				if ( pHrc != nullptr )
				{
					id				context								  = static_cast<id>( pHrc );
					GLint			interval							  = vsync ? 1 : 0;
					constexpr GLint kNsOpenGlContextParameterSwapInterval = 222;
					( (void ( * )( id, SEL, GLint*, GLint ))objc_msgSend )(
						context, sel_registerName( "setValues:forParameter:" ), &interval, kNsOpenGlContextParameterSwapInterval );
				}
#endif
			}
		};

#if defined( SW_PLATFORM_LINUX )
		thread_local int32 OpenGLRHIDeviceInternal::t_glxXError{ 0 };
#endif
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "OpenGL" );

	OpenGLRHIDevice::OpenGLRHIDevice()
		: _pHDC{ nullptr }
		, _pHRC{ nullptr }
		, _pHWnd{ nullptr }
		, _width{ 1280 }
		, _height{ 720 }
		, _shaderProgram{ 0 }
		, _vao{ 0 }
		, _vbo{ 0 }
		, _meshVao{ 0 }
		, _defaultSampler{ 0 }
		, _defaultTexture{ 0 }
		, _gpuBuffers{}
		, _boundMeshVb{ 0 }
		, _boundMeshStride{ sizeof( RHIVertex ) }
		, _boundMeshOffset{ 0 }
		, _boundIndexBuffer{ 0 }
		, _boundIndexStride{ 4 }
		, _boundIndexOffset{ 0 }
		, _listRegisteredBindless{}
		, _listBindlessFree{}
		, _listRegisteredUAV{}
		, _listUavFree{}
		, _gpuTextures{}
		, _mapCompositeFbo{}
		, _listRegisteredTexture{}
		, _listTextureFree{}
		, _computeRootConstantUbo{ 0 }
		, _arrComputeRootConstantShadow{}
		, _pipelineStates{}
		, _listRenderPass{}
		, _releaseQueue{ 3 }
		, _immContext{ nullptr }
		, _deferredContext{ nullptr }
		, _swapChainImpl{ nullptr }
		, _resourceImpl{ nullptr }
		, _boundGraphicsPso{ 0 }
		, _lastVsync{ -1 }
		, _bInitialized{ SW_FALSE }
		, _reservedFlags{ 0 }
	{
		_swapChainImpl = sw::make_unique<OpenGLRHISwapChain>( this );
		_resourceImpl  = sw::make_unique<OpenGLRHIResource>( this );
	}

	OpenGLRHIDevice::~OpenGLRHIDevice()
	{
		shutdown();
	}

	IRHISwapChain*		OpenGLRHIDevice::getSwapChain() { return _swapChainImpl.get(); }
	IRHIResource*		OpenGLRHIDevice::getResource() { return _resourceImpl.get(); }
	IRHICommandContext* OpenGLRHIDevice::getImmediateContext() { return _immContext.get(); }
	IRHICommandContext* OpenGLRHIDevice::getDeferredCommandContext() { return _deferredContext.get(); }

	bool OpenGLRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
	{
		if ( _bInitialized == SW_TRUE )
			return true;

		_width	= desc._width;
		_height = desc._height;
		_pHWnd	= desc._pWindowHandle;

#if defined( SW_PLATFORM_WINDOWS )
		HWND hWnd = static_cast<HWND>( desc._pWindowHandle );
		HDC	 hDC  = GetDC( hWnd );

		PIXELFORMATDESCRIPTOR pfd = {};
		pfd.nSize				  = sizeof( PIXELFORMATDESCRIPTOR );
		pfd.nVersion			  = 1;
		pfd.dwFlags				  = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType			  = PFD_TYPE_RGBA;
		pfd.cColorBits			  = 32;
		pfd.cDepthBits			  = 24;
		pfd.cStencilBits		  = 8;

		// SetPixelFormat is once-per-HWND. Verify PFD_SUPPORT_OPENGL if already set.
		int32 pixelFormat = GetPixelFormat( hDC );
		bool  bFormatSet  = false;
		if ( pixelFormat != 0 )
		{
			PIXELFORMATDESCRIPTOR currentPfd{};
			DescribePixelFormat( hDC, pixelFormat, sizeof( currentPfd ), &currentPfd );
			if ( ( currentPfd.dwFlags & PFD_SUPPORT_OPENGL ) != 0 )
				bFormatSet = true;
		}

		if ( bFormatSet == false )
		{
			pixelFormat = ChoosePixelFormat( hDC, &pfd );
			if ( pixelFormat == 0 )
			{
				SW_LOG_ERROR( "ChoosePixelFormat failed (err=%#)", static_cast<uint32>( GetLastError() ) );
				ReleaseDC( hWnd, hDC );
				return false;
			}
			if ( SetPixelFormat( hDC, pixelFormat, &pfd ) == FALSE )
			{
				SW_LOG_ERROR( "SetPixelFormat failed (err=%#)", static_cast<uint32>( GetLastError() ) );
				ReleaseDC( hWnd, hDC );
				return false;
			}
		}

		HGLRC dummyContext = wglCreateContext( hDC );
		if ( dummyContext == nullptr || wglMakeCurrent( hDC, dummyContext ) == FALSE )
		{
			SW_LOG_ERROR( "wglCreateContext/MakeCurrent failed (err=%#)", static_cast<uint32>( GetLastError() ) );
			if ( dummyContext )
				wglDeleteContext( dummyContext );
			ReleaseDC( hWnd, hDC );
			return false;
		}

		PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>( wglGetProcAddress( "wglCreateContextAttribsARB" ) );
		HGLRC							  hRC{ nullptr };
		if ( wglCreateContextAttribsARB )
		{
			static const int32 kArrVersions[][2] = {
				{4, 6},
				{4, 5},
				{4, 3},
				{4, 1},
				{3, 3}
			 };
			for ( const int32( &ver )[2] : kArrVersions )
			{
				int32 arrAttrib[] = {
					WGL_CONTEXT_MAJOR_VERSION_ARB, ver[0],
					WGL_CONTEXT_MINOR_VERSION_ARB, ver[1],
					WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
					0 };
				hRC = wglCreateContextAttribsARB( hDC, nullptr, arrAttrib );
				if ( hRC != nullptr )
				{
					SW_LOG_TRACE( "WGL core context %#.%# created", ver[0], ver[1] );
					break;
				}
			}
			wglMakeCurrent( nullptr, nullptr );
			wglDeleteContext( dummyContext );
			if ( hRC != nullptr )
			{
				wglMakeCurrent( hDC, hRC );
				_pHRC = hRC;
			}
			else
			{
				SW_LOG_ERROR( "Failed to create WGL core context" );
				ReleaseDC( hWnd, hDC );
				return false;
			}
		}
		else
			_pHRC = dummyContext;
		_pHDC = hDC;
#elif defined( SW_PLATFORM_LINUX )
		Display* pDpy = (Display*)desc._pWindowDisplay;
		Window	 win  = (Window)(uintptr_t)desc._pWindowHandle;

		XWindowAttributes wa{};
		if ( XGetWindowAttributes( pDpy, win, &wa ) == 0 || wa.visual == nullptr )
		{
			SW_LOG_ERROR( "XGetWindowAttributes failed" );
			return false;
		}
		const VisualID windowVisualId = XVisualIDFromVisual( wa.visual );

		int32		 fbcount{ 0 };
		GLXFBConfig* pFbcAll = glXGetFBConfigs( pDpy, DefaultScreen( pDpy ), &fbcount );
		if ( pFbcAll == nullptr || fbcount <= 0 )
		{
			SW_LOG_ERROR( "glXGetFBConfigs failed" );
			return false;
		}

		GLXFBConfig chosen{ nullptr };
		for ( int32 configIndex = 0; configIndex < fbcount; ++configIndex )
		{
			int32 usable{ 0 };
			glXGetFBConfigAttrib( pDpy, pFbcAll[configIndex], GLX_DRAWABLE_TYPE, &usable );
			if ( ( usable & GLX_WINDOW_BIT ) == 0 )
				continue;
			glXGetFBConfigAttrib( pDpy, pFbcAll[configIndex], GLX_RENDER_TYPE, &usable );
			if ( ( usable & GLX_RGBA_BIT ) == 0 )
				continue;
			XVisualInfo* pVi = glXGetVisualFromFBConfig( pDpy, pFbcAll[configIndex] );
			if ( pVi == nullptr )
				continue;
			const bool bMatch = ( pVi->visualid == windowVisualId );
			XFree( pVi );
			if ( bMatch )
			{
				chosen = pFbcAll[configIndex];
				break;
			}
		}
		if ( chosen == nullptr )
			chosen = pFbcAll[0];

		PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB =
			(PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB( (const GLubyte*)"glXCreateContextAttribsARB" );

		GLXContext ctx{ nullptr };
		{
			OpenGLRHIDeviceInternal::GlxXErrorScope trap( pDpy );
			if ( glXCreateContextAttribsARB )
			{
				// Prefer 4.6, fall back for WSLg/Mesa (often ≤4.1 / 3.3).
				static const int32 kArrVersions[][2] = {
					{4, 6},
					{4, 5},
					{4, 3},
					{4, 2},
					{4, 1},
					{4, 0},
					{3, 3}
				 };
				for ( const int32( &ver )[2] : kArrVersions )
				{
					int32 arrContextAttrib[] = {
						GLX_CONTEXT_MAJOR_VERSION_ARB, ver[0],
						GLX_CONTEXT_MINOR_VERSION_ARB, ver[1],
						GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
						0 };
					ctx = glXCreateContextAttribsARB( pDpy, chosen, nullptr, 1, arrContextAttrib );
					if ( ctx != nullptr && trap.failed() == false )
					{
						SW_LOG_TRACE( "GLX core context %#.%#", ver[0], ver[1] );
						break;
					}
					if ( ctx != nullptr )
					{
						glXDestroyContext( pDpy, ctx );
						ctx = nullptr;
					}
					trap.failed(); // clear
				}
			}
			if ( ctx == nullptr )
			{
				ctx = glXCreateNewContext( pDpy, chosen, GLX_RGBA_TYPE, nullptr, 1 );
				if ( ctx == nullptr || trap.failed() )
				{
					if ( ctx != nullptr )
					{
						glXDestroyContext( pDpy, ctx );
						ctx = nullptr;
					}
				}
			}
		}
		XFree( pFbcAll );

		if ( ctx == nullptr )
		{
			SW_LOG_ERROR( "Failed to create GLX context (WSLg often lacks GL 4.x — use -vulkan)" );
			return false;
		}
		if ( glXMakeCurrent( pDpy, win, ctx ) == 0 )
		{
			SW_LOG_ERROR( "glXMakeCurrent failed" );
			glXDestroyContext( pDpy, ctx );
			return false;
		}
		_pHDC = pDpy;
		_pHRC = ctx;
#elif defined( SW_PLATFORM_MACOS )

		id windowObj   = (id)desc._pWindowHandle;
		id contentView = ( (id ( * )( id, SEL ))objc_msgSend )( windowObj, sel_registerName( "contentView" ) );

		uint32 arrAttr[] = {
			73,
			0x4100,
			8, 24,
			5,
			0 };
		id pixelFormatClass = (id)objc_getClass( "NSOpenGLPixelFormat" );
		id pixelFormat		= ( (id ( * )( id, SEL, const uint32* ))objc_msgSend )( ( (id ( * )( id, SEL ))objc_msgSend )( pixelFormatClass, sel_registerName( "alloc" ) ), sel_registerName( "initWithAttributes:" ), arrAttr );

		id contextClass = (id)objc_getClass( "NSOpenGLContext" );
		id context		= ( (id ( * )( id, SEL, id, id ))objc_msgSend )( ( (id ( * )( id, SEL ))objc_msgSend )( contextClass, sel_registerName( "alloc" ) ), sel_registerName( "initWithFormat:shareContext:" ), pixelFormat, nullptr );

		( (void ( * )( id, SEL, id ))objc_msgSend )( context, sel_registerName( "setView:" ), contentView );
		( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "makeCurrentContext" ) );

		_pHDC = contentView;
		_pHRC = context;
#endif

		if ( gladLoadGL() != 0 )
		{
			SW_LOG_INFO( "OpenGL glad Loaded Successfully." );

#ifdef GL_CLIP_CONTROL
			if ( GLAD_GL_VERSION_4_5 )
			{
				// UPPER_LEFT + ZERO_TO_ONE = Direct3D clip/NDC (Y-up, top-left origin, Z in [0,1]).
				glClipControl( GL_UPPER_LEFT, GL_ZERO_TO_ONE );
				SW_LOG_TRACE( "OpenGL glClipControl: GL_UPPER_LEFT, GL_ZERO_TO_ONE (match DirectX NDC / top-left UV)" );
			}
#endif
		}
		else
		{
			SW_LOG_WARNING( "gladLoadGL이 0을 반환함, OpenGL 디바이스 초기화 실패" );
			return false;
		}

		_bInitialized = SW_TRUE;

		{
			const RHIVertex arrFullscreenVert[3] = {
				{{ -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
				{ { 3.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
				{ { -1.0f, 3.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			};
			glGenVertexArrays( 1, &_vao );
			glGenBuffers( 1, &_vbo );
			glBindVertexArray( _vao );
			glBindBuffer( GL_ARRAY_BUFFER, _vbo );
			glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeof( arrFullscreenVert ) ), arrFullscreenVert, GL_STATIC_DRAW );
			glEnableVertexAttribArray( 0 );
			glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>( sizeof( RHIVertex ) ), reinterpret_cast<void*>( 0 ) );
			glEnableVertexAttribArray( 1 );
			glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>( sizeof( RHIVertex ) ),
								   reinterpret_cast<void*>( SW_OFFSET_OF( RHIVertex, _arrColor ) ) );
			glBindVertexArray( 0 );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );

			glGenVertexArrays( 1, &_meshVao );
			glBindVertexArray( _meshVao );
			glEnableVertexAttribArray( 0 );
			glEnableVertexAttribArray( 1 );
			glBindVertexArray( 0 );

			GLuint defaultTex{ 0 };
			glGenTextures( 1, &defaultTex );
			glBindTexture( GL_TEXTURE_2D, defaultTex );
			const uint32 whitePixel = 0xFFFFFFFF;
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whitePixel );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glBindTexture( GL_TEXTURE_2D, 0 );
			_defaultTexture = defaultTex;

			GLuint defaultSampler{ 0 };
			glGenSamplers( 1, &defaultSampler );
			glSamplerParameteri( defaultSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glSamplerParameteri( defaultSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glSamplerParameteri( defaultSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glSamplerParameteri( defaultSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			glSamplerParameteri( defaultSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE );
			for ( uint32 unit = 0; unit < 64; ++unit )
			{
				glBindTextureUnit( unit, defaultTex );
				glBindSampler( unit, defaultSampler );
			}
			_defaultSampler = defaultSampler;
		}

		_immContext		 = sw::make_unique<OpenGLRHICommandContext>( this );
		_deferredContext = sw::make_unique<OpenGLRHICommandContext>( this );

		SW_LOG_INFO( "OpenGL RHI Backend Device Active (DirectX Coordinate System & Top-Left UV Texture Space Configured)" );
		return true;
	}

	void OpenGLRHIDevice::shutdownInternal()
	{
		if ( _bInitialized == SW_FALSE )
			return;

#if defined( SW_PLATFORM_WINDOWS )
		if ( _pHDC && _pHRC )
			wglMakeCurrent( static_cast<HDC>( _pHDC ), static_cast<HGLRC>( _pHRC ) );
#endif

		_releaseQueue.flushAll();
		_immContext.reset();
		_deferredContext.reset();

		if ( _defaultSampler )
		{
			for ( uint32 unit = 0; unit < 64; ++unit )
			{
				glBindSampler( unit, 0 );
				glBindTextureUnit( unit, 0 );
			}
			glDeleteSamplers( 1, &_defaultSampler );
			_defaultSampler = 0;
		}
		if ( _defaultTexture )
		{
			glDeleteTextures( 1, &_defaultTexture );
			_defaultTexture = 0;
		}
		if ( _shaderProgram )
		{
			glDeleteProgram( _shaderProgram );
			_shaderProgram = 0;
		}
		if ( _vao )
		{
			glDeleteVertexArrays( 1, &_vao );
			_vao = 0;
		}
		if ( _vbo )
		{
			glDeleteBuffers( 1, &_vbo );
			_vbo = 0;
		}
		if ( _meshVao )
		{
			glDeleteVertexArrays( 1, &_meshVao );
			_meshVao = 0;
		}
		_gpuBuffers.forEach( []( uint32& name )
		{
			if ( name != 0 )
			{
				GLuint glName = name;
				glDeleteBuffers( 1, &glName );
				name = 0;
			}
		} );
		_boundMeshVb	  = 0;
		_boundMeshStride  = sizeof( RHIVertex );
		_boundMeshOffset  = 0;
		_boundIndexBuffer = 0;
		_boundIndexStride = 4;
		_boundIndexOffset = 0;
		_listRegisteredBindless.clear();
		_listRegisteredUAV.clear();
		_listRegisteredTexture.clear();
		_listTextureFree.clear();
		_listBindlessFree.clear();
		_listUavFree.clear();
		Memory::set( _arrComputeRootConstantShadow, 0, sizeof( _arrComputeRootConstantShadow ) );

		_pipelineStates.forEach( []( OpenGLPipelineStateRecord& pso )
		{
			if ( pso._program != 0 )
			{
				glDeleteProgram( pso._program );
				pso._program = 0;
			}
		} );
		_pipelineStates.clear();
		_listRenderPass.clear();

		_gpuBuffers.forEach( []( uint32& glBuf )
		{
			if ( glBuf != 0 )
			{
				glDeleteBuffers( 1, &glBuf );
				glBuf = 0;
			}
		} );
		_gpuBuffers.clear();

		_gpuTextures.forEach( []( OpenGLTextureRecord& rec )
		{
			if ( rec._texture != 0 )
			{
				glDeleteTextures( 1, &rec._texture );
				rec._texture = 0;
			}
			if ( rec._fbo != 0 )
			{
				glDeleteFramebuffers( 1, &rec._fbo );
				rec._fbo = 0;
			}
		} );
		_gpuTextures.clear();

		for ( auto& [key, fbo] : _mapCompositeFbo )
		{
			if ( fbo != 0 )
			{
				GLuint glFbo = fbo;
				glDeleteFramebuffers( 1, &glFbo );
			}
		}
		_mapCompositeFbo.clear();

		if ( _vao != 0 )
		{
			glDeleteVertexArrays( 1, &_vao );
			_vao = 0;
		}
		if ( _vbo != 0 )
		{
			glDeleteBuffers( 1, &_vbo );
			_vbo = 0;
		}
		if ( _meshVao != 0 )
		{
			glDeleteVertexArrays( 1, &_meshVao );
			_meshVao = 0;
		}
		if ( _shaderProgram != 0 )
		{
			glDeleteProgram( _shaderProgram );
			_shaderProgram = 0;
		}
		if ( _defaultSampler != 0 )
		{
			glDeleteSamplers( 1, &_defaultSampler );
			_defaultSampler = 0;
		}
		if ( _computeRootConstantUbo != 0 )
		{
			glDeleteBuffers( 1, &_computeRootConstantUbo );
			_computeRootConstantUbo = 0;
		}

#if defined( SW_PLATFORM_WINDOWS )
		if ( _pHDC != nullptr )
			wglMakeCurrent( nullptr, nullptr );
		if ( _pHRC != nullptr )
		{
			wglDeleteContext( static_cast<HGLRC>( _pHRC ) );
			_pHRC = nullptr;
		}
		if ( _pHDC != nullptr && _pHWnd != nullptr )
		{
			ReleaseDC( static_cast<HWND>( _pHWnd ), static_cast<HDC>( _pHDC ) );
			_pHDC = nullptr;
		}
#elif defined( SW_PLATFORM_LINUX )
		if ( _pHDC != nullptr && _pHRC != nullptr )
		{
			glXMakeCurrent( static_cast<Display*>( _pHDC ), 0, nullptr );
			glXDestroyContext( static_cast<Display*>( _pHDC ), static_cast<GLXContext>( _pHRC ) );
		}
		_pHDC = nullptr;
		_pHRC = nullptr;
#endif

		_bInitialized = SW_FALSE;
		SW_LOG_INFO( "OpenGL RHI Device Shutdown cleanly." );
	}

	void OpenGLRHIDevice::resize( uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;
		glViewport( 0, 0, static_cast<GLsizei>( width ), static_cast<GLsizei>( height ) );
		SW_LOG_TRACE( "OpenGL RHI Resized to %# x %#", width, height );
	}

	void OpenGLRHIDevice::beginFrame( const float4& clearColor )
	{
		if ( _bInitialized == SW_FALSE )
			return;

#if defined( SW_PLATFORM_WINDOWS )
		if ( _pHDC != nullptr && _pHRC != nullptr )
			wglMakeCurrent( static_cast<HDC>( _pHDC ), static_cast<HGLRC>( _pHRC ) );
#endif

		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glViewport( 0, 0, static_cast<GLsizei>( _width ), static_cast<GLsizei>( _height ) );
		glClearColor( clearColor._x, clearColor._y, clearColor._z, clearColor._w );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	void OpenGLRHIDevice::endFrame( bool vsync, bool bPresent )
	{
		if ( _bInitialized == SW_FALSE )
			return;

		if ( bPresent == false )
		{
			_releaseQueue.tickFrame();
			return;
		}

		const int8 desired = vsync ? 1 : 0;
		if ( _lastVsync != desired )
		{
			OpenGLRHIDeviceInternal::applyVsyncInterval( _pHDC, _pHRC, vsync );
			_lastVsync = desired;
		}

#if defined( SW_PLATFORM_WINDOWS )
		// Ensure the RHI context is current after ImGui multi-viewport may have switched DCs.
		if ( _pHDC && _pHRC )
			wglMakeCurrent( static_cast<HDC>( _pHDC ), static_cast<HGLRC>( _pHRC ) );
		SwapBuffers( static_cast<HDC>( _pHDC ) );
#elif defined( SW_PLATFORM_LINUX )
		glXSwapBuffers( (Display*)_pHDC, (Window)(uintptr_t)_pHWnd );
#elif defined( SW_PLATFORM_MACOS )
		id context = (id)_pHRC;
		( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "flushBuffer" ) );
#endif
		_releaseQueue.tickFrame();
	}

	void OpenGLRHIDevice::waitIdle()
	{
		if ( _bInitialized == SW_FALSE )
			return;

		ScopedOpenGLContext ctxScope( this );
		glFinish();
		_releaseQueue.flushAll();
	}

	bool OpenGLRHIDevice::bindGraphicsContext()
	{
		if ( _bInitialized == SW_FALSE || _pHRC == nullptr )
			return false;
#if defined( SW_PLATFORM_WINDOWS )
		if ( _pHDC == nullptr )
			return false;
		if ( wglMakeCurrent( static_cast<HDC>( _pHDC ), static_cast<HGLRC>( _pHRC ) ) == FALSE )
		{
			if ( _bInitialized == SW_FALSE )
				return false;
			SW_LOG_ERROR( "bindGraphicsContext wglMakeCurrent failed (err=%#)",
						  static_cast<uint32>( GetLastError() ) );
			return false;
		}
#elif defined( SW_PLATFORM_LINUX )
		if ( glXMakeCurrent( (Display*)_pHDC, (Window)(uintptr_t)_pHWnd, (GLXContext)_pHRC ) == 0 )
		{
			SW_LOG_ERROR( "bindGraphicsContext glXMakeCurrent failed" );
			return false;
		}
#elif defined( SW_PLATFORM_MACOS )
		id context = (id)_pHRC;
		( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "makeCurrentContext" ) );
#endif
		return true;
	}

	void OpenGLRHIDevice::unbindGraphicsContext()
	{
		if ( _bInitialized == SW_FALSE )
			return;
#if defined( SW_PLATFORM_WINDOWS )
		wglMakeCurrent( nullptr, nullptr );
#elif defined( SW_PLATFORM_LINUX )
		if ( _pHDC != nullptr )
			glXMakeCurrent( (Display*)_pHDC, 0, nullptr );
#elif defined( SW_PLATFORM_MACOS )
		// NSOpenGLContext clearCurrentContext
		Class cls = objc_getClass( "NSOpenGLContext" );
		if ( cls != nullptr )
		{
			SEL sel = sel_registerName( "clearCurrentContext" );
			( (void ( * )( Class, SEL ))objc_msgSend )( cls, sel );
		}
#endif
	}

	RHIBufferHandle OpenGLRHIDevice::createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride )
	{
		(void)indexStride;
		if ( _bInitialized == SW_FALSE || sizeBytes == 0 )
			return 0;

		GLuint ibo{ 0 };
		glGenBuffers( 1, &ibo );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo );
		glBufferData( GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeBytes ), pData, GL_STATIC_DRAW );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

		return storeGlBuffer( ibo );
	}

	void OpenGLRHIDevice::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
										RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( _bInitialized == SW_FALSE || argumentBuffer == 0 )
			return;

		if ( materialDescriptorIndex != kInvalidDescriptorIndex &&
			 materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _listRegisteredBindless.size() ) )
		{
			GLuint ubo = resolveGlBuffer( _listRegisteredBindless[materialDescriptorIndex]._buffer );
			if ( ubo != 0 )
				glBindBufferBase( GL_UNIFORM_BUFFER, 0, ubo );
		}

		GLuint buf = resolveGlBuffer( argumentBuffer );
		if ( buf == 0 )
			return;
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );

		if ( glad_glDrawArraysIndirect != nullptr )
			glDrawArraysIndirect( GL_TRIANGLES, reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) ) );
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
	}

	void OpenGLRHIDevice::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
											 RHIBufferHandle countBuffer, uint32 countBufferOffset )
	{
		if ( _bInitialized == SW_FALSE || argumentBuffer == 0 || maxCommandCount == 0 )
			return;

		GLuint buf = resolveGlBuffer( argumentBuffer );
		if ( buf == 0 )
			return;
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );

		constexpr GLsizei stride  = sizeof( RHIDrawIndirectCommand );
		const void*		  pOffset = reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) );

		if ( countBuffer != 0 && glad_glMultiDrawArraysIndirectCount != nullptr )
		{
			GLuint count = resolveGlBuffer( countBuffer );
			if ( count != 0 )
				glBindBuffer( GL_PARAMETER_BUFFER, count );
			glMultiDrawArraysIndirectCount( GL_TRIANGLES, pOffset, static_cast<GLintptr>( countBufferOffset ),
											static_cast<GLsizei>( maxCommandCount ), stride );
			glBindBuffer( GL_PARAMETER_BUFFER, 0 );
		}
		else if ( glad_glMultiDrawArraysIndirect != nullptr )
			glMultiDrawArraysIndirect( GL_TRIANGLES, pOffset, static_cast<GLsizei>( maxCommandCount ), stride );
		else if ( glad_glDrawArraysIndirect != nullptr )
		{
			for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
			{
				const void* pCmdOffset = reinterpret_cast<const void*>( argumentBufferOffset + commandIndex * sizeof( RHIDrawIndirectCommand ) );
				glDrawArraysIndirect( GL_TRIANGLES, pCmdOffset );
			}
		}

		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
	}

	unique_ptr<IRHICommandList> OpenGLRHIDevice::createCommandList( RHICommandListMode mode )
	{
		unique_ptr<RHIDeferredCommandList> list = make_unique<RHIDeferredCommandList>( mode, getCommandContextForMode( mode ) );
		return list;
	}

	void OpenGLRHIDevice::executeCommandList( IRHICommandList* pCmdList )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( _pHDC != nullptr && _pHRC != nullptr )
			wglMakeCurrent( static_cast<HDC>( _pHDC ), static_cast<HGLRC>( _pHRC ) );
#endif
		RHIDeferredCommandList::execute( this, pCmdList );
	}

	bool OpenGLRHIDevice::ensureComputeRootConstantUbo()
	{
		if ( _computeRootConstantUbo != 0 )
			return true;
		if ( _bInitialized == SW_FALSE )
			return false;

		GLuint ubo{ 0 };
		if ( glad_glCreateBuffers != nullptr )
		{
			glCreateBuffers( 1, &ubo );
			glNamedBufferStorage( ubo, static_cast<GLsizeiptr>( sizeof( _arrComputeRootConstantShadow ) ), nullptr, GL_DYNAMIC_STORAGE_BIT );
		}
		else
		{
			glGenBuffers( 1, &ubo );
			glBindBuffer( GL_UNIFORM_BUFFER, ubo );
			glBufferData( GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>( sizeof( _arrComputeRootConstantShadow ) ), nullptr, GL_DYNAMIC_DRAW );
			glBindBuffer( GL_UNIFORM_BUFFER, 0 );
		}
		_computeRootConstantUbo = ubo;
		return _computeRootConstantUbo != 0;
	}

	uint32 OpenGLRHIDevice::ensureCompositeFbo( RHITextureHandle color, RHITextureHandle depth )
	{
		RHITextureHandle arrColor[1] = { color };
		return ensureCompositeFboMRT( arrColor, color != 0 ? 1u : 0u, depth );
	}

	uint32 OpenGLRHIDevice::ensureCompositeFboMRT( const RHITextureHandle* pColor, uint32 colorCount, RHITextureHandle depth )
	{
		CompositeFboKey key{};
		key._colorCount = colorCount > kMaxColorAttachments ? kMaxColorAttachments : colorCount;
		for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
		{
			key._arrColor[colorIndex] = pColor[colorIndex];
		}
		key._depth = depth;

		auto existing = _mapCompositeFbo.find( key );
		if ( existing != _mapCompositeFbo.end() )
			return existing->second;

		GLuint arrColorTex[kMaxColorAttachments]{};
		uint32 attachedColors{ 0 };
		for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
		{
			if ( key._arrColor[colorIndex] == 0 )
				continue;
			const OpenGLTextureRecord* pRec = resolveTexture( key._arrColor[colorIndex] );
			if ( pRec == nullptr || pRec->_bDepthStencil != 0 )
				return 0;
			arrColorTex[attachedColors++] = pRec->_texture;
		}

		GLuint depthTex{ 0 };
		if ( depth != 0 )
		{
			const OpenGLTextureRecord* pDepthRec = resolveTexture( depth );
			if ( pDepthRec == nullptr || pDepthRec->_bDepthStencil == 0 )
				return 0;
			depthTex = pDepthRec->_texture;
		}
		if ( attachedColors == 0 && depthTex == 0 )
			return 0;

		GLuint fbo{ 0 };
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		GLenum arrDrawBuffer[kMaxColorAttachments]{};
		for ( uint32 colorIndex = 0; colorIndex < attachedColors; ++colorIndex )
		{
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colorIndex, GL_TEXTURE_2D, arrColorTex[colorIndex], 0 );
			arrDrawBuffer[colorIndex] = GL_COLOR_ATTACHMENT0 + colorIndex;
		}
		if ( attachedColors == 0 )
		{
			glDrawBuffer( GL_NONE );
			glReadBuffer( GL_NONE );
		}
		else
			glDrawBuffers( static_cast<GLsizei>( attachedColors ), arrDrawBuffer );

		if ( depthTex != 0 )
		{
			const OpenGLTextureRecord* pDepthRec	   = resolveTexture( depth );
			const GLenum			   depthAttachment = ( pDepthRec != nullptr && pDepthRec->_format == RHIFormat::D24_UNORM_S8_UINT )
														   ? GL_DEPTH_STENCIL_ATTACHMENT
														   : GL_DEPTH_ATTACHMENT;
			glFramebufferTexture2D( GL_FRAMEBUFFER, depthAttachment, GL_TEXTURE_2D, depthTex, 0 );
		}

		const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		if ( status != GL_FRAMEBUFFER_COMPLETE )
		{
			SW_LOG_WARNING( "Composite FBO incomplete (status=%#).", static_cast<uint32>( status ) );
			glDeleteFramebuffers( 1, &fbo );
			return 0;
		}
		_mapCompositeFbo.emplace( key, fbo );
		return fbo;
	}

	uint32 OpenGLRHIDevice::resolveGlBuffer( RHIBufferHandle handle ) const
	{
		const uint32* pSlot = _gpuBuffers.get( handle );
		return pSlot != nullptr ? *pSlot : 0;
	}

	RHIBufferHandle OpenGLRHIDevice::storeGlBuffer( uint32 glName )
	{
		if ( glName == 0 )
			return 0;
		return _gpuBuffers.insert( glName );
	}

	OpenGLRHIDevice::OpenGLTextureRecord* OpenGLRHIDevice::resolveTexture( RHITextureHandle handle )
	{
		return _gpuTextures.get( handle );
	}

	const OpenGLRHIDevice::OpenGLTextureRecord* OpenGLRHIDevice::resolveTexture( RHITextureHandle handle ) const
	{
		return _gpuTextures.get( handle );
	}
} // namespace sw
