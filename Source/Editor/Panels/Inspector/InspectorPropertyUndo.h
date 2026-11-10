/**
 * @file InspectorPropertyUndo.h
 * @brief 인스펙터 프로퍼티 편집 Undo(ImGui 위젯 활성화 · 해제 기준). 위젯 상태만 읽어 넘기고, 스냅샷 · 기록은 Model 의 `InspectorEditRecorder` 가 한다(MVVM 계약).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    struct PropertyValueRow;
} // namespace sw::editor

namespace sw::editor
{
    class InspectorPropertyUndo
    {
    public:
        /**
         * @brief 방금 그린 값 행 @p row 의 위젯 편집(활성 ~ 해제)을 되돌리기 한 단계로 남깁니다. 이름은 행의 표시 이름이다(위젯 라벨 "##value" 를 넘길 수 없게 행을 받는다).
         * @details 위젯 바로 뒤, 쓰기 명령(`PropertyGridModel::setRowValue` · `setRowNumber`)보다 **먼저** 부른다 — 활성화된 프레임에 편집 전을 찍고 나서 쓴다.
         */
        static void trackRowEdit( const PropertyValueRow& row );
        /**
         * @brief 방금 그린 항목(위젯이나 `ImGui::EndGroup` 으로 닫은 그룹)의 편집을 위젯 하나처럼 되돌리기에 남깁니다.
         * @details 그룹 안의 위젯이 활성화되면 편집 전을 찍고, 편집을 마치면 기록합니다(ImGui 는 그룹 안의 활성 항목을 그룹의 상태로 넘긴다).
         *          인스펙터의 확장 구역(Transform · Camera …)이 이것으로 위젯마다 따로 부르지 않고 한 번에 남깁니다.
         */
        static void trackLastItem( const utf8* pPropertyLabel );
        /**
         * @brief 이번 프레임에 풀린 편집을 되돌리기에 남깁니다. 인스펙터가 그리기를 마친 뒤 부릅니다.
         * @details 위젯이 풀린 순간이 아니라 그 뒤에 기록하는 까닭: 다중 선택은 편집 통지(위젯 뒤)에서 값을 나머지 오브젝트에 입힌다. 풀린 순간에 "뒤" 스냅샷을 뜨면 그 입히기가 빠진다.
         */
        static void commitFinishedEdits();
    };
} // namespace sw::editor
