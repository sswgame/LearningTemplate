/**
 * @file InspectorPropertyUndo.h
 * @brief 인스펙터 프로퍼티 편집 Undo(ImGui 위젯 활성화 · 해제 기준). 위젯 상태만 읽어 그리드 모델에 넘기고, 값 글을 찍고 기록하는 일은 모델의 편집 구간이 한다(MVVM 계약).
 * @details 부르는 순서: 위젯 → 이 추적 → 쓰기 명령(`PropertyGridModel::setRowValue` …). 모델은 쓰기 직전에 방금 그린 위젯의 구간을 잇거나 연다 —
 *          추적을 쓰기 뒤에 부르면 끄는 동안의 쓰기가 프레임마다 한 번 쓰기 구간이 된다. 닫힌 구간은 그리드의 `endFrame` 이 남긴다.
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
        /**
         * @brief 방금 그린 값 행 위젯의 상태(활성 · 해제)를 @p model 에 넘깁니다 — 끄는 동안의 쓰기가 한 구간이고, 풀리면 한 단계로 오른다.
         * @details 위젯 바로 뒤, 쓰기 명령(`PropertyGridModel::setRowValue` · `setRowNumber`)보다 **먼저** 부른다. 누르기만 하고 나온 위젯은 아무것도 남기지 않는다(구간은 첫 쓰기가 연다).
         */
        static void trackRowEdit( PropertyGridModel& model );
        /**
         * @brief 방금 그린 확장 영역(`ImGui::EndGroup` 으로 닫은 그룹)을 위젯 하나처럼 추적합니다 — 안의 위젯이 활성화되면 @p scope 씬 오브젝트 @p objectID 의
         *        컴포넌트 @p componentID 프로퍼티 전부의 값 글을 찍어 구간을 열고, 풀리면 바뀐 프로퍼티만 남긴다(@p pLabel 은 되돌리기 이름).
         * @details 확장(Transform · Camera …)은 컴포넌트에 바로 써 모델의 쓰기 길을 지나지 않는다 — 유니티 `Undo.RecordObject` 처럼 대상 하나를 찍고 차이를 프로퍼티로 남긴다.
         *          ImGui 는 그룹 안의 활성 항목을 그룹의 상태로 넘긴다.
         */
        static void trackComponentRegion( PropertyGridModel& model, EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, const utf8* pLabel );
    };
} // namespace sw::editor
