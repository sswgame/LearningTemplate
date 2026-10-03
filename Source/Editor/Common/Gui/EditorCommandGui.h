/**
 * @file EditorCommandGui.h
 * @brief 커맨드 레지스트리를 ImGui 에 연결합니다(기본 커맨드 표 · 전역 단축키 · 메뉴 항목).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    /**
     * @class EditorCommandGui
     * @brief 에디터 커맨드를 등록하고, 단축키를 처리하고, 메뉴 항목 하나를 그립니다.
     * @details 예전에는 커맨드 하나가 **세 곳**에 따로 적혀 있었습니다. `EditorMenuBar` 의 메뉴 항목, 같은 파일
     *          `processHotkeys` 의 if 사다리, `CommandPalettePopup` 의 정적 목록입니다. 그래서 라벨의 단축키 안내와 실제
     *          처리가 어긋나고(F7 은 어디에도 적혀 있지 않았고 Ctrl+Shift+Z 는 Inspector 에서만 먹었습니다), Ctrl+Z 는 두
     *          곳이 처리해 두 번 되돌렸습니다. 이제 정의는 `registerDefaults` 의 표 하나이고, 세 곳은 그것을 읽기만 합니다.
     *          커맨드를 하나 더하려면 표에 한 줄을 넣으면 메뉴 · 단축키 · 팔레트에 함께 나타납니다. 어느 메뉴의 어디에 놓일지도
     *          그 줄의 메뉴 경로 · 순서 칸이 정합니다.
     */
    class EditorCommandGui
    {
    public:
        /** @brief 기본 커맨드 표를 레지스트리에 등록합니다. 중복 id·단축키는 오류로 로그합니다. */
        static void registerDefaults();

        /** @brief 등록된 단축키를 검사해 맞는 커맨드를 실행합니다. 텍스트 입력 중에는 아무것도 하지 않습니다. */
        static void processHotkeys();

        /** @brief 메뉴 경로의 부모가 `commandmenu::kMainMenuBar` 인 메뉴들을 표의 순서대로 메뉴바에 그립니다. */
        static void drawMainMenus();
        /**
         * @brief 커맨드 표가 이 경로에 놓은 항목을 순서 · 구분선대로 그립니다. `BeginMenu` · `BeginPopup` 안에서 부릅니다.
         * @details 그 경로에 놓인 커맨드가 없으면 아무것도 그리지 않습니다. 눌린 항목은 실행합니다.
         */
        static void drawMenuItems( string_view menuPath );
    };
} // namespace sw::editor
