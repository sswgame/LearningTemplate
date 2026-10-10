/**
 * @file EditorCommandGUI.h
 * @brief 커맨드 레지스트리를 ImGui 에 연결합니다(기본 커맨드 표 · 전역 단축키 · 메뉴 항목).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    struct EditorCommandShortcut;

    class EditorShortcutOverrides;
    /**
     * @class EditorCommandGUI
     * @brief 에디터 커맨드를 등록하고, 단축키를 처리하고, 메뉴 항목 하나를 그립니다.
     * @details 정의는 `registerDefaults` 의 표 하나이고, 메뉴바 · 단축키 처리 · 커맨드 팔레트는 그것을 읽기만 합니다 — 곳마다 따로
     *          적으면 라벨의 단축키 안내와 실제 처리가 어긋나고, 같은 조합을 두 곳이 처리합니다.
     *          커맨드를 하나 더하려면 표에 한 줄을 넣으면 메뉴 · 단축키 · 팔레트에 함께 나타납니다. 어느 메뉴의 어디에 놓일지도
     *          그 줄의 메뉴 경로 · 순서 칸이 정합니다.
     */
    class EditorCommandGUI
    {
    public:
        /** @brief 기본 커맨드 표를 레지스트리에 등록합니다. 중복 id·단축키는 오류로 로그합니다. */
        static void registerDefaults();
        /** @brief 커맨드 등록 줄(`SW_EDITOR_COMMAND`)의 세대가 바뀌었으면 표와 등록 줄로 레지스트리를 다시 만듭니다. 세대가 같으면 아무것도 하지 않습니다. */
        static void syncWithRegistry();
        /** @brief 등록 줄이 [@p pBegin, @p pEnd)(언로드되는 모듈 이미지) 안인 커맨드를 빼고 레지스트리를 다시 만듭니다. 뺀 수를 돌려줍니다. */
        static uint32 releaseCommandsWithin( const void* pBegin, const void* pEnd );
        /** @brief 전역 단축키 처리를 멈추거나 다시 켭니다(단축키 편집기가 조합을 받는 동안). */
        static void setHotkeysSuspended( bool bSuspended );
        /** @brief 사용자 단축키 덮어쓰기입니다(레지스트리를 다시 만들 때 입힌다). 바꾼 뒤에는 `registerDefaults` 로 다시 만든다. */
        static EditorShortcutOverrides& getShortcutOverrides();
        /** @brief `Saved/Editor/Shortcuts.json` 을 읽습니다. 파일이 없으면 false 입니다(처음 — 정상). */
        [[nodiscard]] static bool loadShortcutOverrides();
        /** @brief 이번 프레임에 눌린 키 하나와 지금 수정자로 조합을 만듭니다. 눌린 키가 없으면 false 입니다(단축키 편집기의 키 받기). */
        [[nodiscard]] static bool findPressedShortcut( EditorCommandShortcut& outShortcut );

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
