/**
 * @file SceneViewModel.h
 * @brief 씬 뷰 창의 ViewModel — 카메라 비행이 쓰는 프레임 경과와 디버그 그리기 범주 켜고 끄기입니다. ImGui 를 모릅니다.
 * @details 렌더 타깃은 `EditorViewSurface`, 카메라 · 오버레이는 뷰포트 클라이언트가 든다. 범주 켜기는 보기 설정이라 되돌리기에 남지 않는다. `docs/agents/EditorUI.md` MVVM 계약.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    struct float3;
} // namespace sw

namespace sw::editor
{
    /** @brief 디버그 그리기 범주 하나입니다(값만). */
    struct SceneViewDebugCategoryRow
    {
        hashed_string _name;
        bool          _bEnabled{ false };
    };

    /** @brief 개발 명령이 씬 뷰 창에 넣는 요청의 종류입니다. 카메라는 뷰포트 클라이언트(View)가 들어 창이 그릴 때 꺼내 적용한다(오버레이 배치는 `SceneViewOverlayLayoutStore` 를 바로 고친다). */
    enum class SceneViewRequestKind : uint8
    {
        CameraReset,
        CameraSet,
    };

    /** @brief 씬 뷰 창 요청 하나입니다(값만). */
    struct SceneViewRequest
    {
        SceneViewRequestKind _kind{ SceneViewRequestKind::CameraReset };
        float32              _arrPose[5]{}; ///< `CameraSet` — x · y · z · 피치 · 요(도)
    };

    /** @brief 씬 뷰 창의 ViewModel 입니다(상태 없음 — 개발 명령의 요청은 에디터 범위 상태에 쌓인다). */
    struct SceneViewModel
    {
        /** @brief 엔진 프레임의 실제 경과(초 — 시간 배율 · 정지와 무관)입니다. 고정 프레임 시간(`-gv_fixedFrameDelta`)으로 도는 시나리오에서 카메라 비행이 결정적이다. */
        static float32 getUnscaledDeltaTime();
        /** @brief 디버그 그리기 큐에 나온 범주와 켜짐을 채웁니다(@p outListRow 를 비우고 채운다). */
        static void gatherDebugCategories( vector<SceneViewDebugCategoryRow>& outListRow );
        /** @brief 디버그 그리기 범주 @p category 를 켜거나 끕니다. */
        static void setDebugCategoryEnabled( const hashed_string& category, bool bEnabled );
        /** @brief 씬 뷰 카메라(에디터 카메라 — 플레이 중에도)의 월드 위치를 @p outPosition 에 담습니다. 활성 씬에 에디터 카메라가 없으면 false 입니다(감시 값이 읽는다). */
        [[nodiscard]] static bool findCameraPosition( float3& outPosition );
        /** @brief 씬 뷰 툴바의 보기 모드 번호(`RenderViewMode`)를 @p outMode 에 담습니다. 렌더러가 없으면(필수 서비스 — 버그) 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool findViewMode( uint32& outMode );
        /** @brief 작업 공간의 기즈모 조작(0 이동, 1 회전, 2 크기)입니다. */
        static int32 getGizmoOperation();
        /** @brief 개발 명령(`camera.*` · `overlay.*`)의 요청을 쌓습니다. 창이 다음에 그릴 때 꺼낸다(`takeRequest`). */
        static void postRequest( SceneViewRequest request );
        /** @brief 쌓인 요청 하나를 앞에서 꺼냅니다. 없으면 false 입니다. */
        [[nodiscard]] static bool takeRequest( SceneViewRequest& outRequest );
    };
} // namespace sw::editor
