#include "pch.h"

#include "Engine/UI/Accessibility/UIAccessibilityTree.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    namespace
    {
        struct UIAccessibilityTreeInternal
        {
            /** @brief @p widget 와 자손을 읽는 순서로 훑어 노드를 덧붙입니다. @p parentNode 는 지금까지 가장 가까운 노드 조상(-1 이면 루트 아래)입니다. */
            static void visit( Widget& widget, int32 parentNode, uint32 screen, WidgetID focusedWidget, UIAccessibilitySnapshot& inoutSnapshot )
            {
                // 보이지 않거나 완전히 투명하면 자손까지 읽지 않는다(화면에 없는 것을 읽어 주면 안 된다).
                if ( widget.isVisible() == false || widget.getOpacity() <= 0.0f )
                    return;
                AccessibleInfo info{};
                widget.describeAccessible( info );
                int32 nodeIndex = parentNode;
                if ( info._role != AccessibleRole::None )
                {
                    // 이름이 없으면 레이블 위젯의 글이 이름이다(옵션 행: 슬라이더 옆 글).
                    const bool bLabelled = info._name.empty() && widget.getAccessibleLabelledBy().empty() == false && widget.getTree() != nullptr;
                    if ( bLabelled )
                    {
                        Widget* pLabel = widget.getTree()->findWidgetByName( widget.getAccessibleLabelledBy() );
                        if ( pLabel != nullptr )
                            info._name = collectLabelOf( *pLabel );
                    }
                    if ( widget.isEnabledInHierarchy() == false )
                        info._states |= AccessibleState::kDisabled;
                    if ( widget.supportsFocus() )
                        info._states |= AccessibleState::kFocusable;
                    const bool bFocused = focusedWidget != kInvalidWidgetID && widget.getID() == focusedWidget;
                    if ( bFocused )
                        info._states |= AccessibleState::kFocused;

                    const UIRect   bounds = widget.getGeometry().computeScreenBounds();
                    AccessibleNode node{};
                    node._widget     = widget.getID();
                    node._screen     = screen;
                    node._parent     = parentNode;
                    node._bounds     = float4{ bounds._left, bounds._top, bounds._right - bounds._left, bounds._bottom - bounds._top };
                    const bool bLeaf = info._bLeaf;
                    node._info       = std::move( info );
                    nodeIndex        = static_cast<int32>( inoutSnapshot._listNode.size() );
                    inoutSnapshot._listNode.push_back( std::move( node ) );
                    if ( bFocused )
                        inoutSnapshot._focusedNode = nodeIndex;
                    if ( bLeaf )
                        return;
                }
                PanelWidget* pPanel = widget.asPanel();
                if ( pPanel == nullptr )
                    return;
                for ( uint32 childIndex = 0; childIndex < pPanel->getChildCount(); ++childIndex )
                {
                    visit( *pPanel->getChild( childIndex ), nodeIndex, screen, focusedWidget, inoutSnapshot );
                }
            }

            static string collectLabelOf( Widget& root )
            {
                string label;
                appendLabel( root, label );
                return label;
            }

            /**
             * @brief @p widget 아래 글 위젯의 보이는 글을 트리 순서로 이어 붙입니다. 글이 아닌 위젯은 `describeAccessible` 을 부르지 않고 자식만 걷는다 —
             *        단추의 이름이 이 함수를 부르므로, 단추 자신에게 `describeAccessible` 을 부르면 끝없이 돈다.
             */
            static void appendLabel( Widget& widget, string& inoutLabel )
            {
                if ( widget.isVisible() == false )
                    return;
                if ( castTo<TextWidget>( &widget ) != nullptr )
                {
                    AccessibleInfo info{};
                    widget.describeAccessible( info );
                    if ( info._role == AccessibleRole::Text && info._name.empty() == false )
                    {
                        if ( inoutLabel.empty() == false )
                            inoutLabel += ' ';
                        inoutLabel += info._name;
                    }
                    return;
                }
                PanelWidget* pPanel = widget.asPanel();
                if ( pPanel == nullptr )
                    return;
                for ( uint32 childIndex = 0; childIndex < pPanel->getChildCount(); ++childIndex )
                {
                    appendLabel( *pPanel->getChild( childIndex ), inoutLabel );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 UIAccessibilitySnapshot::findNode( uint32 screen, WidgetID widget ) const
    {
        for ( size_t index = 0; index < _listNode.size(); ++index )
        {
            if ( _listNode[index]._screen == screen && _listNode[index]._widget == widget )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void UIAccessibilityTree::appendTree( const WidgetTree& tree, uint32 screen, WidgetID focusedWidget, UIAccessibilitySnapshot& inoutSnapshot )
    {
        Widget* pRoot = tree.getRoot();
        if ( pRoot != nullptr )
            UIAccessibilityTreeInternal::visit( *pRoot, -1, screen, focusedWidget, inoutSnapshot );
    }

    string UIAccessibilityTree::collectLabel( Widget& root )
    {
        // 글 위젯이 자기 이름을 알려 주는 쪽이 `describeAccessible` 이라 같은 길로 읽는다(리치 텍스트 표기도 벗겨진다).
        return UIAccessibilityTreeInternal::collectLabelOf( root );
    }
} // namespace sw
