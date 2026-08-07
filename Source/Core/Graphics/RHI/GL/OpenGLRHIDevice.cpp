/**
 * @file OpenGLRHIDevice.cpp
 * @brief OpenGL RHI 디바이스 구현
 */
#include "OpenGLRHIDevice.h"
#include "Core/Graphics/RHI/RHIDeferredCommandList.h"
#include "Core/Graphics/Shader/ShaderCache.h"
#include "Core/Common/PlatformHeaders.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Utility/Delegate/Delegate.h"
#include <glad/glad.h>

#if defined( SW_PLATFORM_WINDOWS )
	// WGL system header — kept local (not in PlatformHeaders) to avoid clashing with glad.
	#include <gl/GL.h>
	#define WGL_CONTEXT_MAJOR_VERSION_ARB	 0x2091
	#define WGL_CONTEXT_MINOR_VERSION_ARB	 0x2092
	#define WGL_CONTEXT_PROFILE_MASK_ARB	 0x9126
	#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef HGLRC( WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC )( HDC hDC, HGLRC hShareContext, const int* attribList );
#elif defined( SW_PLATFORM_LINUX )
	#define GLX_CONTEXT_MAJOR_VERSION_ARB	 0x2091
	#define GLX_CONTEXT_MINOR_VERSION_ARB	 0x2092
	#define GLX_CONTEXT_PROFILE_MASK_ARB	 0x9126
	#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef GLXContext ( *PFNGLXCREATECONTEXTATTRIBSARBPROC )( Display*, GLXFBConfig, GLXContext, Bool, const int* );
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
			0 };
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

		uint32 size		   = elementSize * elementCount;
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
		GLuint glBuffer = static_cast<GLuint>( buffer );
		bool   bFound	= false;
		for ( auto it = _constantBuffers.begin(); it != _constantBuffers.end(); ++it )
		{
			if ( *it == glBuffer )
			{
				_constantBuffers.erase( it );
				bFound = true;
				break;
			}
		}
		for ( auto it = _structuredBuffers.begin(); it != _structuredBuffers.end(); ++it )
		{
			if ( *it == glBuffer )
			{
				_structuredBuffers.erase( it );
				bFound = true;
				break;
			}
		}
		if ( bFound == false )
			return;

		auto releaseCb = [glBuffer]()
		{
			GLuint name = glBuffer;
			glDeleteBuffers( 1, &name );
		};
		_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
	}

	namespace
	{
		GLenum toGlInternalFormat( RHIFormat format )
		{
			switch ( format )
			{
				case RHIFormat::R8G8B8A8_UNORM:
				case RHIFormat::B8G8R8A8_UNORM:
					return GL_RGBA8;
				case RHIFormat::R16G16B16A16_FLOAT:
					return GL_RGBA16F;
				case RHIFormat::D24_UNORM_S8_UINT:
					return GL_DEPTH24_STENCIL8;
				case RHIFormat::R32G32B32_FLOAT:
					return GL_RGB32F;
				case RHIFormat::R32G32_FLOAT:
					return GL_RG32F;
				case RHIFormat::R32_FLOAT:
					return GL_R32F;
			}
			return GL_RGBA8;
		}

		GLenum toGlFormat( RHIFormat format )
		{
			switch ( format )
			{
				case RHIFormat::R8G8B8A8_UNORM:
				case RHIFormat::R16G16B16A16_FLOAT:
					return GL_RGBA;
				case RHIFormat::B8G8R8A8_UNORM:
					return GL_BGRA;
				case RHIFormat::D24_UNORM_S8_UINT:
					return GL_DEPTH_STENCIL;
				case RHIFormat::R32G32B32_FLOAT:
					return GL_RGB;
				case RHIFormat::R32G32_FLOAT:
					return GL_RG;
				case RHIFormat::R32_FLOAT:
					return GL_RED;
			}
			return GL_RGBA;
		}

		GLenum toGlType( RHIFormat format )
		{
			switch ( format )
			{
				case RHIFormat::R8G8B8A8_UNORM:
				case RHIFormat::B8G8R8A8_UNORM:
					return GL_UNSIGNED_BYTE;
				case RHIFormat::R16G16B16A16_FLOAT:
					return GL_HALF_FLOAT;
				case RHIFormat::D24_UNORM_S8_UINT:
					return GL_UNSIGNED_INT_24_8;
				case RHIFormat::R32G32B32_FLOAT:
				case RHIFormat::R32G32_FLOAT:
				case RHIFormat::R32_FLOAT:
					return GL_FLOAT;
			}
			return GL_UNSIGNED_BYTE;
		}

		GLenum toGlPrimitive( RHIPrimitiveTopology topology )
		{
			switch ( topology )
			{
				case RHIPrimitiveTopology::LineList:
					return GL_LINES;
				case RHIPrimitiveTopology::PointList:
					return GL_POINTS;
				case RHIPrimitiveTopology::TriangleList:
				default:
					return GL_TRIANGLES;
			}
		}

		void applyVsyncInterval( void* hDC, void* hRC, bool vsync )
		{
#if defined( SW_PLATFORM_WINDOWS )
			(void)hRC;
			using PFNWGLSWAPINTERVALEXTPROC = BOOL( WINAPI* )( int );
			static PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT =
				reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>( wglGetProcAddress( "wglSwapIntervalEXT" ) );
			if ( s_wglSwapIntervalEXT )
				s_wglSwapIntervalEXT( vsync ? 1 : 0 );
#elif defined( SW_PLATFORM_LINUX )
			(void)hRC;
			using PFNGLXSWAPINTERVALEXTPROC = void ( * )( Display*, GLXDrawable, int );
			static PFNGLXSWAPINTERVALEXTPROC s_glXSwapIntervalEXT =
				reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>( glXGetProcAddressARB( (const GLubyte*)"glXSwapIntervalEXT" ) );
			if ( s_glXSwapIntervalEXT && hDC )
			{
				Display* dpy = static_cast<Display*>( hDC );
				s_glXSwapIntervalEXT( dpy, glXGetCurrentDrawable(), vsync ? 1 : 0 );
			}
#elif defined( SW_PLATFORM_MACOS )
			(void)hDC;
			if ( hRC )
			{
				id		 context = static_cast<id>( hRC );
				GLint	 interval = vsync ? 1 : 0;
				( (void ( * )( id, SEL, GLint*, GLint ))objc_msgSend )(
					context, sel_registerName( "setValues:forParameter:" ), &interval, 222 /* NSOpenGLCPSwapInterval */ );
			}
#endif
		}
	} // namespace

	RHITextureHandle OpenGLRHIDevice::createTexture2D( const RHITextureDesc& desc )
	{
		if ( _bInitialized == false || desc._width == 0 || desc._height == 0 )
			return 0;

		const uint32 mipLevels = desc._mipLevels > 0 ? desc._mipLevels : 1;
		const GLenum internalFmt = toGlInternalFormat( desc._format );
		const bool	 bDepth		 = desc._bIsDepthStencil || desc._format == RHIFormat::D24_UNORM_S8_UINT;

		GLuint tex = 0;
		glGenTextures( 1, &tex );
		glBindTexture( GL_TEXTURE_2D, tex );

		// Immutable storage when mips/UAV requested (required for image load/store).
		if ( mipLevels > 1 || desc._bIsUnorderedAccess )
		{
			glTexStorage2D( GL_TEXTURE_2D, static_cast<GLsizei>( mipLevels ), internalFmt,
							static_cast<GLsizei>( desc._width ), static_cast<GLsizei>( desc._height ) );
		}
		else
		{
			glTexImage2D( GL_TEXTURE_2D, 0, static_cast<GLint>( internalFmt ),
						  static_cast<GLsizei>( desc._width ), static_cast<GLsizei>( desc._height ),
						  0, toGlFormat( desc._format ), toGlType( desc._format ), nullptr );
		}

		const GLint minFilter = ( mipLevels > 1 ) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		if ( mipLevels > 1 && desc._bIsShaderResource )
			glGenerateMipmap( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, 0 );

		OpenGLTextureRecord record{};
		record.texture		 = tex;
		record.width		 = desc._width;
		record.height		 = desc._height;
		record.mipLevels	 = mipLevels;
		record.format		 = desc._format;
		record.bDepthStencil = bDepth ? 1 : 0;
		record.bUAV			 = desc._bIsUnorderedAccess ? 1 : 0;
		record.reserved		 = 0;

		if ( desc._bIsRenderTarget || bDepth )
		{
			GLuint fbo = 0;
			glGenFramebuffers( 1, &fbo );
			glBindFramebuffer( GL_FRAMEBUFFER, fbo );
			if ( bDepth )
			{
				glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tex, 0 );
				glDrawBuffer( GL_NONE );
				glReadBuffer( GL_NONE );
			}
			else
			{
				glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
			}
			const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
			glBindFramebuffer( GL_FRAMEBUFFER, 0 );
			if ( status != GL_FRAMEBUFFER_COMPLETE )
			{
				SW_LOG_WARNING( "[OpenGL] createTexture2D FBO incomplete (status=%#) — texture kept without FBO.",
								static_cast<uint32>( status ) );
				glDeleteFramebuffers( 1, &fbo );
			}
			else
			{
				record.fbo = fbo;
			}
		}

		const RHITextureHandle handle = static_cast<RHITextureHandle>( _nextTextureId++ );
		_textures.emplace( handle, record );
		return handle;
	}

	void OpenGLRHIDevice::destroyTexture( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return;

		auto it = _textures.find( texture );
		if ( it == _textures.end() )
			return;

		const GLuint fboName = it->second.fbo;
		const GLuint texName = it->second.texture;
		_textures.erase( it );

		for ( size_t i = 0; i < _registeredTextures.size(); ++i )
		{
			if ( _registeredTextures[i].texture == texName )
			{
				_registeredTextures[i].texture = 0;
				_textureFreeList.push_back( static_cast<uint32>( i ) );
			}
		}

		auto releaseCb = [fboName, texName]()
		{
			if ( fboName != 0 )
			{
				GLuint name = fboName;
				glDeleteFramebuffers( 1, &name );
			}
			if ( texName != 0 )
			{
				GLuint name = texName;
				glDeleteTextures( 1, &name );
			}
		};
		_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
	}

	uint32 OpenGLRHIDevice::getGLTextureName( RHITextureHandle texture ) const
	{
		if ( texture == 0 )
			return 0;
		auto it = _textures.find( texture );
		return it != _textures.end() ? it->second.texture : 0;
	}

	RHIDescriptorIndex OpenGLRHIDevice::registerBindlessTexture( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return kInvalidDescriptorIndex;

		const uint32 glName = getGLTextureName( texture );
		if ( glName == 0 )
			return kInvalidDescriptorIndex;

		RHIDescriptorIndex index;
		if ( _textureFreeList.empty() == false )
		{
			index = _textureFreeList.back();
			_textureFreeList.pop_back();
		}
		else
		{
			index = static_cast<RHIDescriptorIndex>( _registeredTextures.size() );
		}

		if ( index >= _registeredTextures.size() )
			_registeredTextures.resize( index + 1 );

		_registeredTextures[index].texture = glName;
		return index;
	}

	RHIDescriptorIndex OpenGLRHIDevice::registerBindlessResource( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;

		GLuint			   ubo = static_cast<GLuint>( buffer );
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

		GLuint			   ssbo = static_cast<GLuint>( buffer );
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
		if ( _bInitialized == false )
			return;

		GLuint program = _shaderProgram;
		GLuint vao	   = _vao;
		GLenum mode	   = GL_TRIANGLES;

		if ( _boundGraphicsPso != 0 && _boundGraphicsPso <= _pipelineStates.size() )
		{
			const OpenGLPipelineStateRecord& pso = _pipelineStates[_boundGraphicsPso - 1];
			if ( pso.program != 0 )
			{
				program = pso.program;
				mode	= toGlPrimitive( pso.topology );
				if ( pso.vao != 0 )
					vao = pso.vao;
			}
		}

		if ( program == 0 || vao == 0 )
			return;

		glUseProgram( program );

		if ( materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _registeredBindlessVector.size() ) )
		{
			GLuint ubo = _registeredBindlessVector[materialDescriptorIndex].buffer;
			glBindBufferBase( GL_UNIFORM_BUFFER, 0, ubo );
		}

		glBindVertexArray( vao );
		glDrawArrays( mode, 0, 3 );
		glBindVertexArray( 0 );
	}

	void OpenGLRHIDevice::shutdownInternal()
	{
		if ( _bInitialized == false )
			return;

		_releaseQueue.flushAll();

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
		_registeredTextures.clear();
		_textureFreeList.clear();
		_bindlessFreeList.clear();
		_uavFreeList.clear();
		if ( _computeRootConstantUbo != 0 )
		{
			GLuint ubo = _computeRootConstantUbo;
			glDeleteBuffers( 1, &ubo );
			_computeRootConstantUbo = 0;
		}
		std::memset( _computeRootConstantShadow, 0, sizeof( _computeRootConstantShadow ) );

		for ( auto& pso : _pipelineStates )
		{
			if ( pso.program != 0 )
				glDeleteProgram( pso.program );
			pso = OpenGLPipelineStateRecord{};
		}
		_pipelineStates.clear();
		_renderPasses.clear();
		_boundGraphicsPso = 0;

		for ( auto& pair : _textures )
		{
			if ( pair.second.fbo != 0 )
				glDeleteFramebuffers( 1, &pair.second.fbo );
			if ( pair.second.texture != 0 )
				glDeleteTextures( 1, &pair.second.texture );
		}
		_textures.clear();

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

		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glViewport( 0, 0, static_cast<GLsizei>( _width ), static_cast<GLsizei>( _height ) );
		glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	void OpenGLRHIDevice::beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] )
	{
		if ( colorTarget == 0 )
		{
			beginFrame( clearColor );
			return;
		}

		if ( _bInitialized == false )
			return;

		auto it = _textures.find( colorTarget );
		if ( it == _textures.end() || it->second.fbo == 0 )
			return;

		const OpenGLTextureRecord& record = it->second;
		glBindFramebuffer( GL_FRAMEBUFFER, record.fbo );
		glViewport( 0, 0, static_cast<GLsizei>( record.width ), static_cast<GLsizei>( record.height ) );
		glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	void OpenGLRHIDevice::endOffscreenPass( RHITextureHandle colorTarget )
	{
		if ( colorTarget == 0 || _bInitialized == false )
			return;

		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		// Make color attachment readable as a texture for subsequent sampling (ImGui Game View etc.).
		glMemoryBarrier( GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT );
	}

	void OpenGLRHIDevice::waitIdle()
	{
		if ( _bInitialized == false )
			return;
		glFinish();
		_releaseQueue.flushAll();
	}

	void OpenGLRHIDevice::endFrame( bool vsync )
	{
		if ( _bInitialized == false )
			return;

		const int8 desired = vsync ? 1 : 0;
		if ( _lastVsync != desired )
		{
			applyVsyncInterval( _hDC, _hRC, vsync );
			_lastVsync = desired;
		}

#if defined( SW_PLATFORM_WINDOWS )
		SwapBuffers( static_cast<HDC>( _hDC ) );
#elif defined( SW_PLATFORM_LINUX )
		glXSwapBuffers( (Display*)_hDC, (Window)(uintptr_t)_hWnd );
#elif defined( SW_PLATFORM_MACOS )
		id context = (id)_hRC;
		( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "flushBuffer" ) );
#endif
		_releaseQueue.tickFrame();
	}

	void OpenGLRHIDevice::resize( uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;

		glViewport( 0, 0, static_cast<GLsizei>( width ), static_cast<GLsizei>( height ) );
		SW_LOG_INFO( "OpenGL RHI Resized to %# x %#", width, height );
	}

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

	void OpenGLRHIDevice::setViewport( const RHIViewport& viewport )
	{
		if ( _bInitialized == false )
			return;
		glViewport( static_cast<GLint>( viewport._x ),
					static_cast<GLint>( viewport._y ),
					static_cast<GLsizei>( viewport._width ),
					static_cast<GLsizei>( viewport._height ) );
	}

	bool OpenGLRHIDevice::ensureComputeRootConstantUbo()
	{
		if ( _computeRootConstantUbo != 0 )
			return true;
		if ( _bInitialized == false )
			return false;

		GLuint ubo = 0;
		if ( glad_glCreateBuffers != nullptr )
		{
			glCreateBuffers( 1, &ubo );
			glNamedBufferStorage( ubo, static_cast<GLsizeiptr>( sizeof( _computeRootConstantShadow ) ), nullptr, GL_DYNAMIC_STORAGE_BIT );
		}
		else
		{
			glGenBuffers( 1, &ubo );
			glBindBuffer( GL_UNIFORM_BUFFER, ubo );
			glBufferData( GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>( sizeof( _computeRootConstantShadow ) ), nullptr, GL_DYNAMIC_DRAW );
			glBindBuffer( GL_UNIFORM_BUFFER, 0 );
		}
		_computeRootConstantUbo = ubo;
		return _computeRootConstantUbo != 0;
	}

	void OpenGLRHIDevice::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues )
	{
		if ( _bInitialized == false || num32BitValues == 0 || data == nullptr )
			return;
		if ( destOffsetIn32BitValues >= kMaxComputeRootConstantDwords )
			return;

		const uint32 maxCount = kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
		const uint32 count	  = num32BitValues < maxCount ? num32BitValues : maxCount;
		if ( ensureComputeRootConstantUbo() == false )
			return;

		std::memcpy( _computeRootConstantShadow + destOffsetIn32BitValues, data, static_cast<size_t>( count ) * sizeof( uint32 ) );

		if ( glad_glNamedBufferSubData != nullptr )
		{
			glNamedBufferSubData( _computeRootConstantUbo, 0, static_cast<GLsizeiptr>( sizeof( _computeRootConstantShadow ) ), _computeRootConstantShadow );
		}
		else
		{
			glBindBuffer( GL_UNIFORM_BUFFER, _computeRootConstantUbo );
			glBufferSubData( GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>( sizeof( _computeRootConstantShadow ) ), _computeRootConstantShadow );
			glBindBuffer( GL_UNIFORM_BUFFER, 0 );
		}
		glBindBufferBase( GL_UNIFORM_BUFFER, rootParameterIndex, _computeRootConstantUbo );
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
		if ( _bInitialized == false || name == nullptr )
			return;
		// OpenGL 4.3+ / 4.6 Core: KHR_debug
		glPushDebugGroup( GL_DEBUG_SOURCE_APPLICATION, 0, -1, name );
	}

	void OpenGLRHIDevice::endEventMarker()
	{
		if ( _bInitialized == false )
			return;
		glPopDebugGroup();
	}

	std::unique_ptr<IRHICommandList> OpenGLRHIDevice::createCommandList()
	{
		return std::make_unique<RHIDeferredCommandList>();
	}

	void OpenGLRHIDevice::executeCommandList( IRHICommandList* cmdList )
	{
		executeDeferredCommandList( this, cmdList );
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
			return 0;

		record.topology		   = desc._topology;
		record.fillMode		   = desc._fillMode;
		record.cullMode		   = desc._cullMode;
		record.bEnableDepthTest = desc._bEnableDepthTest ? 1 : 0;
		record.bEnableBlend	   = desc._bEnableBlend ? 1 : 0;
		record.reserved		   = 0;

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
					glGetProgramInfoLog( program, sizeof( infoLog ), nullptr, infoLog );
					SW_LOG_ERROR( "[OpenGLRHIDevice] Compute shader program link failed: %s", infoLog );
					glDeleteProgram( program );
				}
			}
			else
			{
				GLchar infoLog[1024];
				glGetShaderInfoLog( cs, sizeof( infoLog ), nullptr, infoLog );
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

		OpenGLPipelineStateRecord& record = _pipelineStates[pso - 1];
		const GLuint			   program = record.program;
		const GLuint			   vao	   = record.vao;
		record							  = OpenGLPipelineStateRecord{};

		if ( _boundGraphicsPso == pso )
			_boundGraphicsPso = 0;

		if ( program == 0 && vao == 0 )
			return;

		auto releaseCb = [program, vao]()
		{
			if ( program != 0 )
			{
				GLuint name = program;
				glDeleteProgram( name );
			}
			if ( vao != 0 )
			{
				GLuint name = vao;
				glDeleteVertexArrays( 1, &name );
			}
		};
		_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
	}

	void OpenGLRHIDevice::setPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() )
			return;

		const auto& record = _pipelineStates[pso - 1];
		if ( record.program == 0 )
			return;

		_boundGraphicsPso = pso;
		glUseProgram( record.program );

		if ( record.vao != 0 )
			glBindVertexArray( record.vao );
		else if ( _vao != 0 )
			glBindVertexArray( _vao );

		glPolygonMode( GL_FRONT_AND_BACK, record.fillMode == RHIFillMode::Wireframe ? GL_LINE : GL_FILL );

		if ( record.cullMode == RHICullMode::None )
		{
			glDisable( GL_CULL_FACE );
		}
		else
		{
			glEnable( GL_CULL_FACE );
			glCullFace( record.cullMode == RHICullMode::Front ? GL_FRONT : GL_BACK );
			glFrontFace( GL_CW ); // match DirectX / clip-control path
		}

		if ( record.bEnableDepthTest )
		{
			glEnable( GL_DEPTH_TEST );
			glDepthFunc( GL_LESS );
			glDepthMask( GL_TRUE );
		}
		else
		{
			glDisable( GL_DEPTH_TEST );
			glDepthMask( GL_FALSE );
		}

		if ( record.bEnableBlend )
		{
			glEnable( GL_BLEND );
			glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		}
		else
		{
			glDisable( GL_BLEND );
		}
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
		OpenGLRenderPassRecord record{};
		record.desc		= desc;
		record.bAlive	= 1;
		record.reserved = 0;
		_renderPasses.push_back( record );
		return static_cast<RHIRenderPassHandle>( _renderPasses.size() );
	}

	void OpenGLRHIDevice::destroyRenderPass( RHIRenderPassHandle pass )
	{
		if ( pass == 0 || pass > _renderPasses.size() )
			return;
		_renderPasses[pass - 1].bAlive = 0;
		_renderPasses[pass - 1].desc   = RHIRenderPassDesc{};
	}

	void OpenGLRHIDevice::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		if ( _bInitialized == false )
			return;

		uint32 w = beginInfo._width > 0 ? beginInfo._width : _width;
		uint32 h = beginInfo._height > 0 ? beginInfo._height : _height;

		bool bDepthTarget = false;
		if ( beginInfo._colorTarget != 0 )
		{
			auto it = _textures.find( beginInfo._colorTarget );
			if ( it != _textures.end() && it->second.fbo != 0 )
			{
				const OpenGLTextureRecord& record = it->second;
				glBindFramebuffer( GL_FRAMEBUFFER, record.fbo );
				bDepthTarget = record.bDepthStencil != 0;
				if ( beginInfo._width == 0 )
					w = record.width;
				if ( beginInfo._height == 0 )
					h = record.height;
			}
			else
			{
				glBindFramebuffer( GL_FRAMEBUFFER, 0 );
			}
		}
		else
		{
			glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		}

		glViewport( 0, 0, static_cast<GLsizei>( w ), static_cast<GLsizei>( h ) );

		RHIRenderPassLoadOp colorLoad  = RHIRenderPassLoadOp::Clear;
		bool				bClearDepth = true;
		float32				clearDepth  = 1.0f;
		if ( beginInfo._renderPass != 0 && beginInfo._renderPass <= _renderPasses.size() )
		{
			const OpenGLRenderPassRecord& rp = _renderPasses[beginInfo._renderPass - 1];
			if ( rp.bAlive )
			{
				if ( rp.desc._colorAttachments.empty() == false )
					colorLoad = rp.desc._colorAttachments[0]._loadOp;
				bClearDepth = rp.desc._bHasDepthStencil != 0 || bDepthTarget;
				clearDepth	= rp.desc._clearDepth;
			}
		}

		GLbitfield clearMask = 0;
		if ( bDepthTarget == false && colorLoad == RHIRenderPassLoadOp::Clear )
		{
			glClearColor( beginInfo._clearColor[0], beginInfo._clearColor[1], beginInfo._clearColor[2], beginInfo._clearColor[3] );
			clearMask |= GL_COLOR_BUFFER_BIT;
		}
		if ( bClearDepth || bDepthTarget )
		{
			glClearDepth( static_cast<GLclampd>( clearDepth ) );
			clearMask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
		}
		if ( clearMask != 0 )
			glClear( clearMask );
	}

	void OpenGLRHIDevice::endRenderPass()
	{
		if ( _bInitialized == false )
			return;
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}
} // namespace sw
