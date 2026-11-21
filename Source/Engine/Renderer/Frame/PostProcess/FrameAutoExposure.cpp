/**
 * @file FrameAutoExposure.cpp
 * @brief 자동 노출입니다 — 뷰마다 HDR 장면의 평균 로그 휘도를 재어 노출을 천천히 맞춥니다(언리얼 Auto Exposure Basic 에 해당).
 * @details 흐름: 셋업(`prepareAutoExposure`, 뷰마다 프레임에 한 번)이 잴지 · 측정 상수 · 쓰는 기록 칸을 정하고, 톤맵하는 패스(`findTonemapPass`)가 자기 그리기 앞에서
 *          측정 풀스크린 하나를 같은 리스트에 기록한다(`recordAutoExposure`, auto_exposure.hlsl). 측정은 지난 칸의 EV 를 읽어 목표 EV 로 다가간 값을 이번 칸(1x1 R32)에 쓰고,
 *          톤맵은 이번 칸을 `g_AutoExposureIndex` 로 읽어 2^-EV 를 곱한다(post_grade.hlsli). 칸이 둘인 까닭은 한 텍스처를 읽으며 쓸 수 없어서다.
 *          EV 는 평균 휘도가 중간 회색(0.18)일 때 0 이다 — 카메라의 범위가 0 · 0(기본)이면 노출 1 이라 자동 노출을 쓰지 않는 카메라의 화면이 그대로다.
 *          기록을 버리는 프레임(처음 · 컷 — `FrameViewTarget::_bHistoryReset`)은 다가가는 비율이 1 이라 측정값을 그대로 쓴다(지난 칸은 읽어도 섞지 않는다).
 *          측정은 휘도 히스토그램(log2 휘도 64 칸) + 백분위 컷이다(언리얼 Auto Exposure Histogram · Adria) — 아래 · 위 몫(EngineConfig `_render._autoExposureLowPercent` ·
 *          `_autoExposureHighPercent`, 셰이더 define)을 버린 평균이라 화면 대부분이 하늘이거나 광원이 든 장면에서도 평균이 끝으로 끌리지 않는다.
 *          네이티브 bindless(DX12 · Vulkan)는 컴퓨트 한 그룹(256 스레드가 128² 표본을 그룹 공유 히스토그램에 모은 뒤 한 스레드가 컷 · 적응)이 RW 번호로 기록 칸에 쓴다.
 *          에뮬 백엔드(DX11 · GL · WebGPU)는 컴퓨트가 샘플 텍스처를 못 읽어(binding.hlsli) 1x1 풀스크린이 32² 표본으로 같은 식을 돈다 — 능력 깃발로 가른 것이다(웹만의 제약이 아니다).
 */
#include "pch.h"

#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/Execute/ViewGraphResource.h"
#include "Engine/Renderer/Frame/FramePassContext.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/TemporalMath.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Frame/View/RenderStability.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
    namespace
    {
        struct FrameAutoExposureInternal
        {
            /// 측정 셰이더의 루트 상수입니다 — auto_exposure.hlsl 의 `SwRootConstants` 와 같은 순서 · 크기다.
            struct RootConstants
            {
                uint32  _source{ 0 };  ///< HDR 원본의 bindless 번호(네이티브). 에뮬은 엔진 텍스처 슬롯 0
                uint32  _history{ 0 }; ///< 지난 칸의 bindless 번호(네이티브). 에뮬은 AutoExposure 칸
                float32 _minEv{ 0.0f };
                float32 _maxEv{ 0.0f };
                float32 _rateUp{ 1.0f };
                float32 _rateDown{ 1.0f };
                uint32  _sourceWidth{ 0 }; ///< 원본에서 뷰 사각형이 덮는 크기(px) — 사각형 밖(자동 렌더 배율로 그리지 않은 곳)을 재지 않는다
                uint32  _sourceHeight{ 0 };
                uint32  _output{ kInvalidDescriptorIndex }; ///< 이번 칸의 RW 번호(히스토그램 컴퓨트). 풀스크린은 쓰지 않는다
            };
            static_assert( sizeof( RootConstants ) == 9 * sizeof( uint32 ), "auto_exposure.hlsl 의 루트 상수와 크기가 같아야 한다" );

            /** @brief 지수 적응에서 @p deltaSeconds 동안 목표로 다가가는 비율(1 - e^(-속도 · dt))입니다. 기록을 버리는 프레임은 1(측정값 그대로)입니다. */
            static float32 computeAdaptRate( float32 speed, float32 deltaSeconds, bool bReset )
            {
                return bReset ? 1.0f : 1.0f - MathUtil::exp( -MathUtil::max( 0.0f, speed ) * deltaSeconds );
            }
        };
    } // namespace

    FrameAutoExposure::FrameAutoExposure( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const FramePSOManager& psoManager )
        : _pDevice{ pDevice }
        , _pipeline{ pipeline }
        , _psoManager{ psoManager }
    {
    }

    uint32 FrameAutoExposure::computeHistogramBin( float32 luminance, float32 logMin, float32 logMax, uint32 binCount )
    {
        if ( binCount == 0 )
            return 0;
        return static_cast<uint32>( shadermath::TemporalMath::computeHistogramBin( luminance, logMin, logMax, static_cast<float32>( binCount ) ) );
    }

    float32 FrameAutoExposure::computeHistogramPercentileMeanLog( vector_reference<const uint32> listBinCount, float32 logMin, float32 logMax, float32 lowPercent, float32 highPercent )
    {
        // 칸마다의 식은 셰이더와 같은 파일(temporal_math.hlsli)이고, 고리는 auto_exposure.hlsl 의 `SW_AUTO_EXPOSURE_PERCENTILE_MEAN_LOG` 와 같은 모양이다.
        using shadermath::TemporalMath;
        const float32 binCount = static_cast<float32>( listBinCount.size() );
        float32       total    = 0.0f;
        for ( const uint32 count : listBinCount )
            total += static_cast<float32>( count );
        if ( total <= 0.0f )
            return 0.5f * ( logMin + logMax );
        const float32 lowCount     = TemporalMath::computeHistogramLowCount( total, lowPercent );
        const float32 highCount    = TemporalMath::computeHistogramHighCount( total, lowPercent, highPercent );
        float32       sumLog       = 0.0f;
        float32       sumWeight    = 0.0f;
        float32       cumulative   = 0.0f;
        float32       lowBinCenter = 0.5f * ( logMin + logMax ); // 남는 몫이 없을 때 — 아래 경계가 든 칸의 가운데
        bool          bLowBinFound = false;
        for ( size_t bin = 0; bin < listBinCount.size(); ++bin )
        {
            const float32 count  = static_cast<float32>( listBinCount[bin] );
            const float32 start  = cumulative;
            const float32 end    = cumulative + count;
            const float32 center = TemporalMath::computeHistogramBinCenterLog( static_cast<float32>( bin ), logMin, logMax, binCount );
            const float32 kept   = TemporalMath::computeHistogramKeptCount( start, end, lowCount, highCount );
            sumLog += kept * center;
            sumWeight += kept;
            if ( bLowBinFound == false && count > 0.0f && end >= lowCount )
            {
                lowBinCenter = center;
                bLowBinFound = true;
            }
            cumulative = end;
        }
        return sumWeight > 0.0f ? sumLog / sumWeight : lowBinCenter;
    }

    const RenderGraphPassDesc* FrameAutoExposure::findTonemapPass( const RenderPipelineAsset& pipeline )
    {
        for ( const RenderGraphPassDesc& pass : pipeline.getGraphPass() )
        {
            if ( passAppliesTonemap( pass ) )
                return &pass;
        }
        return nullptr;
    }

    void FrameAutoExposure::ensureAutoExposureHistory( FrameViewTarget& view )
    {
        const bool bNotNeeded = _pDevice == nullptr || findTonemapPass( _pipeline ) == nullptr || view._arrAutoExposure[0] != 0;
        if ( bNotNeeded )
            return;
        RHITextureDesc desc{};
        desc._width             = 1;
        desc._height            = 1;
        desc._format            = getRenderPassKindInfo( RenderPassKind::AutoExposure )._pColorFormat[0]; // 측정 PSO 와 같은 포맷 한 곳(R 지금 EV · G 목표 EV)
        desc._bIsRenderTarget   = SW_TRUE;
        desc._bIsShaderResource = SW_TRUE;
        // 히스토그램 컴퓨트가 도는 백엔드(PSO 를 만드는 조건과 같다 — 네이티브 bindless + 컴퓨트)는 같은 칸에 RW 로도 쓴다. 풀스크린 길은 RT 로 쓴다.
        const bool bHistogramCompute  = _pDevice->supportsNativeBindlessSampling() && _pDevice->getCapabilities()._bCompute != SW_FALSE;
        desc._bIsUnorderedAccess      = bHistogramCompute ? SW_TRUE : SW_FALSE;
        IRHIResourceFactory* pFactory = _pDevice->getResourceFactory();
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            view._arrAutoExposure[slot] = pFactory->createTexture2D( desc );
            pFactory->setTextureDebugName( view._arrAutoExposure[slot], "AutoExposure" );
            if ( view._arrAutoExposure[slot] != 0 )
                view._arrAutoExposureSRV[slot] = pFactory->registerBindlessTexture( view._arrAutoExposure[slot] );
            if ( view._arrAutoExposure[slot] != 0 && bHistogramCompute )
                view._arrAutoExposureUAV[slot] = pFactory->registerBindlessTextureUAV( view._arrAutoExposure[slot] );
        }
        const bool bCreated = view._arrAutoExposureSRV[0] != kInvalidDescriptorIndex && view._arrAutoExposureSRV[1] != kInvalidDescriptorIndex;
        if ( bCreated == false )
            SW_LOG_ERROR_ID( "FrameAutoExposure.FailedCreateAutoExposure", "Failed to create the auto exposure history (2 x 1x1 R32 float RT) - auto exposure is not measured on this view" );
    }

    void FrameAutoExposure::releaseAutoExposureHistory( FrameViewTarget& view )
    {
        if ( _pDevice != nullptr && _pDevice->getResourceFactory() != nullptr && view._exposureReadbackTicket.isValid() )
            _pDevice->getResourceFactory()->discardTextureReadback( view._exposureReadbackTicket );
        view._exposureReadbackTicket = RHITransferTicket{};
        view._exposureAdaptationEv   = 0.0f;
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            // SRV · RW 등록을 먼저 놓고 텍스처를 지운다(레지스트리에 죽은 핸들을 남기지 않는다 — 깊이 히스토리와 같은 순서).
            if ( _pDevice != nullptr && view._arrAutoExposureSRV[slot] != kInvalidDescriptorIndex )
                _pDevice->getResourceFactory()->unregisterBindlessTexture( view._arrAutoExposureSRV[slot] );
            if ( _pDevice != nullptr && view._arrAutoExposureUAV[slot] != kInvalidDescriptorIndex )
                _pDevice->getResourceFactory()->unregisterBindlessUAV( view._arrAutoExposureUAV[slot] );
            if ( _pDevice != nullptr && view._arrAutoExposure[slot] != 0 )
                _pDevice->releaseHandle( view._arrAutoExposure[slot] );
            view._arrAutoExposure[slot]    = 0;
            view._arrAutoExposureSRV[slot] = kInvalidDescriptorIndex;
            view._arrAutoExposureUAV[slot] = kInvalidDescriptorIndex;
        }
        view._bAutoExposureThisFrame = SW_FALSE;
        view._bAutoExposureMeasured  = SW_FALSE;
    }

    void FrameAutoExposure::prepareAutoExposure( FrameViewTarget& view, float32 now, float32& inoutExposureScale ) const
    {
        const PostProcessValues& post = view._settings._postProcess;
        view._bAutoExposureThisFrame  = SW_FALSE;
        view._exposureErrorEv         = 0.0f; // 재지 않으면 노출이 고정이다(아래에서 재면 다시 정한다)
        // 범위가 한 점이면 재지 않는 고정 노출이다(기본 0 · 0 은 노출 1). 후처리를 끈 뷰 · 표면 값 보기는 톤맵이 카메라 노출을 걸지 않으므로 재지 않는다.
        if ( post.usesAutoExposure() == false )
        {
            inoutExposureScale *= MathUtil::pow( 2.0f, -post._autoExposureMinEv );
            return;
        }
        const bool bHasHistory = view._arrAutoExposureSRV[0] != kInvalidDescriptorIndex && view._arrAutoExposureSRV[1] != kInvalidDescriptorIndex;
        const bool bHasPSO     = _psoManager.getEnginePSO( RenderPassKind::AutoExposure ) != 0 || findHistogramPSO( view ) != 0;
        const bool bMeasures   = view._settings.usesPostProcess() && bHasHistory && bHasPSO;
        if ( bMeasures == false )
            return;
        readBackAdaptation( view ); // 칸을 바꾸기 전 — 지난 프레임이 쓴 칸(`_autoExposureWrite`)을 읽는다

        // 시각은 패킷의 게임 시계다(고정 프레임 시간이면 결정적이다). 쉬던 추가 뷰(갱신 주기)는 그동안의 시간만큼 한 번에 다가간다.
        const float32 deltaSeconds = MathUtil::max( 0.0f, now - view._autoExposureClock );
        // 기록을 버리는 프레임(처음 · 컷)이거나 이 기록에 아직 측정한 값이 없으면(그동안 재지 않던 뷰) 지난 칸을 섞지 않는다 — 새 텍스처에는 값이 없다(NaN 일 수 있다).
        const bool bReset            = view._bHistoryReset == SW_TRUE || view._bAutoExposureMeasured == SW_FALSE;
        view._bAutoExposureMeasured  = SW_TRUE;
        view._autoExposureClock      = now;
        view._autoExposureWrite      = view._autoExposureWrite == 0 ? 1 : 0;
        view._autoExposureParams     = float4{ post._autoExposureMinEv, post._autoExposureMaxEv,
                                           FrameAutoExposureInternal::computeAdaptRate( post._autoExposureSpeedUp, deltaSeconds, bReset ),
                                           FrameAutoExposureInternal::computeAdaptRate( post._autoExposureSpeedDown, deltaSeconds, bReset ) };
        view._bAutoExposureThisFrame = SW_TRUE;
        // 사진 앞 정착(조건 `RenderAutoExposure`) — 남은 노출 오차의 상한. 카메라가 움직인 프레임(카메라 정지 프레임 0)은 범위 끝까지 되돌린다.
        // 물체 움직임으로는 되돌리지 않는다 — 캐릭터가 늘 숨 쉬는 장면은 그러면 상한이 끝내 줄지 않았다(TAA 조건과 같은 카메라 기준).
        view._exposureErrorEv = RenderStabilityUtil::advanceExposureError( view._exposureErrorEv, bReset, view._cameraStillFrameCount == 0, post._autoExposureMaxEv - post._autoExposureMinEv,
                                                                           MathUtil::min( view._autoExposureParams._z, view._autoExposureParams._w ) );
    }

    RHIPipelineStateHandle FrameAutoExposure::findHistogramPSO( const FrameViewTarget& view ) const
    {
        const bool bWritable = view._arrAutoExposureUAV[0] != kInvalidDescriptorIndex && view._arrAutoExposureUAV[1] != kInvalidDescriptorIndex;
        return bWritable ? _psoManager.getEnginePSO( RenderPassKind::AutoExposureHistogram ) : RHIPipelineStateHandle{ 0 };
    }

    void FrameAutoExposure::readBackAdaptation( FrameViewTarget& view ) const
    {
        IRHIResourceFactory* const pFactory = _pDevice != nullptr ? _pDevice->getResourceFactory() : nullptr;
        if ( pFactory == nullptr )
            return;
        if ( view._exposureReadbackTicket.isValid() )
        {
            const RHITransferStatus status = pFactory->pollTransfer( view._exposureReadbackTicket );
            if ( status == RHITransferStatus::Pending )
                return;
            vector<uint8> listByte;
            const bool    bTaken = status == RHITransferStatus::Complete && pFactory->takeTextureReadback( view._exposureReadbackTicket, listByte );
            if ( bTaken == false )
                pFactory->discardTextureReadback( view._exposureReadbackTicket );
            float32 arrEv[2]{};
            if ( bTaken && listByte.size() >= sizeof( arrEv ) )
            {
                Memory::copy( arrEv, listByte.data(), sizeof( arrEv ) );
                view._exposureAdaptationEv = MathUtil::abs( arrEv[1] - arrEv[0] );
            }
            view._exposureReadbackTicket = RHITransferTicket{};
            return;
        }
        // 아직 잰 적이 없는 기록 칸에는 값이 없다(NaN 일 수 있다).
        if ( view._bAutoExposureMeasured == SW_FALSE )
            return;
        RHITextureMipSpan       layout{};
        const RHITransferSubmit submit = pFactory->submitTextureReadback( view._arrAutoExposure[view._autoExposureWrite], 0, 0, layout );
        if ( submit.isAccepted() )
            view._exposureReadbackTicket = submit._ticket; // `Busy`(스테이징이 찼다)면 다음 프레임에 다시 낸다
    }

    void FrameAutoExposure::recordAutoExposure( FramePassContext& context, const FrameViewTarget& view, const RenderGraphPassDesc& passDesc ) const
    {
        if ( &passDesc != findTonemapPass( _pipeline ) || view._bAutoExposureThisFrame == SW_FALSE )
            return;
        TransientAttachmentPool::Attachment source{};
        for ( const RenderGraphPassDesc::ResolvedAttachment& input : passDesc._listResolvedInput )
        {
            if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor )
                source = view._transientPool.find( input._attachment );
        }
        // 톤맵하는 패스는 원본이 필수 입력이다(계약) — 이번 프레임에 없으면 그 패스도 그리지 못한다.
        if ( source._texture == 0 )
            return;

        const uint32                             writeSlot       = view._autoExposureWrite;
        const uint32                             readSlot        = 1u - writeSlot;
        const RHITextureHandle                   target          = view._arrAutoExposure[writeSlot];
        IRHIPassCommandList&                     cmd             = *context._pCmd;
        const bool                               bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        FrameAutoExposureInternal::RootConstants constants{};
        constants._source       = bNativeBindless ? static_cast<uint32>( source._srv ) : 0u;
        constants._history      = bNativeBindless ? static_cast<uint32>( view._arrAutoExposureSRV[readSlot] ) : 0u;
        constants._minEv        = view._autoExposureParams._x;
        constants._maxEv        = view._autoExposureParams._y;
        constants._rateUp       = view._autoExposureParams._z;
        constants._rateDown     = view._autoExposureParams._w;
        constants._sourceWidth  = view.scaleToRect( source, true );
        constants._sourceHeight = view.scaleToRect( source, false );
        // 원본은 이 패스의 입력 선언이, 지난 칸 · 이번 칸은 종류의 선언(`ViewGraphResource::autoExposureHistory` · `autoExposureTarget` — 재는 뷰만 등록)이 레벨 콜백에서 옮겼다
        // (지난 칸 셰이더 읽기, 이번 칸 RW 텍스처). 이번 칸은 같은 패스(톤맵)가 읽으므로 쓴 뒤 셰이더 읽기로 옮긴다.
        const RenderGraphPassContext& graphPass = *context._pGraphPass;

        // 히스토그램 컴퓨트(네이티브 bindless) — 한 그룹이 표본을 모아 컷 · 적응까지 하고 이번 칸에 RW 로 쓴다. 풀스크린보다 표본이 16 배다.
        const RHIPipelineStateHandle histogramPSO = findHistogramPSO( view );
        if ( histogramPSO != 0 )
        {
            constants._output = static_cast<uint32>( view._arrAutoExposureUAV[writeSlot] );
            cmd.setComputePipelineState( histogramPSO );
            cmd.setComputeRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
            cmd.dispatchCompute( 1, 1, 1 );
            graphPass.transition( ViewGraphResource::autoExposureTarget(), RHITextureState::ShaderResource );
            context.resetBindingCache();
            context._resourceRegistry.registerTexture( attachmentNames()._autoExposure, target, view._arrAutoExposureSRV[writeSlot] );
            return;
        }
        // 1 px 그리기 판 — 이번 칸을 렌더 타깃으로 옮긴다(선언은 UAV — 컴퓨트 판이 기본이다).
        graphPass.transition( ViewGraphResource::autoExposureTarget(), RHITextureState::RenderTarget );

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = target;
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::DontCare;
        beginInfo._depthTarget       = 0;
        beginInfo._width             = 1;
        beginInfo._height            = 1;
        cmd.beginRenderPass( beginInfo );
        cmd.setPipelineState( _psoManager.getEnginePSO( RenderPassKind::AutoExposure ) );
        // 입력 — 네이티브는 bindless 번호(루트 상수), 에뮬은 SourceColor · AutoExposure 칸(auto_exposure.hlsl 이 같은 이름으로 읽는다). PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다).
        if ( bNativeBindless == false )
        {
            constexpr uint32 kSourceSlot  = shaderslot::emulated::getSlot( "SourceColor" );
            constexpr uint32 kHistorySlot = shaderslot::emulated::getSlot( "AutoExposure" );
            cmd.bindShaderResource( source._srv, kSourceSlot );
            cmd.bindShaderResource( view._arrAutoExposureSRV[readSlot], kHistorySlot );
        }
        cmd.setGraphicsRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.draw( 3, 0 );
        cmd.endRenderPass();
        graphPass.transition( ViewGraphResource::autoExposureTarget(), RHITextureState::ShaderResource );
        // 측정 그리기는 PSO · 슬롯을 직접 바꿨다 — 톤맵 그리기가 바인딩 캐시를 믿고 PSO 를 건너뛰지 않게 지운다.
        context.resetBindingCache();
        context._resourceRegistry.registerTexture( attachmentNames()._autoExposure, target, view._arrAutoExposureSRV[writeSlot] );
    }
} // namespace sw
