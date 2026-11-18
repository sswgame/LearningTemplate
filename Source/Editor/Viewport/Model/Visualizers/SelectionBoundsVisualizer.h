/**
 * @file SelectionBoundsVisualizer.h
 * @brief 선택 상자 시각화(뷰포트 시각화 하나, 등록은 cpp)가 바깥에 보이는 상태 — 지난 프레임에 그린 상자 수, 씬 뷰가 알린 호버 오브젝트입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 선택 상자 시각화가 지난 프레임에 그린 상자 수입니다. 프로브 `Editor.SelectionOutlineBoxes` 가 읽습니다. */
    struct SW_EDITOR_API SelectionBoundsVisualizer
    {
        static uint32 getDrawnBoxCount();
        /** @brief 씬 뷰가 이번 프레임의 호버 오브젝트를 알립니다. 0 이면 없습니다. 선택한 오브젝트이면 그리지 않습니다. */
        static void   setHoveredObjectID( uint64 objectID );
        static uint64 getHoveredObjectID();
    };
} // namespace sw::editor
