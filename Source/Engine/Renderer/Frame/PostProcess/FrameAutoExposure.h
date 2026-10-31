/**
 * @file FrameAutoExposure.h
 * @brief 자동 노출(auto_exposure.hlsl)입니다 — 뷰마다 노출 기록(1x1 R32 실수 RT 둘)을 셋업에서 갖추고, 톤맵하는 패스 앞에서 측정 · 적응을 기록합니다.
 * @details 언리얼이 `PostProcessing.cpp` 의 효과별 `AddXxxPass` 를 장면 렌더러 밖 함수로 두고 뷰 상태(`FViewInfo`)만 받는 것과 같은 자리다. 렌더러가 소유하고
 *          디바이스 · 파이프라인 에셋 · PSO 관리자는 렌더러의 것을 읽기만 한다. 지금 그리는 뷰는 인자로 받는다(병렬 기록에서 패스끼리 덮어쓰지 않게).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct FogSettings;
    struct FramePassContext;
    struct FrameViewTarget;
    struct RenderGraphPassDesc;
    struct RenderView;

    class FramePSOManager;
    class GPULightBuffer;
    class IRHICommandList;
    class IRHIDevice;
    class RenderPipelineAsset;

    /**
     * @class FrameAutoExposure
     * @brief 자동 노출의 셋업과 기록입니다. 파일 머리말 참고.
     */
    class SW_API FrameAutoExposure
    {
    public:
        FrameAutoExposure( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const FramePSOManager& psoManager );
        FrameAutoExposure( const FrameAutoExposure& )            = delete;
        FrameAutoExposure& operator=( const FrameAutoExposure& ) = delete;

        /**
         * @brief HDR 색을 톤맵하는 첫 패스입니다(Tonemap, 또는 퍼뮤테이션 `SW_POST_TONEMAP` 을 켠 후처리 체인 Present — `passAppliesTonemap`). 없으면(2D 파이프라인) nullptr 입니다.
         * @details 포인터를 들고 있지 않고 지금 파이프라인에서 찾는다 — 파이프라인을 다시 읽으면 패스 목록이 바뀐다.
         */
        static const RenderGraphPassDesc* findTonemapPass( const RenderPipelineAsset& pipeline );

        /** @brief 뷰의 자동 노출 기록 둘(1x1 R32 실수 RT + SRV)을 **셋업 단계에서** 만들어 둡니다. 톤맵하는 패스가 없으면 만들지 않습니다. */
        void ensureAutoExposureHistory( FrameViewTarget& view );
        /** @brief 뷰의 자동 노출 기록을 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseAutoExposureHistory( FrameViewTarget& view );
        /**
         * @brief 뷰 @p view 의 이번 프레임 노출을 정합니다 — 잴지, 측정 상수(EV 범위 · 다가가는 비율), 쓰는 기록 칸입니다. 셋업에서 뷰마다 한 번 부릅니다.
         * @details 카메라의 최소 EV 와 최대 EV 가 같으면 재지 않고 고정 노출 2^-EV 를 `g_CameraGrade.x` 가 될 @p inoutExposureScale 에 곱한다.
         * @param now 렌더러 시계(초, `FrameRenderState::getAnimationTime`)입니다. 쉬던 추가 뷰는 그동안의 시간만큼 한 번에 다가간다.
         */
        void prepareAutoExposure( FrameViewTarget& view, float32 now, float32& inoutExposureScale ) const;
        /**
         * @brief 톤맵하는 패스 앞에서 자동 노출 측정 · 적응 풀스크린을 같은 리스트에 기록하고, 그 패스가 읽을 이번 EV 를 `AutoExposure` 이름으로 겁니다.
         * @details @p passDesc 가 톤맵하는 패스가 아니거나 이번 프레임에 재지 않는 뷰면 아무것도 하지 않는다. 기록 중(병렬)에 불리므로 뷰 상태를 쓰지 않는다.
         */
        void recordAutoExposure( FramePassContext& context, const FrameViewTarget& view, const RenderGraphPassDesc& passDesc ) const;

    private:
        IRHIDevice* const&         _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        const RenderPipelineAsset& _pipeline;
        const FramePSOManager&     _psoManager;
    };
} // namespace sw
