#include "pch.h"

#include "Engine/UI/Widgets/ButtonWidget.h"

#include "Core/String/hashed_string.h"

#include "Engine/UI/Base/UiEvents.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Screen/UiScreen.h"

namespace sw
{
    namespace
    {
        struct ButtonWidgetInternal
        {
            /** @brief 기본 겉모습(스타일 시트 5-2 가 생기기 전 — 어두운 반투명 바탕 · 둥근 모서리)입니다. */
            static constexpr float32 kCornerRadius = 6.0f;

            static UiBrush makeBrush( float32 shade, float32 alpha )
            {
                return UiBrush::makeSolid( float4{ shade, shade * 1.1f, shade * 1.35f, alpha }, kCornerRadius );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ButtonWidget::ButtonWidget()
        : BorderPanel{}
        , _onClicked{}
        , _clickCount{ 0 }
        , _hoveredBrush{ ButtonWidgetInternal::makeBrush( 0.24f, 0.95f ) }
        , _pressedBrush{ ButtonWidgetInternal::makeBrush( 0.10f, 0.95f ) }
        , _disabledBrush{ ButtonWidgetInternal::makeBrush( 0.16f, 0.4f ) }
        , _command{}
        , _bPressed{ false }
    {
        setBackground( ButtonWidgetInternal::makeBrush( 0.16f, 0.95f ) );
        setContentPadding( float4{ 16.0f, 8.0f, 16.0f, 8.0f } );
    }

    ButtonWidget::~ButtonWidget() = default;

    const TypeInfo* ButtonWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void ButtonWidget::setStateBrushes( const UiBrush& hovered, const UiBrush& pressed, const UiBrush& disabled )
    {
        _hoveredBrush  = hovered;
        _pressedBrush  = pressed;
        _disabledBrush = disabled;
        invalidate( WidgetDirty::kPaint );
    }

    const UiBrush& ButtonWidget::getBackgroundBrush() const
    {
        if ( isEnabledInHierarchy() == false )
            return _disabledBrush;
        if ( _bPressed )
            return _pressedBrush;
        if ( isHovered() )
            return _hoveredBrush;
        return BorderPanel::getBackgroundBrush();
    }

    UiReply ButtonWidget::onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase )
    {
        // 버블 — 안의 글 · 그림이 먼저 지나가고 버튼이 받는다. 누른 동안은 포인터를 잡아 경로의 잎이 버튼이다.
        if ( phase != UiRoutePhase::Bubble || event._button != MouseButton::Left )
            return UiReply::makeUnhandled();
        if ( event._kind == UiPointerEventKind::Down )
        {
            _bPressed = true;
            invalidate( WidgetDirty::kStyle | WidgetDirty::kPaint ); // :pressed
            return UiReply::makeHandled().capturePointer().requestFocus( getId() );
        }
        if ( event._kind == UiPointerEventKind::Up && _bPressed )
        {
            _bPressed = false;
            invalidate( WidgetDirty::kStyle | WidgetDirty::kPaint ); // :pressed
            // 누른 위젯 = 뗀 위젯일 때만 클릭이다(밖으로 끌고 나가 떼면 취소).
            float2 local{};
            if ( getGeometry().inverseTransformPoint( event._position, local ) && getGeometry().containsLocal( local ) )
                handleClick();
            return UiReply::makeHandled().releasePointer();
        }
        return UiReply::makeUnhandled();
    }

    UiReply ButtonWidget::onActionEvent( const UiActionEvent& event, UiRoutePhase phase )
    {
        if ( phase != UiRoutePhase::Bubble || event._action != hashed_string( UiActionName::kAccept ) )
            return UiReply::makeUnhandled();
        handleClick();
        return UiReply::makeHandled();
    }

    uint32 ButtonWidget::computeStyleStates() const
    {
        return Widget::computeStyleStates() | ( _bPressed ? UiStyleState::kPressed : UiStyleState::kNone );
    }

    void ButtonWidget::handleClick()
    {
        ++_clickCount;
        _onClicked.broadcast( getId() );
        UiScreen* pScreen = getTree() != nullptr ? getTree()->getScreen() : nullptr;
        if ( pScreen != nullptr )
            pScreen->dispatchCommand( _command, *this );
    }
} // namespace sw
