/**
 * @file FrameScreenRotation.cpp
 * @brief 화면 회전 출력입니다 — 논리 크기 대기 텍스처의 수명과 스왑체인 이미지로 돌려 그리는 패스(screen_rotation.hlsl).
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Screen/FrameScreenRotation.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHISurfaceTransform.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"

namespace sw
{
    namespace
    {
        struct FrameScreenRotationInternal
        {
            /// screen_rotation.hlsl 의 루트 상수입니다 — `SwRootConstants` 와 같은 순서 · 크기다.
            struct RootConstants
            {
                uint32  _source{ 0 };
                float32 _logicalWidth{ 0.0f };
                float32 _logicalHeight{ 0.0f };
                float32 _m00{ 1.0f };
                float32 _m01{ 0.0f };
                float32 _m10{ 0.0f };
                float32 _m11{ 1.0f };
                float32 _offsetU{ 0.0f };
                float32 _offsetV{ 0.0f };
            };
            static_assert( sizeof( RootConstants ) == 9 * sizeof( uint32 ), "screen_rotation.hlsl 의 루트 상수와 크기가 같아야 한다" );
        };
    } // namespace

    FrameScreenRotation::FrameScreenRotation( IRHIDevice* const& pDevice, const FramePSOManager& psoManager )
        : _pDevice{ pDevice }
        , _psoManager{ psoManager }
        , _texture{ 0 }
        , _srv{ kInvalidDescriptorIndex }
        , _width{ 0 }
        , _height{ 0 }
        , _format{ RHIFormat::Unknown }
    {
    }

    void FrameScreenRotation::release()
    {
        if ( _pDevice != nullptr && _srv != kInvalidDescriptorIndex )
            _pDevice->getResourceFactory()->unregisterBindlessTexture( _srv );
        if ( _pDevice != nullptr && _texture != 0 )
            _pDevice->getResourceFactory()->destroyTexture( _texture );
        _texture = 0;
        _srv     = kInvalidDescriptorIndex;
        _width   = 0;
        _height  = 0;
        _format  = RHIFormat::Unknown;
    }

    void FrameScreenRotation::prepare( uint32 logicalWidth, uint32 logicalHeight )
    {
        // 데스크톱은 늘 Identity 라 여기서 물러난다 — 대기 텍스처도 회전 패스도 없다(비용 0).
        const bool bRotates = _pDevice != nullptr && _pDevice->getSurfaceTransform() != RHISurfaceTransform::Identity && logicalWidth > 0 && logicalHeight > 0;
        if ( bRotates == false )
        {
            if ( _texture != 0 )
                release();
            return;
        }
        const RHIFormat format = _pDevice->getBackBufferFormat();
        if ( _texture != 0 && _width == logicalWidth && _height == logicalHeight && _format == format )
            return;
        release();
        // 백버퍼 형식이다 — 화면에 쓰는 PSO(Present · Canvas · 개발 겹치기 · UI)가 모두 그 형식으로 만들어져 있다.
        RHITextureDesc desc{};
        desc._width                   = logicalWidth;
        desc._height                  = logicalHeight;
        desc._format                  = format;
        desc._bIsRenderTarget         = SW_TRUE;
        desc._bIsShaderResource       = SW_TRUE;
        IRHIResourceFactory* pFactory = _pDevice->getResourceFactory();
        _texture                      = pFactory->createTexture2D( desc );
        if ( _texture != 0 )
        {
            pFactory->setTextureDebugName( _texture, "ScreenRotationTarget" );
            _srv = pFactory->registerBindlessTexture( _texture );
        }
        if ( _srv == kInvalidDescriptorIndex )
            SW_FATAL( "Failed to create the screen rotation target (%#x%#, format %#) - the rotated screen cannot be presented", logicalWidth, logicalHeight, static_cast<uint32>( format ) );
        _width  = logicalWidth;
        _height = logicalHeight;
        _format = format;
    }

    void FrameScreenRotation::recordToSwapchain( IRHICommandList& cmd ) const
    {
        if ( _texture == 0 )
            return;
        // 장치가 이번 프레임에 알린 회전으로 돌린다. 대기 텍스처 크기가 장치의 논리 크기와 잠깐 어긋나도(크기 바뀜 직후) UV 로 읽으므로 화면 전체를 덮는다.
        const RHISurfaceRotation     rotation = _pDevice->getSurfaceRotation();
        const RHIPipelineStateHandle pso      = _psoManager.findOutputPSO( RenderPassType::ScreenRotation, _pDevice->getBackBufferFormat() ); // 셋업에 없으면 멈춘다
        const RHISurfaceRect         viewRect = rotation.mapLogicalRect( RHISurfaceRect{ 0, 0, rotation._logicalWidth, rotation._logicalHeight } );

        cmd.prepareTextureForShaderRead( _texture );
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = 0; // 스왑체인 이미지(실제 크기)
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::DontCare;
        beginInfo._width             = rotation.getPhysicalWidth();
        beginInfo._height            = rotation.getPhysicalHeight();
        cmd.beginRenderPass( beginInfo );
        cmd.setPipelineState( pso );
        RHIViewport viewport{};
        viewport._x      = static_cast<float32>( viewRect._x );
        viewport._y      = static_cast<float32>( viewRect._y );
        viewport._width  = static_cast<float32>( viewRect._width );
        viewport._height = static_cast<float32>( viewRect._height );
        cmd.setViewport( viewport );

        const RHISurfaceUVMapping                  mapping         = rotation.computeLogicalUVMapping();
        const bool                                 bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        FrameScreenRotationInternal::RootConstants constants{};
        constants._source            = bNativeBindless ? static_cast<uint32>( _srv ) : 0u;
        constants._logicalWidth      = static_cast<float32>( _width );
        constants._logicalHeight     = static_cast<float32>( _height );
        constants._m00               = mapping._m00;
        constants._m01               = mapping._m01;
        constants._m10               = mapping._m10;
        constants._m11               = mapping._m11;
        constants._offsetU           = mapping._offsetU;
        constants._offsetV           = mapping._offsetV;
        constexpr uint32 kSourceSlot = shaderslot::emulated::getSlot( "SourceColor" );
        if ( bNativeBindless == false )
            cmd.bindShaderResource( _srv, kSourceSlot );
        cmd.setGraphicsRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.draw( 3, 0 );
        cmd.endRenderPass();
        cmd.prepareTextureForRenderTarget( _texture );
    }
} // namespace sw
