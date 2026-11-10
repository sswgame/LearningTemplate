#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/thread.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandList.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIUtil.h"
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"
#include "Engine/Graphics/RHI/Transfer/RHIStagingRingUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    OpenGLRHIDevice::OpenGLRHIDevice( uint32 deviceGeneration )
        : _pHDC{ nullptr }
        , _pHRC{ nullptr }
        , _width{ 1280 }
        , _height{ 720 }
        , _vao{ 0 }
        , _meshVao{ 0 }
        , _defaultSampler{ 0 }
        , _materialSampler{ 0 }
        , _engineSampler{ 0 }
        , _defaultTexture{ 0 }
        , _contextOwnerThread{}
        , _contextOwnedStartNanos{ 0 }
        , _gpuBuffers{}
        , _bindlessMutex{}
        , _bufferRegistry{}
        , _uavRegistry{}
        , _gpuTextures{}
        , _mapCompositeFbo{}
        , _textureRegistry{}
        , _arrTimestampQuery{}
        , _timestampRing{}
        , _timestampFrame{}
        , _arrComputeRootConstantShadow{}
        , _pipelineStates{}
        , _renderPassRegistry{}
        , _releaseQueue{}
        , _arrFrameDoneSync{}
        , _frameFenceRing{}
        , _transferSerials{ deviceGeneration }
        , _transferQueue{ _transferSerials }
        , _readbackTable{}
        , _stagingRing{}
        , _pStagingRingMapped{ nullptr }
        , _readbackRing{}
        , _pReadbackRingMapped{ nullptr }
        , _stagingSolo{}
        , _readbackSolo{}
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
        , _timestampProvider{ *this }
        , _contextThreadBinding{ *this }
        , _computeRootConstantUbo{ 0 }
        , _stagingRingBuffer{ 0 }
        , _readbackRingBuffer{ 0 }
        , _lastVsync{ -1 }
        , _bTimestampEnabled{ SW_FALSE }
        , _bTimestampReady{ SW_FALSE }
        , _bInitialized{ SW_FALSE }
        , _bNvxMemoryInfo{ SW_FALSE }
        , _bAtiMemInfo{ SW_FALSE }
        , _bTextureASTC{ SW_FALSE }
        , _bTextureETC2{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
        _resourceImpl          = sw::make_unique<OpenGLRHIResourceFactory>( this );
        _pTimestampProvider    = &_timestampProvider;
        _pContextThreadBinding = &_contextThreadBinding;
    }

    OpenGLRHIDevice::~OpenGLRHIDevice()
    {
        shutdown();
    }

    IRHIResourceFactory* OpenGLRHIDevice::getResourceFactory()
    {
        return _resourceImpl.get();
    }
    IRHICommandContext* OpenGLRHIDevice::getFrameStreamContext()
    {
        return _frameStreamContext.get();
    }

    bool OpenGLRHIDevice::queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget )
    {
        if ( _bInitialized == SW_FALSE || ( _bNvxMemoryInfo == SW_FALSE && _bAtiMemInfo == SW_FALSE ) )
            return false;

        // 확장 값은 KB 단위이고, 다른 프로세스가 쓰는 양까지 포함한 디바이스 전체 값이다. 그래서 추적기를 빼 "엔진 밖"을 내지 않는다(Scope Device).
        ScopedOpenGLContext contextScope( this );
        outBudget._scope = RHIMemoryScope::Device;
        if ( _bNvxMemoryInfo == SW_TRUE )
        {
            GLint totalKb{ 0 };
            GLint currentKb{ 0 };
            glGetIntegerv( OpenGLRHIUtil::kGPUMemoryInfoTotalAvailableNvx, &totalKb );
            glGetIntegerv( OpenGLRHIUtil::kGPUMemoryInfoCurrentAvailableNvx, &currentKb );
            if ( totalKb <= 0 )
                return false;
            const uint64 totalBytes    = static_cast<uint64>( totalKb ) * constant::memory::kBytesPerKilobyte;
            const uint64 currentBytes  = currentKb > 0 ? static_cast<uint64>( currentKb ) * constant::memory::kBytesPerKilobyte : 0;
            outBudget._budgetBytes     = totalBytes;
            outBudget._availableBytes  = currentBytes;
            outBudget._usageBytes      = totalBytes > currentBytes ? totalBytes - currentBytes : 0;
            outBudget._bUsageKnown     = SW_TRUE;
            outBudget._bBudgetKnown    = SW_TRUE;
            outBudget._bAvailableKnown = SW_TRUE;
            return true;
        }

        // ATI 는 풀별 [남은 양, 가장 큰 덩어리, 보조 남은 양, 보조 가장 큰 덩어리] 를 준다. 총량은 알려 주지 않는다.
        GLint arrTextureFreeKb[4]{};
        glGetIntegerv( OpenGLRHIUtil::kTextureFreeMemoryAti, arrTextureFreeKb );
        if ( arrTextureFreeKb[0] <= 0 )
            return false;
        outBudget._availableBytes  = static_cast<uint64>( arrTextureFreeKb[0] ) * constant::memory::kBytesPerKilobyte;
        outBudget._bAvailableKnown = SW_TRUE;
        return true;
    }

    RHIBufferHandle OpenGLRHIDevice::createIndexBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _bInitialized == SW_FALSE || sizeBytes == 0 )
            return 0;

        GLuint ibo{ 0 };
        glGenBuffers( 1, &ibo );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo );
        glBufferData( GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeBytes ), pData, GL_STATIC_DRAW );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

        return storeGlBuffer( ibo, sizeBytes );
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

    void OpenGLRHIDevice::attachTextureToFramebuffer( uint32 attachment, const OpenGLTextureRecord& record, uint32 slice )
    {
        if ( record._target == GL_TEXTURE_2D )
            glFramebufferTexture2D( GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, record._texture, 0 );
        else
            glFramebufferTextureLayer( GL_FRAMEBUFFER, attachment, record._texture, 0, static_cast<GLint>( slice ) ); // 큐브는 층 = 면
    }

    uint32 OpenGLRHIDevice::ensureCompositeFboMrt( const RHITextureHandle* pColor, const uint16* pColorSlice, uint32 colorCount, RHITextureHandle depth,
                                                   uint32 depthSlice )
    {
        CompositeFboKey key{};
        key._colorCount = colorCount > kMaxColorAttachments ? kMaxColorAttachments : colorCount;
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            key._arrColor[colorIndex]      = pColor[colorIndex];
            key._arrColorSlice[colorIndex] = pColorSlice != nullptr ? pColorSlice[colorIndex] : uint16{ 0 };
        }
        key._depth      = depth;
        key._depthSlice = static_cast<uint16>( depthSlice );

        auto existing = _mapCompositeFbo.find( key );
        if ( existing != _mapCompositeFbo.end() )
            return existing->second;

        const OpenGLTextureRecord* arrColorRecord[kMaxColorAttachments]{};
        uint16                     arrColorSlice[kMaxColorAttachments]{};
        uint32                     attachedColors{ 0 };
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            if ( key._arrColor[colorIndex] == 0 )
                continue;
            const OpenGLTextureRecord* pRecord                 = resolveTexture( key._arrColor[colorIndex] );
            const bool                 bInvalidColorAttachment = pRecord == nullptr || pRecord->_bDepthStencil != SW_FALSE || key._arrColorSlice[colorIndex] >= pRecord->_arraySize;
            if ( bInvalidColorAttachment )
                return 0;
            arrColorSlice[attachedColors]    = key._arrColorSlice[colorIndex];
            arrColorRecord[attachedColors++] = pRecord;
        }

        const OpenGLTextureRecord* pDepthRecord{ nullptr };
        if ( depth != 0 )
        {
            pDepthRecord                       = resolveTexture( depth );
            const bool bInvalidDepthAttachment = pDepthRecord == nullptr || pDepthRecord->_bDepthStencil == SW_FALSE || depthSlice >= pDepthRecord->_arraySize;
            if ( bInvalidDepthAttachment )
                return 0;
        }
        if ( attachedColors == 0 && pDepthRecord == nullptr )
            return 0;

        GLuint fbo{ 0 };
        glGenFramebuffers( 1, &fbo );
        glBindFramebuffer( GL_FRAMEBUFFER, fbo );
        GLenum arrDrawBuffer[kMaxColorAttachments]{};
        for ( uint32 colorIndex = 0; colorIndex < attachedColors; ++colorIndex )
        {
            attachTextureToFramebuffer( GL_COLOR_ATTACHMENT0 + colorIndex, *arrColorRecord[colorIndex], arrColorSlice[colorIndex] );
            arrDrawBuffer[colorIndex] = GL_COLOR_ATTACHMENT0 + colorIndex;
        }
        if ( attachedColors == 0 )
        {
            glDrawBuffer( GL_NONE );
            glReadBuffer( GL_NONE );
        }
        else
            glDrawBuffers( static_cast<GLsizei>( attachedColors ), arrDrawBuffer );

        if ( pDepthRecord != nullptr )
        {
            const GLenum depthAttachment = ( pDepthRecord->_format == RHIFormat::D24_UNORM_S8_UINT ) ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
            attachTextureToFramebuffer( depthAttachment, *pDepthRecord, depthSlice );
        }

        const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        if ( status != GL_FRAMEBUFFER_COMPLETE )
        {
            // status==0 은 "불완전" 이 아니라 **glCheckFramebufferStatus 자체가 실패했다**는 뜻이다.
            // 실질적으로 현재 GL 컨텍스트가 없다는 신호다(GL 컨텍스트는 스레드 전용이고, 백엔드
            // 핫스왑은 Windows 에서 창과 컨텍스트를 통째로 재생성한다). 둘을 같은 문구로 찍으면
            // 첨부 포맷 문제인 줄 알고 엉뚱한 데를 파게 된다.
            if ( status == 0 )
                SW_LOG_WARNING_ID( "OpenGL.CompositeFBOCheckFailed", "Composite FBO check failed - there is no current GL context "
                                                                     "(check thread binding, or window/context recreation during a backend hot swap)." );
            else
                SW_LOG_WARNING_ID( "OpenGL.CompositeFBOIncompleteStatus", "Composite FBO incomplete (status=%#).", static_cast<uint32>( status ) );
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

    RHIBufferHandle OpenGLRHIDevice::storeGlBuffer( uint32 glName, uint32 sizeBytes )
    {
        if ( glName == 0 )
            return 0;
        const RHIBufferHandle handle = _gpuBuffers.insert( glName );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeBuffer( handle ), RHIMemoryKind::Buffer, sizeBytes );
        return handle;
    }

    OpenGLRHIDevice::OpenGLTextureRecord* OpenGLRHIDevice::resolveTexture( RHITextureHandle handle )
    {
        return _gpuTextures.get( handle );
    }

    const OpenGLRHIDevice::OpenGLTextureRecord* OpenGLRHIDevice::resolveTexture( RHITextureHandle handle ) const
    {
        return _gpuTextures.get( handle );
    }

    bool OpenGLRHIDevice::isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( space == RHIBindlessIndexSpace::Texture )
            return _textureRegistry.isLive( index );
        return _bufferRegistry.isLive( index );
    }

    OpenGLRHIDevice::BindlessResourceRecord OpenGLRHIDevice::findBindlessBuffer( RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        return _bufferRegistry.get( index );
    }

    OpenGLRHIDevice::BindlessResourceRecord OpenGLRHIDevice::findBindlessUAV( RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        return _uavRegistry.get( index );
    }

    RHITextureHandle OpenGLRHIDevice::findBindlessTexture( RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        return _textureRegistry.get( index )._texture;
    }

    namespace
    {
        /**
         * @brief KHR_debug 메시지를 엔진 로그로 보냅니다. DX11/DX12 디버그 레이어(flushDebugMessages) · Vulkan 검증 레이어와 같은 자리입니다.
         * @details GL 오류가 로그에 나오는 경로는 이것뿐입니다(엔진은 glGetError 를 부르지 않습니다). 이 콜백이 있어야 드로우가 정상으로
         *          나가고 화면만 비는 결함이 스모크의 `[Error]` 수로 드러납니다.
         *          알림(NOTIFICATION)은 드라이버 잡담("Buffer detailed info")이라 glDebugMessageControl 로 아예 끕니다.
         */
        void APIENTRY onOpenGLDebugMessage( GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* pMessage,
                                            const void* pUserParam )
        {
            (void)source;
            (void)length;
            (void)pUserParam;
            if ( pMessage == nullptr || severity == GL_DEBUG_SEVERITY_NOTIFICATION )
                return;
            // 성능 안내(상태에 따른 셰이더 재컴파일 · 픽셀 전송 동기화 — NVIDIA 가 낸다)는 결함이 아니라 정보다. 언리얼도 GL 성능 메시지는 상세 로그다 — 따로 id 로 남긴다.
            if ( type == GL_DEBUG_TYPE_PERFORMANCE && severity != GL_DEBUG_SEVERITY_HIGH )
            {
                SW_LOG_INFO_ID( "OpenGL.GLDebugPerformance", "GL performance note %#: %#", static_cast<uint32>( id ), pMessage );
                return;
            }
            if ( type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH )
                SW_LOG_ERROR_ID( "OpenGL.GLDebug", "GL debug %#: %#", static_cast<uint32>( id ), pMessage );
            else if ( severity == GL_DEBUG_SEVERITY_MEDIUM )
                SW_LOG_WARNING_ID( "OpenGL.GLDebug", "GL debug %#: %#", static_cast<uint32>( id ), pMessage );
            else
                SW_LOG_TRACE_ID( "OpenGL.GLDebug", "GL debug %#: %#", static_cast<uint32>( id ), pMessage );
        }
    } // namespace

    // 길이 유지(긴 함수 정리 기준): 컨텍스트 · 확장 확인 · 기본 객체 생성의 순서가 곧 의미라 나누지 않는다(나누면 순서를 여러 곳에서 읽는다).
    bool OpenGLRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        if ( _bInitialized == SW_TRUE )
            return true;

        _width  = desc._width;
        _height = desc._height;
        _swapChain.setSize( desc._width, desc._height );
        if ( desc._bSoftwareAdapter )
            SW_LOG_WARNING_ID( "OpenGL.GvRhiSoftwareAdapterIgnoredOpenGL", "gv_rhiSoftwareAdapter is ignored on OpenGL (the driver decides)" );
        // GL 에는 자원의 할당 크기를 물을 API 가 없다. 추적기는 서술(버퍼 크기 · 텍스처 서술)로 계산한 논리 크기다.
        getMemoryTracker().setSizeBasis( RHIMemorySizeBasis::Logical );

        // 플랫폼 컨텍스트 생성(WGL · GLX · EGL)은 GL/Platform 이 맡는다. 디바이스는 플랫폼 분기를 갖지 않는다.
        _platformContext = IOpenGLPlatformContext::create( desc._surfaceKind );
        if ( _platformContext == nullptr )
            return false;

        OpenGLContextHandles contextHandles{};
        if ( _platformContext->initialize( desc, contextHandles ) == false )
        {
            _platformContext.reset();
            return false;
        }
        _pHDC = contextHandles._pDeviceContext;
        _pHRC = contextHandles._pRenderContext;
        markInitPhase( "context" );

        if ( gladLoadGL() != 0 )
        {
            markInitPhase( "glad" );
            SW_LOG_INFO_ID( "OpenGL.OpenGLGladLoadedSuccessfully", "OpenGL glad Loaded Successfully." );

            // 쿠킹 변형을 고를 때 묻는 값이다(`supportsTextureFormat` — 아무 스레드에서나 불리므로 컨텍스트가 있는 지금 물어 둔다).
            if ( glad_glGetInternalformativ != nullptr )
            {
                const auto isFormatSupported = []( GLenum internalFormat )
                {
                    GLint supported{ GL_FALSE };
                    glGetInternalformativ( GL_TEXTURE_2D, internalFormat, GL_INTERNALFORMAT_SUPPORTED, 1, &supported );
                    return supported == GL_TRUE;
                };
                _bTextureASTC = isFormatSupported( OpenGLRHIDeviceFormat::kCompressedRGBAASTC4x4 ) ? SW_TRUE : SW_FALSE;
                _bTextureETC2 = isFormatSupported( OpenGLRHIDeviceFormat::kCompressedRGBA8ETC2EAC ) ? SW_TRUE : SW_FALSE;
            }

            // ZERO_TO_ONE = Direct3D 의 깊이 NDC [0,1]. 클립 **원점**은 대상마다 달라야 해서 여기서 고정하지 않는다.
            // 오프스크린 FBO 는 UPPER_LEFT(행 순서를 DX 와 맞춤), 기본 프레임버퍼는 LOWER_LEFT(창 표시가 아래에서 위)로
            // OpenGLRHICommandContext::beginRenderPass 가 매 패스 정한다. 여기서는 가능 여부만 확인하고 기본값을 깐다.
            // **이 호출은 선택이 아니다.** 빠지면 GL 만 프레임버퍼 원점이 좌하단이라 SceneColor 의 행 순서가 다른 세
            // 백엔드와 반대로 쌓인다. 풀스크린 블릿은 DX 규약(NDC 위쪽 = uv.y 0)을 백엔드 분기 없이 쓰므로 화면과
            // 스크린샷이 통째로 상하 반전된다. 깊이도 [-1,1] 로 남아 [0,1] 을 내보내는 투영이 버퍼의 절반만 쓴다.
            // 주의: 가능 여부를 `#ifdef GL_CLIP_CONTROL` 로 가리면 안 된다. 그런 GL 토큰은 없다(실제 토큰은
            // GL_CLIP_ORIGIN / GL_CLIP_DEPTH_MODE 이고 GL_CLIP_CONTROL 은 함수 이름일 뿐이다). 그러면 4.6 컨텍스트에서도
            // 블록이 통째로 컴파일에서 빠져 로그 없이 한 번도 불리지 않는다.
            // 그래서 함수 포인터로 판단하고, 실제로 걸렸는지 GL 에 되물어 확인한다.
            if ( glad_glClipControl != nullptr )
            {
                glClipControl( GL_UPPER_LEFT, GL_ZERO_TO_ONE );

                GLint clipOrigin{ 0 };
                GLint clipDepth{ 0 };
                glGetIntegerv( GL_CLIP_ORIGIN, &clipOrigin );
                glGetIntegerv( GL_CLIP_DEPTH_MODE, &clipDepth );
                if ( clipOrigin != GL_UPPER_LEFT || clipDepth != GL_ZERO_TO_ONE )
                {
                    SW_LOG_ERROR_ID( "OpenGL.GlClipControlNotAppliedOrigin", "glClipControl was not applied (origin %#, depth %#) - the screen is flipped vertically and depth precision is halved.",
                                     static_cast<uint32>( clipOrigin ), static_cast<uint32>( clipDepth ) );
                }
                else
                {
                    SW_LOG_INFO_ID( "OpenGL.OpenGLGlClipControlAvailableDepth", "OpenGL glClipControl available - depth fixed to [0,1]. The clip origin is chosen per target (offscreen UPPER_LEFT / default framebuffer LOWER_LEFT)." );
                }
            }
            else
            {
                SW_LOG_ERROR_ID( "OpenGL.GlClipControlUnavailableNeedsGL", "glClipControl is unavailable (needs GL 4.5 / ARB_clip_control) - the screen is flipped vertically and depth precision is halved." );
            }

            // **GL 오류를 로그로.** 드라이버는 디버그 컨텍스트(WGL_CONTEXT_DEBUG_BIT_ARB)에서만 메시지를 만들 의무가 있다.
            // 그 비트는 플랫폼 컨텍스트(WGL · GLX)가 진단을 켜는 빌드에서 켠다(`build::kShipping` 가 아닌 구성). SYNCHRONOUS 라 메시지가 원인 호출 안에서 나온다.
            const bool bDebugOutput = build::kShipping == false; // 배포본은 디버그 컨텍스트가 없어 메시지도 없다
            const bool bHasDebugAPI = glad_glDebugMessageCallback != nullptr && glad_glDebugMessageControl != nullptr;
            if ( bDebugOutput && bHasDebugAPI )
            {
                glEnable( GL_DEBUG_OUTPUT );
                glEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
                glDebugMessageCallback( &onOpenGLDebugMessage, nullptr );
                glDebugMessageControl( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE );
                SW_LOG_INFO_ID( "OpenGL.OpenGLKHRDebugOutput", "OpenGL KHR_debug output enabled - GL errors appear as error logs (counted in the smoke error count)." );
            }
            else if ( bDebugOutput )
                SW_LOG_WARNING_ID( "OpenGL.GlDebugMessageCallbackUnavailableNeedsGL", "glDebugMessageCallback is unavailable (needs GL 4.3 / KHR_debug) - GL errors will not appear in the log." );

            // **프로보킹 정점은 FIRST 다.** GL 기본은 LAST 인데 DirectX · Vulkan 은 FIRST 라, `nointerpolation`
            // 값이 삼각형의 **다른 정점**에서 온다. 지금 flat 으로 넘기는 값(materialIndex)은 배치 안에서 모두
            // 같아 증상이 없지만, 정점마다 다른 flat 값을 넘기는 날 GL 만 다른 그림을 낸다. 백엔드 하나만
            // 조용히 다른 종류의 버그는 여기서 미리 막는다.
            if ( glad_glProvokingVertex != nullptr )
                glProvokingVertex( GL_FIRST_VERTEX_CONVENTION );
            else
                SW_LOG_WARNING_ID( "OpenGL.GlProvokingVertexUnavailableNointerpolationValues", "glProvokingVertex is unavailable - nointerpolation values come from a different vertex than on DX/Vulkan." );

            // GPU 메모리 총량 · 남은 양은 벤더 확장에만 있다. 없으면 드라이버 값은 "모름" 이다(queryGPUMemoryBudgetInternal).
            _bNvxMemoryInfo = OpenGLRHIUtil::hasGlExtension( "GL_NVX_gpu_memory_info" ) ? SW_TRUE : SW_FALSE;
            _bAtiMemInfo    = OpenGLRHIUtil::hasGlExtension( "GL_ATI_meminfo" ) ? SW_TRUE : SW_FALSE;

            // **정점 스테이지의 SSBO 한도를 실제로 물어본다.** GL 4.3 스펙이 요구하는 최소값은 0 이다.
            // 정점 셰이더에서 구조버퍼를 읽는 것이 보장된 기능이 아니다. 이 엔진은 정점 셰이더에서
            // 인스턴스(t4) · 가시 목록(t6) · 모프 정점(t7)을 읽으므로, 한도가 그보다 작으면 링크는
            // 통과해도 읽기가 어긋난다. 화면으로는 "기하가 무너진다" 로만 보여서 원인을 짚기 어렵다.
            GLint maxVertexSsbo{ 0 };
            GLint maxComputeSsbo{ 0 };
            GLint maxFragmentSsbo{ 0 };
            glGetIntegerv( GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &maxVertexSsbo );
            glGetIntegerv( GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &maxComputeSsbo );
            glGetIntegerv( GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, &maxFragmentSsbo );
            SW_LOG_INFO_ID( "OpenGL.OpenGLSSBOLimitsVertex", "OpenGL SSBO limits: vertex %#, fragment %#, compute %#", static_cast<int32>( maxVertexSsbo ),
                            static_cast<int32>( maxFragmentSsbo ), static_cast<int32>( maxComputeSsbo ) );
            if ( maxVertexSsbo < static_cast<GLint>( shaderslot::kSRVSlotCount ) )
            {
                SW_LOG_WARNING_ID( "OpenGL.VertexStageSSBOLimit", "The vertex stage SSBO limit (%#) is smaller than the SRV slot count (%#) - reads of high-numbered slots may be wrong.",
                                   static_cast<int32>( maxVertexSsbo ), shaderslot::kSRVSlotCount );
            }
            // 머티리얼 텍스처는 텍스처 유닛 16..31 이다(binding_slots.hlsli 2). 프래그먼트 유닛이 모자라면 큰 번호의 머티리얼 텍스처가 조용히 흰색이 되므로 이 백엔드를 쓰지 않는다.
            GLint       maxFragmentTextureUnit{ 0 };
            const GLint requiredTextureUnit = static_cast<GLint>( shaderslot::kMaterialTexture0 + shaderslot::kMaterialTextureCount );
            glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &maxFragmentTextureUnit );
            GLint       maxCombinedTextureUnit{ 0 };
            const GLint requiredCombinedUnit = static_cast<GLint>( shaderslot::kEngineTexture0 + shaderslot::kEngineTextureCount );
            glGetIntegerv( GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTextureUnit );
            if ( maxCombinedTextureUnit < requiredCombinedUnit )
            {
                SW_LOG_WARNING_ID( "OpenGL.OpenGLBackendUnusableCombined", "The OpenGL backend is unusable - %# combined texture units, but engine texture slots need %#.",
                                   static_cast<int32>( maxCombinedTextureUnit ), static_cast<int32>( requiredCombinedUnit ) );
                _initResult = RHIInitResult::DriverUnsupported;
                return false;
            }
            if ( maxFragmentTextureUnit < requiredTextureUnit )
            {
                SW_LOG_WARNING_ID( "OpenGL.OpenGLBackendUnusableFragment", "The OpenGL backend is unusable - %# fragment texture units, but %# are needed for material texture slots.",
                                   static_cast<int32>( maxFragmentTextureUnit ), static_cast<int32>( requiredTextureUnit ) );
                _initResult = RHIInitResult::DriverUnsupported;
                return false;
            }
        }
        else
        {
            SW_LOG_WARNING_ID( "OpenGL.GladLoadGLReturnedOpenGLDevice", "gladLoadGL returned 0, OpenGL device initialization failed" );
            return false;
        }

        loadCore46Extensions();

        // 이 백엔드는 셰이더를 SPIR-V 로만 올린다(DXC → SPIR-V → glSpecializeShader). ARB_gl_spirv 가
        // 없으면 PSO 를 단 하나도 만들지 못해 "초기화는 성공했는데 화면이 텅 비는" 상태가 된다.
        // 그대로 두면 부르는 쪽이 이 백엔드를 멀쩡한 것으로 믿고 계속 쓴다. 여기서 실패로 끊어야
        // initialize 가 false 를 반환해 부르는 쪽(RHI · 테스트)이 이 백엔드를 쓸 수 없다는 것을 안다.
        if ( glad_glShaderBinary == nullptr || glad_glSpecializeShader == nullptr )
        {
            SW_LOG_WARNING_ID( "OpenGL.OpenGLBackendUnusableGL", "The OpenGL backend is unusable - GL_ARB_gl_spirv is missing (glShaderBinary/glSpecializeShader are null)." );
            _initResult = RHIInitResult::DriverUnsupported;
            return false;
        }

        if ( initializeStagingRing( desc._stagingRingBytes, desc._stagingSoloMaxBytes ) == false )
            return false;
        if ( initializeReadbackRing( desc._readbackRingBytes, desc._stagingSoloMaxBytes ) == false )
        {
            shutdownStagingRing(); // _bInitialized 전이라 shutdownInternal 이 링을 지우지 않는다
            return false;
        }

        _bInitialized = SW_TRUE;

        {
            glGenVertexArrays( 1, &_vao ); // 빈 VAO — 정점 입력이 없는 셰이더의 드로우

            glGenVertexArrays( 1, &_meshVao ); // 속성은 드로우마다 PSO 의 형식 표대로 켠다(bindVertexInputsForDraw)

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
            // 머티리얼 텍스처 유닛(t16..t31)은 머티리얼 샘플러(선형 · 랩, shaderslot::kMaterialTextureSampler)다. 다른 세 백엔드가 머티리얼을
            // 그 샘플러로 읽는다(DX12 · Vulkan 정적 샘플러, DX11 슬롯 결합 샘플러). 기본 샘플러로 두면 최근접 · 클램프라 확대한 텍스처가
            // 계단이 된다. 밉 필터는 텍스처의 밉 수가 정한다(glTexStorage2D 가 할당한 수 = 완전한 텍스처).
            static_assert( shaderslot::kMaterialTextureSampler == SW_SAMPLER_LINEAR_WRAP, "GL 머티리얼 샘플러의 필터 · 주소가 계약과 다르다" );
            GLuint materialSampler{ 0 };
            glGenSamplers( 1, &materialSampler );
            glSamplerParameteri( materialSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
            glSamplerParameteri( materialSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glSamplerParameteri( materialSampler, GL_TEXTURE_WRAP_S, GL_REPEAT );
            glSamplerParameteri( materialSampler, GL_TEXTURE_WRAP_T, GL_REPEAT );
            glSamplerParameteri( materialSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE );
            // 엔진 텍스처 유닛(칸 번호 — 풀스크린 입력 · G버퍼 · 깊이 · 그림자 맵 · 쿠키 · 노출)은 엔진 텍스처 샘플러(선형 · 클램프, shaderslot::kEngineTextureSampler)다.
            // 네이티브 bindless 의 Bindless::sampleIndex 가 고르는 샘플러와 같아야 블룸의 반 텍셀 탭이 같은 텍셀 넷을 섞는다. 렌더 타깃은 밉이 하나라
            // 축소 필터에 밉 모드를 두지 않는다.
            static_assert( shaderslot::kEngineTextureSampler == SW_SAMPLER_LINEAR_CLAMP, "GL 엔진 텍스처 샘플러의 필터 · 주소가 계약과 다르다" );
            GLuint engineSampler{ 0 };
            glGenSamplers( 1, &engineSampler );
            glSamplerParameteri( engineSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
            glSamplerParameteri( engineSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glSamplerParameteri( engineSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glSamplerParameteri( engineSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glSamplerParameteri( engineSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE );
            for ( uint32 unit = 0; unit < shaderslot::gl::kTextureUnitLimit; ++unit )
            {
                const bool bMaterialUnit = ( shaderslot::kMaterialTexture0 <= unit && unit < shaderslot::kMaterialTexture0 + shaderslot::kMaterialTextureCount );
                // 엔진 텍스처 칸의 GL 유닛은 레지스터 번호 그대로다(t32..).
                const bool bEngineUnit = ( shaderslot::kEngineTexture0 <= unit && unit < shaderslot::kEngineTexture0 + shaderslot::kEngineTextureCount );
                glBindTextureUnit( unit, defaultTex );
                if ( bMaterialUnit )
                    glBindSampler( unit, materialSampler );
                else if ( bEngineUnit )
                    glBindSampler( unit, engineSampler );
                else
                    glBindSampler( unit, defaultSampler );
            }
            _defaultSampler  = defaultSampler;
            _materialSampler = materialSampler;
            _engineSampler   = engineSampler;
        }

        _frameStreamContext = sw::make_unique<OpenGLRHICommandContext>( this );

        SW_LOG_INFO_ID( "OpenGL.OpenGLRHIBackendDevice", "OpenGL RHI Backend Device Active (DirectX Coordinate System & Top-Left UV Texture Space Configured)" );
        // GL 은 어댑터를 고르지 않는다(드라이버 · 실행 파일 내보내기가 정한다) — 어느 GPU 에서 도는지 남긴다(DX 의 AdapterSelected 자리, 제조사마다 그림이 다를 때 가른다).
        SW_LOG_INFO_ID( "OpenGL.AdapterSelected", "OpenGL renderer: %# (%#)", reinterpret_cast<const char*>( glGetString( GL_RENDERER ) ),
                        reinterpret_cast<const char*>( glGetString( GL_VENDOR ) ) );
        return true;
    }

    void OpenGLRHIDevice::loadCore46Extensions()
    {
        GLint extensionCount{ 0 };
        glGetIntegerv( GL_NUM_EXTENSIONS, &extensionCount );
        for ( GLint index = 0; index < extensionCount; ++index )
        {
            const string_view extension{ reinterpret_cast<const char*>( glGetStringi( GL_EXTENSIONS, static_cast<GLuint>( index ) ) ) };
            if ( extension == "GL_ARB_gl_spirv" && glad_glSpecializeShader == nullptr )
            {
                glad_glSpecializeShader = reinterpret_cast<PFNGLSPECIALIZESHADERPROC>( _platformContext->findProc( "glSpecializeShaderARB" ) );
            }
            else if ( extension == "GL_ARB_indirect_parameters" && glad_glMultiDrawArraysIndirectCount == nullptr )
            {
                glad_glMultiDrawArraysIndirectCount   = reinterpret_cast<PFNGLMULTIDRAWARRAYSINDIRECTCOUNTPROC>( _platformContext->findProc( "glMultiDrawArraysIndirectCountARB" ) );
                glad_glMultiDrawElementsIndirectCount = reinterpret_cast<PFNGLMULTIDRAWELEMENTSINDIRECTCOUNTPROC>( _platformContext->findProc( "glMultiDrawElementsIndirectCountARB" ) );
            }
        }
    }

    bool OpenGLRHIDevice::initializeStagingRing( uint64 capacityBytes, uint64 soloMaxBytes )
    {
        if ( capacityBytes == 0 )
        {
            SW_LOG_ERROR_ID( "OpenGL.OpenGLBackendNeedsStaging", "The OpenGL backend needs a staging ring capacity (gv_rhiStagingRingMegabytes must be positive)" );
            return false;
        }
        // 영속 매핑(glBufferStorage + GL_MAP_PERSISTENT_BIT)은 GL 4.4 코어다(glad 는 4.4 미만 컨텍스트에서 이 포인터를 채우지 않는다). 없으면 링을 두지 않고
        // 텍스처 업로드를 모두 Rejected 로 돌려준다 — 클라이언트 메모리에서 바로 올리는 두 번째 경로를 두지 않는다.
        if ( glad_glBufferStorage == nullptr )
        {
            SW_LOG_ERROR_ID( "OpenGL.OpenGLARBBufferStorage", "OpenGL 4.4 (ARB_buffer_storage) is unavailable - there is no staging ring and every texture upload is rejected" );
            return true;
        }

        // COHERENT 라 복사한 바이트는 뒤에 낸 GL 명령에 보인다(flush 가 필요 없다).
        uint8* pMapped = createPersistentRingBuffer( capacityBytes, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT, _stagingRingBuffer );
        if ( pMapped == nullptr )
        {
            SW_LOG_ERROR_ID( "OpenGL.OpenGLStagingRingBytes", "The OpenGL staging ring (%# bytes) could not be created or mapped (gv_rhiStagingRingMegabytes)", capacityBytes );
            return false;
        }

        _pStagingRingMapped = pMapped;
        _stagingRing.initialize( capacityBytes, kTransferRingAlignmentBytes, soloMaxBytes );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( &_stagingRing ), RHIMemoryKind::Staging, capacityBytes );
        return true;
    }

    bool OpenGLRHIDevice::initializeReadbackRing( uint64 capacityBytes, uint64 soloMaxBytes )
    {
        if ( capacityBytes == 0 )
        {
            SW_LOG_ERROR_ID( "OpenGL.OpenGLBackendNeedsReadback", "The OpenGL backend needs a readback ring capacity (gv_rhiReadbackRingMegabytes must be positive)" );
            return false;
        }
        // 업로드 링과 같은 GL 4.4 조건이다. 없으면 텍스처 되읽기를 모두 Rejected 로 돌려준다 — 컨텍스트를 쥔 채 동기로 읽는 두 번째 경로를 두지 않는다.
        if ( glad_glBufferStorage == nullptr )
        {
            SW_LOG_ERROR_ID( "OpenGL.OpenGLARBBufferStorage2", "OpenGL 4.4 (ARB_buffer_storage) is unavailable - there is no readback ring and every texture readback is rejected" );
            return true;
        }

        // GPU 가 쓰고 CPU 가 읽는 버퍼다. COHERENT 라 펜스가 신호한 뒤에는 GPU 가 쓴 바이트가 매핑에 보인다(glMemoryBarrier 가 필요 없다).
        uint8* pMapped = createPersistentRingBuffer( capacityBytes, GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT, _readbackRingBuffer );
        if ( pMapped == nullptr )
        {
            SW_LOG_ERROR_ID( "OpenGL.OpenGLReadbackRingBytes", "The OpenGL readback ring (%# bytes) could not be created or mapped (gv_rhiReadbackRingMegabytes)", capacityBytes );
            return false;
        }

        _pReadbackRingMapped = pMapped;
        _readbackRing.initialize( capacityBytes, kTransferRingAlignmentBytes, soloMaxBytes );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( &_readbackRing ), RHIMemoryKind::Staging, capacityBytes );
        return true;
    }

    uint8* OpenGLRHIDevice::createPersistentRingBuffer( uint64 capacityBytes, uint32 mapFlags, uint32& outBuffer )
    {
        outBuffer = 0;
        GLuint name{ 0 };
        glGenBuffers( 1, &name );
        if ( name == 0 )
            return nullptr;
        // GL_COPY_WRITE_BUFFER 로 만들어 다른 바인딩을 건드리지 않는다.
        glBindBuffer( GL_COPY_WRITE_BUFFER, name );
        glBufferStorage( GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>( capacityBytes ), nullptr, mapFlags );
        void* pMapped = glMapBufferRange( GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>( capacityBytes ), mapFlags );
        glBindBuffer( GL_COPY_WRITE_BUFFER, 0 );
        if ( pMapped == nullptr )
        {
            glDeleteBuffers( 1, &name );
            return nullptr;
        }
        outBuffer = name;
        return static_cast<uint8*>( pMapped );
    }

    void OpenGLRHIDevice::shutdownStagingRing()
    {
        if ( _stagingRingBuffer == 0 )
            return;
        // 매핑된 버퍼를 지우면 매핑도 풀린다. 전송은 waitIdle(종료 2단계)이 모두 끝냈다.
        deleteSoloBuffer( _stagingSolo );
        GLuint name = _stagingRingBuffer;
        glDeleteBuffers( 1, &name );
        getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( &_stagingRing ) );
        _stagingRing.shutdown();
        _stagingRingBuffer  = 0;
        _pStagingRingMapped = nullptr;
    }

    void OpenGLRHIDevice::shutdownReadbackRing()
    {
        if ( _readbackRingBuffer == 0 )
            return;
        deleteSoloBuffer( _readbackSolo );
        GLuint name = _readbackRingBuffer;
        glDeleteBuffers( 1, &name );
        getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( &_readbackRing ) );
        _readbackRing.shutdown();
        _readbackRingBuffer  = 0;
        _pReadbackRingMapped = nullptr;
    }

    void OpenGLRHIDevice::detachCommandRecordingInternal()
    {
        _frameStreamContext.reset();
    }

    // 길이 유지(긴 함수 정리 기준): 생성의 역순으로 놓는 해제 목록이라 나누지 않는다.
    void OpenGLRHIDevice::shutdownInternal()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // GL 자원을 지우기 전에 컨텍스트를 되찾는다 (플랫폼이 필요 없으면 아무것도 하지 않는다).
        if ( _platformContext != nullptr )
            _platformContext->reacquireForFrame();
        for ( const unique_ptr<OpenGLRHISwapChain>& pWindow : _windowSwapChains.getOwnedList() )
        {
            if ( _platformContext != nullptr )
                _platformContext->destroyWindowSurface( pWindow->getSurface() );
        }
        _windowSwapChains.clear();
        _pCurrentSwapChain = &_swapChain;

        // 프레임 펜스는 `waitIdleInternal`(종료 2단계)이 이미 놓았다. 다시 세우는 디바이스가 이어받지 않도록 번호만 처음으로 돌린다.
        _frameFenceRing.reset();

        if ( _defaultSampler )
        {
            for ( uint32 unit = 0; unit < shaderslot::gl::kTextureUnitLimit; ++unit )
            {
                glBindSampler( unit, 0 );
                glBindTextureUnit( unit, 0 );
            }
            glDeleteSamplers( 1, &_defaultSampler );
            _defaultSampler = 0;
        }
        if ( _materialSampler != 0 )
        {
            glDeleteSamplers( 1, &_materialSampler );
            _materialSampler = 0;
        }
        if ( _engineSampler != 0 )
        {
            glDeleteSamplers( 1, &_engineSampler );
            _engineSampler = 0;
        }
        if ( _defaultTexture )
        {
            glDeleteTextures( 1, &_defaultTexture );
            _defaultTexture = 0;
        }
        if ( _vao )
        {
            glDeleteVertexArrays( 1, &_vao );
            _vao = 0;
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
        _recordingState._geometry = RHIGeometryBindings{};
        {
            UniqueLock<shared_mutex> lock{ _bindlessMutex };
            _bufferRegistry.clear();
            _uavRegistry.clear();
            _textureRegistry.clear();
        }
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
        _renderPassRegistry.clear();

        _gpuBuffers.forEach( []( uint32& glBuf )
        {
            if ( glBuf != 0 )
            {
                glDeleteBuffers( 1, &glBuf );
                glBuf = 0;
            }
        } );
        _gpuBuffers.clear();

        _gpuTextures.forEach( []( OpenGLTextureRecord& record )
        {
            if ( record._texture != 0 )
            {
                glDeleteTextures( 1, &record._texture );
                record._texture = 0;
            }
            if ( record._fbo != 0 )
            {
                glDeleteFramebuffers( 1, &record._fbo );
                record._fbo = 0;
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
        if ( _meshVao != 0 )
        {
            glDeleteVertexArrays( 1, &_meshVao );
            _meshVao = 0;
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
        shutdownStagingRing();
        shutdownReadbackRing();

        if ( _platformContext != nullptr )
        {
            _platformContext->destroy();
            _platformContext.reset();
        }
        _pHDC = nullptr;
        _pHRC = nullptr;

        _bInitialized = SW_FALSE;
        SW_LOG_INFO_ID( "OpenGL.OpenGLRHIDeviceShutdown", "OpenGL RHI Device Shutdown cleanly." );
    }

    void OpenGLRHIDevice::resizeInternal( uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
        _swapChain.setSize( width, height );
        if ( _platformContext != nullptr )
            _platformContext->resize( width, height );
        glViewport( 0, 0, static_cast<GLsizei>( width ), static_cast<GLsizei>( height ) );
        SW_LOG_TRACE_ID( "OpenGL.OpenGLRHIResized", "OpenGL RHI Resized to %#×%#", width, height );
    }

    bool OpenGLRHIDevice::tryMakeCurrentAndRecordOwner()
    {
        if ( _platformContext->makeCurrent() == false )
            return false;
        _contextOwnerThread.store( std::this_thread::get_id(), std::memory_order_relaxed );
        _contextOwnedStartNanos.store( static_cast<uint64>( MonotonicClock::nowNanoseconds() ), std::memory_order_relaxed );
        return true;
    }

    bool OpenGLRHIDevice::bindGraphicsContext()
    {
        // 렌더 워커 · 자원 생성 가드(ScopedOpenGLContext)는 프레임 끝 · 생성 끝에 놓는다. 한 번만 시도하면 그 짧은 사이에 진 쪽이 [Error] 를 남기고
        // 그 프레임의 GL 호출을 모두 잃는다 — 기다리는 쪽과 같은 시한으로 기다린다.
        return acquireGraphicsContextBlocking( kContextAcquireTimeoutMs );
    }

    bool OpenGLRHIDevice::acquireGraphicsContextBlocking( uint32 timeoutMs )
    {
        if ( _bInitialized == SW_FALSE || _platformContext == nullptr )
            return false;

        // 경합이 없을 때의 정상 경로다. 대개 여기서 끝난다.
        if ( tryMakeCurrentAndRecordOwner() )
            return true;

        // 렌더 워커가 프레임 끝마다 놓는다. 조용히 다시 집는다. 시도마다 로그를 남기면 정상
        // 경합이 오류로 보인다(-gl -EnableEditor 의 ERROR_BUSY 가 그런 정상 경합이다).
        const Deadline deadline = Deadline::afterMilliseconds( timeoutMs );
        while ( deadline.isExpired() == false )
        {
            ThreadUtil::sleepForMilliseconds( 1 );
            if ( tryMakeCurrentAndRecordOwner() )
                return true;
        }

        // 누가 쥐었는지 적는다 — 렌더 스레드인지(드레인 등록 때 받은 id) 다른 스레드인지, 얼마나 오래인지.
        const std::thread::id owner      = _contextOwnerThread.load( std::memory_order_relaxed );
        const uint64          nowNanos   = static_cast<uint64>( MonotonicClock::nowNanoseconds() );
        const uint64          sinceNanos = _contextOwnedStartNanos.load( std::memory_order_relaxed );
        const uint64          heldMs     = nowNanos > sinceNanos ? ( nowNanos - sinceNanos ) / 1000000ull : 0ull;
        const bool            bRtOwner   = owner == _renderThreadID.load( std::memory_order_relaxed ) && owner != std::thread::id{};
        SW_LOG_ERROR_ID( "OpenGL.GLContextNotAcquired", "GL context not acquired in %# ms - held by thread %# (%#) for %# ms; GL calls on this thread are dropped", timeoutMs,
                         static_cast<uint64>( std::hash<std::thread::id>{}( owner ) ), bRtOwner ? "render thread" : "not the render thread", heldMs );
        return false;
    }

    bool OpenGLRHIDevice::isGraphicsContextCurrent() const
    {
        if ( _bInitialized == SW_FALSE || _platformContext == nullptr )
            return false;
        return _platformContext->isCurrent();
    }

    void OpenGLRHIDevice::unbindGraphicsContext()
    {
        if ( _bInitialized == SW_FALSE || _platformContext == nullptr )
            return;
        _contextOwnerThread.store( std::thread::id{}, std::memory_order_relaxed );
        _platformContext->clearCurrent();
    }

    uint32 OpenGLRHIDevice::getTimestampSlotCount() const
    {
        return ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _bTimestampReady.load( std::memory_order_acquire ) != SW_FALSE ) ? constant::render::kMaxGPUTimestampSlot : 0u;
    }

    bool OpenGLRHIDevice::readTimestamps( RHITimestampFrame& outFrame )
    {
        outFrame = _timestampFrame;
        return outFrame._listMicro.empty() == false;
    }

    bool OpenGLRHIDevice::readGPUClockNanos( int64& outGPUNanos )
    {
        outGPUNanos = 0;
        // GL_TIMESTAMP 는 기다리지 않고 지금 GPU 시계(나노초, 쿼리 타임스탬프와 같은 시계)를 준다. 컨텍스트를 소유한 스레드에서만 부른다.
        if ( _bInitialized == SW_FALSE || glGetInteger64v == nullptr )
            return false;
        GLint64 now{ 0 };
        glGetInteger64v( GL_TIMESTAMP, &now );
        if ( now <= 0 )
            return false;
        outGPUNanos = RHITimestampFrame::convertTickToNanos( static_cast<uint64>( now ), 1.0 );
        return true;
    }

    void OpenGLRHIDevice::writeTimestampSlot( uint32 slotIndex )
    {
        const bool bCannotWriteTimestamp = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _bTimestampReady.load( std::memory_order_acquire ) == SW_FALSE || slotIndex >= constant::render::kMaxGPUTimestampSlot;
        if ( bCannotWriteTimestamp )
            return;

        // glQueryCounter 는 Begin/End 쌍이 아니다. 한 번 부르면 "여기까지 GPU 가 끝낸 시각" 이 찍힌다.
        glQueryCounter( _arrTimestampQuery[_frameFenceRing.getRecordingSlot() * constant::render::kMaxGPUTimestampSlot + slotIndex], GL_TIMESTAMP );
        _timestampRing.markWritten( slotIndex );
    }

    void OpenGLRHIDevice::ensureTimestampQueries()
    {
        if ( _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _bTimestampReady.load( std::memory_order_relaxed ) != SW_FALSE )
            return;
        // GL 3.3 코어이지만 로더가 채우지 못했을 수 있다. 없으면 이 백엔드는 조용히 보고하지 않는다.
        const bool bQueryFunctionsMissing = glGenQueries == nullptr || glQueryCounter == nullptr || glGetQueryObjectui64v == nullptr ||
                                            glGetQueryObjectiv == nullptr;
        if ( bQueryFunctionsMissing )
            return;

        constexpr uint32 kQueryCount = constant::render::kMaxGPUTimestampSlot * constant::render::kMaxFrameCountInFlight;
        glGenQueries( static_cast<GLsizei>( kQueryCount ), _arrTimestampQuery );
        if ( _arrTimestampQuery[0] == 0 )
            return;
        _bTimestampReady.store( SW_TRUE, std::memory_order_release ); // 위에서 만든 쿼리를 다른 스레드가 본다
    }

    void OpenGLRHIDevice::collectTimestampsForSlot()
    {
        _timestampFrame._listMicro.clear();
        const uint32               ringSlot            = _frameFenceRing.getRecordingSlot();
        const GPUTimestampSlotMask writtenMask         = _timestampRing.getSlotMask( ringSlot );
        const bool                 bNoTimestampsToRead = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _bTimestampReady.load( std::memory_order_acquire ) == SW_FALSE || writtenMask.any() == false;
        if ( bNoTimestampsToRead )
            return;

        const uint32         base = ringSlot * constant::render::kMaxGPUTimestampSlot;
        uint64               arrTick[constant::render::kMaxGPUTimestampSlot]{};
        GPUTimestampSlotMask readyMask{};
        for ( uint32 slotIndex = 0; slotIndex < constant::render::kMaxGPUTimestampSlot; ++slotIndex )
        {
            if ( writtenMask.test( slotIndex ) == false )
                continue;

            // **가용 여부부터 묻는다.** 바로 결과를 읽으면 GL 이 그 자리에서 GPU 를 기다려,
            // 측정하려던 파이프라인을 멈춰 세운다. 그러면 측정값이 거짓이 된다.
            GLint bAvailable{ 0 };
            glGetQueryObjectiv( _arrTimestampQuery[base + slotIndex], GL_QUERY_RESULT_AVAILABLE, &bAvailable );
            if ( bAvailable == GL_FALSE )
                continue;

            GLuint64 tick{ 0 };
            glGetQueryObjectui64v( _arrTimestampQuery[base + slotIndex], GL_QUERY_RESULT, &tick );
            arrTick[slotIndex] = tick;
            readyMask.set( slotIndex );
        }
        // GL 타임스탬프는 나노초다.
        RHITimestampFrame::resolve( arrTick, readyMask, 1.0, _timestampFrame );
    }

    IRHISwapChain* OpenGLRHIDevice::createSwapChainInternal( const RHISwapChainDesc& desc )
    {
        void* const pSurface = _platformContext != nullptr ? _platformContext->createWindowSurface( desc ) : nullptr;
        if ( pSurface == nullptr )
            return nullptr;
        return _windowSwapChains.add( make_unique<OpenGLRHISwapChain>( pSurface, desc._width, desc._height ) );
    }

    void OpenGLRHIDevice::resizeSwapChainInternal( IRHISwapChain& swapChain, uint32 width, uint32 height )
    {
        static_cast<OpenGLRHISwapChain&>( swapChain ).setSize( width, height ); // 이 디바이스가 만든 것만 들어온다(`createSwapChain`)
    }

    void OpenGLRHIDevice::destroySwapChainInternal( IRHISwapChain& swapChain )
    {
        OpenGLRHISwapChain& window = static_cast<OpenGLRHISwapChain&>( swapChain );
        if ( _platformContext != nullptr )
            _platformContext->destroyWindowSurface( window.getSurface() );
        _windowSwapChains.remove( window );
    }

    OpenGLRHISwapChain* OpenGLRHIDevice::useSwapChain( IRHISwapChain* pSwapChain )
    {
        OpenGLRHISwapChain* const pTarget = pSwapChain != nullptr ? static_cast<OpenGLRHISwapChain*>( pSwapChain ) : &_swapChain;
        if ( pTarget != _pCurrentSwapChain )
        {
            if ( _platformContext == nullptr || _platformContext->makeCurrentOnWindowSurface( pTarget->getSurface() ) == false )
                return nullptr;
            _pCurrentSwapChain = pTarget;
        }
        if ( pTarget != &_swapChain )
            (void)_windowSwapChains.markUsedThisFrame( *pTarget );
        return pTarget;
    }

    void OpenGLRHIDevice::beginFrameInternal()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        if ( _platformContext != nullptr )
            _platformContext->reacquireForFrame();
        _pCurrentSwapChain = &_swapChain;

        // 백버퍼(FBO 0) 바인딩과 클리어는 여기서 하지 않는다. beginFrame 은 프레임 수명주기(GL 은
        // 컨텍스트 확보)만 맡고, 백버퍼를 바인딩하고 클리어하는 것은 RenderThread 가 여는 명시적
        // beginRenderPass(핸들 0) 뿐이다. 뷰포트만 창 크기 전체로 되돌려 둔다.
        glViewport( 0, 0, static_cast<GLsizei>( _width ), static_cast<GLsizei>( _height ) );

        // 이 슬롯은 곧 다시 쓴다. 덮어쓰기 전에 지난 바퀴의 결과를 한 번만 묻는다.
        ensureTimestampQueries();
        collectTimestampsForSlot();
        _timestampRing.beginFrame( _frameFenceRing.getRecordingSlot() );
    }

    void OpenGLRHIDevice::endFrameInternal( bool vsync, bool bPresent )
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // present 여부와 무관하게 이번 프레임의 슬롯은 다 기록됐다. 칸에 굳힌다(칸은 `retireFrameOnGPU` 의 제출 표시로 넘어간다).
        (void)_timestampRing.closeFrame( _frameFenceRing.getRecordingSlot() );

        // 이 프레임에 그린 다른 창을 먼저 내보내고 메인 창으로 돌아간다(아래 present 가 메인 창을 다시 건다).
        for ( OpenGLRHISwapChain* pWindow : _windowSwapChains.getFrameList() )
        {
            if ( bPresent && _platformContext != nullptr && _platformContext->makeCurrentOnWindowSurface( pWindow->getSurface() ) )
                _platformContext->presentWindowSurface( pWindow->getSurface() );
        }
        _windowSwapChains.clearFrame();
        if ( _pCurrentSwapChain != &_swapChain && _platformContext != nullptr )
            (void)_platformContext->makeCurrent(); // 실패는 다음 프레임의 reacquireForFrame 이 다시 본다
        _pCurrentSwapChain = &_swapChain;

        if ( bPresent == false )
        {
            retireFrameOnGPU();
            return;
        }

        const int8 desired = vsync ? 1 : 0;
        if ( _lastVsync != desired )
        {
            if ( _platformContext != nullptr )
                _platformContext->setSwapInterval( vsync );
            _lastVsync = desired;
        }

        // 스왑 직전 컨텍스트 되찾기는 플랫폼이 필요할 때만 한다(WGL 은 ImGui 멀티 뷰포트 때문에
        // 반드시 필요하고, GLX 는 하지 않는다).
        if ( _platformContext != nullptr )
            _platformContext->present();
        retireFrameOnGPU();
    }

    void OpenGLRHIDevice::waitIdleInternal()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        {
            ScopedOpenGLContext contextScope( this );
            glFinish();
            // GPU 가 쉰다 — 낸 프레임이 모두 끝났으니 펜스를 놓는다.
            for ( void*& rpSync : _arrFrameDoneSync )
            {
                if ( rpSync != nullptr )
                    glDeleteSync( static_cast<GLsync>( rpSync ) );
                rpSync = nullptr;
            }
            _frameFenceRing.markAllCompleted();
            // glFinish 가 낸 전송도 모두 끝냈다. 펜스를 놓고 워터마크를 올린다.
            _transferQueue.retireAll( []( void*& pSync )
            {
                glDeleteSync( static_cast<GLsync>( pSync ) ); // 펜스 없는 항목(glFenceSync 실패)은 0 이라 GL 이 무시한다
                pSync = nullptr;
            } );
        }
        _releaseQueue.flushAll();
    }

    bool OpenGLRHIDevice::debugRemoveDevice()
    {
        if ( _bInitialized == SW_FALSE )
            return false;
        SW_LOG_WARNING_ID( "OpenGL.MarkingDeviceLost", "Marking the GL device lost on request (rhi.removeDevice) - GL cannot lose its context on demand, so this is the state after the first failed fence: later transfer submits are rejected" );
        _transferSerials.markLost(); // 이미 잃은 장치여도 같은 상태다
        return true;
    }

    uint64 OpenGLRHIDevice::endTransfer()
    {
        // 펜스는 앞선 모든 명령의 끝을 알린다. glFlush 로 명령을 GPU 로 내보내야 프레임을 돌리지 않아도 신호가 선다.
        void* pSync = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
        glFlush();
        // 펜스가 없으면 이 전송이 끝났는지 알 길이 없다 — 끝난 것으로 치지 않고 장치 손실로 둔다(티켓은 Failed, 이후 제출은 Rejected).
        if ( pSync == nullptr && _transferSerials.markLost() )
            SW_LOG_ERROR_ID( "OpenGL.EndTransferGlFenceSyncReturnedNo", "endTransfer: glFenceSync returned no fence; treating the GL device as lost" );
        const uint64 serial = _transferQueue.enqueue( pSync );
        retireFinishedTransfers(); // 앞서 낸 전송 가운데 끝난 것을 모은다(이 스레드가 컨텍스트를 쥐고 있다)
        return serial;
    }

    void OpenGLRHIDevice::retireFinishedTransfers()
    {
        // 큐의 펜스는 모두 endTransfer 가 단 것이다. 펜스가 없는 항목(glFenceSync 실패)은 glClientWaitSync 가 GL_WAIT_FAILED 로 돌려 손실로 남는다.
        _transferQueue.retireDone(
            [this]( void*& pSync )
        {
            const GLenum waitResult = glClientWaitSync( static_cast<GLsync>( pSync ), 0, 0 );
            if ( waitResult == GL_WAIT_FAILED )
            {
                // 실패한 항목은 큐 맨 앞에 남아 매 프레임 다시 여기로 온다 — 처음 표시한 때만 로그를 남긴다.
                if ( _transferSerials.markLost() )
                    SW_LOG_ERROR_ID( "OpenGL.PollTransferGlClientWaitSyncFailedTransfer", "pollTransfer: glClientWaitSync failed on a transfer fence" );
                return false;
            }
            return waitResult != GL_TIMEOUT_EXPIRED;
        },
            []( void*& pSync )
        {
            glDeleteSync( static_cast<GLsync>( pSync ) ); // 끝났다고 본 펜스만 온다 — 펜스가 없는 항목은 위에서 끝나지 않는다
            pSync = nullptr;
        } );
        // 할당기가 단독 구간을 돌려받았으면 그 일회성 버퍼를 지운다. 버퍼는 컨텍스트가 있어야 지우므로 컨텍스트를 쥔 이곳(프레임 끝 · 제출 · 폴링)에서만 한다.
        const uint64 watermark = _transferSerials.getWatermark();
        deleteIdleSoloBuffer( _stagingRing, _stagingSolo, watermark );
        deleteIdleSoloBuffer( _readbackRing, _readbackSolo, watermark );
    }

    void OpenGLRHIDevice::deleteIdleSoloBuffer( RHIStagingAllocator& ring, OpenGLSoloTransferBuffer& rSolo, uint64 completedSerial )
    {
        if ( rSolo._buffer == 0 )
            return;
        ring.freeCompleted( completedSerial );
        if ( ring.hasLiveSolo() )
            return;
        deleteSoloBuffer( rSolo );
    }

    void OpenGLRHIDevice::deleteSoloBuffer( OpenGLSoloTransferBuffer& rSolo )
    {
        if ( rSolo._buffer == 0 )
            return;
        GLuint name = rSolo._buffer;
        glDeleteBuffers( 1, &name ); // 매핑된 버퍼를 지우면 매핑도 풀린다
        getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( &rSolo ) );
        rSolo = OpenGLSoloTransferBuffer{};
    }

    RHISubmitResult OpenGLRHIDevice::attachSoloBuffer( RHIStagingAllocator& ring, OpenGLSoloTransferBuffer& rSolo, const RHIStagingSpan& span, uint32 mapFlags,
                                                       const utf8* pCaller )
    {
        // 버퍼를 만드는 동안만 컨텍스트를 빌린다. 바이트 복사(업로드)는 돌아간 뒤 컨텍스트 밖에서 한다.
        ScopedOpenGLContext contextScope( this );
        if ( isGraphicsContextCurrent() == false )
        {
            ring.free( span );
            SW_LOG_ERROR_ID( "OpenGL.GLContextCouldNot", "%#: the GL context could not be acquired to create a %#-byte single transfer buffer; submit again later", pCaller, span._sizeBytes );
            return RHISubmitResult::Busy;
        }
        deleteSoloBuffer( rSolo ); // 할당기가 이미 돌려받았지만 아직 지우지 못한 앞 단독 버퍼
        rSolo._pMapped = createPersistentRingBuffer( span._sizeBytes, mapFlags, rSolo._buffer );
        if ( rSolo._pMapped == nullptr )
        {
            ring.free( span );
            SW_LOG_ERROR_ID( "OpenGL.ByteSingleTransferBuffer", "%#: a %#-byte single transfer buffer could not be created or mapped; the transfer is rejected", pCaller, span._sizeBytes );
            return RHISubmitResult::Rejected;
        }
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( &rSolo ), RHIMemoryKind::Staging, span._sizeBytes );
        return RHISubmitResult::Accepted;
    }

    uint8* OpenGLRHIDevice::getStagingBytes( const RHIStagingSpan& span ) const
    {
        return span._bSolo != SW_FALSE ? _stagingSolo._pMapped : _pStagingRingMapped + span._offsetBytes;
    }

    uint32 OpenGLRHIDevice::getStagingBuffer( const RHIStagingSpan& span ) const
    {
        return span._bSolo != SW_FALSE ? _stagingSolo._buffer : _stagingRingBuffer;
    }

    const uint8* OpenGLRHIDevice::getReadbackBytes( const RHIStagingSpan& span ) const
    {
        return span._bSolo != SW_FALSE ? _readbackSolo._pMapped : _pReadbackRingMapped + span._offsetBytes;
    }

    uint32 OpenGLRHIDevice::getReadbackBuffer( const RHIStagingSpan& span ) const
    {
        return span._bSolo != SW_FALSE ? _readbackSolo._buffer : _readbackRingBuffer;
    }

    bool OpenGLRHIDevice::tryRetireFinishedTransfers()
    {
        if ( isGraphicsContextCurrent() )
        {
            retireFinishedTransfers();
            return true;
        }
        const bool bBorrowed = requiresExclusiveContextThread() && tryMakeCurrentAndRecordOwner();
        if ( bBorrowed == false )
            return false;
        retireFinishedTransfers();
        unbindGraphicsContext();
        return true;
    }

    RHISubmitResult OpenGLRHIDevice::allocateStagingSpan( uint64 sizeBytes, const utf8* pCaller, RHIStagingSpan& outSpan )
    {
        if ( _stagingRingBuffer == 0 )
        {
            SW_LOG_ERROR_ID( "OpenGL.GLStagingRingUnavailable", "%#: the GL staging ring is unavailable (needs OpenGL 4.4 / ARB_buffer_storage); the upload is rejected", pCaller );
            return RHISubmitResult::Rejected;
        }
        RHIStagingAllocateResult result = _stagingRing.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        if ( result == RHIStagingAllocateResult::Full && tryRetireFinishedTransfers() )
            result = _stagingRing.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        // 결과 옮기기 · 못 받은 구간 놓기 · 이유 로그는 세 백엔드가 같이 쓰는 한 곳이다 — 장치를 잃었으면(펜스 확인이 GL_WAIT_FAILED) 링이 다시 비지 않으므로 Full 도 Rejected 다(계약 2).
        // Busy 는 앞선 전송이 끝나면 비므로 호출자가 다시 제출한다.
        const RHISubmitResult submitResult = RHIStagingRingUtil::settleAllocation( _stagingRing, _transferSerials, result, sizeBytes, "GL", pCaller, outSpan );
        if ( submitResult != RHISubmitResult::Accepted || outSpan._bSolo == SW_FALSE )
            return submitResult;
        return attachSoloBuffer( _stagingRing, _stagingSolo, outSpan, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT, pCaller );
    }

    RHISubmitResult OpenGLRHIDevice::allocateReadbackSpan( uint64 sizeBytes, RHIStagingSpan& outSpan )
    {
        if ( _readbackRingBuffer == 0 )
        {
            SW_LOG_ERROR_ID( "OpenGL.SubmitTextureReadbackGLReadbackRing", "submitTextureReadback: the GL readback ring is unavailable (needs OpenGL 4.4 / ARB_buffer_storage); the readback is rejected" );
            return RHISubmitResult::Rejected;
        }
        // 워터마크는 버린 되읽기(markSubmitted)의 구간만 돌려준다 — 그래서 차 있으면 펜스를 한 번 모아 본다. 가져갈 되읽기의 구간은 take 가 free 로 놓는다.
        RHIStagingAllocateResult result = _readbackRing.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        if ( result == RHIStagingAllocateResult::Full && tryRetireFinishedTransfers() )
            result = _readbackRing.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        // 결과 옮기기 · 못 받은 구간 놓기 · 이유 로그는 업로드 쪽과 같은 한 곳이다 — 잃은 장치의 Full 은 Rejected 다. Busy 는 앞선 되읽기를 가져가거나 버리면 비므로 호출자가 다시 제출한다(계약 2).
        const utf8* const     pCaller      = "submitTextureReadback";
        const RHISubmitResult submitResult = RHIStagingRingUtil::settleAllocation( _readbackRing, _transferSerials, result, sizeBytes, "GL", pCaller, outSpan );
        if ( submitResult != RHISubmitResult::Accepted || outSpan._bSolo == SW_FALSE )
            return submitResult;
        return attachSoloBuffer( _readbackRing, _readbackSolo, outSpan, GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT, pCaller );
    }

    void OpenGLRHIDevice::retireFrameOnGPU()
    {
        ScopedOpenGLContext contextScope( this );
        // 링이 한 바퀴 돌았다. 이 칸은 kMaxFrameCountInFlight 프레임 앞의 것이니, 그 프레임이 끝나야 칸을 다시 쓴다.
        (void)_frameFenceRing.waitForRecordingSlot( [this]( uint32 slot, uint64 busySerial )
        {
            constexpr GLuint64 kRingWaitTimeoutNanoseconds = 10ull * 1000ull * 1000ull * 1000ull;
            void*&             rpSlotSync                  = _arrFrameDoneSync[slot];
            const GLenum       waitResult                  = glClientWaitSync( static_cast<GLsync>( rpSlotSync ), GL_SYNC_FLUSH_COMMANDS_BIT, kRingWaitTimeoutNanoseconds );
            if ( waitResult == GL_TIMEOUT_EXPIRED || waitResult == GL_WAIT_FAILED )
                SW_LOG_ERROR_ID( "OpenGL.EndFrameGPUDidNot", "endFrame: the GPU did not finish frame %# (glClientWaitSync result %#)", busySerial, static_cast<uint32>( waitResult ) );
            glDeleteSync( static_cast<GLsync>( rpSlotSync ) );
            rpSlotSync = nullptr;
            return true; // 시한을 넘겨도 칸은 다시 쓴다(이전과 같다)
        } );
        _arrFrameDoneSync[_frameFenceRing.getRecordingSlot()] = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
        (void)_frameFenceRing.markSubmitted();

        // 이미 끝난 프레임을 오래된 순서로 모은다(기다리지 않고 묻기만 한다).
        const uint64 completedSerial = _frameFenceRing.retire( [this]( uint32 slot, uint64 serial )
        {
            (void)serial;
            void*&       rpSync     = _arrFrameDoneSync[slot];
            const GLenum pollResult = glClientWaitSync( static_cast<GLsync>( rpSync ), 0, 0 );
            if ( pollResult == GL_TIMEOUT_EXPIRED )
                return false;
            glDeleteSync( static_cast<GLsync>( rpSync ) );
            rpSync = nullptr;
            return true;
        } );
        // 프레임을 돌리는 스레드는 컨텍스트를 쥐고 있으니 여기서 전송 워터마크도 올린다(컨텍스트 없는 스레드의 폴링은 이 값을 본다).
        retireFinishedTransfers();
        _releaseQueue.tickCompleted( completedSerial );
    }

    void OpenGLRHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _frameFenceRing.getRecordingSerial() );
    }

    unique_ptr<IRHICommandList> OpenGLRHIDevice::createCommandList()
    {
        return make_unique<OpenGLRHICommandList>( this );
    }

    void OpenGLRHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        // 기록이 이미 GL 호출로 즉시 나갔으므로(beginCommandList 에서 컨텍스트 재바인딩까지 마침)
        // 순서를 맞출 일은 없다. 다만 GL 도 커맨드를 모아 뒀다가 보내므로, 즉시 모드에서는
        // 리스트 경계마다 밀어내 오류가 어느 리스트에서 났는지 드러나게 한다.
        (void)pCmdList;
        if ( _bImmediateSubmit )
            glFlush();
    }
} // namespace sw
