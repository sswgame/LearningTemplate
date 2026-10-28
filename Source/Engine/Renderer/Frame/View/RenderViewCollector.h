/**
 * @file RenderViewCollector.h
 * @brief 씬의 카메라에서 이번 프레임의 뷰 요청을 만드는 게임 스레드 쪽 함수 모음입니다. 주 시점의 출력 설정과 추가 뷰(캡처, 화면 사각형) 목록을 다룹니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"

namespace sw
{
    struct OcclusionTileSnapshot;

    class CameraComponent;
    class GameObjectManager;

    /**
     * @struct RenderViewCollector
     * @brief 패킷 경로(Engine)와 씬 직접 경로(`FrameRenderer::execute( scene )`)가 **같은 규칙**으로 뷰를 고르게 하는 한 자리입니다.
     * @details 추가 뷰는 레지스트리의 켜진 카메라 중 출력이 화면 사각형이나 렌더 텍스처인 것입니다(주 시점 카메라와 에디터 카메라 제외, `kMaxExtraRenderView` 까지).
     *          보이는가는 카메라 출력의 `_visibilityObject`(모니터)가 사람이 보는 화면(주 시점과 화면 사각형 뷰) 어디엔가 `_minScreenSize` 이상으로 보이는가입니다
     *          (화면 크기는 애니메이션 LOD 와 같은 `AnimationLODUtil::computeScreenSize`). 주 시점에서의 가려짐은 렌더러가 되읽은 가림 타일 사본(`OcclusionTileSnapshot`,
     *          `gv_occlusionCulling` 이 켜졌을 때만 있다)으로 봅니다 — 언리얼 `WasRecentlyRendered` 처럼 "지난 프레임에 그려졌나" 이다. 사본이 없으면 가려짐을 보지 않습니다.
     *          화면 사각형 뷰는 사본이 없어 가려짐을 보지 않습니다. 그릴지는 `RenderViewScheduler`(갱신 주기, `RenderViewBudget`)가 정합니다.
     *          쉬는 뷰도 목록에 실어 렌더러가 그 뷰의 자원을 유지하게 합니다.
     */
    struct SW_API RenderViewCollector
    {
        /// 자동 렌더 배율을 쓰지 않는 경로(씬 직접 경로 `FrameRenderer::execute`: 테스트 · 초상화 베이크)가 넘기는 값입니다. 프레임 시계가 없고 출력이 결정적이어야 해 배율을 곱하지 않는다.
        static constexpr float32 kDirectPathAutoScale = 1.0f;

        /** @brief 주 시점 카메라의 출력 설정입니다(사각형, 배율, 끌 기능). @p autoScale 은 자동 렌더 배율(`RenderScaleAdjuster`, 꺼졌거나 쓰지 않는 경로는 1)이다. 컷 표시는 여기서 읽고 지웁니다(`CameraComponent::consumeCut`). nullptr 이면 기본값입니다. */
        static RenderViewSettings makeMainSettings( CameraComponent* pMainCamera, float32 autoScale );
        /** @brief 출력 크기와 사각형의 화면 비율입니다(크기를 모르면 16:9). */
        static float32 computeAspect( const RenderViewSettings& settings, uint32 outputWidth, uint32 outputHeight );
        /** @brief 렌더 텍스처 뷰의 한 프레임 예산입니다(`-gv_renderViewBudget` 뷰 수, `-gv_renderViewMegapixelBudget` 픽셀 합). */
        static RenderViewBudget getDefaultBudget();
        /**
         * @brief 추가 뷰 요청을 @p outListView 에 채웁니다(지우고 채운다).
         * @param mainViewProj 주 시점의 뷰-투영(보이는가 판정의 절두체와 화면 크기).
         * @param mainPosition 주 시점의 눈 위치(화면 크기 판정).
         * @param outputWidth  주 출력의 크기(화면 사각형 뷰의 픽셀 크기를 정한다). 0 이면 화면 사각형 뷰는 건너뛴다.
         * @param autoScale    자동 렌더 배율. 화면 사각형 뷰의 내부 해상도에 곱한다(주 시점과 같은 규칙). 렌더 텍스처 뷰는 자기 크기가 데이터라 곱하지 않는다.
         * @param now          스케줄러 시각(초).
         * @param pMainOcclusion 주 시점의 가림 타일 사본(`FrameRenderer::getOcclusionSnapshot`). nullptr 이면 가려짐을 보지 않는다. 호출하는 동안 살려 둔다.
         */
        static void collectExtraViews( const GameObjectManager& manager, const CameraComponent* pMainCamera, const float4x4& mainViewProj, const float3& mainPosition,
                                       uint32 outputWidth, uint32 outputHeight, float32 autoScale, float64 now, const RenderViewBudget& budget, RenderViewScheduler& scheduler,
                                       const OcclusionTileSnapshot* pMainOcclusion, vector<RenderViewRequest>& outListView );
        /**
         * @brief 호스트 타깃 뷰(에디터 씬 뷰) 요청을 @p inoutListView 끝에 더합니다. 예산과 갱신 주기와 무관하게 매 프레임 그립니다.
         * @details 목록이 가득 찼으면(`kMaxExtraRenderView`) 마지막 추가 뷰 위치를 씁니다. 호스트 뷰는 빠지면 패널이 멈춰 보입니다.
         *          호스트 타깃은 화면 출력 뷰가 아니다: 이미지가 타깃 크기 그대로 보이고(늘이지 않는다, `HostViewTarget`) Present 의 업샘플이 없으므로
         *          `gv_renderScale` · 자동 렌더 배율을 곱하지 않는다(`applyScreenRenderScale` 은 화면 출력 뷰 전용). 내부 해상도는 타깃 크기 그대로(배율 1)다.
         *          카메라 출력의 `_resolutionScale` 이 1 이 아니면 풀만 작아지고 이미지가 늘어나므로 오류를 남기고 이 뷰를 더하지 않는다(렌더 텍스처 뷰도 같다).
         * @param viewMode 이 뷰의 보기 모드(씬 뷰 툴바, `FrameRenderer::getSceneViewMode`).
         */
        static void appendHostView( CameraComponent& camera, const HostViewTarget& target, RenderViewMode viewMode, vector<RenderViewRequest>& inoutListView );
        /** @brief 화면 사각형 뷰(분할 화면, PiP)를 뺍니다. 주 출력이 게임 화면이 아닐 때(씬 뷰만 그리는 프레임) 씁니다. */
        static void removeScreenRectViews( vector<RenderViewRequest>& inoutListView );
    };
} // namespace sw
