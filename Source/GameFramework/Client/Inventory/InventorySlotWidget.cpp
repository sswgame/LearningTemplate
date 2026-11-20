#include "pch.h"

#include "GameFramework/Client/Inventory/InventorySlotWidget.h"

#include "GameFramework/Client/Inventory/InventoryScreen.h"

namespace sw
{
    InventorySlotWidget::InventorySlotWidget( InventoryScreen& screen, int32 bagSlot )
        : ButtonWidget{}
        , _screen{ screen }
        , _bagSlot{ bagSlot }
    {
    }

    InventorySlotWidget::~InventorySlotWidget() = default;

    UIReply InventorySlotWidget::onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase )
    {
        if ( phase == UIRoutePhase::Bubble && event._button == MouseButton::Right && event._kind == UIPointerEventKind::Down )
        {
            _screen.onSlotSecondary( _bagSlot );
            return UIReply::makeHandled().requestFocus( getID() );
        }
        const bool bLeft = phase == UIRoutePhase::Bubble && event._button == MouseButton::Left;
        if ( bLeft && event._kind == UIPointerEventKind::Down )
            _screen.onSlotPressed( _bagSlot );
        const UIReply reply = ButtonWidget::onPointerEvent( event, phase ); // 누름 · 클릭 · 포인터 잡기는 단추 그대로
        if ( bLeft && event._kind == UIPointerEventKind::Up )
            _screen.onSlotReleased( _bagSlot, event._position );
        return reply;
    }
} // namespace sw
