/**
 * @file EditorComponentMenu.h
 * @brief "Add Component" 목록(검색 칸 + 묶음별 항목)입니다. Hierarchy 의 오른쪽 클릭 메뉴와 인스펙터의 Add Component 단추가 같이 씁니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorComponentMenu
     * @brief 유니티 Inspector 의 Add Component 검색 창 · 언리얼 Details 의 Add 단추와 같은 목록입니다. 메뉴(`BeginMenu`)나 팝업 안에서 부릅니다.
     */
    struct SW_EDITOR_API EditorComponentMenu
    {
        /**
         * @brief 검색 칸과 컴포넌트 종류 목록을 그립니다. 메뉴에 숨긴 타입(`HideInMenu`)은 빠집니다.
         * @param search 검색어 버퍼(부르는 쪽이 들고 있다 — 열 때마다 비울지는 부르는 쪽이 정한다).
         * @param pMarkPrefix 이름표 머리(`<머리>.search` · `<머리>.<타입>`).
         * @param bFocusSearch true 면 검색 칸에 초점을 둔다(팝업을 막 열었을 때). 그때 Enter 는 검색에 맞는 첫 항목을 고른다.
         * @param outbFailed 더하기가 실패했으면 true 가 된다(부르는 쪽이 알림 창을 연다).
         * @return 컴포넌트를 더했으면 true 입니다(되돌리기에 남는다 — `EditorSceneCommands::addComponent`).
         */
        static bool drawAddComponentList( GameObject* pObj, fixed_string<constant::kMaxBuffer64>& search, const utf8* pMarkPrefix, bool bFocusSearch, bool& outbFailed );
    };
} // namespace sw::editor
