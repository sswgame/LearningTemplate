#include "pch.h"

#include "Engine/UI/Widgets/ButtonWidget.h"

#include "Engine/UI/Core/WidgetTree.h"

namespace sw
{
    namespace
    {
        struct ButtonWidgetInternal
        {
            /** @brief 화면 점 @p position 이 버튼의 놓인 사각형 안인가(렌더 변환까지 풀어서)입니다. */
            static bool containsScreenPoint( const Widget& widget, const float2& position )
            {
                float2 local{};
                if ( widget.getGeometry().inverseTransformPoint( position, local ) == false )
                    return false;
                return widget.getGeometry().containsLocal( local );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ButtonWidget::ButtonWidget()
        : BorderPanel{}
        , _clickedHandler{}
        , _command{}
        , _bPressed{ false }
    {
    }

    ButtonWidget::~ButtonWidget() = default;

    const TypeInfo* ButtonWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void ButtonWidget::click()
    {
        if ( isEnabledInHierarchy() == false )
            return;
        if ( _clickedHandler.isBound() )
            _clickedHandler( *this );
        WidgetTree* pTree = getTree();
        if ( pTree != nullptr )
            pTree->dispatchCommand( _command, *this );
    }

    UiReply ButtonWidget::onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase )
    {
        if ( phase != UiRoutePhase::Bubble || event._button != MouseButton::Left )
            return UiReply::makeUnhandled();
        switch ( event._kind )
        {
            case UiPointerEventKind::Down:
            {
                setPressed( true );
                return UiReply::makeHandled().capturePointer().requestFocus( getId() );
            }
            case UiPointerEventKind::Up:
            {
                // 잡은 포인터라 밖에서 떼도 여기로 온다 — 누른 버튼 위에서 뗐을 때만 클릭이다.
                const bool bClicked = _bPressed && ButtonWidgetInternal::containsScreenPoint( *this, event._position );
                setPressed( false );
                if ( bClicked )
                    click();
                return UiReply::makeHandled().releasePointer();
            }
            case UiPointerEventKind::Move:
            case UiPointerEventKind::Wheel:
            {
                return UiReply::makeUnhandled();
            }
        }
        return UiReply::makeUnhandled();
    }

    UiReply ButtonWidget::onActionEvent( const UiActionEvent& event, UiRoutePhase phase )
    {
        static const hashed_string s_accept( UiActionName::kAccept );
        if ( phase != UiRoutePhase::Bubble || event._action != s_accept || event._bRepeat == SW_TRUE )
            return UiReply::makeUnhandled();
        click();
        return UiReply::makeHandled();
    }

    void ButtonWidget::setPressed( bool bPressed )
    {
        if ( _bPressed == bPressed )
            return;
        _bPressed = bPressed;
        invalidate( WidgetDirty::kStyle );
    }
} // namespace sw
