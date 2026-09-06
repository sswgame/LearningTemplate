#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::ensurePassResources()
    {
        if ( _pDevice == nullptr || _bPassResourcesReady != 0 )
            return;

        // 패스마다 자기 상수 버퍼를 갖도록 슬롯을 미리 만들어 둔다.
        _listPassCbSlot.clear();
        _listPassCbSlot.reserve( _s_kPassCbSlotCount );
        for ( uint32 slotIndex = 0; slotIndex < _s_kPassCbSlotCount; ++slotIndex )
        {
            PassCbSlot slot{};
            slot._buffer = _pDevice->getResource()->createConstantBuffer( _s_kEnginePassCbSize );
            if ( slot._buffer == 0 )
                break;
            slot._index = _pDevice->getResource()->registerBindlessResource( slot._buffer );
            _listPassCbSlot.push_back( slot );
        }
        _passCbCursor.store( 0, std::memory_order_relaxed );
        // 직렬 경로 시드는 0번 슬롯을 쓴다. (패스별 경로는 acquirePassCb 로 덮어쓴다)
        if ( _listPassCbSlot.empty() == false )
        {
            _frameCtx._passCb      = _listPassCbSlot[0]._buffer;
            _frameCtx._passCbIndex = _listPassCbSlot[0]._index;
        }

        struct GpuCullParams
        {
            float32 _planes[6][4]{};
            uint32  _instanceCount{ 0 };
            uint32  _batchCount{ 0 };
            uint32  _pad[2]{};
        };
        _gpuCullCb = _pDevice->getResource()->createConstantBuffer( sizeof( GpuCullParams ) );
        if ( _gpuCullCb != 0 )
            _gpuCullCbIndex = _pDevice->getResource()->registerBindlessResource( _gpuCullCb );

        constexpr RHIFormat arrGbufferFormat[] = { RHIFormat::R8G8B8A8_UNORM, RHIFormat::R16G16B16A16_FLOAT };
        const EngineData&   engineData         = engine::getEngineData();
        // Shader paths prefer pipeline XML pass recipes; EngineData paths are last-resort fallbacks only.

        auto registerPso = [this]( RenderPassType passType, string_view shaderPath, bool bDepthTest = true, uint32 numRt = 1,
                                   const RHIFormat* pRtFormats = nullptr, bool bBlend = false, bool bDepthWrite = true ) -> RHIPipelineStateHandle
        {
            const RHIPipelineStateHandle pso =
                createPsoForPassType( passType, shaderPath, bDepthTest, numRt, pRtFormats, bBlend, bDepthWrite );
            if ( pso != 0 )
                _mapEnginePso.insert_or_assign( passType, pso );
            return pso;
        };

        registerPso( RenderPassType::Shadow, engineData._shaderShadowDepth.c_str(), true, 0, nullptr, false, true );
        registerPso( RenderPassType::DepthPrepass, engineData._shaderShadowDepth.c_str(), true, 0, nullptr, false, true );
        registerPso( RenderPassType::ForwardOpaque, engineData._shaderForwardLit.c_str(), true );
        registerPso( RenderPassType::ForwardOpaqueNoDepthWrite, engineData._shaderForwardLit.c_str(), true, 1, nullptr, false, false );
        registerPso( RenderPassType::Transparent, engineData._shaderForwardLit.c_str(), true, 1, nullptr, true, false );
        registerPso( RenderPassType::GBuffer, engineData._shaderGBuffer.c_str(), true, 2, arrGbufferFormat );
        registerPso( RenderPassType::GBufferAlbedo, engineData._shaderGBufferAlbedo.c_str(), true );
        registerPso( RenderPassType::GBufferNormal, engineData._shaderGBufferNormal.c_str(), true );
        registerPso( RenderPassType::Lighting, engineData._shaderDeferredLighting.c_str(), false );
        registerPso( RenderPassType::Bloom, engineData._shaderPostBloom.c_str(), false );

        {
            RHIPipelineStateHandle psoOutline = createPsoForPassType( RenderPassType::Outline, engineData._shaderPostOutlineCommon.c_str(), false );
            if ( psoOutline == 0 )
                psoOutline = createEnginePso( engineData._shaderPostOutlineEngine.c_str(), false );
            if ( psoOutline != 0 )
                _mapEnginePso.insert_or_assign( RenderPassType::Outline, psoOutline );
        }

        registerPso( RenderPassType::Present, engineData._shaderFullscreenBlit.c_str(), false );

        const RHIPipelineStateHandle psoSsao = registerPso( RenderPassType::SSAO, engineData._shaderSsao.c_str(), false );
        if ( psoSsao != 0 )
            _mapEnginePso.insert_or_assign( RenderPassType::SSAO, psoSsao );

        registerPso( RenderPassType::TAA, engineData._shaderTaa.c_str(), false );
        registerPso( RenderPassType::Tonemap, engineData._shaderTonemap.c_str(), false );

        // Compute PSO: GpuCull — capabilities + 실제 PSO 생성 성공 시에만 GPU-driven.
        const RHICapabilities caps = _pDevice->getCapabilities();
        if ( caps._bGpuCulling != 0 )
        {
            const RHIPipelineStateHandle psoGpuCull =
                _pDevice->getResource()->createComputePipelineState( engineData._shaderGpuCull.c_str(), FrameRendererUtil::Entry::kCSMain );
            if ( psoGpuCull != 0 )
                _mapEnginePso.insert_or_assign( RenderPassType::GpuCull, psoGpuCull );
        }

        _bUseGpuDriven = ( caps._bIndirectDraw != 0 && caps._bGpuCulling != 0 && getEnginePso( RenderPassType::GpuCull ) != 0 ) ? 1 : 0;

        _bPassResourcesReady = 1;
        SW_LOG_INFO( "Pass PSOs/CB ready (shadow=%# forward=%# transparent=%# deferred=%# bloom=%# outline=%# gpuDriven=%#)",
                     getEnginePso( RenderPassType::Shadow ), getEnginePso( RenderPassType::ForwardOpaque ),
                     getEnginePso( RenderPassType::Transparent ), getEnginePso( RenderPassType::Lighting ),
                     getEnginePso( RenderPassType::Bloom ), getEnginePso( RenderPassType::Outline ), static_cast<uint32>( _bUseGpuDriven ) );
    }

    bool FrameRenderer::markAttachmentCleared( const hashed_string& key )
    {
        std::scoped_lock<mutex> lock{ _clearedMutex };
        if ( std::find( _listClearedThisFrame.begin(), _listClearedThisFrame.end(), key ) != _listClearedThisFrame.end() )
            return false;
        _listClearedThisFrame.push_back( key );
        return true;
    }

    void FrameRenderer::resetClearedAttachments()
    {
        std::scoped_lock<mutex> lock{ _clearedMutex };
        _listClearedThisFrame.clear();
    }

    void FrameRenderer::resetPassCbRing()
    {
        // 0번은 프레임 시드 전용이라 패스에는 1번부터 나눠 준다.
        _passCbCursor.store( 1, std::memory_order_relaxed );
        _bPassCbExhaustedLogged.store( 0 );
        if ( _listPassCbSlot.empty() )
            return;
        _frameCtx._passCb      = _listPassCbSlot[0]._buffer;
        _frameCtx._passCbIndex = _listPassCbSlot[0]._index;
    }

    void FrameRenderer::acquirePassCb( FramePassContext& ctx )
    {
        if ( _listPassCbSlot.empty() )
            return;

        uint32 ticket = _passCbCursor.fetch_add( 1, std::memory_order_relaxed );
        if ( ticket >= static_cast<uint32>( _listPassCbSlot.size() ) )
        {
            // 슬롯이 모자라면 마지막 슬롯을 공유한다. 예전엔 0번으로 되돌렸는데 0번은 프레임 시드
            // 전용이라(resetPassCbRing 참고) 그 패스가 시드 값을 덮어써 다른 패스까지 망가뜨렸다.
            // 경고는 프레임당 한 번만 — 패스마다 찍으면 로그가 잠긴다.
            if ( _bPassCbExhaustedLogged.exchange( 1 ) == 0 )
            {
                SW_LOG_WARNING( "acquirePassCb: pass constant slots exhausted (%#), passes will share the last slot",
                                static_cast<uint32>( _listPassCbSlot.size() ) );
            }
            ticket = static_cast<uint32>( _listPassCbSlot.size() ) - 1;
        }

        // 슬롯 배열은 프레임 시작에 잡아 두고 병렬 구간에서 크기가 변하지 않는다. 분배도 위의
        // atomic 커서가 하므로 락이 필요 없다 — 다만 **const 로 읽어야** 한다. 비-const 접근은
        // "쓰기" 로 취급되어, 서로 다른 슬롯을 읽기만 하는 패스 둘도 레이스로 잡힌다.
        const vector<FrameRenderer::PassCbSlot>& listSlot = _listPassCbSlot;
        ctx._passCb                                       = listSlot[ticket]._buffer;
        ctx._passCbIndex                                  = listSlot[ticket]._index;
        // 값은 드로우 직전 ShaderBindingBinder::bindGraphics 가 리플렉션 오프셋으로 채운다
        // (ctx._passValues 에 이미 프레임 시드가 들어있으므로 별도 선-업로드가 필요 없다).
    }

    void FrameRenderer::releasePassResources()
    {
        if ( _pDevice == nullptr )
        {
            _listPassCbSlot.clear();
            _passCbCursor.store( 0, std::memory_order_relaxed );
            _frameCtx._passCb      = 0;
            _frameCtx._passCbIndex = kInvalidDescriptorIndex;
            _gpuCullCb             = 0;
            _gpuCullCbIndex        = kInvalidDescriptorIndex;
            _taaHistory            = 0;
            _taaHistorySrv         = kInvalidDescriptorIndex;
            _mapEnginePso.clear();
            _mapMaterialPassPso.clear();
            _bPassResourcesReady = 0;
            return;
        }

        for ( auto& [name, pso] : _mapEnginePso )
        {
            if ( pso != 0 )
            {
                _pDevice->getResource()->destroyPipelineState( pso );
                pso = 0;
            }
        }
        _mapEnginePso.clear();

        for ( auto& [pass, pso] : _mapMaterialPassPso )
        {
            if ( pso != 0 )
            {
                _pDevice->getResource()->destroyPipelineState( pso );
                pso = 0;
            }
        }
        _mapMaterialPassPso.clear();
        _gpuScene.releaseGpu( _pDevice );

        auto releaseResource = [this]( RHIBufferHandle& handle, RHIDescriptorIndex& srvIndex, bool bIsTexture = false )
        {
            if ( srvIndex != kInvalidDescriptorIndex )
            {
                _pDevice->getResource()->unregisterBindlessResource( srvIndex );
                srvIndex = kInvalidDescriptorIndex;
            }
            if ( handle != 0 )
            {
                if ( bIsTexture )
                    _pDevice->getResource()->destroyTexture( handle );
                else
                    _pDevice->getResource()->destroyBuffer( handle );
                handle = 0;
            }
        };

        for ( PassCbSlot& slot : _listPassCbSlot )
            releaseResource( slot._buffer, slot._index );
        _listPassCbSlot.clear();
        _passCbCursor.store( 0, std::memory_order_relaxed );
        _frameCtx._passCb      = 0;
        _frameCtx._passCbIndex = kInvalidDescriptorIndex;
        releaseResource( _gpuCullCb, _gpuCullCbIndex );
        releaseResource( _taaHistory, _taaHistorySrv, true );
        _bPassResourcesReady = 0;
    }

    void FrameRenderer::ensureTransientResources( uint32 overrideWidth, uint32 overrideHeight )
    {
        if ( _pDevice == nullptr )
            return;

        uint32 width  = FrameRendererUtil::kDefaultTransientSize;
        uint32 height = FrameRendererUtil::kDefaultTransientSize;
        if ( overrideWidth > 0 && overrideHeight > 0 )
        {
            width  = overrideWidth;
            height = overrideHeight;
        }
        else
        {
            IWindow* pWindow = IWindow::getActiveWindow();
            if ( pWindow != nullptr )
            {
                if ( pWindow->getWidth() > 0 )
                    width = pWindow->getWidth();
                if ( pWindow->getHeight() > 0 )
                    height = pWindow->getHeight();
            }
        }

        if ( width == _transientWidth && height == _transientHeight && _mapTransient.empty() == false )
            return;

        releaseTransientResources();
        _transientWidth  = width;
        _transientHeight = height;

        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            const RHIFormat format = parseAttachmentFormat( att._format );
            allocTransient( att._name, format, FrameRendererUtil::isDepthFormat( format ), att._clearColor );
        }

        auto ensureNamed = [&]( string_view name )
        {
            const string key( name );
            if ( _mapTransient.find( key ) != _mapTransient.end() || name == FrameRendererUtil::Attachment::kSwapchain )
                return;

            float4     clearColor{};
            const bool bHasClear = tryGetAttachmentClearColor( name, clearColor );

            if ( name == FrameRendererUtil::Attachment::kShadowMap || name == FrameRendererUtil::Attachment::kSceneDepth )
                allocTransient( name, RHIFormat::D24_UNORM_S8_UINT, true, bHasClear ? clearColor : FrameRendererUtil::kDepthClear );
            else if ( name == FrameRendererUtil::Attachment::kGBufferNormal || name == FrameRendererUtil::Attachment::kLitColor || name == FrameRendererUtil::Attachment::kBloomColor || name == FrameRendererUtil::Attachment::kBloomBright )
                allocTransient( name, RHIFormat::R16G16B16A16_FLOAT, false, bHasClear ? clearColor : FrameRendererUtil::kBloomClear );
            else if ( name == FrameRendererUtil::Attachment::kSceneColor )
                allocTransient( name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : FrameRendererUtil::kSceneClear );
            else
                allocTransient( name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : FrameRendererUtil::kBlackClear );
        };

        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            for ( const string& in : pass._listInput )
            {
                ensureNamed( in );
            }
            for ( const string& out : pass._listOutput )
            {
                ensureNamed( out );
            }
        }
    }

    void FrameRenderer::releaseTransientResources()
    {
        if ( _pDevice == nullptr )
        {
            _mapTransient.clear();
            _mapTransientSrv.clear();
            _transientWidth  = 0;
            _transientHeight = 0;
            return;
        }
        for ( auto& [name, srv] : _mapTransientSrv )
        {
            if ( srv != kInvalidDescriptorIndex )
                _pDevice->getResource()->unregisterBindlessResource( srv );
        }
        for ( auto& [name, tex] : _mapTransient )
        {
            if ( tex != 0 )
                _pDevice->getResource()->destroyTexture( tex );
        }
        _mapTransient.clear();
        _mapTransientSrv.clear();
        _transientWidth  = 0;
        _transientHeight = 0;
    }

    void FrameRenderer::allocTransient( string_view name, RHIFormat format, bool bDepth, const float4& clearColor )
    {
        if ( _mapTransient.find( string( name ) ) != _mapTransient.end() || _pDevice == nullptr )
            return;

        RHITextureDesc desc{};
        desc._width                   = _transientWidth;
        desc._height                  = _transientHeight;
        desc._format                  = format;
        desc._bIsRenderTarget         = bDepth ? 0 : 1;
        desc._bIsDepthStencil         = bDepth ? 1 : 0;
        desc._bIsShaderResource       = 1;
        desc._clearDepth              = clearColor._x;
        desc._clearColor              = clearColor;
        const RHITextureHandle handle = _pDevice->getResource()->createTexture2D( desc );
        if ( handle == 0 )
        {
            SW_LOG_WARNING( "Failed to allocate transient '%#'", name );
            return;
        }
        _mapTransient.emplace( name, handle );
        const RHIDescriptorIndex srv = _pDevice->getResource()->registerBindlessTexture( handle );
        if ( srv != kInvalidDescriptorIndex )
            _mapTransientSrv.emplace( name, srv );
    }

    bool FrameRenderer::tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const
    {
        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            if ( att._name == attachmentName )
            {
                outClearColor = att._clearColor;
                return att._bClear;
            }
        }
        return false;
    }

    float4 FrameRenderer::getAttachmentClearColorOrDefault( string_view attachmentName, const float4& fallback ) const
    {
        float4 clearColor = fallback;
        tryGetAttachmentClearColor( attachmentName, clearColor );
        return clearColor;
    }

    RHITextureHandle FrameRenderer::findTransient( string_view name ) const
    {
        const auto it = _mapTransient.find( string( name ) );
        return it != _mapTransient.end() ? it->second : 0;
    }

    RHIDescriptorIndex FrameRenderer::findTransientSrv( string_view name ) const
    {
        const auto it = _mapTransientSrv.find( string( name ) );
        return it != _mapTransientSrv.end() ? it->second : kInvalidDescriptorIndex;
    }

    RHIFormat FrameRenderer::parseAttachmentFormat( string_view formatName ) const
    {
        const string formatNt( formatName );
        const string f = StringUtil::toUpper( formatNt.c_str() );
        if ( f == "D24_UNORM_S8_UINT" || f == "D24S8" )
            return RHIFormat::D24_UNORM_S8_UINT;
        if ( f == "R16G16B16A16_FLOAT" )
            return RHIFormat::R16G16B16A16_FLOAT;
        if ( f == "B8G8R8A8_UNORM" )
            return RHIFormat::B8G8R8A8_UNORM;
        return RHIFormat::R8G8B8A8_UNORM;
    }

    string FrameRenderer::resolvePresentSource() const
    {
        const utf8* pName = FrameRendererUtil::pickFirstExisting(
            _mapTransient,
            { "TonemapColor", "OutlineColor", "BloomColor", "TaaColor",
              "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
        return pName != nullptr ? string( pName ) : string{};
    }

    RHIPipelineStateHandle FrameRenderer::getEnginePso( RenderPassType passType ) const
    {
        const auto it = _mapEnginePso.find( passType );
        return ( it != _mapEnginePso.end() ) ? it->second : 0;
    }
} // namespace sw
