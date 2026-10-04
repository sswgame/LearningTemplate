#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandList.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.h"
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

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
        , _materialSampler{ 0 }
        , _engineSampler{ 0 }
        , _defaultTexture{ 0 }
        , _gpuBuffers{}
        , _listRegisteredBindless{}
        , _listBindlessFree{}
        , _listRegisteredUAV{}
        , _listUavFree{}
        , _gpuTextures{}
        , _mapCompositeFbo{}
        , _listRegisteredTexture{}
        , _listTextureFree{}
        , _arrTimestampQuery{}
        , _arrTimestampMask{}
        , _timestampFrameIndex{ 0 }
        , _timestampFrame{}
        , _arrComputeRootConstantShadow{}
        , _pipelineStates{}
        , _listRenderPass{}
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
        , _computeRootConstantUbo{ 0 }
        , _lastVsync{ -1 }
        , _bTimestampEnabled{ SW_FALSE }
        , _bTimestampReady{ SW_FALSE }
        , _bInitialized{ SW_FALSE }
        , _bNvxMemoryInfo{ SW_FALSE }
        , _bAtiMemInfo{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
        _resourceImpl = sw::make_unique<OpenGLRHIResourceFactory>( this );
    }

    OpenGLRHIDevice::~OpenGLRHIDevice()
    {
        shutdown();
    }

    IRHIResourceFactory* OpenGLRHIDevice::getResourceFactory() { return _resourceImpl.get(); }
    IRHICommandContext*  OpenGLRHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

    bool OpenGLRHIDevice::queryGpuMemoryBudgetInternal( RHIGpuMemoryBudget& outBudget )
    {
        if ( _bInitialized == SW_FALSE || ( _bNvxMemoryInfo == SW_FALSE && _bAtiMemInfo == SW_FALSE ) )
            return false;

        // 확장 값은 KB 단위이고, 다른 프로세스 몫까지 든 디바이스 전체 값이다. 그래서 장부를 빼 "엔진 밖" 을 내지 않는다(Scope Device).
        ScopedOpenGLContext ctxScope( this );
        constexpr uint64    kKilobyte = 1024;
        outBudget._scope              = RHIGpuMemoryScope::Device;
        if ( _bNvxMemoryInfo == SW_TRUE )
        {
            GLint totalKb{ 0 };
            GLint currentKb{ 0 };
            glGetIntegerv( OpenGLRHIDeviceInternal::kGpuMemoryInfoTotalAvailableNvx, &totalKb );
            glGetIntegerv( OpenGLRHIDeviceInternal::kGpuMemoryInfoCurrentAvailableNvx, &currentKb );
            if ( totalKb <= 0 )
                return false;
            const uint64 totalBytes    = static_cast<uint64>( totalKb ) * kKilobyte;
            const uint64 currentBytes  = currentKb > 0 ? static_cast<uint64>( currentKb ) * kKilobyte : 0;
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
        glGetIntegerv( OpenGLRHIDeviceInternal::kTextureFreeMemoryAti, arrTextureFreeKb );
        if ( arrTextureFreeKb[0] <= 0 )
            return false;
        outBudget._availableBytes  = static_cast<uint64>( arrTextureFreeKb[0] ) * kKilobyte;
        outBudget._bAvailableKnown = SW_TRUE;
        return true;
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
            const OpenGLTextureRecord* pRecord = resolveTexture( key._arrColor[colorIndex] );
            if ( pRecord == nullptr || pRecord->_bDepthStencil != SW_FALSE || key._arrColorSlice[colorIndex] >= pRecord->_arraySize )
                return 0;
            arrColorSlice[attachedColors]    = key._arrColorSlice[colorIndex];
            arrColorRecord[attachedColors++] = pRecord;
        }

        const OpenGLTextureRecord* pDepthRecord{ nullptr };
        if ( depth != 0 )
        {
            pDepthRecord = resolveTexture( depth );
            if ( pDepthRecord == nullptr || pDepthRecord->_bDepthStencil == SW_FALSE || depthSlice >= pDepthRecord->_arraySize )
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
                SW_LOG_WARNING( "Composite FBO 확인 실패 — 현재 GL 컨텍스트가 없습니다 "
                                "(스레드 바인딩 또는 백엔드 핫스왑 중 창/컨텍스트 재생성 확인)." );
            else
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

    RHIBufferHandle OpenGLRHIDevice::storeGlBuffer( uint32 glName, uint32 sizeBytes )
    {
        if ( glName == 0 )
            return 0;
        const RHIBufferHandle handle = _gpuBuffers.insert( glName );
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeBuffer( handle ), RHIMemoryKind::Buffer, sizeBytes );
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
} // namespace sw
