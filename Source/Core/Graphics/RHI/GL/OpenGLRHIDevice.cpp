/**
 * @file OpenGLRHIDevice.cpp
 * @brief OpenGL RHI 디바이스 구현
 */
#include "OpenGLRHIDevice.h"
#include "Core/Graphics/Shader/ShaderCache.h"
#include "Core/Common/PlatformHeaders.h"
#include "Core/Common/CommonHeaders.h"
#include <glad/glad.h>

#if defined( SW_PLATFORM_WINDOWS )
	#include <gl/GL.h>
	#define WGL_CONTEXT_MAJOR_VERSION_ARB	 0x2091
	#define WGL_CONTEXT_MINOR_VERSION_ARB	 0x2092
	#define WGL_CONTEXT_PROFILE_MASK_ARB	 0x9126
	#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef HGLRC( WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC )( HDC hDC, HGLRC hShareContext, const int* attribList );
#elif defined( SW_PLATFORM_LINUX )
	#include <X11/Xlib.h>
	#include <GL/glx.h>
	#define GLX_CONTEXT_MAJOR_VERSION_ARB	 0x2091
	#define GLX_CONTEXT_MINOR_VERSION_ARB	 0x2092
	#define GLX_CONTEXT_PROFILE_MASK_ARB	 0x9126
	#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef GLXContext ( *PFNGLXCREATECONTEXTATTRIBSARBPROC )( Display*, GLXFBConfig, GLXContext, Bool, const int* );
#elif defined( SW_PLATFORM_MACOS )
	#include <objc/message.h>
	#include <objc/runtime.h>
#endif
namespace sw
{
	OpenGLRHIDevice::OpenGLRHIDevice()
		: _bInitialized{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	OpenGLRHIDevice::~OpenGLRHIDevice()
	{
		shutdown();
	}

	bool OpenGLRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
	{
		if ( _bInitialized == true )
			return true;

		_width	= desc._width;
		_height = desc._height;
		_hWnd	= desc._windowHandle;

#if defined( SW_PLATFORM_WINDOWS )
		HWND hWnd = static_cast<HWND>( desc._windowHandle );
		HDC	 hDC  = GetDC( hWnd );

		PIXELFORMATDESCRIPTOR pfd = {};
		pfd.nSize				  = sizeof( PIXELFORMATDESCRIPTOR );
		pfd.nVersion			  = 1;
		pfd.dwFlags				  = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType			  = PFD_TYPE_RGBA;
		pfd.cColorBits			  = 32;
		pfd.cDepthBits			  = 24;
		pfd.cStencilBits		  = 8;

		int pixelFormat = ChoosePixelFormat( hDC, &pfd );
		SetPixelFormat( hDC, pixelFormat, &pfd );

		HGLRC dummyContext = wglCreateContext( hDC );
		wglMakeCurrent( hDC, dummyContext );

		PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>( wglGetProcAddress( "wglCreateContextAttribsARB" ) );
		if ( wglCreateContextAttribsARB )
		{
			int attribs[] = {
				WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
				WGL_CONTEXT_MINOR_VERSION_ARB, 6,
				WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
				0 };
			HGLRC hRC = wglCreateContextAttribsARB( hDC, nullptr, attribs );
			wglMakeCurrent( nullptr, nullptr );
			wglDeleteContext( dummyContext );
			wglMakeCurrent( hDC, hRC );
			_hRC = hRC;
		}
		else
		{
			_hRC = dummyContext;
		}
		_hDC = hDC;
#elif defined( SW_PLATFORM_LINUX )
		Display* dpy = (Display*)desc._windowDisplay;
		Window	 win = (Window)(uintptr_t)desc._windowHandle;

		int visual_attribs[] = {
			GLX_RENDER_TYPE, GLX_RGBA_BIT,
			GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
			GLX_DOUBLEBUFFER, True,
			GLX_RED_SIZE, 8,
			GLX_GREEN_SIZE, 8,
			GLX_BLUE_SIZE, 8,
			GLX_DEPTH_SIZE, 24,
			None };
		int			 fbcount = 0;
		GLXFBConfig* fbc	 = glXChooseFBConfig( dpy, DefaultScreen( dpy ), visual_attribs, &fbcount );

		if ( !fbc )
		{
			SW_LOG_ERROR( "Failed to retrieve a framebuffer config" );
			return false;
		}

		PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB = (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB( (const GLubyte*)"glXCreateContextAttribsARB" );
		GLXContext						  ctx						 = 0;
		if ( glXCreateContextAttribsARB )
		{
			int context_attribs[] = {
				GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
				GLX_CONTEXT_MINOR_VERSION_ARB, 6,
				GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
				None };
			ctx = glXCreateContextAttribsARB( dpy, fbc[0], NULL, True, context_attribs );
		}
		else
		{
			ctx = glXCreateNewContext( dpy, fbc[0], GLX_RGBA_TYPE, NULL, True );
		}
		XFree( fbc );
		glXMakeCurrent( dpy, win, ctx );
		_hDC = dpy;
		_hRC = ctx;
#elif defined( SW_PLATFORM_MACOS )

		id windowObj   = (id)desc._windowHandle;
		id contentView = ( (id ( * )( id, SEL ))objc_msgSend )( windowObj, sel_registerName( "contentView" ) );

		uint32_t attrs[] = {
			73,
			0x4100,
			8, 24,
			5,
			0
		};
		id pixelFormatClass = (id)objc_getClass( "NSOpenGLPixelFormat" );
		id pixelFormat		= ( (id ( * )( id, SEL, const uint32_t* ))objc_msgSend )( ( (id ( * )( id, SEL ))objc_msgSend )( pixelFormatClass, sel_registerName( "alloc" ) ), sel_registerName( "initWithAttributes:" ), attrs );

		id contextClass = (id)objc_getClass( "NSOpenGLContext" );
		id context		= ( (id ( * )( id, SEL, id, id ))objc_msgSend )( ( (id ( * )( id, SEL ))objc_msgSend )( contextClass, sel_registerName( "alloc" ) ), sel_registerName( "initWithFormat:shareContext:" ), pixelFormat, nullptr );

		( (void ( * )( id, SEL, id ))objc_msgSend )( context, sel_registerName( "setView:" ), contentView );
		( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "makeCurrentContext" ) );

		_hDC = contentView;
		_hRC = context;
#endif

		if ( gladLoadGL() != 0 )
		{
			SW_LOG_INFO( "OpenGL glad Loaded Successfully." );

#ifdef GL_CLIP_CONTROL
			if ( GLAD_GL_ARB_clip_control || GLAD_GL_VERSION_4_5 )
			{
				glClipControl( GL_LOWER_LEFT, GL_ZERO_TO_ONE );
				SW_LOG_INFO( "OpenGL glClipControl configured: GL_LOWER_LEFT, GL_ZERO_TO_ONE (DirectX NDC Y-Up & Z-Depth [0, 1] Active)" );
			}
#endif
		}
		else
		{
			SW_LOG_WARNING( "gladLoadGL이 0을 반환함, OpenGL 디바이스 초기화 실패" );
			return false;
		}

		_bInitialized = true;

		createTriangleResources();

		SW_LOG_INFO( "OpenGL RHI Backend Device Active (DirectX Coordinate System & Top-Left UV Texture Space Configured)" );
		return true;
	}

	bool OpenGLRHIDevice::createTriangleResources()
	{
		ShaderCompileDesc vsDesc{};
		vsDesc._filePath			 = "Shaders/BindlessTriangle.hlsl";
		vsDesc._entryPoint			 = "VSMain";
		vsDesc._stage				 = ShaderStage::Vertex;
		vsDesc._targetFormat		 = ShaderTargetFormat::SPIRV_OpenGL;
		ShaderCompileResult vsResult = ShaderCache::getOrCompile( vsDesc );

		ShaderCompileDesc psDesc{};
		psDesc._filePath			 = "Shaders/BindlessTriangle.hlsl";
		psDesc._entryPoint			 = "PSMain";
		psDesc._stage				 = ShaderStage::Pixel;
		psDesc._targetFormat		 = ShaderTargetFormat::SPIRV_OpenGL;
		ShaderCompileResult psResult = ShaderCache::getOrCompile( psDesc );

		if ( vsResult._bSuccess == false || psResult._bSuccess == false )
		{
			SW_LOG_ERROR( "[OpenGLRHIDevice] Failed to compile HLSL SPIR-V Shaders!" );
			return false;
		}

		GLuint vs = glCreateShader( GL_VERTEX_SHADER );
		GLuint ps = glCreateShader( GL_FRAGMENT_SHADER );

		glShaderBinary( 1, &vs, GL_SHADER_BINARY_FORMAT_SPIR_V, vsResult._bytecode.data(), static_cast<GLsizei>( vsResult._bytecode.size() ) );
		glSpecializeShader( vs, vsDesc._entryPoint.c_str(), 0, nullptr, nullptr );

		glShaderBinary( 1, &ps, GL_SHADER_BINARY_FORMAT_SPIR_V, psResult._bytecode.data(), static_cast<GLsizei>( psResult._bytecode.size() ) );
		glSpecializeShader( ps, psDesc._entryPoint.c_str(), 0, nullptr, nullptr );

		GLint vsCompiled = GL_FALSE;
		GLint psCompiled = GL_FALSE;
		glGetShaderiv( vs, GL_COMPILE_STATUS, &vsCompiled );
		glGetShaderiv( ps, GL_COMPILE_STATUS, &psCompiled );

		if ( vsCompiled != GL_TRUE || psCompiled != GL_TRUE )
		{
			SW_LOG_ERROR( "[OpenGLRHIDevice] Failed to specialize OpenGL 4.6 SPIR-V Shader!" );
			glDeleteShader( vs );
			glDeleteShader( ps );
			return false;
		}

		_shaderProgram = glCreateProgram();
		glAttachShader( _shaderProgram, vs );
		glAttachShader( _shaderProgram, ps );
		glLinkProgram( _shaderProgram );

		GLint linkStatus = GL_FALSE;
		glGetProgramiv( _shaderProgram, GL_LINK_STATUS, &linkStatus );

		glDeleteShader( vs );
		glDeleteShader( ps );

		if ( linkStatus != GL_TRUE )
		{
			SW_LOG_ERROR( "[OpenGLRHIDevice] Failed to link OpenGL 4.6 SPIR-V Shader Program!" );
			return false;
		}

		SW_LOG_INFO( "[OpenGLRHIDevice] Successfully compiled & linked OpenGL 4.6 HLSL SPIR-V Shaders!" );

		RHIVertex vertices[] = {
			{  { 0.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }}
		   };

		glGenVertexArrays( 1, &_vao );
		glGenBuffers( 1, &_vbo );

		glBindVertexArray( _vao );
		glBindBuffer( GL_ARRAY_BUFFER, _vbo );
		glBufferData( GL_ARRAY_BUFFER, sizeof( vertices ), vertices, GL_STATIC_DRAW );

		glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( RHIVertex ), reinterpret_cast<const void*>( offsetof( RHIVertex, position ) ) );
		glEnableVertexAttribArray( 0 );

		glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, sizeof( RHIVertex ), reinterpret_cast<const void*>( offsetof( RHIVertex, color ) ) );
		glEnableVertexAttribArray( 1 );

		glBindVertexArray( 0 );
		return true;
	}

	RHIBufferHandle OpenGLRHIDevice::createConstantBuffer( uint32 size )
	{
		uint32 alignedSize = ( size + 255u ) & ~255u;
		GLuint ubo;
		glGenBuffers( 1, &ubo );
		glBindBuffer( GL_UNIFORM_BUFFER, ubo );
		glBufferData( GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>( alignedSize ), nullptr, GL_DYNAMIC_DRAW );
		glBindBuffer( GL_UNIFORM_BUFFER, 0 );

		_constantBuffers.push_back( ubo );
		return static_cast<RHIBufferHandle>( ubo );
	}

	RHIBufferHandle OpenGLRHIDevice::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
	{
		if ( _bInitialized == false || elementSize == 0 || elementCount == 0 )
			return 0;

		uint32 size = elementSize * elementCount;
		uint32 alignedSize = ( size + 255u ) & ~255u;

		GLuint ssbo;
		glGenBuffers( 1, &ssbo );
		glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
		glBufferData( GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>( alignedSize ), nullptr, GL_DYNAMIC_DRAW );
		glBindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );

		_structuredBuffers.push_back( ssbo );
		return static_cast<RHIBufferHandle>( ssbo );
	}

	void OpenGLRHIDevice::updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size )
	{
		if ( buffer == 0 || data == nullptr )
			return;
		GLuint ubo = static_cast<GLuint>( buffer );
		glBindBuffer( GL_UNIFORM_BUFFER, ubo );
		glBufferSubData( GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>( size ), data );
		glBindBuffer( GL_UNIFORM_BUFFER, 0 );
	}

	void OpenGLRHIDevice::updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size )
	{
		if ( _bInitialized == false || buffer == 0 || data == nullptr || size == 0 )
			return;

		GLuint ssbo = static_cast<GLuint>( buffer );
		glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
		glBufferSubData( GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>( size ), data );
		glBindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );
	}

	void OpenGLRHIDevice::destroyBuffer( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return;
		GLuint ubo = static_cast<GLuint>( buffer );
		glDeleteBuffers( 1, &ubo );
		for ( auto it = _constantBuffers.begin(); it != _constantBuffers.end(); ++it )
		{
			if ( *it == ubo )
			{
				_constantBuffers.erase( it );
				break;
			}
		}
		for ( auto it = _structuredBuffers.begin(); it != _structuredBuffers.end(); ++it )
		{
			if ( *it == ubo )
			{
				_structuredBuffers.erase( it );
				break;
			}
		}
	}

	RHIDescriptorIndex OpenGLRHIDevice::registerBindlessResource( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;

		GLuint			   ubo	 = static_cast<GLuint>( buffer );
		RHIDescriptorIndex index;
		if ( _bindlessFreeList.empty() == false )
		{
			index = _bindlessFreeList.back();
			_bindlessFreeList.pop_back();
		}
		else
		{
			index = static_cast<RHIDescriptorIndex>( _registeredBindlessVector.size() );
		}

		if ( index >= _registeredBindlessVector.size() )
		{
			_registeredBindlessVector.resize( index + 1 );
		}
		_registeredBindlessVector[index].buffer = ubo;
		return index;
	}

	void OpenGLRHIDevice::unregisterBindlessResource( RHIDescriptorIndex index )
	{
		if ( index < _registeredBindlessVector.size() )
		{
			_registeredBindlessVector[index].buffer = 0;
			_bindlessFreeList.push_back( index );
		}
	}

	RHIDescriptorIndex OpenGLRHIDevice::registerBindlessUAV( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;

		GLuint			   ssbo	 = static_cast<GLuint>( buffer );
		RHIDescriptorIndex index;
		if ( _uavFreeList.empty() == false )
		{
			index = _uavFreeList.back();
			_uavFreeList.pop_back();
		}
		else
		{
			index = static_cast<RHIDescriptorIndex>( _registeredUAVs.size() );
		}

		if ( index >= _registeredUAVs.size() )
		{
			_registeredUAVs.resize( index + 1 );
		}
		_registeredUAVs[index].buffer = ssbo;
		return index;
	}

	void OpenGLRHIDevice::unregisterBindlessUAV( RHIDescriptorIndex index )
	{
		if ( index < _registeredUAVs.size() )
		{
			_registeredUAVs[index].buffer = 0;
			_uavFreeList.push_back( index );
		}
	}

	void OpenGLRHIDevice::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		if ( _bInitialized == false )
			return;

		if ( index < static_cast<RHIDescriptorIndex>( _registeredUAVs.size() ) )
		{
			GLuint ssbo = _registeredUAVs[index].buffer;
			glBindBufferBase( GL_SHADER_STORAGE_BUFFER, slot, ssbo );
		}
	}

	void OpenGLRHIDevice::drawTriangle( RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( _bInitialized == false || _shaderProgram == 0 || _vao == 0 )
			return;

		glUseProgram( _shaderProgram );

		if ( materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _registeredBindlessVector.size() ) )
		{
			GLuint ubo = _registeredBindlessVector[materialDescriptorIndex].buffer;

			glBindBufferBase( GL_UNIFORM_BUFFER, 0, ubo );
		}

		glBindVertexArray( _vao );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		glBindVertexArray( 0 );
	}

	void OpenGLRHIDevice::shutdownInternal()
	{
		if ( _bInitialized == false )
			return;

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
		for ( GLuint ubo : _constantBuffers )
		{
			glDeleteBuffers( 1, &ubo );
		}
		for ( GLuint ssbo : _structuredBuffers )
		{
			glDeleteBuffers( 1, &ssbo );
		}
		_constantBuffers.clear();
		_structuredBuffers.clear();
		_registeredBindlessVector.clear();
		_registeredUAVs.clear();

		_hDC = nullptr;

#if defined( SW_PLATFORM_WINDOWS )
		if ( _hRC )
		{
			wglMakeCurrent( nullptr, nullptr );
			wglDeleteContext( static_cast<HGLRC>( _hRC ) );
			_hRC = nullptr;
		}
#elif defined( SW_PLATFORM_LINUX )
		if ( _hRC )
		{
			glXMakeCurrent( (Display*)_hDC, None, NULL );
			glXDestroyContext( (Display*)_hDC, (GLXContext)_hRC );
			_hRC = nullptr;
		}
#elif defined( SW_PLATFORM_MACOS )
		if ( _hRC )
		{
			( (void ( * )( id, SEL ))objc_msgSend )( (id)_hRC, sel_registerName( "clearDrawable" ) );
			_hRC = nullptr;
		}
#endif

		_bInitialized = false;
		SW_LOG_INFO( "OpenGL RHI Backend Device Shutdown Cleanly." );
	}

	void OpenGLRHIDevice::beginFrame( float32 clearColor[4] )
	{
		if ( _bInitialized == false )
			return;

		glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	void OpenGLRHIDevice::endFrame( bool vsync )
	{
		if ( _bInitialized == false )
			return;
		(void)vsync;

#if defined( SW_PLATFORM_WINDOWS )
		SwapBuffers( static_cast<HDC>( _hDC ) );
#elif defined( SW_PLATFORM_LINUX )
		glXSwapBuffers( (Display*)_hDC, (Window)(uintptr_t)_hWnd );
#elif defined( SW_PLATFORM_MACOS )
		id context = (id)_hRC;
		( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "flushBuffer" ) );
#endif
	}

	void OpenGLRHIDevice::resize( uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;

		glViewport( 0, 0, static_cast<GLsizei>( width ), static_cast<GLsizei>( height ) );
		SW_LOG_INFO( "OpenGL RHI Resized to %# x %#", width, height );
	}

	class OpenGLCommandList final : public IRHICommandList
	{
	public:
		OpenGLCommandList( OpenGLRHIDevice* device )
			: _device{ device }
		{
		}

		void beginCommandList() override {}
		void endCommandList() override {}

		void setViewport( const RHIViewport& vp ) override
		{
			glViewport( static_cast<GLint>( vp._x ), static_cast<GLint>( vp._y ), static_cast<GLsizei>( vp._width ), static_cast<GLsizei>( vp._height ) );
		}

		void setPipelineState( RHIPipelineStateHandle pso ) override
		{
			if ( _device != nullptr )
				_device->setPipelineState( pso );
		}
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override
		{
			if ( _device != nullptr )
				_device->beginRenderPass( beginInfo );
		}
		void endRenderPass() override
		{
			if ( _device != nullptr )
				_device->endRenderPass();
		}

		void drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) override
		{
			if ( _device != nullptr )
			{
				_device->drawTriangle( materialDescriptorIndex );
			}
		}

		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override
		{
			if ( _device != nullptr )
			{
				_device->dispatchCompute( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
			}
		}

		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues = 0 ) override
		{

			(void)rootParameterIndex;
			(void)num32BitValues;
			(void)data;
			(void)destOffsetIn32BitValues;
		}

		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
		{
			if ( _device != nullptr )
			{
				_device->drawIndirect( argumentBuffer, argumentBufferOffset );
			}
		}

		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
		{
			if ( _device != nullptr )
			{
				_device->dispatchIndirect( argumentBuffer, argumentBufferOffset );
			}
		}

		void beginEventMarker( const utf8* name ) override
		{
			if ( _device != nullptr )
			{
				_device->beginEventMarker( name );
			}
		}

		void endEventMarker() override
		{
			if ( _device != nullptr )
			{
				_device->endEventMarker();
			}
		}

	private:
		OpenGLRHIDevice* _device;
	};

	void OpenGLRHIDevice::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		if ( _bInitialized == false )
			return;

		if ( glad_glDispatchCompute != nullptr )
		{
			glDispatchCompute( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
			glMemoryBarrier( GL_ALL_BARRIER_BITS );
		}
	}

	void OpenGLRHIDevice::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _bInitialized == false || argumentBuffer == 0 )
			return;

		GLuint buf = static_cast<GLuint>( argumentBuffer );
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );

		if ( glad_glDrawArraysIndirect != nullptr )
		{
			glDrawArraysIndirect( GL_TRIANGLES, reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) ) );
		}
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
	}

	void OpenGLRHIDevice::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _bInitialized == false || argumentBuffer == 0 )
			return;

		GLuint buf = static_cast<GLuint>( argumentBuffer );
		glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, buf );
		if ( glad_glDispatchComputeIndirect != nullptr )
		{
			glDispatchComputeIndirect( static_cast<GLintptr>( argumentBufferOffset ) );
			glMemoryBarrier( GL_ALL_BARRIER_BITS );
		}
		glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, 0 );
	}

	void OpenGLRHIDevice::beginEventMarker( const utf8* name )
	{
		(void)name;
	}

	void OpenGLRHIDevice::endEventMarker()
	{
	}

	std::unique_ptr<IRHICommandList> OpenGLRHIDevice::createCommandList()
	{
		return std::make_unique<OpenGLCommandList>( this );
	}

	void OpenGLRHIDevice::executeCommandList( IRHICommandList* cmdList )
	{
		(void)cmdList;
	}

	RHIPipelineStateHandle OpenGLRHIDevice::createPipelineState( const RHIPipelineStateDesc& desc )
	{
		OpenGLPipelineStateRecord record{};

		ShaderCompileDesc vsDesc{};
		vsDesc._filePath			 = desc._vertexShaderPath;
		vsDesc._entryPoint			 = desc._vertexEntryPoint;
		vsDesc._stage				 = ShaderStage::Vertex;
		vsDesc._targetFormat		 = ShaderTargetFormat::SPIRV_OpenGL;
		ShaderCompileResult vsResult = ShaderCache::getOrCompile( vsDesc );

		ShaderCompileDesc psDesc{};
		psDesc._filePath			 = desc._pixelShaderPath;
		psDesc._entryPoint			 = desc._pixelEntryPoint;
		psDesc._stage				 = ShaderStage::Pixel;
		psDesc._targetFormat		 = ShaderTargetFormat::SPIRV_OpenGL;
		ShaderCompileResult psResult = ShaderCache::getOrCompile( psDesc );

		if ( vsResult._bSuccess && psResult._bSuccess )
		{
			GLuint vs = glCreateShader( GL_VERTEX_SHADER );
			GLuint ps = glCreateShader( GL_FRAGMENT_SHADER );

			glShaderBinary( 1, &vs, GL_SHADER_BINARY_FORMAT_SPIR_V, vsResult._bytecode.data(), static_cast<GLsizei>( vsResult._bytecode.size() ) );
			glSpecializeShader( vs, vsDesc._entryPoint.c_str(), 0, nullptr, nullptr );

			glShaderBinary( 1, &ps, GL_SHADER_BINARY_FORMAT_SPIR_V, psResult._bytecode.data(), static_cast<GLsizei>( psResult._bytecode.size() ) );
			glSpecializeShader( ps, psDesc._entryPoint.c_str(), 0, nullptr, nullptr );

			GLint vsCompiled = GL_FALSE;
			GLint psCompiled = GL_FALSE;
			glGetShaderiv( vs, GL_COMPILE_STATUS, &vsCompiled );
			glGetShaderiv( ps, GL_COMPILE_STATUS, &psCompiled );

			if ( vsCompiled == GL_TRUE && psCompiled == GL_TRUE )
			{
				GLuint program = glCreateProgram();
				glAttachShader( program, vs );
				glAttachShader( program, ps );
				glLinkProgram( program );

				GLint isLinked = 0;
				glGetProgramiv( program, GL_LINK_STATUS, &isLinked );
				if ( isLinked == GL_TRUE )
				{
					record.program = program;
				}
				else
				{
					glDeleteProgram( program );
				}
			}
			glDeleteShader( vs );
			glDeleteShader( ps );
		}

		if ( record.program == 0 )
		{

			return 0;
		}

		_pipelineStates.push_back( record );
		return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
	}

	RHIPipelineStateHandle OpenGLRHIDevice::createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint )
	{
		OpenGLPipelineStateRecord record{};

		ShaderCompileDesc csDesc{};
		csDesc._filePath			 = shaderPath;
		csDesc._entryPoint			 = entryPoint;
		csDesc._stage				 = ShaderStage::Compute;
		csDesc._targetFormat		 = ShaderTargetFormat::SPIRV_OpenGL;
		ShaderCompileResult csResult = ShaderCache::getOrCompile( csDesc );

		if ( csResult._bSuccess )
		{
			GLuint cs = glCreateShader( GL_COMPUTE_SHADER );

			glShaderBinary( 1, &cs, GL_SHADER_BINARY_FORMAT_SPIR_V, csResult._bytecode.data(), static_cast<GLsizei>( csResult._bytecode.size() ) );
			glSpecializeShader( cs, csDesc._entryPoint.c_str(), 0, nullptr, nullptr );

			GLint csCompiled = GL_FALSE;
			glGetShaderiv( cs, GL_COMPILE_STATUS, &csCompiled );

			if ( csCompiled == GL_TRUE )
			{
				GLuint program = glCreateProgram();
				glAttachShader( program, cs );
				glLinkProgram( program );

				GLint isLinked = 0;
				glGetProgramiv( program, GL_LINK_STATUS, &isLinked );
				if ( isLinked == GL_TRUE )
				{
					record.program = program;
				}
				else
				{
					GLchar infoLog[1024];
					glGetProgramInfoLog( program, sizeof(infoLog), nullptr, infoLog );
					SW_LOG_ERROR( "[OpenGLRHIDevice] Compute shader program link failed: %s", infoLog );
					glDeleteProgram( program );
				}
			}
			else
			{
				GLchar infoLog[1024];
				glGetShaderInfoLog( cs, sizeof(infoLog), nullptr, infoLog );
				SW_LOG_ERROR( "[OpenGLRHIDevice] Compute shader specialize/compile failed: %s", infoLog );
			}
			glDeleteShader( cs );
		}
		if ( record.program == 0 )
		{

			return 0;
		}

		_pipelineStates.push_back( record );
		return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
	}

	void OpenGLRHIDevice::destroyPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() )
			return;
		_pipelineStates[pso - 1] = OpenGLPipelineStateRecord{};
	}

	void OpenGLRHIDevice::setPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() )
			return;

		const auto& record = _pipelineStates[pso - 1];
		if ( record.program != 0 )
			glUseProgram( record.program );

		if ( record.vao != 0 )
			glBindVertexArray( record.vao );
		else if ( _vao != 0 )
			glBindVertexArray( _vao );
	}

	void OpenGLRHIDevice::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() )
			return;

		const auto& record = _pipelineStates[pso - 1];
		if ( record.program != 0 )
			glUseProgram( record.program );
	}

	RHIRenderPassHandle OpenGLRHIDevice::createRenderPass( const RHIRenderPassDesc& desc )
	{
		OpenGLRenderPassRecord record{ desc };
		_renderPasses.push_back( record );
		return static_cast<RHIRenderPassHandle>( _renderPasses.size() );
	}

	void OpenGLRHIDevice::destroyRenderPass( RHIRenderPassHandle pass )
	{
		(void)pass;
	}

	void OpenGLRHIDevice::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		if ( _bInitialized == false )
			return;

		glClearColor( beginInfo._clearColor[0], beginInfo._clearColor[1], beginInfo._clearColor[2], beginInfo._clearColor[3] );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

		uint32 w = beginInfo._width > 0 ? beginInfo._width : _width;
		uint32 h = beginInfo._height > 0 ? beginInfo._height : _height;
		glViewport( 0, 0, static_cast<GLsizei>( w ), static_cast<GLsizei>( h ) );
	}

	void OpenGLRHIDevice::endRenderPass()
	{
	}
}
