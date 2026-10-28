/**
 * @file UIAccessibilityTree.h
 * @brief 화면의 위젯 트리에서 스크린 리더가 읽을 접근성 트리를 뽑습니다(이름 · 역할 · 상태 · 화면 사각형을 가진 노드의 읽는 순서 목록).
 * @details 위젯 트리가 아니라 **스냅샷**이다 — 브리지(플랫폼 접근성 API)가 프레임 시점의 값을 읽고, 노드는 (화면 핸들, 위젯 번호)로 위젯을 다시 찾아 스스로 값을 새로 읽을 수도 있다.
 *          보이는 위젯만 들어가고(`Collapsed` · `Hidden` · 불투명도 0 은 자손까지 뺀다), 역할이 `None` 인 위젯은 노드를 만들지 않고 자손이 가장 가까운 노드 조상 아래로 올라온다.
 *          단추처럼 안의 글이 이름이 되는 위젯은 잎이다(`describeAccessible` 이 `_bLeaf` 를 켠다) — 안의 글을 노드로 또 내지 않는다. 읽는 순서는 트리 순서(자식 순서)다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    class Widget;
    class WidgetTree;

    /** @brief 접근성 트리의 노드 하나입니다. */
    struct AccessibleNode
    {
        WidgetID       _widget{ kInvalidWidgetID }; ///< 위젯 번호입니다(화면 트리 안에서 유일).
        uint32         _screen{ 0 };                ///< 위젯이 속한 화면 핸들입니다(`UIScreenHandle`, 0 은 화면 없음).
        int32          _parent{ -1 };               ///< 가장 가까운 노드 조상의 목록 번호입니다(-1 이면 접근성 루트 바로 아래).
        float4         _bounds{};                   ///< 화면 사각형입니다(x, y, 너비, 높이, UI 단위·뷰포트 원점 기준).
        AccessibleInfo _info{};
    };
} // namespace sw

namespace sw
{
    /** @brief 한 번 뽑은 접근성 트리입니다. 노드는 읽는 순서이고 부모가 자식보다 앞에 온다. */
    struct SW_API UIAccessibilitySnapshot
    {
        vector<AccessibleNode> _listNode{};
        int32                  _focusedNode{ -1 }; ///< 포커스 위젯의 노드 번호입니다(없으면 -1).

        void clear()
        {
            _listNode.clear();
            _focusedNode = -1;
        }
        /** @brief (화면, 위젯)의 노드 번호입니다. 없으면 -1 입니다. */
        int32 findNode( uint32 screen, WidgetID widget ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIAccessibilityTree
     * @brief 접근성 트리를 뽑는 도구입니다.
     */
    struct SW_API UIAccessibilityTree
    {
        /**
         * @brief @p tree 의 보이는 위젯을 읽는 순서로 @p inoutSnapshot 뒤에 덧붙입니다.
         * @param screen          트리가 속한 화면 핸들입니다(노드에 적는다).
         * @param focusedWidget   이 트리에서 포커스를 가진 위젯입니다(없으면 `kInvalidWidgetID`). 그 노드에 포커스 상태가 켜지고 `_focusedNode` 가 정해진다.
         */
        static void appendTree( const WidgetTree& tree, uint32 screen, WidgetID focusedWidget, UIAccessibilitySnapshot& inoutSnapshot );
        /** @brief @p root 아래 글 위젯의 보이는 글을 트리 순서로 공백 하나로 이어 돌려줍니다(단추의 이름). 글이 없으면 빈 문자열입니다. */
        [[nodiscard]] static string collectLabel( Widget& root );
    };
} // namespace sw
