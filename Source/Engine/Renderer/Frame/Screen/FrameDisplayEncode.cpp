/**
 * @file FrameDisplayEncode.cpp
 * @brief HDR 화면 출력입니다 — 장면 출력 RT · SDR UI RT 의 수명과 끝의 UI 합성 · 인코딩 패스(display_encode.hlsl).
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Screen/FrameDisplayEncode.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"

namespace sw
{
    namespace
    {
        struct FrameDisplayEncodeInternal
        {
            /// display_encode.hlsl 의 루트 상수입니다 — `SwRootConstants` 와 같은 순서 · 크기다.
            struct RootConstants
            {
                uint32  _source{ 0 };
                float32 _sourceWidth{ 0.0f };
                float32 _sourceHeight{ 0.0f };
                float32 _scale{ 1.0f };
            };
            static_assert( sizeof( RootConstants ) == 4 * sizeof( uint32 ), "display_encode.hlsl 의 루트 상수와 크기가 같아야 한다" );

            /// 장면 출력 RT 의 포맷 — `RenderPassKind::DisplayCompositeUI` · scRGB 인코딩의 고정 포맷과 같다(RenderPassKindInfo.cpp).
            static constexpr RHIFormat kSceneFormat = RHIFormat::R16G16B16A16_FLOAT;
            /// SDR 화면이 신호를 보이는 감마의 역 — display_encode.hlsli `kDisplaySignalGamma`(2.2)와 짝이다.
            static constexpr float32 kInverseSignalGamma = 1.0f / 2.2f;
        };
    } // namespace

    FrameDisplayEncode::FrameDisplayEncode( IRHIDevice* const& pDevice, const FramePSOManager& psoManager )
        : _pDevice{ pDevice }
        , _psoManager{ psoManager }
        , _sceneTarget{}
        , _uiTarget{}
        , _colorSpace{ RHIColorSpace::SRGBNonLinear }
        , _output{}
    {
    }

    void FrameDisplayEncode::release()
    {
        _sceneTarget.release( _pDevice );
        _uiTarget.release( _pDevice );
    }

    RHIFormat FrameDisplayEncode::getSceneFormat()
    {
        return FrameDisplayEncodeInternal::kSceneFormat;
    }

    void FrameDisplayEncode::prepare( uint32 width, uint32 height, const DisplayOutputDesc& output )
    {
        // SDR 은 여기서 물러난다 — RT 도 패스도 없다(비용 0).
        const RHIColorSpace colorSpace = _pDevice != nullptr ? _pDevice->getBackBufferColorSpace() : RHIColorSpace::SRGBNonLinear;
        const bool          bHDR       = colorSpace != RHIColorSpace::SRGBNonLinear && width > 0 && height > 0;
        if ( bHDR == false )
        {
            release();
            return;
        }
        _colorSpace = colorSpace;
        _output     = output;
        ensureTarget( _sceneTarget, FrameDisplayEncodeInternal::kSceneFormat, width, height, "DisplaySceneTarget" );
        // UI 는 SDR 화면과 같은 포맷이다(BGRA8 백버퍼면 BGRA8) — 화면에 쓰는 PSO(Canvas · 개발 겹치기 · 렌더 스레드 UI)가 모두 이 포맷으로 만들어져 있다.
        ensureTarget( _uiTarget, _pDevice->getUITargetFormat(), width, height, "DisplayUITarget" );
    }

    void FrameDisplayEncode::ensureTarget( RHITextureSlot& target, RHIFormat format, uint32 width, uint32 height, const utf8* pDebugName ) const
    {
        RHITextureDesc desc{};
        desc._width             = width;
        desc._height            = height;
        desc._format            = format;
        desc._bIsRenderTarget   = SW_TRUE;
        desc._bIsShaderResource = SW_TRUE;
        if ( target.ensure( *_pDevice, desc, pDebugName ) == false )
            reportFatalError( "Failed to create the HDR display target %# (%#x%#, format %#) - the HDR back buffer cannot be presented", pDebugName, width, height, RHIFormatUtil::getName( format ) );
    }

    float32 FrameDisplayEncode::getToneShoulder() const
    {
        if ( isActive() == false )
            return 1.0f;
        const float32 peakRatio = _output._maxLuminanceNits / MathUtil::max( _output._paperWhiteNits, 1.0f );
        return MathUtil::max( MathUtil::pow( peakRatio, FrameDisplayEncodeInternal::kInverseSignalGamma ), 1.0f );
    }

    void FrameDisplayEncode::recordToSwapchain( IRHICommandList& cmd )
    {
        if ( isActive() == false )
            return;
        // 1) UI 합성 — UI 신호(1 = 종이 흰색)를 장면 신호(1 = 최대 휘도)로 옮겨 장면 출력 RT 위에 미리 곱한 알파로 섞는다.
        const RHIPipelineStateHandle composePSO = _psoManager.getEnginePSO( RenderPassKind::DisplayCompositeUI );
        const RenderPassKind         encodeKind = _colorSpace == RHIColorSpace::HDR10ST2084 ? RenderPassKind::DisplayEncodeHDR10 : RenderPassKind::DisplayEncodeScRGB;
        const RHIPipelineStateHandle encodePSO  = _psoManager.getEnginePSO( encodeKind );
        // 백버퍼가 HDR 이면 셋업이 이 PSO 를 만들었다(모니터 능력이 HDR 일 때 — `FramePSOManager::createPassPSOs`). 없으면 HDR 백버퍼에 낼 길이 없다.
        if ( composePSO == 0 || encodePSO == 0 )
            reportFatalError( "HDR display output has no %# PSO - the HDR back buffer cannot be presented", composePSO == 0 ? RenderPassKind::DisplayCompositeUI : encodeKind );
        const float32 paperWhiteRatio = _output._paperWhiteNits / MathUtil::max( _output._maxLuminanceNits, 1.0f );
        const float32 uiScale         = MathUtil::pow( MathUtil::min( paperWhiteRatio, 1.0f ), FrameDisplayEncodeInternal::kInverseSignalGamma );

        cmd.prepareTextureForShaderRead( _uiTarget._texture );
        cmd.beginRenderPass( RHIRenderPassBeginInfo::color( _sceneTarget._texture, RHIRenderPassLoadOp::Load ).setExtent( getWidth(), getHeight() ) );
        drawFullscreen( cmd, composePSO, _uiTarget._srv, uiScale );
        cmd.endRenderPass();

        // 2) 인코딩 — 장면 출력 RT 를 nits 로 풀어 백버퍼 값(HDR10 PQ · scRGB 선형)으로 쓴다.
        cmd.prepareTextureForShaderRead( _sceneTarget._texture );
        // 이 프레임에 아무도 쓰지 않은 스왑체인 이미지(Vulkan 레이아웃 UNDEFINED)는 Load 로 열 수 없다
        cmd.beginRenderPass( RHIRenderPassBeginInfo::backBuffer( nullptr, RHIRenderPassLoadOp::Clear, float4{ 0.0f, 0.0f, 0.0f, 1.0f } ).setExtent( getWidth(), getHeight() ) );
        drawFullscreen( cmd, encodePSO, _sceneTarget._srv, _output._maxLuminanceNits );
        cmd.endRenderPass();
        cmd.prepareTextureForRenderTarget( _sceneTarget._texture );
        // 3) UI RT 를 비운다 — 다음 프레임의 화면 쓰기(Canvas · 렌더 스레드 UI)는 Load 로 열고, 아무도 쓰지 않은 프레임은 투명한 UI 를 합성한다.
        cmd.prepareTextureForRenderTarget( _uiTarget._texture );
        cmd.clearRenderTargets( RHIRenderPassBeginInfo::color( _uiTarget._texture, RHIRenderPassLoadOp::Clear, float4{ 0.0f, 0.0f, 0.0f, 0.0f } ).setExtent( getWidth(), getHeight() ) );
    }

    void FrameDisplayEncode::drawFullscreen( IRHICommandList& cmd, RHIPipelineStateHandle pso, RHITextureSRVIndex srv, float32 scale ) const
    {
        cmd.setPipelineState( pso );
        FrameDisplayEncodeInternal::RootConstants constants{};
        constants._source       = cmd.bindSampledTexture( srv, shaderslot::emulated::getSlot( "SourceColor" ) );
        constants._sourceWidth  = static_cast<float32>( getWidth() );
        constants._sourceHeight = static_cast<float32>( getHeight() );
        constants._scale        = scale;
        cmd.setGraphicsRootConstants( sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.draw( 3, 0 );
    }
} // namespace sw
