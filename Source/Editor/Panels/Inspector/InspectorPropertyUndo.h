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
    };
} // namespace sw::editor
