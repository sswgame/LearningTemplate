/**
 * @file EditorCommandGUI.h
 * @brief 커맨드 레지스트리를 ImGui 메뉴에 연결합니다(기본 커맨드 테이블 · 메뉴 항목). 단축키는 에디터 입력 맵(`EditorInputActions`)이 판정합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"

namespace sw::editor
{
    /**
     * @class EditorCommandGUI
     * @brief 에디터 커맨드를 등록하고 메뉴 항목을 그립니다.
     * @details 정의는 `registerDefaults` 의 테이블 하나이고, 메뉴바 · 커맨드 팔레트는 그것을 읽기만 합니다. 커맨드를 하나 더하려면 테이블에 한 줄을 넣으면
     *          메뉴 · 팔레트에 함께 나타납니다. 단축키는 그 줄의 입력 액션 이름이 가리키는 `editor.input.xml` 의 액션이 정하고, 메뉴의 단축키 글자도 그 맵에서 만든다 —
     *          키를 두 곳에 적어 라벨과 실제 처리가 어긋나지 않게.
     */
    class EditorCommandGUI
    {
    public:
        /** @brief 기본 커맨드 테이블을 레지스트리에 등록합니다. 중복 id · 같은 액션은 오류로 로그합니다. */
        static void registerDefaults();
        /** @brief 커맨드 등록 줄(`SW_EDITOR_COMMAND`)의 세대가 바뀌었으면 표와 등록 줄로 레지스트리를 다시 만듭니다. 세대가 같으면 아무것도 하지 않습니다. */
        static void syncWithRegistry();

        /** @brief 메뉴 경로의 부모가 `commandmenu::kMainMenuBar` 인 메뉴들을 테이블의 순서대로 메뉴바에 그립니다. */
        static void drawMainMenus();
        /**
         * @brief 커맨드 테이블이 이 경로에 놓은 항목을 순서 · 구분선대로 그립니다. `BeginMenu` · `BeginPopup` 안에서 부릅니다.
         * @details 그 경로에 놓인 커맨드가 없으면 아무것도 그리지 않습니다. 눌린 항목은 실행합니다.
         */
        static void drawMenuItems( string_view menuPath );
        /**
         * @brief 패널 도구줄에 명령 하나를 단추로 그립니다. 눌렸으면 다음 프레임 첫머리에 @p target 으로 실행하고(`queueCommand`) true 입니다.
         * @details 아이콘이 있는 명령은 정사각 아이콘 단추, 없으면 이름표 단추다. 툴팁은 "이름 — 설명 (단축키)" 이고, 꺼진 명령은 회색으로 그린다.
         *          이름표 · 꺼짐 · 단축키 글자를 명령 하나에서 읽으므로 메뉴 · 팔레트 · 도구줄이 서로 다른 글을 보이지 않는다
         *          (언리얼 FUICommandInfo 가 메뉴 항목과 툴바 단추를 같은 명령에서 만드는 것과 같다). 표식은 `toolbar.<명령 id>` 다.
         *          없는 명령 id 는 부른 쪽의 잘못이라 멈춘다.
         */
        static bool drawToolbarButton( string_view commandID, const EditorCommandTarget& target = EditorCommandTarget::none() );
    };
} // namespace sw::editor
