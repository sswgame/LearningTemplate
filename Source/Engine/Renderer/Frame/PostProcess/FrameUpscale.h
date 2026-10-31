/**
 * @file FrameUpscale.h
 * @brief FSR 1 업스케일(fsr1.hlsl)입니다 — 뷰가 Present 에서 FSR 로 키울지 셋업에서 정하고 중간 RT 를 맞추며, Present 앞에 EASU, 안에 RCAS 를 기록합니다.
 * @details 렌더러가 소유한다. 디바이스 · 파이프라인 에셋 · PSO 관리자는 렌더러의 것을 읽기만 하고, 지금 그리는 뷰는 인자로 받는다.
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
     * @class FrameUpscale
     * @brief FSR 1 업스케일의 셋업과 기록입니다. 파일 머리말 참고.
     */
    class SW_API FrameUpscale
    {
    public:
        FrameUpscale( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const FramePSOManager& psoManager );
        FrameUpscale( const FrameUpscale& )            = delete;
        FrameUpscale& operator=( const FrameUpscale& ) = delete;

        /**
         * @brief 뷰 @p view 가 이번 프레임에 Present 에서 FSR 1 로 키울지 정하고, 키우면 중간 RT 를 **셋업 단계에서** 뷰포트 크기로 맞춥니다.
         * @details Present 가 톤맵하는 파이프라인(후처리 체인)에서 켜면 한 번 오류를 남기고 선형으로 키운다 — 그 파이프라인에는 톤맵한 중간 그림이 없다.
         */
        void prepareUpscale( FrameViewTarget& view, uint32 viewportWidth, uint32 viewportHeight );
        /** @brief 뷰의 FSR 중간 RT 를 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseUpscaleIntermediate( FrameViewTarget& view );
        /**
         * @brief Present 렌더 패스를 열기 **전에** EASU(배율 크기 원본 → 중간 RT)를 기록합니다. 이번 프레임에 FSR 로 키우지 않는 뷰면 false 이고 아무것도 하지 않습니다.
         * @return true 면 Present 는 블릿 대신 `drawUpscaleRcas` 로 그린다
         */
        bool recordUpscaleEasu( FramePassContext& context, const FrameViewTarget& view, string_view sourceName ) const;
        /** @brief 열린 Present 렌더 패스(뷰포트를 건 뒤)에 RCAS(중간 RT → 대상)를 그립니다. @p viewportX · @p viewportY 는 대상 안의 뷰포트 왼쪽 위(px)입니다. */
        void drawUpscaleRcas( FramePassContext& context, const FrameViewTarget& view, RHIFormat targetFormat, float32 viewportX, float32 viewportY ) const;

    private:
        IRHIDevice* const&         _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        const RenderPipelineAsset& _pipeline;
        const FramePSOManager&     _psoManager;
        /// @brief Present 가 톤맵하는 파이프라인에서 FSR 1 을 켰다는 오류를 남겼다(셋업에서만 읽고 쓴다 — 한 번만 알린다).
        uint8 _bUpscalePostChainLogged{ SW_FALSE };
    };
} // namespace sw
