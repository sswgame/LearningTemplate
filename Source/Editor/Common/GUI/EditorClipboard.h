/**
 * @file EditorClipboard.h
 * @brief 에디터의 OS 클립보드 쓰기 창구입니다. ImGui 를 모르는 쪽(ViewModel · 명령)이 부른다 — 구현은 ImGui 플랫폼 백엔드의 클립보드를 쓴다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief OS 클립보드에 글을 씁니다(언리얼 `FPlatformApplicationMisc::ClipboardCopy` 자리). */
    struct SW_EDITOR_API EditorClipboard
    {
        /** @brief @p text 를 클립보드에 둡니다. ImGui 컨텍스트가 없으면(헤드리스 테스트) 아무것도 하지 않습니다. */
        static void setText( string_view text );
    };
} // namespace sw::editor
