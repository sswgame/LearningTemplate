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
        , _sceneTexture{ 0 }
        , _uiTexture{ 0 }
        , _sceneSRV{ kInvalidDescriptorIndex }
        , _uiSRV{ kInvalidDescriptorIndex }
        , _width{ 0 }
        , _height{ 0 }
        , _colorSpace{ RHIColorSpace::SRGBNonLinear }
        , _output{}
    {
    }

    void FrameDisplayEncode::release()
    {
        IRHIResourceFactory* const pFactory = _pDevice != nullptr ? _pDevice->getResourceFactory() : nullptr;
        for ( RHIDescriptorIndex* pSRV : { &_sceneSRV, &_uiSRV } )
        {
            if ( pFactory != nullptr && *pSRV != kInvalidDescriptorIndex )
                pFactory->unregisterBindlessTexture( *pSRV );
            *pSRV = kInvalidDescriptorIndex;
        }
        for ( RHITextureHandle* pTexture : { &_sceneTexture, &_uiTexture } )
        {
            if ( pFactory != nullptr && *pTexture != 0 )
                pFactory->destroyTexture( *pTexture );
            *pTexture = 0;
        }
        _width  = 0;
        _height = 0;
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
            if ( _sceneTexture != 0 || _uiTexture != 0 )
                release();
            return;
        }
        _colorSpace = colorSpace;
        _output     = output;
        if ( _sceneTexture != 0 && _width == width && _height == height )
            return;
        release();
        _width  = width;
        _height = height;
        createTarget( FrameDisplayEncodeInternal::kSceneFormat, "DisplaySceneTarget", _sceneTexture, _sceneSRV );
        // UI 는 SDR 화면과 같은 포맷이다(BGRA8 백버퍼면 BGRA8) — 화면에 쓰는 PSO(Canvas · 개발 겹치기 · 렌더 스레드 UI)가 모두 이 포맷으로 만들어져 있다.
        createTarget( _pDevice->getUITargetFormat(), "DisplayUITarget", _uiTexture, _uiSRV );
    }

    void FrameDisplayEncode::createTarget( RHIFormat format, const utf8* pDebugName, RHITextureHandle& outTexture, RHIDescriptorIndex& outSRV ) const
    {
        RHITextureDesc desc{};
        desc._width                         = _width;
        desc._height                        = _height;
        desc._format                        = format;
        desc._bIsRenderTarget               = SW_TRUE;
        desc._bIsShaderResource             = SW_TRUE;
        IRHIResourceFactory* const pFactory = _pDevice->getResourceFactory();
        outTexture                          = pFactory->createTexture2D( desc );
        if ( outTexture != 0 )
        {
            pFactory->setTextureDebugName( outTexture, pDebugName );
            outSRV = pFactory->registerBindlessTexture( outTexture );
        }
        if ( outSRV == kInvalidDescriptorIndex )
            SW_FATAL( "Failed to create the HDR display target %# (%#x%#, format %#) - the HDR back buffer cannot be presented", pDebugName, _width, _height, RHIFormatUtil::getName( format ) );
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
        if ( _uiTexture == 0 )
            return;
        // 1) UI 합성 — UI 신호(1 = 종이 흰색)를 장면 신호(1 = 최대 휘도)로 옮겨 장면 출력 RT 위에 미리 곱한 알파로 섞는다.
        const RHIPipelineStateHandle composePSO = _psoManager.getEnginePSO( RenderPassKind::DisplayCompositeUI );
        const RenderPassKind         encodeKind = _colorSpace == RHIColorSpace::HDR10ST2084 ? RenderPassKind::DisplayEncodeHDR10 : RenderPassKind::DisplayEncodeScRGB;
        const RHIPipelineStateHandle encodePSO  = _psoManager.getEnginePSO( encodeKind );
        // 백버퍼가 HDR 이면 셋업이 이 PSO 를 만들었다(모니터 능력이 HDR 일 때 — `FramePSOManager::createPassPSOs`). 없으면 HDR 백버퍼에 낼 길이 없다.
        if ( composePSO == 0 || encodePSO == 0 )
            SW_FATAL( "HDR display output has no %# PSO - the HDR back buffer cannot be presented", composePSO == 0 ? RenderPassKind::DisplayCompositeUI : encodeKind );
        const float32 paperWhiteRatio = _output._paperWhiteNits / MathUtil::max( _output._maxLuminanceNits, 1.0f );
        const float32 uiScale         = MathUtil::pow( MathUtil::min( paperWhiteRatio, 1.0f ), FrameDisplayEncodeInternal::kInverseSignalGamma );

        cmd.prepareTextureForShaderRead( _uiTexture );
        RHIRenderPassBeginInfo composeInfo{};
        composeInfo._bBindColor        = SW_TRUE;
        composeInfo._colorTargetCount  = 1;
        composeInfo._arrColorTarget[0] = _sceneTexture;
        composeInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Load;
        composeInfo._width             = _width;
        composeInfo._height            = _height;
        cmd.beginRenderPass( composeInfo );
        drawFullscreen( cmd, composePSO, _uiSRV, uiScale );
        cmd.endRenderPass();

        // 2) 인코딩 — 장면 출력 RT 를 nits 로 풀어 백버퍼 값(HDR10 PQ · scRGB 선형)으로 쓴다.
        cmd.prepareTextureForShaderRead( _sceneTexture );
        RHIRenderPassBeginInfo encodeInfo{};
        encodeInfo._bBindColor        = SW_TRUE;
        encodeInfo._colorTargetCount  = 1;
        encodeInfo._arrColorTarget[0] = 0;                          // 스왑체인 이미지
        encodeInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear; // 이 프레임에 아무도 쓰지 않은 스왑체인 이미지(Vulkan 레이아웃 UNDEFINED)는 Load 로 열 수 없다
        encodeInfo._arrClearColor[0]  = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
        encodeInfo._width             = _width;
        encodeInfo._height            = _height;
        cmd.beginRenderPass( encodeInfo );
        drawFullscreen( cmd, encodePSO, _sceneSRV, _output._maxLuminanceNits );
        cmd.endRenderPass();
        cmd.prepareTextureForRenderTarget( _sceneTexture );
        // 3) UI RT 를 비운다 — 다음 프레임의 화면 쓰기(Canvas · 렌더 스레드 UI)는 Load 로 열고, 아무도 쓰지 않은 프레임은 투명한 UI 를 합성한다.
        cmd.prepareTextureForRenderTarget( _uiTexture );
        RHIRenderPassBeginInfo clearInfo{};
        clearInfo._bBindColor        = SW_TRUE;
        clearInfo._colorTargetCount  = 1;
        clearInfo._arrColorTarget[0] = _uiTexture;
        clearInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
        clearInfo._arrClearColor[0]  = float4{ 0.0f, 0.0f, 0.0f, 0.0f };
        clearInfo._width             = _width;
        clearInfo._height            = _height;
        cmd.beginRenderPass( clearInfo );
        cmd.endRenderPass();
    }

    void FrameDisplayEncode::drawFullscreen( IRHICommandList& cmd, RHIPipelineStateHandle pso, RHIDescriptorIndex srv, float32 scale ) const
    {
        cmd.setPipelineState( pso );
        const bool                                bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        FrameDisplayEncodeInternal::RootConstants constants{};
        constants._source            = bNativeBindless ? static_cast<uint32>( srv ) : 0u;
        constants._sourceWidth       = static_cast<float32>( _width );
        constants._sourceHeight      = static_cast<float32>( _height );
        constants._scale             = scale;
        constexpr uint32 kSourceSlot = shaderslot::emulated::getSlot( "SourceColor" );
        if ( bNativeBindless == false )
            cmd.bindShaderResource( srv, kSourceSlot );
        cmd.setGraphicsRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.draw( 3, 0 );
    }
} // namespace sw
