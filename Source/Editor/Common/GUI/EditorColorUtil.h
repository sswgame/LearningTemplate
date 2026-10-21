/**
 * @file EditorColorUtil.h
 * @brief `sw::Color` 와 ImGui 색 값(`ImVec4` · `ImU32`) 사이의 변환을 한곳에 둡니다.
 * @details 에디터 코드는 색을 `Color` 로 보관하고, ImGui 에 넘기는 지점에서만 여기를 거칩니다. `toImU32` 는 `IM_COL32` 와 같은 값(`Color::toAbgr`)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Color.h"

#include "Editor/Common/EditorExports.h"

struct ImVec4;

namespace sw::editor
{
    /**
     * @struct EditorColorUtil
     * @brief `Color` ↔ ImGui 색 변환입니다.
     */
    struct SW_EDITOR_API EditorColorUtil
    {
        /** @brief `ImVec4`(x=R, y=G, z=B, w=A)로 바꿉니다. 채널은 자르지 않습니다. */
        static ImVec4 toImVec4( const Color& color );
        /** @brief `ImU32`(`IM_COL32` 와 같은 `0xAABBGGRR`)로 패킹합니다. 채널은 0~1 로 자릅니다. */
        static uint32 toImU32( const Color& color );
        /** @brief `ImVec4` 에서 만듭니다(ImGui 스타일 색을 읽을 때). */
        static Color fromImVec4( const ImVec4& value );
    };
} // namespace sw::editor
