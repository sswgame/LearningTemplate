/**
 * @file InspectorPropertyUndo.h
 * @brief 인스펙터 프로퍼티 편집 Undo (ImGui 활성화/해제 기준)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    class InspectorPropertyUndo
    {
    public:
        static void trackPod( void* pData, size_t size, const utf8* pLabel );
        static void trackString( string* pPtr, const utf8* pLabel );
        /**
         * @brief 방금 그린 항목(위젯이나 `ImGui::EndGroup` 으로 닫은 묶음)의 편집을 위젯 하나처럼 되돌리기에 남깁니다.
         * @details 묶음 안의 위젯이 활성화되면 편집 전을 찍고, 편집을 마치면 기록합니다(ImGui 는 묶음 안의 활성 항목을 묶음의 상태로 넘긴다).
         *          인스펙터의 확장 구역(Transform · Camera …)이 이것으로 위젯마다 따로 부르지 않고 한 번에 남깁니다.
         */
        static void trackLastItem( const utf8* pLabel );
    };
} // namespace sw::editor
