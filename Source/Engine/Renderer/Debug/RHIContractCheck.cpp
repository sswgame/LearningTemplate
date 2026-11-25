#include "pch.h"

#include "Engine/Renderer/Debug/RHIContractCheck.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIConstantBuffer.h"
#include "Engine/Graphics/RHI/RHIGeometryBuffer.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"

namespace sw
{
    namespace
    {
        struct RHIContractCheckInternal
        {
            static constexpr const utf8* kShaderPath     = "engine/shaders/rhi_contract.hlsl";
            static constexpr uint32      kTargetWidth    = 5;
            static constexpr uint32      kBytesPerPixel  = 4;
            static constexpr uint32      kColorDwords    = 4;
            static constexpr uint32      kConstantBytes  = 256;
            static constexpr uint32      kPastThePage    = 1u << 20; ///< 어느 백엔드의 상수 버퍼 하나보다도 뒤
            static constexpr int32       kPixelTolerance = 1;        ///< 8 비트 반올림

            static constexpr uint8   kFullByte = 255;
            static constexpr uint8   kPartByte = 64;
            static constexpr float32 kFull     = 1.0f;
            static constexpr float32 kPart     = static_cast<float32>( kPartByte ) / static_cast<float32>( kFullByte ); ///< 8 비트로 정확히 떨어지는 값

            // 칸마다의 그리기 — 첫 그리기는 칸 0 부터 끝까지 덮는다.
            static constexpr uint32 kFirstColumn    = 0;
            static constexpr uint32 kOverflowColumn = 1;
            static constexpr uint32 kRebindColumn   = 2;
            static constexpr uint32 kReturnColumn   = 3;
            static constexpr uint32 kScissorColumn  = 4; ///< 첫 그리기만 덮는다
            static constexpr uint32 kVertexCount    = 3; ///< 풀스크린 삼각형

            static constexpr uint32 kColorChannels               = 3;
            static constexpr uint8  kFirstPixel[kColorChannels]  = { kFullByte, 0, kPartByte };
            static constexpr uint8  kRebindPixel[kColorChannels] = { 0, kFullByte, kPartByte };
            static constexpr uint8  kReturnPixel[kColorChannels] = { 0, kFullByte, kFullByte };

            /** @brief 검사 대상 하나의 서술입니다(RGBA8, 5x1). */
            static RHITextureDesc makeTargetDesc()
            {
                RHITextureDesc desc{};
                desc._width             = kTargetWidth;
                desc._height            = 1;
                desc._format            = RHIFormat::R8G8B8A8_UNORM;
                desc._bIsRenderTarget   = SW_TRUE;
                desc._bIsShaderResource = SW_TRUE;
                return desc;
            }

            /** @brief 검사 셰이더의 PSO 하나를 만듭니다(둘을 만들어 서로 다른 파이프라인으로 쓴다). */
            static bool createPipeline( IRHIDevice& device, RHIPipelineState& outPipeline )
            {
                if ( outPipeline.isValid() )
                    return true;
                RHIPipelineStateDesc desc{};
                desc._vertexShaderPath               = kShaderPath;
                desc._pixelShaderPath                = kShaderPath;
                desc._vertexEntryPoint               = "VSMain";
                desc._pixelEntryPoint                = "PSMain";
                desc._numRenderTargets               = 1;
                desc._arrRTVFormat[0]                = RHIFormat::R8G8B8A8_UNORM;
                desc._depthStencilFormat             = RHIFormat::Unknown;
                desc._renderState._bEnableDepthWrite = SW_FALSE;
                return outPipeline.createGraphics( device, desc );
            }

            static bool createValues( IRHIDevice& device, const float4& value, RHIStructuredBuffer& outBuffer )
            {
                return outBuffer.ensureCapacity( &device, sizeof( float4 ), 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, &value );
            }

            /** @brief 한 픽셀 @p column 만 덮는 뷰포트입니다. */
            static RHIViewport makeColumnViewport( uint32 column )
            {
                RHIViewport viewport{};
                viewport._x      = static_cast<float32>( column );
                viewport._y      = 0.0f;
                viewport._width  = 1.0f;
                viewport._height = 1.0f;
                return viewport;
            }

            static bool isPixel( const vector<uint8>& listByte, uint32 column, const uint8 ( &arrExpected )[kColorChannels] )
            {
                const uint8* const pPixel = listByte.data() + static_cast<size_t>( column ) * kBytesPerPixel;
                for ( uint32 channel = 0; channel < kColorChannels; ++channel )
                {
                    if ( MathUtil::abs( static_cast<int32>( pPixel[channel] ) - static_cast<int32>( arrExpected[channel] ) ) > kPixelTolerance )
                        return false;
                }
                return true;
            }
        };
    } // namespace

    void RHIContractCheck::expect( bool bPassed, const utf8* pWhat )
    {
        ++_expectCount;
        if ( bPassed )
            return;
        ++_failureCount;
        SW_LOG_ERROR_ID( "RHIContractCheck.Failed", "RHI contract: %#", pWhat );
    }

    bool RHIContractCheck::ensureResources( IRHIDevice& device )
    {
        using Internal = RHIContractCheckInternal;
        return _target.ensure( device, Internal::makeTargetDesc(), "RHIContractCheck" ) && Internal::createValues( device, float4{ 0.0f, 0.0f, Internal::kPart, Internal::kFull }, _firstValues ) &&
               Internal::createValues( device, float4{ 0.0f, 0.0f, Internal::kFull, Internal::kFull }, _secondValues ) && Internal::createPipeline( device, _firstPipeline ) &&
               Internal::createPipeline( device, _secondPipeline );
    }

    void RHIContractCheck::expectRejections( IRHIDevice& device )
    {
        using Internal               = RHIContractCheckInternal;
        IRHIResourceFactory& factory = *device.getResourceFactory();

        RHITextureSlot rejected;
        RHITextureDesc withoutMips = Internal::makeTargetDesc();
        withoutMips._mipLevels     = 0;
        expect( rejected.ensure( device, withoutMips, "RHIContractCheck" ) == false, "a texture with zero mips was created (RHI.TextureShapeInvalid)" );
        rejected.release( &device );

        RHITextureDesc sliced   = Internal::makeTargetDesc();
        sliced._bIsRenderTarget = SW_FALSE;
        sliced._arraySize       = 2;
        sliced._dimension       = RHITextureDimension::Texture2DArray;
        expect( rejected.ensure( device, sliced, "RHIContractCheck" ) == false, "an array texture was registered as a Texture2D (RHI.BindlessTextureNotTexture2D)" );
        rejected.release( &device );

        expect( factory.registerBindlessTextureUAV( _target._texture ) == kInvalidDescriptorIndex, "a texture without unordered access was registered as a UAV (RHI.BindlessTextureNotUnorderedAccess)" );

        RHIConstantBuffer constants;
        const bool        bConstants = constants.create( &device, Internal::kConstantBytes );
        expect( bConstants, "the constant buffer could not be created" );
        if ( bConstants )
            expect( factory.registerBindlessBufferSRV( RHIBufferSlice{ constants._buffer, Internal::kPastThePage, Internal::kConstantBytes } ) == kInvalidDescriptorIndex,
                    "a constant buffer slice past the page was registered (RHI.BadConstantBufferSlice)" );
        constants.release( &device );

        RHIGeometryBuffer empty;
        expect( empty.createVertex( device, nullptr, sizeof( float4 ) ) == false, "a vertex buffer without content was created (RHI.BufferContentMissing)" );
        empty.release( &device );

        const RHIBufferSRVIndex index = factory.registerBindlessBufferSRV( _firstValues._buffer );
        expect( index != kInvalidDescriptorIndex, "a live structured buffer could not be registered twice" );
        factory.unregisterBindlessResource( index );
        factory.unregisterBindlessResource( index ); // 두 번째 해제는 알린다(RHI.BindlessIndexNotOwned)

        // 메모리 추적: 방금 만든 대상이 이름 · 크기와 함께 올라 있다.
        vector<RHIMemoryResourceEntry> listEntry;
        device.getMemoryTracker().collectResources( listEntry );
        const RHIMemoryKey key      = RHIMemoryKey::makeTexture( _target._texture );
        bool               bTracked = false;
        for ( const RHIMemoryResourceEntry& entry : listEntry )
        {
            bTracked = bTracked || ( entry._key._space == key._space && entry._key._id == key._id && entry._bytes != kRHIMemoryUnknownBytes && entry._label == "RHIContractCheck" );
        }
        expect( bTracked, "the render target is not in the GPU memory tracker" );
    }

    void RHIContractCheck::recordDraws( IRHICommandList& cmd )
    {
        using Internal = RHIContractCheckInternal;
        const float4 firstColor{ Internal::kFull, 0.0f, 0.0f, 0.0f };
        const float4 secondColor{ 0.0f, Internal::kFull, 0.0f, 0.0f };

        cmd.beginRenderPass( RHIRenderPassBeginInfo::color( _target._texture, RHIRenderPassLoadOp::Clear, float4{ 0.0f, 0.0f, 0.0f, 0.0f } ).setExtent( Internal::kTargetWidth, 1 ) );

        // 칸 0 과 4: 가위를 좁힌 뒤의 뷰포트는 가위를 되돌린다 — 끝 칸(4)까지 칠해진다(다른 그리기는 그 칸을 건드리지 않는다).
        cmd.setPipelineState( _firstPipeline._handle );
        cmd.bindStructuredBuffer( _firstValues._srv, shaderslot::kPassBuffer );
        cmd.setGraphicsRootConstants( Internal::kColorDwords, &firstColor );
        RHIScissorRect corner{};
        corner._x      = 0;
        corner._y      = 0;
        corner._width  = 1;
        corner._height = 1;
        cmd.setScissorRect( corner );
        RHIViewport full = Internal::makeColumnViewport( Internal::kFirstColumn );
        full._width      = static_cast<float32>( Internal::kTargetWidth );
        cmd.setViewport( full );
        cmd.draw( Internal::kVertexCount );

        // 칸 1: 블록을 넘는 쓰기는 통째로 버린다 — 앞 색 그대로다(들어가는 만큼만 쓰면 파랑이 찬다).
        const float32 arrOverflow[shaderslot::kRootConstantDwords]{ 0.0f, Internal::kFull };
        cmd.setGraphicsRootConstants( shaderslot::kRootConstantDwords, arrOverflow, 1 );
        cmd.setViewport( Internal::makeColumnViewport( Internal::kOverflowColumn ) );
        cmd.draw( Internal::kVertexCount );

        // 칸 2: 다른 파이프라인을 걸면 슬롯이 빈다 — 같은 번호를 다시 걸면 걸린다(같은 번호라고 건너뛰면 빈 칸을 읽는다). 루트 상수는 남는다.
        cmd.setPipelineState( _secondPipeline._handle );
        cmd.bindStructuredBuffer( _firstValues._srv, shaderslot::kPassBuffer );
        cmd.setGraphicsRootConstants( Internal::kColorDwords, &secondColor );
        cmd.setViewport( Internal::makeColumnViewport( Internal::kRebindColumn ) );
        cmd.draw( Internal::kVertexCount );

        // 칸 3: 되돌아온 파이프라인이 앞 바인딩이 아니라 지금 건 것을 읽는다.
        cmd.setPipelineState( _firstPipeline._handle );
        cmd.bindStructuredBuffer( _secondValues._srv, shaderslot::kPassBuffer );
        cmd.setViewport( Internal::makeColumnViewport( Internal::kReturnColumn ) );
        cmd.draw( Internal::kVertexCount );
        cmd.endRenderPass();
        cmd.prepareTextureForShaderRead( _target._texture );
    }

    void RHIContractCheck::comparePixels( const vector<uint8>& listByte )
    {
        using Internal = RHIContractCheckInternal;
        if ( listByte.size() < static_cast<size_t>( Internal::kTargetWidth ) * Internal::kBytesPerPixel )
        {
            expect( false, "the target readback is too short" );
            return;
        }
        expect( Internal::isPixel( listByte, Internal::kScissorColumn, Internal::kFirstPixel ), "scissor column: a viewport set after a scissor did not reset the scissor" );
        expect( Internal::isPixel( listByte, Internal::kFirstColumn, Internal::kFirstPixel ), "first column: the first draw did not read its root constants and buffer" );
        expect( Internal::isPixel( listByte, Internal::kOverflowColumn, Internal::kFirstPixel ), "overflow column: a root constant write past the block changed the constants" );
        expect( Internal::isPixel( listByte, Internal::kRebindColumn, Internal::kRebindPixel ), "rebind column: binding the same buffer after a pipeline change did not take effect" );
        expect( Internal::isPixel( listByte, Internal::kReturnColumn, Internal::kReturnPixel ), "return column: the draw read a stale buffer or stale root constants" );
    }

    void RHIContractCheck::service( IRHICommandList& cmd, IRHIDevice& device, RenderDebugRegistry& registry )
    {
        if ( _step == Step::Recorded && device.getFrameNumber() > _recordedFrame )
        {
            RHITextureMipSpan       layout{};
            const RHITransferSubmit submit = device.getResourceFactory()->submitTextureReadback( _target._texture, 0, 0, layout );
            if ( submit._result == RHISubmitResult::Busy )
                return;
            expect( submit.isAccepted(), "the target readback was rejected" );
            _readback = RHITransferUtil::PendingReadback{ *device.getResourceFactory(), submit._ticket };
            _step     = Step::Reading;
        }
        if ( _step == Step::Reading )
        {
            const RHITransferStatus status = _readback.poll();
            if ( status == RHITransferStatus::Pending )
                return;
            vector<uint8> listByte;
            if ( status == RHITransferStatus::Complete && _readback.take( listByte ) )
                comparePixels( listByte );
            else
                expect( false, "the target readback failed" );
            SW_LOG_INFO_ID( "RHIContractCheck.Done", "RHI contract on %#: %# of %# expectations failed", device.getBackendName(), _failureCount, _expectCount );
            release( &device );
        }
        if ( _step != Step::Idle || registry.takeRHIContractCheckRequest() == false )
            return;
        _expectCount  = 0;
        _failureCount = 0;
        if ( ensureResources( device ) == false )
        {
            expect( false, "the check could not create its target, buffers or pipelines" );
            release( &device );
            return;
        }
        expectRejections( device );
        recordDraws( cmd );
        _recordedFrame = device.getFrameNumber();
        _step          = Step::Recorded;
    }

    void RHIContractCheck::release( IRHIDevice* pDevice )
    {
        _readback = RHITransferUtil::PendingReadback{};
        _target.release( pDevice );
        _firstValues.release( pDevice );
        _secondValues.release( pDevice );
        _firstPipeline.release( pDevice );
        _secondPipeline.release( pDevice );
        _step = Step::Idle;
    }
} // namespace sw
