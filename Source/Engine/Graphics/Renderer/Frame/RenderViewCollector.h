/**
 * @file RenderViewCollector.h
 * @brief 씬의 카메라에서 이번 프레임의 뷰 요청을 만드는 게임 스레드 쪽 창구입니다 — 주 시점의 출력 설정과 추가 뷰(캡처 · 화면 사각형) 목록.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Renderer/Frame/RenderView.h"

namespace sw
{
    class CameraComponent;
    class GameObjectManager;
    class RenderViewScheduler;

    /**
     * @struct RenderViewCollector
     * @brief 패킷 경로(EngineLoop)와 씬 직접 경로(`FrameRenderer::execute( pScene )`)가 **같은 규칙**으로 뷰를 고르게 하는 한 자리입니다.
     * @details 추가 뷰 = 등록부의 켜진 카메라 중 출력이 화면 사각형 · 렌더 텍스처인 것(주 시점 카메라 · 에디터 카메라 제외, `kMaxExtraRenderView` 까지).
     *          보이는가는 카메라 출력의 `_visibilityObject`(모니터)의 경계 구가 주 시점 절두체와 겹치는가이고, 그릴지는 `RenderViewScheduler`
     *          (갱신 주기 · 예산)가 정합니다. 쉬는 뷰도 목록에 실어 렌더러가 그 뷰의 자원을 지키게 합니다.
     */
    struct SW_API RenderViewCollector
    {
        /** @brief 주 시점 카메라의 출력 설정입니다(사각형 · 배율 · 끌 기능). 컷 표시는 여기서 읽고 지웁니다(`CameraComponent::consumeCut`). nullptr 이면 기본값입니다. */
        static RenderViewSettings makeMainSettings( CameraComponent* pMainCamera );
        /** @brief 출력 크기 · 사각형의 화면 비율입니다(크기를 모르면 16:9). */
        static float32 computeAspect( const RenderViewSettings& settings, uint32 outputWidth, uint32 outputHeight );
        /** @brief 한 프레임에 그릴 추가 뷰 수의 상한입니다(`-gv_renderViewBudget`). */
        static uint32 getDefaultBudget();
        /**
         * @brief 추가 뷰 요청을 @p outListView 에 채웁니다(지우고 채운다).
         * @param mainViewProj 주 시점의 뷰-투영(보이는가 판정의 절두체).
         * @param outputWidth  주 출력의 크기(화면 사각형 뷰의 픽셀 크기를 정한다). 0 이면 화면 사각형 뷰는 건너뛴다.
         * @param now          스케줄러 시각(초).
         */
        static void collectExtraViews( const GameObjectManager& manager, const CameraComponent* pMainCamera, const float4x4& mainViewProj, uint32 outputWidth,
                                       uint32 outputHeight, float64 now, uint32 budget, RenderViewScheduler& scheduler, vector<RenderViewRequest>& outListView );
    };
} // namespace sw
