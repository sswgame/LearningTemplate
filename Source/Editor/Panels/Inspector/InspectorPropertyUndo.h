/**
 * @file InspectorPropertyUndo.h
 * @brief 인스펙터 프로퍼티 편집 Undo 의 View 쪽 — 행(과 확장 영역)을 그리는 동안 활성 위젯을 그렸는지만 읽어 그리드 모델에 넘깁니다(MVVM 계약).
 * @details 값 글을 찍고 기록하는 일은 모델의 편집 구간이 한다(`PropertyEditSpanSet`): 쓰기가 그 행의 구간을 열고, 행이 활성인 동안 이어지고, 쓰지도 활성이지도 않은
 *          프레임의 마감(`EditorPropertyGrid::endFrame`)이 한 단계로 올린다. 그리기 확장(`IInspectorProperty`)은 되돌리기를 모른다 — 쓰기 명령만 부른다.
 *          판정이 행 단위라 위젯 여럿으로 된 행(벡터 축 셋 · 색 고르기)도 행 하나다. 예전에는 행의 마지막 위젯만 보아 X 축을 끌면 기록이 빠졌다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    enum class EditorPropertyGridScope : uint8;

    class PropertyGridModel;
} // namespace sw::editor

namespace sw::editor
{
    class InspectorPropertyUndo
    {
    public:
        /** @brief 지금 활성 위젯이 이번 프레임에 이미 그려졌으면 true 입니다. 행을 그리기 **전에** 읽어 `trackRow` 에 넘긴다. */
        [[nodiscard]] static bool isActiveItemDrawn();
        /** @brief 행 @p rowID 를 그린 뒤 부릅니다 — 그리기 전(@p bActiveDrawnBefore)에는 없던 활성 위젯이 그 사이 그려졌으면 그 행의 구간을 잇는다. */
        static void trackRow( PropertyGridModel& model, uint32 rowID, bool bActiveDrawnBefore );
        /**
         * @brief 방금 그린 확장 영역(`ImGui::EndGroup` 으로 닫은 그룹)을 행 하나처럼 추적합니다 — 안의 위젯이 활성이면 @p scope 씬 오브젝트 @p objectID 의
         *        컴포넌트 @p componentID 구간을 잇거나(처음이면 그 컴포넌트 프로퍼티 전부를 찍어) 엽니다(@p pLabel 은 되돌리기 이름).
         * @details 확장(Transform · Camera …)은 컴포넌트에 바로 써 모델의 쓰기 길을 지나지 않는다 — 유니티 `Undo.RecordObject` 처럼 대상 하나를 찍고 차이를 프로퍼티로 남긴다.
         *          ImGui 는 그룹 안의 활성 항목을 그룹의 상태로 넘긴다.
         */
        static void trackComponentRegion( PropertyGridModel& model, EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, const utf8* pLabel );
    };
} // namespace sw::editor
