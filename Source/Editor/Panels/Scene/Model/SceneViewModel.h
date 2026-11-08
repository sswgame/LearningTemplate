/**
 * @file SceneViewModel.h
 * @brief 씬 뷰 창의 ViewModel — 카메라 비행이 쓰는 프레임 경과와 디버그 그리기 범주 켜고 끄기입니다. ImGui 를 모릅니다.
 * @details 렌더 타깃은 `EditorViewSurface`, 카메라 · 오버레이는 뷰포트 클라이언트가 든다. 범주 켜기는 보기 설정이라 되돌리기에 남지 않는다. `docs/agents/EditorUI.md` MVVM 계약.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw::editor
{
    /** @brief 디버그 그리기 범주 하나입니다(값만). */
    struct SceneViewDebugCategoryRow
    {
        hashed_string _name;
        bool          _bEnabled{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다)
    };

    /** @brief 씬 뷰 창의 ViewModel 입니다(상태 없음). */
    struct SceneViewModel
    {
        /** @brief 엔진 프레임의 실제 경과(초 — 시간 배율 · 정지와 무관)입니다. 고정 프레임 시간(`-gv_fixedFrameDelta`)으로 도는 시나리오에서 카메라 비행이 결정적이다. */
        static float32 getUnscaledDeltaTime();
        /** @brief 디버그 그리기 큐에 나온 범주와 켜짐을 채웁니다(@p outListRow 를 비우고 채운다). */
        static void gatherDebugCategories( vector<SceneViewDebugCategoryRow>& outListRow );
        /** @brief 디버그 그리기 범주 @p category 를 켜거나 끕니다. */
        static void setDebugCategoryEnabled( const hashed_string& category, bool bEnabled );
    };
} // namespace sw::editor
