#include "pch.h"

#include "Engine/UI/Base/UiEventRouter.h"

#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"

namespace sw
{
    namespace
    {
        struct UiEventRouterInternal
        {
            static bool hitTestRecursive( const Widget& widget, const float2& screenPoint, vector<WidgetId>& inoutListWidget )
            {
                const WidgetVisibility visibility = widget.getVisibility();
                if ( visibility == WidgetVisibility::Collapsed || visibility == WidgetVisibility::Hidden || visibility == WidgetVisibility::HitTestInvisible )
                    return false;
                float2 local{};
                if ( widget.getGeometry().inverseTransformPoint( screenPoint, local ) == false )
                    return false;
                const bool         bInside = widget.getGeometry().containsLocal( local );
                const PanelWidget* pPanel  = castTo<const PanelWidget>( &widget );
                // 자르는 패널 밖의 점은 자식도 받지 않는다(스크롤 영역 밖으로 삐져나온 항목).
                const bool bChildrenReachable = pPanel != nullptr && ( pPanel->clipsChildren() == false || bInside );
                inoutListWidget.push_back( widget.getId() );
                if ( bChildrenReachable && pPanel->hasCustomPaintOrder() )
                {
                    // z 순서를 둔 패널(캔버스) — 그리기 순서의 역순.
                    vector<uint32> listOrder;
                    pPanel->collectPaintOrder( listOrder );
                    for ( uint32 order = static_cast<uint32>( listOrder.size() ); order > 0; --order )
                    {
                        if ( hitTestRecursive( *pPanel->getChild( listOrder[order - 1] ), screenPoint, inoutListWidget ) )
                            return true;
                    }
                }
                else if ( bChildrenReachable )
                {
                    for ( uint32 index = pPanel->getChildCount(); index > 0; --index ) // 나중에 그린 자식이 위다
                    {
                        if ( hitTestRecursive( *pPanel->getChild( index - 1 ), screenPoint, inoutListWidget ) )
                            return true;
                    }
                }
                const bool bSelfHit = bInside && visibility == WidgetVisibility::Visible;
                if ( bSelfHit )
                    return true;
                inoutListWidget.pop_back();
                return false;
            }

            /** @brief 경로를 따라 터널링 → 버블링으로 사건을 보냅니다. @p Handler 는 그 사건을 받는 Widget 의 가상 함수입니다. */
            template <typename EventType, UiReply ( Widget::*Handler )( const EventType&, UiRoutePhase )>
            static UiReply route( const WidgetTree& tree, const UiWidgetPath& path, const EventType& event, WidgetId& outHandler )
            {
                outHandler = kInvalidWidgetId;
                // 경로는 시작할 때 고정한다 — 처리 중에 위젯이 떨어지면(목록을 지우는 핸들러) 그 번호만 건너뛴다.
                const vector<WidgetId> listWidget = path._listWidget;
                const uint32           count      = static_cast<uint32>( listWidget.size() );
                for ( uint32 index = 0; index < count; ++index )
                {
                    Widget* pWidget = tree.findWidgetById( listWidget[index] );
                    if ( pWidget == nullptr || pWidget->isEnabledInHierarchy() == false )
                        continue;
                    const UiReply reply = ( pWidget->*Handler )( event, UiRoutePhase::Tunnel );
                    if ( reply.isHandled() )
                    {
                        outHandler = listWidget[index];
                        return reply;
                    }
                }
                for ( uint32 index = count; index > 0; --index )
                {
                    Widget* pWidget = tree.findWidgetById( listWidget[index - 1] );
                    if ( pWidget == nullptr || pWidget->isEnabledInHierarchy() == false )
                        continue;
                    const UiReply reply = ( pWidget->*Handler )( event, UiRoutePhase::Bubble );
                    if ( reply.isHandled() )
                    {
                        outHandler = listWidget[index - 1];
                        return reply;
                    }
                }
                return UiReply::makeUnhandled();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool UiWidgetPath::contains( WidgetId id ) const
    {
        for ( const WidgetId value : _listWidget )
        {
            if ( value == id )
                return true;
        }
        return false;
    }

    bool UiEventRouter::hitTest( const WidgetTree& tree, const float2& screenPoint, UiWidgetPath& outPath )
    {
        outPath._listWidget.clear();
        const Widget* pRoot = tree.getRoot();
        return pRoot != nullptr && UiEventRouterInternal::hitTestRecursive( *pRoot, screenPoint, outPath._listWidget );
    }

    bool UiEventRouter::makePathTo( const WidgetTree& tree, WidgetId widget, UiWidgetPath& outPath )
    {
        outPath._listWidget.clear();
        const Widget* pWidget = tree.findWidgetById( widget );
        if ( pWidget == nullptr )
            return false;
        for ( const Widget* pCurrent = pWidget; pCurrent != nullptr; pCurrent = pCurrent->getParent() )
        {
            outPath._listWidget.push_back( pCurrent->getId() );
        }
        // 잎 → 뿌리로 모았다 — 뒤집는다.
        const uint32 count = static_cast<uint32>( outPath._listWidget.size() );
        for ( uint32 index = 0; index < count / 2; ++index )
        {
            const WidgetId swapped                 = outPath._listWidget[index];
            outPath._listWidget[index]             = outPath._listWidget[count - 1 - index];
            outPath._listWidget[count - 1 - index] = swapped;
        }
        return true;
    }

    UiReply UiEventRouter::routePointerEvent( const WidgetTree& tree, const UiWidgetPath& path, const UiPointerEvent& event, WidgetId& outHandler )
    {
        return UiEventRouterInternal::route<UiPointerEvent, &Widget::onPointerEvent>( tree, path, event, outHandler );
    }

    UiReply UiEventRouter::routeActionEvent( const WidgetTree& tree, const UiWidgetPath& path, const UiActionEvent& event, WidgetId& outHandler )
    {
        return UiEventRouterInternal::route<UiActionEvent, &Widget::onActionEvent>( tree, path, event, outHandler );
    }
} // namespace sw
