#include "pch.h"

#include "Engine/UI/Widgets/ButtonWidget.h"

#include "Core/String/hashed_string.h"

#include "Engine/UI/Accessibility/UIAccessibilityTree.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    namespace
    {
        struct ButtonWidgetInternal
        {
            /** @brief 기본 겉모습입니다(스타일 시트 5-2 가 생기기 전의 어두운 반투명 바탕과 둥근 모서리입니다). */
            static constexpr float32 kCornerRadius = 6.0f;

            static UIBrush makeBrush( float32 shade, float32 alpha )
            {
                return UIBrush::makeSolid( float4{ shade, shade * 1.1f, shade * 1.35f, alpha }, kCornerRadius );
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

    void ButtonWidget::setStateBrushes( const UIBrush& hovered, const UIBrush& pressed, const UIBrush& disabled )
    {
        _hoveredBrush  = hovered;
        _pressedBrush  = pressed;
        _disabledBrush = disabled;
        invalidate( WidgetDirty::kPaint );
    }

    const UIBrush& ButtonWidget::selectBackgroundBrush( uint32 styleStates ) const
    {
        if ( ( styleStates & UIStyleState::kDisabled ) != 0 )
            return _disabledBrush;
        if ( ( styleStates & UIStyleState::kPressed ) != 0 )
            return _pressedBrush;
        if ( ( styleStates & UIStyleState::kHover ) != 0 )
            return _hoveredBrush;
        return BorderPanel::selectBackgroundBrush( styleStates );
    }

    UIReply ButtonWidget::onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase )
    {
        // 버블: 안의 텍스트와 이미지가 먼저 지나가고 버튼이 받는다. 누른 동안은 포인터를 잡아 경로의 잎이 버튼이다.
        if ( phase != UIRoutePhase::Bubble || event._button != MouseButton::Left )
            return UIReply::makeUnhandled();
        if ( event._kind == UIPointerEventKind::Down )
        {
            _bPressed = true;
            invalidate( WidgetDirty::kStyle | WidgetDirty::kPaint ); // :pressed
            return UIReply::makeHandled().capturePointer().requestFocus( getID() );
        }
        if ( event._kind == UIPointerEventKind::Up && _bPressed )
        {
            _bPressed = false;
            invalidate( WidgetDirty::kStyle | WidgetDirty::kPaint ); // :pressed
            // 누른 위젯과 뗀 위젯이 같을 때만 클릭이다(밖으로 끌고 나가 떼면 취소).
            float2 local{};
            if ( getGeometry().inverseTransformPoint( event._position, local ) && getGeometry().containsLocal( local ) )
                handleClick();
            return UIReply::makeHandled().releasePointer();
        }
        return UIReply::makeUnhandled();
    }

    UIReply ButtonWidget::onActionEvent( const UIActionEvent& event, UIRoutePhase phase )
    {
        if ( phase != UIRoutePhase::Bubble || event._action != hashed_string( UIActionName::kAccept ) )
            return UIReply::makeUnhandled();
        handleClick();
        return UIReply::makeHandled();
    }

    uint32 ButtonWidget::computeStyleStates() const
    {
        return Widget::computeStyleStates() | ( _bPressed ? UIStyleState::kPressed : UIStyleState::kNone );
    }

    void ButtonWidget::handleClick()
    {
        ++_clickCount;
        _onClicked.broadcast( getID() );
        UIScreen* pScreen = getTree() != nullptr ? getTree()->getScreen() : nullptr;
        if ( pScreen != nullptr )
            pScreen->dispatchCommand( _command, *this );
    }
} // namespace sw

namespace sw
{
    void ButtonWidget::describeAccessible( AccessibleInfo& outInfo )
    {
        outInfo._role  = AccessibleRole::Button;
        outInfo._bLeaf = true;
        outInfo._name  = getAccessibleName().empty() ? UIAccessibilityTree::collectLabel( *this ) : getAccessibleName();
        if ( _bPressed )
            outInfo._states |= AccessibleState::kPressed;
    }
} // namespace sw
