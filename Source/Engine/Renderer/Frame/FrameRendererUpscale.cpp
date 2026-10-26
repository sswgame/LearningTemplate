/**
 * @file FrameRendererUpscale.cpp
 * @brief Present 의 FSR 1 업스케일입니다 — 렌더 배율(`gv_renderScale`)로 작게 그린 뷰를 선형 블릿 대신 EASU + RCAS 로 출력 크기로 키웁니다(fsr1.hlsl).
 * @details 흐름: 셋업(`prepareUpscale`, 뷰마다 프레임에 한 번)이 키울지 정하고 뷰포트 크기 중간 RT 를 맞춘다. Present 가 렌더 패스를 열기 전에 EASU(풀 크기 원본 →
 *          중간 RT)를 기록하고(`recordUpscaleEasu`), 렌더 패스 안에서 블릿 대신 RCAS(중간 RT → 대상 뷰포트)를 그린다(`drawUpscaleRcas`).
 *          EASU 는 톤맵한 지각 공간 색을 받는다(AMD 권고 — 톤맵 뒤 · UI 앞). 그래서 톤맵 패스가 따로 있고 Present 가 그 결과를 키우는 파이프라인(디퍼드)에서만 쓴다.
 *          언리얼의 Spatial Upscale(r.Upscale.Quality)과 같은 자리다 — 시간 누적 업스케일(TSR · FSR 2)은 아니다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    /**
     * @brief `-gv_upscalerSharpness=<단계>`: FSR 1 RCAS 의 선명도를 줄이는 단계입니다(0 = 가장 선명, 1 단계마다 절반 — AMD FsrRcasCon 의 sharpness). AMD 예제 기본값은 0.2 다.
     */
    SW_GLOBAL_VARIABLE( float32, gv_upscalerSharpness, 0.2f, "FSR 1 선명화(RCAS) 를 줄이는 단계 — 0 가장 선명, 1 마다 절반 (graphics.upscaler 가 spatial 일 때)" );

    namespace
    {
        struct FrameRendererUpscaleInternal
        {
            /// `gv_upscaler` 의 FSR 1 선택지 순번입니다(engine.settings.xml 의 graphics.upscaler — off 0, spatial 1).
            static constexpr int32 kUpscalerSpatial = 1;

            /// fsr1.hlsl 의 루트 상수입니다 — `SwRootConstants` 와 같은 순서 · 크기다.
            struct RootConstants
            {
                uint32  _source{ 0 };
                float32 _inputWidth{ 0.0f };
                float32 _inputHeight{ 0.0f };
                float32 _outputWidth{ 0.0f };
                float32 _outputHeight{ 0.0f };
                float32 _viewportX{ 0.0f };
                float32 _viewportY{ 0.0f };
                float32 _sharpness{ 0.0f };
            };
            static_assert( sizeof( RootConstants ) == 8 * sizeof( uint32 ), "fsr1.hlsl 의 루트 상수와 크기가 같아야 한다" );
        };
    } // namespace

    void FrameRenderer::releaseUpscaleIntermediate( ViewTarget& view )
    {
        if ( _pDevice != nullptr && view._upscaleIntermediateSrv != kInvalidDescriptorIndex )
            _pDevice->getResourceFactory()->unregisterBindlessTexture( view._upscaleIntermediateSrv );
        if ( _pDevice != nullptr && view._upscaleIntermediate != 0 )
            _pDevice->getResourceFactory()->destroyTexture( view._upscaleIntermediate );
        view._upscaleIntermediate    = 0;
        view._upscaleIntermediateSrv = kInvalidDescriptorIndex;
        view._upscaleWidth           = 0;
        view._upscaleHeight          = 0;
        view._bUpscaleThisFrame      = SW_FALSE;
    }

    void FrameRenderer::prepareUpscale( ViewTarget& view, uint32 viewportWidth, uint32 viewportHeight )
    {
        using Internal                                  = FrameRendererUpscaleInternal;
        view._bUpscaleThisFrame                         = SW_FALSE;
        const TransientAttachmentPool& pool             = view._transientPool;
        const bool                     bSpatial         = static_cast<int32>( gv_upscaler ) == Internal::kUpscalerSpatial;
        const bool                     bSmaller         = pool.getWidth() < viewportWidth || pool.getHeight() < viewportHeight;
        const bool                     bCandidate       = _pDevice != nullptr && bSpatial && bSmaller && view._settings._upscaleZoom == 0 && pool.isEmpty() == false;
        const RenderGraphPassDesc*     pTonemap         = findTonemapPass();
        const bool                     bPostChain       = pTonemap != nullptr && pTonemap->_resolvedType == RenderPassType::Present;
        const bool                     bReportPostChain = bCandidate && bPostChain && _bUpscalePostChainLogged == SW_FALSE;
        if ( bReportPostChain )
        {
            // 이 파이프라인은 Present 가 톤맵까지 한다 — 키울 톤맵한 중간 그림이 없다(EASU 는 톤맵한 색을 받는다). 데이터(파이프라인)와 설정이 맞지 않는 것을 알린다.
            SW_LOG_ERROR( "Pipeline '%#' tonemaps in its Present pass - FSR 1 needs a tonemapped image to upscale, so this view is upscaled linearly",
                          _pipelineResource.getDesc()._name.c_str() );
            _bUpscalePostChainLogged = SW_TRUE;
        }
        const bool bUpscale = bCandidate && bPostChain == false && getEnginePSO( RenderPassType::UpscaleEasu ) != 0;
        if ( bUpscale == false )
        {
            if ( view._upscaleIntermediate != 0 )
                releaseUpscaleIntermediate( view );
            return;
        }
        const bool bSameSize = view._upscaleIntermediate != 0 && view._upscaleWidth == viewportWidth && view._upscaleHeight == viewportHeight;
        if ( bSameSize == false )
        {
            releaseUpscaleIntermediate( view );
            RHITextureDesc desc{};
            desc._width                   = viewportWidth;
            desc._height                  = viewportHeight;
            desc._format                  = RHIFormat::R8G8B8A8_UNORM;
            desc._bIsRenderTarget         = SW_TRUE;
            desc._bIsShaderResource       = SW_TRUE;
            IRHIResourceFactory* pFactory = _pDevice->getResourceFactory();
            view._upscaleIntermediate     = pFactory->createTexture2D( desc );
            if ( view._upscaleIntermediate != 0 )
                view._upscaleIntermediateSrv = pFactory->registerBindlessTexture( view._upscaleIntermediate );
            if ( view._upscaleIntermediateSrv == kInvalidDescriptorIndex )
            {
                SW_LOG_ERROR( "Failed to create the FSR 1 intermediate (%#x%#, RGBA8 RT) - this view is upscaled linearly", viewportWidth, viewportHeight );
                releaseUpscaleIntermediate( view );
                return;
            }
            view._upscaleWidth  = viewportWidth;
            view._upscaleHeight = viewportHeight;
        }
        view._bUpscaleThisFrame = SW_TRUE;
    }

    bool FrameRenderer::recordUpscaleEasu( FramePassContext& context, string_view sourceName )
    {
        const ViewTarget& view = *_pActiveView;
        if ( view._bUpscaleThisFrame == SW_FALSE )
            return false;
        const TransientAttachmentPool::Attachment source = findTransientAttachment( sourceName );
        if ( source._texture == 0 )
            return false;

        IRHICommandList& cmd = *context._pCmd;
        cmd.prepareTextureForShaderRead( source._texture );
        cmd.prepareTextureForRenderTarget( view._upscaleIntermediate );
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = view._upscaleIntermediate;
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::DontCare;
        beginInfo._depthTarget       = 0;
        beginInfo._width             = view._upscaleWidth;
        beginInfo._height            = view._upscaleHeight;
        cmd.beginRenderPass( beginInfo );
        cmd.setVertexBuffer( 0, 0, 0, 0 );
        cmd.setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        cmd.setPipelineState( getEnginePSO( RenderPassType::UpscaleEasu ) );
        // 원본 — 네이티브는 bindless 번호(루트 상수), 에뮬은 엔진 텍스처 슬롯 0. PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다).
        const bool                                  bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        FrameRendererUpscaleInternal::RootConstants constants{};
        constants._source       = bNativeBindless ? static_cast<uint32>( source._srv ) : 0u;
        constants._inputWidth   = static_cast<float32>( source._width );
        constants._inputHeight  = static_cast<float32>( source._height );
        constants._outputWidth  = static_cast<float32>( view._upscaleWidth );
        constants._outputHeight = static_cast<float32>( view._upscaleHeight );
        if ( bNativeBindless == false )
            cmd.bindShaderResource( source._srv, shaderslot::kEngineTexture0 );
        cmd.setGraphicsRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.draw( 3, 0 );
        cmd.endRenderPass();
        cmd.prepareTextureForShaderRead( view._upscaleIntermediate );
        // PSO · 슬롯을 직접 바꿨다 — 뒤의 그리기가 바인딩 캐시를 믿고 PSO 를 건너뛰지 않게 지운다.
        context.resetBindingCache();
        return true;
    }

    void FrameRenderer::drawUpscaleRcas( FramePassContext& context, RHIFormat targetFormat, float32 viewportX, float32 viewportY )
    {
        const ViewTarget&            view = *_pActiveView;
        const RHIPipelineStateHandle pso  = findOutputPSO( RenderPassType::UpscaleRcas, targetFormat ); // 셋업에 없으면 멈춘다
        IRHICommandList&             cmd  = *context._pCmd;
        cmd.setVertexBuffer( 0, 0, 0, 0 );
        cmd.setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        cmd.setPipelineState( pso );
        const bool                                  bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        FrameRendererUpscaleInternal::RootConstants constants{};
        constants._source    = bNativeBindless ? static_cast<uint32>( view._upscaleIntermediateSrv ) : 0u;
        constants._viewportX = viewportX;
        constants._viewportY = viewportY;
        constants._sharpness = MathUtil::max( 0.0f, static_cast<float32>( gv_upscalerSharpness ) );
        if ( bNativeBindless == false )
            cmd.bindShaderResource( view._upscaleIntermediateSrv, shaderslot::kEngineTexture0 );
        cmd.setGraphicsRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.draw( 3, 0 );
        context.resetBindingCache();
    }
} // namespace sw
