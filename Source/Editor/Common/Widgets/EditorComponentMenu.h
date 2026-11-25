/**
 * @file EditorComponentMenu.h
 * @brief "Add Component" 목록(검색 필드 + 그룹별 항목)입니다. Hierarchy 의 오른쪽 클릭 메뉴와 인스펙터의 Add Component 단추가 같이 씁니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @struct EditorComponentMenu
     * @brief 유니티 Inspector 의 Add Component 검색 창 · 언리얼 Details 의 Add 단추와 같은 목록입니다. 메뉴(`BeginMenu`)나 팝업 안에서 부릅니다.
     */
    struct SW_EDITOR_API EditorComponentMenu
    {
        /**
         * @brief 검색 칸과 컴포넌트 종류 목록을 그리고 고른 종류 이름을 돌려줍니다. 메뉴에 숨긴 타입(`HideInMenu`)은 빠집니다. 오브젝트를 받지 않는다 —
         *        더하기는 부르는 쪽 ViewModel 명령이 한다(`HierarchyViewModel::addComponent` · `InspectorViewModel::addComponent`, 되돌리기 한 단계).
         * @param search 검색어 버퍼(부르는 쪽이 보관하고 있다 — 열 때마다 비울지는 부르는 쪽이 정한다).
         * @param pMarkPrefix 이름 머리(`<머리>.search` · `<머리>.<타입>`).
         * @param bFocusSearch true 면 검색 칸에 초점을 둔다(팝업을 막 열었을 때). 그때 Enter 는 검색에 맞는 첫 항목을 고른다.
         * @return 고른 종류 이름입니다. 고르지 않았으면 빈 이름입니다.
         */
        [[nodiscard]] static hashed_string drawComponentTypeList( fixed_string<constant::buffer::kMax64>& search, const utf8* pMarkPrefix, bool bFocusSearch );
    };
} // namespace sw::editor
