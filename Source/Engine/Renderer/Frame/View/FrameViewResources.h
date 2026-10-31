/**
 * @file FrameViewResources.h
 * @brief 뷰 하나의 GPU 자원 수명입니다 — 트랜지언트 첨부 풀(파이프라인이 선언한 것을 뷰 크기로), TAA · 깊이 히스토리, 가림 타일, 후처리 기록, 프레임마다의 패스 고르기.
 * @details 첨부는 **구성이 바뀔 때만** 다시 만들어진다(뷰 크기 · 파이프라인 · 그림자 품질). 소유는 뷰의 `TransientAttachmentPool` 이 하고 여기서는 "무엇을 얼마나 만들지" 를
 *          파이프라인 선언에서 읽어 채운다. 모두 셋업 단계(기록 전)의 일이다 — 기록 중에는 버퍼 · 텍스처 생성과 bindless 등록을 할 수 없다.
 *          언리얼 RDG 의 뷰별 외부 텍스처(`FSceneViewState` 의 히스토리)와 트랜지언트 풀을 한 자리에 둔 것과 같다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"

namespace sw
{
    struct FrameViewTarget;

    class FrameAutoExposure;
    class FrameUpscale;
    class IRHIDevice;
    class RenderPipelineAsset;

    /**
     * @class FrameViewResources
     * @brief 뷰(`FrameViewTarget`)의 GPU 자원을 만들고 놓는 협력 객체입니다. 렌더러가 소유하고 뷰를 인자로 받습니다.
     * @details 디바이스는 렌더러의 `_pDevice` 변수를 참조한다(`FrameCapture` 와 같다). 렌더 타깃 목록 공개(`FrameRenderer::publishRenderTargets`)는 렌더러의 몫이라
     *          패스 고르기는 "목록이 바뀌었나" 만 돌려준다.
     */
    class SW_API FrameViewResources
    {
    public:
        /** @brief `applyViewPassSelection` 이 이번 프레임에 바꾼 것입니다. 렌더 타깃 목록을 다시 싣고 메모리를 다시 보고할지 정할 때 씁니다. */
        struct PassSelectionResult
        {
            uint8 _bTargetsRepointed{ SW_FALSE }; ///< 꺼진 효과의 출력이 가리키는 곳이 바뀌었다(화면에 나가는 첨부 표시가 따라간다)
            uint8 _bTargetsChanged{ SW_FALSE };   ///< 처음 도는 패스의 타깃을 만들었거나 오래 빠진 패스의 타깃을 놓았다
        };

        FrameViewResources( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FrameAutoExposure& autoExposure, FrameUpscale& upscale );
        FrameViewResources( const FrameViewResources& )            = delete;
        FrameViewResources& operator=( const FrameViewResources& ) = delete;

        /**
         * @brief 그림자 맵 한 변의 텍셀 수(`gv_shadowMapResolution`)입니다. 텍스처를 만들 수 있는 범위로 자릅니다.
         * @details 그림자 맵 크기는 화면 크기를 따르지 않는다. 게임 스레드(그림자 행렬의 텍셀 스냅)와 렌더 스레드(첨부 할당)가 같은 값을 쓴다(`FrameRenderer::getShadowMapResolution`).
         */
        static uint32 getShadowMapResolution();
        /**
         * @brief 뷰 하나의 트랜지언트를 이 크기로 맞춥니다. 크기 · 그림자 품질이 같으면 그대로 두고, 다시 만들었으면 true 입니다.
         * @details 다시 만들면 TAA · 깊이 히스토리, 가림 타일, 자동 노출 기록도 새로 만든다. 새것이라 시간 누적 기록은 첫 프레임에 버린다(`_bHasHistory` 를 내린다).
         */
        [[nodiscard]] bool updateViewTransients( FrameViewTarget& view, uint32 width, uint32 height );
        /** @brief 뷰 하나의 트랜지언트 · 히스토리 · 가림 타일 · 후처리 기록을 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseViewTransients( FrameViewTarget& view );
        /**
         * @brief 이 뷰가 이번 프레임에 기록할 풀스크린 패스를 고릅니다. 꺼진 효과는 출력이 원본을 가리키거나(`kBypassesToSource`) 없고(`kOmittedWhenOff`),
         *        읽는 패스가 모두 빠진 풀스크린 패스(블룸이 꺼진 뷰의 BloomDownsample)도 빠집니다. 도는 패스의 출력 타깃은 처음이면 만듭니다.
         * @details 셋업 단계에서(기록 전) 뷰마다 한 번 부른다. 빠질 수 있는 것은 출력을 혼자 쓰는 일반 풀스크린 패스뿐이고, 파이프라인에 읽는 패스가 하나도 없는 출력의 패스는 그대로 돈다.
         * @param viewProj 이번 프레임 이 뷰의 뷰-투영입니다(지난번 것 `_prevViewProj` 와 같으면 카메라 모션 블러는 꺼진 것이다). `_bHistoryReset` 을 정한 뒤 부른다.
         */
        [[nodiscard]] PassSelectionResult applyViewPassSelection( FrameViewTarget& view, const float4x4& viewProj );

    private:
        /**
         * @brief 뷰의 TAA 히스토리 텍스처를 **셋업 단계에서** 만들어 둡니다.
         * @details TAA 패스 콜백은 병렬 기록에서 태스크 스레드가 실행하므로, 콜백 안에서 만들고 bindless 에 등록하면 다른 스레드가 같은 레지스트리를 읽는 도중에 레지스트리가 resize 된다.
         */
        void ensureTAAHistory( FrameViewTarget& view );
        /** @brief 뷰의 깊이 히스토리(R32 실수 RT · RW 텍스처)를 풀 크기로 만들어 둡니다. 장면 깊이 첨부가 없으면(2D 파이프라인) 만들지 않는다. */
        void ensureDepthHistory( FrameViewTarget& view );
        /** @brief 뷰의 가림 타일 버퍼를 깊이 히스토리 크기에 맞춰 만들어 둡니다. 깊이 히스토리가 없으면 만들지 않습니다. */
        void ensureOcclusionTiles( FrameViewTarget& view );

    private:
        IRHIDevice* const&         _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다(생성자 참고)
        const RenderPipelineAsset& _pipeline;
        FrameAutoExposure&         _autoExposure;
        FrameUpscale&              _upscale;
        vector<uint8>              _listScratchNeededAttachment; ///< 패스를 고를 때 선언한 첨부 자리마다 "이번 프레임에 읽힌다"(뷰마다 다시 쓴다)
    };
} // namespace sw
