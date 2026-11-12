#include "pch.h"

#include "Engine/UI/WidgetTree/UIWidgetPath.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    namespace
    {
        struct UIEventRouterInternal
        {
            static bool hitTestRecursive( const Widget& widget, const float2& screenPoint, vector<WidgetID>& inoutListWidget )
            {
                const WidgetVisibility visibility      = widget.getVisibility();
                const bool             bNotHitTestable = visibility == WidgetVisibility::Collapsed || visibility == WidgetVisibility::Hidden || visibility == WidgetVisibility::HitTestInvisible;
                if ( bNotHitTestable )
                    return false;
                float2 local{};
                if ( widget.getGeometry().inverseTransformPoint( screenPoint, local ) == false )
                    return false;
                const bool         bInside = widget.getGeometry().containsLocal( local );
                const PanelWidget* pPanel  = castTo<const PanelWidget>( &widget );
                // 자르는 패널 밖의 점은 자식도 받지 않는다(스크롤 영역 밖으로 삐져나온 항목).
                const bool bChildrenReachable = pPanel != nullptr && ( pPanel->shouldClipChildren() == false || bInside );
                inoutListWidget.push_back( widget.getID() );
                if ( bChildrenReachable && pPanel->hasCustomPaintOrder() )
                {
                    // z 순서를 쓰는 패널(캔버스)은 그리기 순서의 역순으로 본다.
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
                    for ( uint32 index = pPanel->getChildCount(); index > 0; --index ) // 나중에 그린 자식이 위에 있다
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

            /** @brief 넓힌 상자 안에서 중심이 가장 가까운 조작을 찾습니다(`hitTestTouch`). 가려진 · 꺼진 위젯은 보지 않는다. */
            static void findNearestTouchTarget( const Widget& widget, const float2& screenPoint, float32 minTargetSize, const Widget*& ioBest, float32& ioBestDistance )
            {
                const WidgetVisibility visibility = widget.getVisibility();
                if ( visibility == WidgetVisibility::Collapsed || visibility == WidgetVisibility::Hidden || visibility == WidgetVisibility::HitTestInvisible )
                    return;
                const bool bTarget = visibility == WidgetVisibility::Visible && widget.supportsFocus() && widget.isEnabledInHierarchy();
                if ( bTarget )
                {
                    const WidgetGeometry& geometry = widget.getGeometry();
                    const float2          corner0  = geometry.transformPoint( float2{} );
                    const float2          corner1  = geometry.transformPoint( geometry._size );
                    const float2          center{ ( corner0._x + corner1._x ) * 0.5f, ( corner0._y + corner1._y ) * 0.5f };
                    const float32         halfX    = MathUtil::max( MathUtil::abs( corner1._x - corner0._x ), minTargetSize ) * 0.5f;
                    const float32         halfY    = MathUtil::max( MathUtil::abs( corner1._y - corner0._y ), minTargetSize ) * 0.5f;
                    const float32         dx       = screenPoint._x - center._x;
                    const float32         dy       = screenPoint._y - center._y;
                    const float32         distance = dx * dx + dy * dy;
                    if ( MathUtil::abs( dx ) <= halfX && MathUtil::abs( dy ) <= halfY && distance < ioBestDistance )
                    {
                        ioBest         = &widget;
                        ioBestDistance = distance;
                    }
                }
                const PanelWidget* pPanel = castTo<const PanelWidget>( &widget );
                for ( uint32 index = 0; pPanel != nullptr && index < pPanel->getChildCount(); ++index )
                {
                    findNearestTouchTarget( *pPanel->getChild( index ), screenPoint, minTargetSize, ioBest, ioBestDistance );
                }
            }

            /** @brief 경로를 따라 터널링 후 버블링 순서로 이벤트를 보냅니다. @p Handler 는 그 이벤트를 받는 Widget 의 가상 함수입니다. */
            template <typename EventType, UIReply ( Widget::*Handler )( const EventType&, UIRoutePhase )>
            static UIReply route( WidgetTree& tree, const UIWidgetPath& path, const EventType& event, WidgetID& outHandler )
            {
                outHandler = kInvalidWidgetID;
                const WidgetTree::DispatchScope dispatch{ tree }; // 핸들러가 떼어 낸 위젯은 전달이 끝난 뒤 지운다
                // 경로는 시작할 때 고정한다. 처리 중에 위젯이 떨어지면(목록을 지우는 핸들러) 그 번호만 건너뛴다.
                const vector<WidgetID> listWidget = path._listWidget;
                const uint32           count      = static_cast<uint32>( listWidget.size() );
                for ( uint32 index = 0; index < count; ++index )
                {
                    Widget* pWidget = tree.findWidgetByID( listWidget[index] );
                    if ( pWidget == nullptr || pWidget->isEnabledInHierarchy() == false )
                        continue;
                    const UIReply reply = ( pWidget->*Handler )( event, UIRoutePhase::Tunnel );
                    if ( reply.isHandled() )
                    {
                        outHandler = listWidget[index];
                        return reply;
                    }
                }
                for ( uint32 index = count; index > 0; --index )
                {
                    Widget* pWidget = tree.findWidgetByID( listWidget[index - 1] );
                    if ( pWidget == nullptr || pWidget->isEnabledInHierarchy() == false )
                        continue;
                    const UIReply reply = ( pWidget->*Handler )( event, UIRoutePhase::Bubble );
                    if ( reply.isHandled() )
                    {
                        outHandler = listWidget[index - 1];
                        return reply;
                    }
                }
                return UIReply::makeUnhandled();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool UIWidgetPath::contains( WidgetID id ) const
    {
        return VectorUtil::contains( _listWidget, id );
    }

    bool UIWidgetPath::hitTest( const WidgetTree& tree, const float2& screenPoint, UIWidgetPath& outPath )
    {
        outPath._listWidget.clear();
        const Widget* pRoot = tree.getRoot();
        return pRoot != nullptr && UIEventRouterInternal::hitTestRecursive( *pRoot, screenPoint, outPath._listWidget );
    }

    bool UIWidgetPath::hitTestTouch( const WidgetTree& tree, const float2& screenPoint, float32 minTargetSize, UIWidgetPath& outPath )
    {
        const bool          bHit    = hitTest( tree, screenPoint, outPath );
        const Widget* const pLeaf   = bHit ? tree.findWidgetByID( outPath.getLeaf() ) : nullptr;
        const bool          bDirect = pLeaf != nullptr && pLeaf->supportsFocus();
        const Widget* const pRoot   = tree.getRoot();
        if ( bDirect || minTargetSize <= 0.0f || pRoot == nullptr )
            return bHit;
        const Widget* pBest        = nullptr;
        float32       bestDistance = MathUtil::kMaxFloat;
        UIEventRouterInternal::findNearestTouchTarget( *pRoot, screenPoint, minTargetSize, pBest, bestDistance );
        if ( pBest == nullptr )
            return bHit;
        return makePathTo( tree, pBest->getID(), outPath );
    }

    bool UIWidgetPath::makePathTo( const WidgetTree& tree, WidgetID widget, UIWidgetPath& outPath )
    {
        outPath._listWidget.clear();
        const Widget* pWidget = tree.findWidgetByID( widget );
        if ( pWidget == nullptr )
            return false;
        for ( const Widget* pCurrent = pWidget; pCurrent != nullptr; pCurrent = pCurrent->getParent() )
        {
            outPath._listWidget.push_back( pCurrent->getID() );
        }
        // 잎에서 루트로 모았으므로 뒤집는다.
        const uint32 count = static_cast<uint32>( outPath._listWidget.size() );
        for ( uint32 index = 0; index < count / 2; ++index )
        {
            const WidgetID swapped                 = outPath._listWidget[index];
            outPath._listWidget[index]             = outPath._listWidget[count - 1 - index];
            outPath._listWidget[count - 1 - index] = swapped;
        }
        return true;
    }

    UIReply UIWidgetPath::routePointerEvent( WidgetTree& tree, const UIWidgetPath& path, const UIPointerEvent& event, WidgetID& outHandler )
    {
        return UIEventRouterInternal::route<UIPointerEvent, &Widget::onPointerEvent>( tree, path, event, outHandler );
    }

    UIReply UIWidgetPath::routeActionEvent( WidgetTree& tree, const UIWidgetPath& path, const UIActionEvent& event, WidgetID& outHandler )
    {
        return UIEventRouterInternal::route<UIActionEvent, &Widget::onActionEvent>( tree, path, event, outHandler );
    }
} // namespace sw
