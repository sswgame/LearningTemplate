/**
 * @file FrameUpscale.cpp
 * @brief Present 의 FSR 1 업스케일입니다 — 렌더 배율(`gv_renderScale`)로 작게 그린 뷰를 선형 블릿 대신 EASU + RCAS 로 출력 크기로 키웁니다(fsr1.hlsl).
 * @details 흐름: 셋업(`prepareUpscale`, 뷰마다 프레임에 한 번)이 키울지 정하고 뷰포트 크기 중간 RT 를 맞춘다. Present 가 렌더 패스를 열기 전에 EASU(풀 크기 원본 →
 *          중간 RT)를 기록하고(`recordUpscaleEasu`), 렌더 패스 안에서 블릿 대신 RCAS(중간 RT → 대상 뷰포트)를 그린다(`drawUpscaleRcas`).
 *          EASU 는 톤맵한 지각 공간 색을 받는다(AMD 권고 — 톤맵 뒤 · UI 앞). 그래서 톤맵 패스가 따로 있고 Present 가 그 결과를 키우는 파이프라인(디퍼드)에서만 쓴다.
 *          언리얼의 Spatial Upscale(r.Upscale.Quality)과 같은 자리다 — 시간 누적 업스케일(TSR · FSR 2)은 아니다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/Execute/ViewGraphResource.h"
#include "Engine/Renderer/Frame/FramePassContext.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
    namespace
    {
        struct FrameUpscaleInternal
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
                float32 _inputTextureWidth{ 0.0f }; ///< EASU: 원본 텍스처 전체 크기(게더 UV) — 입력 크기는 그 안의 뷰 사각형이다
                float32 _inputTextureHeight{ 0.0f };
                float32 _sharpness{ 0.0f };
            };
            static_assert( sizeof( RootConstants ) == 8 * sizeof( uint32 ), "fsr1.hlsl 의 루트 상수와 크기가 같아야 한다" );
        };
    } // namespace

    FrameUpscale::FrameUpscale( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const RenderFrameSettings& frameSettings, const FramePSOManager& psoManager )
        : _pDevice{ pDevice }
        , _pipeline{ pipeline }
        , _frameSettings{ frameSettings }
        , _psoManager{ psoManager }
    {
    }

    void FrameUpscale::releaseUpscaleIntermediate( FrameViewTarget& view )
    {
        view._upscaleIntermediate.release( _pDevice );
        view._bUpscaleThisFrame = SW_FALSE;
    }

    void FrameUpscale::prepareUpscale( FrameViewTarget& view, uint32 viewportWidth, uint32 viewportHeight )
    {
        using Internal                                  = FrameUpscaleInternal;
        view._bUpscaleThisFrame                         = SW_FALSE;
        const TransientAttachmentPool& pool             = view._transientPool;
        const bool                     bSpatial         = _frameSettings._upscaler == Internal::kUpscalerSpatial;
        const bool                     bSmaller         = view._outputRect._width < viewportWidth || view._outputRect._height < viewportHeight; // Present 원본(출력 공간)의 사각형이 화면보다 작다(풀 배율 · 자동 렌더 배율)
        const bool                     bCandidate       = _pDevice != nullptr && bSpatial && bSmaller && view._settings._upscaleZoom == 0 && pool.hasSize();
        const RenderGraphPassDesc*     pTonemap         = FrameAutoExposure::findTonemapPass( _pipeline );
        const bool                     bPostChain       = pTonemap != nullptr && pTonemap->_type == RenderPassKind::Present;
        const bool                     bReportPostChain = bCandidate && bPostChain && _bUpscalePostChainLogged == SW_FALSE;
        if ( bReportPostChain )
        {
            // 이 파이프라인은 Present 가 톤맵까지 한다 — 키울 톤맵한 중간 그림이 없다(EASU 는 톤맵한 색을 받는다). 데이터(파이프라인)와 설정이 맞지 않는 것을 알린다.
            SW_LOG_ERROR_ID( "FrameUpscale.PipelineTonemapsPresentPass", "Pipeline '%#' tonemaps in its Present pass - FSR 1 needs a tonemapped image to upscale, so this view is upscaled linearly",
                             _pipeline.getDesc()._name.c_str() );
            _bUpscalePostChainLogged = SW_TRUE;
        }
        const bool bUpscale = bCandidate && bPostChain == false && _psoManager.getEnginePSO( RenderPassKind::UpscaleEasu ) != 0;
        if ( bUpscale == false )
        {
            releaseUpscaleIntermediate( view );
            return;
        }
        RHITextureDesc desc{};
        desc._width             = viewportWidth;
        desc._height            = viewportHeight;
        desc._format            = getRenderPassKindInfo( RenderPassKind::UpscaleEasu )._pColorFormat[0]; // EASU PSO 와 같은 포맷 한 곳
        desc._bIsRenderTarget   = SW_TRUE;
        desc._bIsShaderResource = SW_TRUE;
        if ( view._upscaleIntermediate.ensure( *_pDevice, desc, "UpscaleIntermediate" ) == false )
        {
            SW_LOG_ERROR_ONCE_ID( "FrameUpscale.FailedCreateFSRIntermediate", "Failed to create the FSR 1 intermediate (%#x%#, RGBA8 RT) - this view is upscaled linearly", viewportWidth, viewportHeight );
            return;
        }
        view._bUpscaleThisFrame = SW_TRUE;
    }

    bool FrameUpscale::recordUpscaleEasu( FramePassContext& context, const FrameViewTarget& view, const hashed_string& sourceName ) const
    {
        if ( view._bUpscaleThisFrame == SW_FALSE )
            return false;
        const TransientAttachmentPool::Attachment source = view._transientPool.find( sourceName );
        if ( source._texture == 0 )
            return false;

        // 원본은 Present 의 입력 선언이, 중간 RT 는 이 패스의 렌더 타깃 선언(`ViewGraphResource::upscaleIntermediate` — 키우는 뷰만 등록)이 레벨 콜백에서 옮겼다.
        IRHIPassCommandList&   cmd = *context._pCmd;
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = view._upscaleIntermediate._texture;
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::DontCare;
        beginInfo._depthTarget       = 0;
        beginInfo._width             = view._upscaleIntermediate._width;
        beginInfo._height            = view._upscaleIntermediate._height;
        context.beginRenderPass( beginInfo );
        context.setPipeline( _psoManager.getEnginePSO( RenderPassKind::UpscaleEasu ) );
        // 원본 — 네이티브는 bindless 번호(루트 상수), 에뮬은 SourceColor 칸(fsr1.hlsl 이 같은 이름으로 읽는다). PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다).
        const bool                          bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        FrameUpscaleInternal::RootConstants constants{};
        constants._source             = bNativeBindless ? static_cast<uint32>( source._srv ) : 0u;
        constants._inputWidth         = static_cast<float32>( view.scaleToRect( source, true ) );
        constants._inputHeight        = static_cast<float32>( view.scaleToRect( source, false ) );
        constants._inputTextureWidth  = static_cast<float32>( source._width );
        constants._inputTextureHeight = static_cast<float32>( source._height );
        constants._outputWidth        = static_cast<float32>( view._upscaleIntermediate._width );
        constants._outputHeight       = static_cast<float32>( view._upscaleIntermediate._height );
        constexpr uint32 kSourceSlot  = shaderslot::emulated::getSlot( "SourceColor" );
        if ( bNativeBindless == false )
            cmd.bindShaderResource( source._srv, kSourceSlot );
        cmd.setGraphicsRootConstants( sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.draw( 3, 0 );
        cmd.endRenderPass();
        // 같은 패스의 RCAS 가 읽는다 — 쓰기로 선언한 중간 RT 를 패스 안에서 셰이더 읽기로 옮긴다.
        context._pGraphPass->transition( ViewGraphResource::upscaleIntermediate(), RenderGraphAccess::ShaderRead );
        return true;
    }

    void FrameUpscale::drawUpscaleRcas( FramePassContext& context, const FrameViewTarget& view, RHIFormat targetFormat ) const
    {
        const RHIPipelineStateHandle pso = _psoManager.findOutputPSO( RenderPassKind::UpscaleRcas, targetFormat ); // 셋업에 없으면 멈춘다
        IRHIPassCommandList&         cmd = *context._pCmd;
        context.setPipeline( pso );
        const bool                          bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        FrameUpscaleInternal::RootConstants constants{};
        constants._source            = bNativeBindless ? static_cast<uint32>( view._upscaleIntermediate._srv ) : 0u;
        constants._outputWidth       = static_cast<float32>( view._upscaleIntermediate._width ); // 픽셀 = 정점 UV × 중간 RT 크기(SV_Position 은 GL 기본 프레임버퍼에서 원점이 다르다)
        constants._outputHeight      = static_cast<float32>( view._upscaleIntermediate._height );
        constants._sharpness         = _frameSettings._upscalerSharpness; // 사본을 뜰 때 0 아래를 잘랐다
        constexpr uint32 kSourceSlot = shaderslot::emulated::getSlot( "SourceColor" );
        if ( bNativeBindless == false )
            cmd.bindShaderResource( view._upscaleIntermediate._srv, kSourceSlot );
        cmd.setGraphicsRootConstants( sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.draw( 3, 0 );
    }
} // namespace sw
