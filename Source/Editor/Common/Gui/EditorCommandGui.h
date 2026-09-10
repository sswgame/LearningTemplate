/**
 * @file EditorCommandGui.h
 * @brief 커맨드 레지스트리의 ImGui 표면 — 기본 커맨드 표 · 전역 단축키 · 메뉴 항목
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    /**
     * @class EditorCommandGui
     * @brief 에디터 커맨드를 등록하고, 단축키를 처리하고, 메뉴 항목 하나를 그립니다.
     * @details 예전에는 커맨드 하나가 **세 곳**에 따로 적혀 있었습니다 — `EditorMenuBar` 의 메뉴 항목,
     *          같은 파일의 `processHotkeys` 키 사다리, `CommandPalettePopup` 의 정적 목록. 그래서
     *          라벨의 단축키 안내와 실제 처리가 어긋나고(F7 은 어디에도 안 적혀 있었고 Ctrl+Shift+Z 는
     *          Inspector 에서만 먹었습니다), Ctrl+Z 는 두 곳이 처리해 두 번 되돌렸습니다.
     *          이제 정의는 `registerDefaults` 의 표 하나이고, 세 표면은 그것을 읽기만 합니다 —
     *          커맨드를 하나 더하려면 표에 한 줄을 넣으면 메뉴·단축키·팔레트에 함께 나타납니다.
     */
    class EditorCommandGui
    {
    public:
        /** @brief 기본 커맨드 표를 레지스트리에 등록합니다. 중복 id·단축키는 오류로 로그합니다. */
        static void registerDefaults();

        /** @brief 등록된 단축키를 검사해 맞는 커맨드를 실행합니다. 텍스트 입력 중에는 아무것도 하지 않습니다. */
        static void processHotkeys();

        /** @brief 커맨드 id 하나를 메뉴 항목으로 그립니다. 눌렸으면 실행하고 true입니다. */
        static bool drawMenuItem( string_view commandId );
    };
} // namespace sw::editor
