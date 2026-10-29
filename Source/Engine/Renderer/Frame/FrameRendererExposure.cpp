/**
 * @file FrameRendererExposure.cpp
 * @brief 자동 노출입니다 — 뷰마다 HDR 장면의 평균 로그 휘도를 재어 노출을 천천히 맞춥니다(언리얼 Auto Exposure Basic 에 해당).
 * @details 흐름: 셋업(`prepareAutoExposure`, 뷰마다 프레임에 한 번)이 잴지 · 측정 상수 · 쓰는 기록 칸을 정하고, 톤맵하는 패스(`findTonemapPass`)가 자기 그리기 앞에서
 *          측정 풀스크린 하나를 같은 리스트에 기록한다(`recordAutoExposure`, autoexposure.hlsl). 측정은 지난 칸의 EV 를 읽어 목표 EV 로 다가간 값을 이번 칸(1x1 R32)에 쓰고,
 *          톤맵은 이번 칸을 `g_AutoExposureIndex` 로 읽어 2^-EV 를 곱한다(postgrade.hlsli). 칸이 둘인 까닭은 한 텍스처를 읽으며 쓸 수 없어서다.
 *          EV 는 평균 휘도가 중간 회색(0.18)일 때 0 이다 — 카메라의 범위가 0 · 0(기본)이면 노출 1 이라 자동 노출을 쓰지 않는 카메라의 화면이 그대로다.
 *          기록을 버리는 프레임(처음 · 컷 — `FrameViewTarget::_bHistoryReset`)은 다가가는 비율이 1 이라 측정값을 그대로 쓴다(지난 칸은 읽어도 섞지 않는다).
 * @note 히스토그램 방식(밝은 끝 · 어두운 끝을 잘라 내는 언리얼 기본값)은 없다 — 화면 대부분이 하늘인 장면은 평균이 하늘 쪽으로 끌린다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    namespace
    {
        struct FrameRendererExposureInternal
        {
            /// 측정 셰이더의 루트 상수입니다 — autoexposure.hlsl 의 `SwRootConstants` 와 같은 순서 · 크기다.
            struct RootConstants
            {
                uint32  _source{ 0 };  ///< HDR 원본의 bindless 번호(네이티브). 에뮬은 엔진 텍스처 슬롯 0
                uint32  _history{ 0 }; ///< 지난 칸의 bindless 번호(네이티브). 에뮬은 AutoExposure 칸
                float32 _minEv{ 0.0f };
                float32 _maxEv{ 0.0f };
                float32 _rateUp{ 1.0f };
                float32 _rateDown{ 1.0f };
            };
            static_assert( sizeof( RootConstants ) == 6 * sizeof( uint32 ), "autoexposure.hlsl 의 루트 상수와 크기가 같아야 한다" );

            /** @brief 지수 적응에서 @p deltaSeconds 동안 목표로 다가가는 비율(1 - e^(-속도 · dt))입니다. 기록을 버리는 프레임은 1(측정값 그대로)입니다. */
            static float32 computeAdaptRate( float32 speed, float32 deltaSeconds, bool bReset )
            {
                return bReset ? 1.0f : 1.0f - MathUtil::exp( -MathUtil::max( 0.0f, speed ) * deltaSeconds );
            }
        };
    } // namespace

    void FrameRenderer::ensureAutoExposureHistory( FrameViewTarget& view )
    {
        const bool bNotNeeded = _pDevice == nullptr || findTonemapPass() == nullptr || view._arrAutoExposure[0] != 0;
        if ( bNotNeeded )
            return;
        RHITextureDesc desc{};
        desc._width                   = 1;
        desc._height                  = 1;
        desc._format                  = RHIFormat::R32_FLOAT;
        desc._bIsRenderTarget         = SW_TRUE;
        desc._bIsShaderResource       = SW_TRUE;
        IRHIResourceFactory* pFactory = _pDevice->getResourceFactory();
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            view._arrAutoExposure[slot] = pFactory->createTexture2D( desc );
            if ( view._arrAutoExposure[slot] != 0 )
                view._arrAutoExposureSRV[slot] = pFactory->registerBindlessTexture( view._arrAutoExposure[slot] );
        }
        const bool bCreated = view._arrAutoExposureSRV[0] != kInvalidDescriptorIndex && view._arrAutoExposureSRV[1] != kInvalidDescriptorIndex;
        if ( bCreated == false )
            SW_LOG_ERROR( "Failed to create the auto exposure history (2 x 1x1 R32 float RT) - auto exposure is not measured on this view" );
    }

    void FrameRenderer::releaseAutoExposureHistory( FrameViewTarget& view )
    {
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            // SRV 등록을 먼저 놓고 텍스처를 지운다(레지스트리에 죽은 핸들을 남기지 않는다 — 깊이 히스토리와 같은 순서).
            if ( _pDevice != nullptr && view._arrAutoExposureSRV[slot] != kInvalidDescriptorIndex )
                _pDevice->getResourceFactory()->unregisterBindlessTexture( view._arrAutoExposureSRV[slot] );
            if ( _pDevice != nullptr && view._arrAutoExposure[slot] != 0 )
                _pDevice->getResourceFactory()->destroyTexture( view._arrAutoExposure[slot] );
            view._arrAutoExposure[slot]    = 0;
            view._arrAutoExposureSRV[slot] = kInvalidDescriptorIndex;
        }
        view._bAutoExposureThisFrame = SW_FALSE;
        view._bAutoExposureMeasured  = SW_FALSE;
    }

    void FrameRenderer::prepareAutoExposure( float32& inoutExposureScale )
    {
        FrameViewTarget&               view = *_pActiveView;
        const CameraPostProcessValues& post = view._settings._postProcess;
        view._bAutoExposureThisFrame        = SW_FALSE;
        // 범위가 한 점이면 재지 않는 고정 노출이다(기본 0 · 0 은 노출 1). 후처리를 끈 뷰 · 표면 값 보기는 톤맵이 카메라 노출을 걸지 않으므로 재지 않는다.
        if ( post.usesAutoExposure() == false )
        {
            inoutExposureScale *= MathUtil::pow( 2.0f, -post._autoExposureMinEv );
            return;
        }
        const bool bHasHistory = view._arrAutoExposureSRV[0] != kInvalidDescriptorIndex && view._arrAutoExposureSRV[1] != kInvalidDescriptorIndex;
        const bool bMeasures   = view._settings.usesPostProcess() && bHasHistory && getEnginePSO( RenderPassType::AutoExposure ) != 0;
        if ( bMeasures == false )
            return;

        // 시각은 렌더러 시계다(테스트는 고정할 수 있다). 쉬던 추가 뷰(갱신 주기)는 그동안의 시간만큼 한 번에 다가간다.
        const float32 now          = getAnimationTime();
        const float32 deltaSeconds = MathUtil::max( 0.0f, now - view._autoExposureClock );
        // 기록을 버리는 프레임(처음 · 컷)이거나 이 기록에 아직 측정한 값이 없으면(그동안 재지 않던 뷰) 지난 칸을 섞지 않는다 — 새 텍스처에는 값이 없다(NaN 일 수 있다).
        const bool bReset            = view._bHistoryReset == SW_TRUE || view._bAutoExposureMeasured == SW_FALSE;
        view._bAutoExposureMeasured  = SW_TRUE;
        view._autoExposureClock      = now;
        view._autoExposureWrite      = view._autoExposureWrite == 0 ? 1 : 0;
        view._autoExposureParams     = float4{ post._autoExposureMinEv, post._autoExposureMaxEv,
                                           FrameRendererExposureInternal::computeAdaptRate( post._autoExposureSpeedUp, deltaSeconds, bReset ),
                                           FrameRendererExposureInternal::computeAdaptRate( post._autoExposureSpeedDown, deltaSeconds, bReset ) };
        view._bAutoExposureThisFrame = SW_TRUE;
    }

    void FrameRenderer::recordAutoExposure( FramePassContext& context, const RenderGraphPassDesc& passDesc )
    {
        const FrameViewTarget& view = *_pActiveView;
        if ( &passDesc != findTonemapPass() || view._bAutoExposureThisFrame == SW_FALSE )
            return;
        TransientAttachmentPool::Attachment source{};
        for ( const RenderGraphPassDesc::ResolvedAttachment& input : passDesc._listResolvedInput )
        {
            if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor )
                source = findTransientAttachment( input._attachment.view() );
        }
        // 톤맵하는 패스는 원본이 필수 입력이다(계약) — 이번 프레임에 없으면 그 패스도 그리지 못한다.
        if ( source._texture == 0 )
            return;

        const uint32           writeSlot = view._autoExposureWrite;
        const uint32           readSlot  = 1u - writeSlot;
        const RHITextureHandle target    = view._arrAutoExposure[writeSlot];
        IRHICommandList&       cmd       = *context._pCmd;
        cmd.prepareTextureForShaderRead( source._texture );
        cmd.prepareTextureForShaderRead( view._arrAutoExposure[readSlot] );
        cmd.prepareTextureForRenderTarget( target );

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = target;
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::DontCare;
        beginInfo._depthTarget       = 0;
        beginInfo._width             = 1;
        beginInfo._height            = 1;
        cmd.beginRenderPass( beginInfo );
        cmd.setVertexBuffer( 0, 0, 0, 0 );
        cmd.setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        cmd.setPipelineState( getEnginePSO( RenderPassType::AutoExposure ) );
        // 입력 — 네이티브는 bindless 번호(루트 상수), 에뮬은 SourceColor · AutoExposure 칸(autoexposure.hlsl 이 같은 이름으로 읽는다). PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다).
        const bool                                   bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        FrameRendererExposureInternal::RootConstants constants{};
        constants._source   = bNativeBindless ? static_cast<uint32>( source._srv ) : 0u;
        constants._history  = bNativeBindless ? static_cast<uint32>( view._arrAutoExposureSRV[readSlot] ) : 0u;
        constants._minEv    = view._autoExposureParams._x;
        constants._maxEv    = view._autoExposureParams._y;
        constants._rateUp   = view._autoExposureParams._z;
        constants._rateDown = view._autoExposureParams._w;
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
        cmd.prepareTextureForShaderRead( target );
        // 측정 그리기는 PSO · 슬롯을 직접 바꿨다 — 톤맵 그리기가 바인딩 캐시를 믿고 PSO 를 건너뛰지 않게 지운다.
        context.resetBindingCache();
        context._resourceRegistry.registerTexture( attachmentNames()._autoExposure, target, view._arrAutoExposureSRV[writeSlot] );
    }
} // namespace sw
