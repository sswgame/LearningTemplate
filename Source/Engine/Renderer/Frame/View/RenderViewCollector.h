/**
 * @file RenderViewCollector.h
 * @brief 씬의 카메라에서 이번 프레임의 뷰 요청을 만드는 게임 스레드 쪽 함수 모음입니다. 주 시점의 출력 설정과 추가 뷰(캡처, 화면 사각형) 목록을 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"

namespace sw
{
    class CameraComponent;
    class GameObjectManager;

    /**
     * @struct RenderViewCollector
     * @brief 패킷 경로(Engine)와 씬 직접 경로(`FrameRenderer::execute( scene )`)가 **같은 규칙**으로 뷰를 고르게 하는 한 자리입니다.
     * @details 추가 뷰는 레지스트리의 켜진 카메라 중 출력이 화면 사각형이나 렌더 텍스처인 것입니다(주 시점 카메라와 에디터 카메라 제외, `kMaxExtraRenderView` 까지).
     *          보이는가는 카메라 출력의 `_visibilityObject`(모니터)가 사람이 보는 화면(주 시점과 화면 사각형 뷰) 어디엔가 `_minScreenSize` 이상으로 보이는가입니다
     *          (화면 크기는 애니메이션 LOD 와 같은 `AnimationLODUtil::computeScreenSize`). 가려짐은 보지 않습니다 — 렌더러에 가림 컬링 결과가 생기면
     *          언리얼 `WasRecentlyRendered` 처럼 "지난 프레임에 그려졌나" 로 바꿉니다. 그릴지는 `RenderViewScheduler`(갱신 주기, `RenderViewBudget`)가 정합니다.
     *          쉬는 뷰도 목록에 실어 렌더러가 그 뷰의 자원을 유지하게 합니다.
     */
    struct SW_API RenderViewCollector
    {
        /** @brief 주 시점 카메라의 출력 설정입니다(사각형, 배율, 끌 기능). 컷 표시는 여기서 읽고 지웁니다(`CameraComponent::consumeCut`). nullptr 이면 기본값입니다. */
        static RenderViewSettings makeMainSettings( CameraComponent* pMainCamera );
        /** @brief 출력 크기와 사각형의 화면 비율입니다(크기를 모르면 16:9). */
        static float32 computeAspect( const RenderViewSettings& settings, uint32 outputWidth, uint32 outputHeight );
        /** @brief 렌더 텍스처 뷰의 한 프레임 예산입니다(`-gv_renderViewBudget` 뷰 수, `-gv_renderViewMegapixelBudget` 픽셀 합). */
        static RenderViewBudget getDefaultBudget();
        /**
         * @brief 추가 뷰 요청을 @p outListView 에 채웁니다(지우고 채운다).
         * @param mainViewProj 주 시점의 뷰-투영(보이는가 판정의 절두체와 화면 크기).
         * @param mainPosition 주 시점의 눈 위치(화면 크기 판정).
         * @param outputWidth  주 출력의 크기(화면 사각형 뷰의 픽셀 크기를 정한다). 0 이면 화면 사각형 뷰는 건너뛴다.
         * @param now          스케줄러 시각(초).
         */
        static void collectExtraViews( const GameObjectManager& manager, const CameraComponent* pMainCamera, const float4x4& mainViewProj, const float3& mainPosition,
                                       uint32 outputWidth, uint32 outputHeight, float64 now, const RenderViewBudget& budget, RenderViewScheduler& scheduler,
                                       vector<RenderViewRequest>& outListView );
        /**
         * @brief 호스트 타깃 뷰(에디터 씬 뷰) 요청을 @p inoutListView 끝에 더합니다. 예산과 갱신 주기와 무관하게 매 프레임 그립니다.
         * @details 목록이 가득 찼으면(`kMaxExtraRenderView`) 마지막 추가 뷰 위치를 씁니다. 호스트 뷰는 빠지면 패널이 멈춰 보입니다.
         * @param viewMode 이 뷰의 보기 모드(씬 뷰 툴바, `FrameRenderer::getSceneViewMode`).
         */
        static void appendHostView( CameraComponent& camera, const HostViewTarget& target, RenderViewMode viewMode, vector<RenderViewRequest>& inoutListView );
        /** @brief 화면 사각형 뷰(분할 화면, PiP)를 뺍니다. 주 출력이 게임 화면이 아닐 때(씬 뷰만 그리는 프레임) 씁니다. */
        static void removeScreenRectViews( vector<RenderViewRequest>& inoutListView );
    };
} // namespace sw
