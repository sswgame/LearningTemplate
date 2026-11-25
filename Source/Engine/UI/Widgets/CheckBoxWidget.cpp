#include "pch.h"

#include "Engine/UI/Widgets/CheckBoxWidget.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Style/UIWidgetSkin.h"

namespace sw
{
    namespace
    {
        struct CheckBoxWidgetInternal
        {
            /** @brief 꺼진 위젯의 색은 @p disabledAlpha 를 넘지 않습니다. */
            static float4 makeStateColor( const float4& color, bool bEnabled, float32 disabledAlpha )
            {
                return float4{ color._x, color._y, color._z, bEnabled ? color._w : MathUtil::min( color._w, disabledAlpha ) };
            }
        };
    } // namespace

    CheckBoxWidget::CheckBoxWidget()
        : ButtonWidget{}
        , _onCheckedChanged{}
        , _boxColor{ UIWidgetSkin::get()._checkBox._box._color }
        , _boxBorderColor{ UIWidgetSkin::get()._checkBox._box._borderColor }
        , _markColor{ UIWidgetSkin::get()._checkBox._mark._color }
        , _bChecked{ false }
    {
        const UIWidgetSkin::CheckBox& skin = UIWidgetSkin::get()._checkBox;
        setBackground( skin._brush._normal );
        setStateBrushes( skin._brush._hovered, skin._brush._pressed, skin._brush._disabled );
        setContentPadding( skin._padding );
    }

    CheckBoxWidget::~CheckBoxWidget() = default;

    const TypeInfo* CheckBoxWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void CheckBoxWidget::setChecked( bool bChecked )
    {
        if ( _bChecked == bChecked )
            return;
        _bChecked = bChecked;
        invalidate( WidgetDirty::kStyle | WidgetDirty::kPaint ); // :checked
    }

    uint32 CheckBoxWidget::computeStyleStates() const
    {
        return ButtonWidget::computeStyleStates() | ( _bChecked ? UIStyleState::kChecked : UIStyleState::kNone );
    }

    void CheckBoxWidget::onBoundPropertyChanged( const PropertyInfo& property )
    {
        if ( property._name == hashed_string( "_bChecked" ) )
        {
            invalidate( WidgetDirty::kPaint );
            return;
        }
        ButtonWidget::onBoundPropertyChanged( property );
    }

    void CheckBoxWidget::handleClick()
    {
        setChecked( _bChecked == false );
        _onCheckedChanged.broadcast( _bChecked );
        notifyValueEdited( "_bChecked" );
        ButtonWidget::handleClick();
    }

    float2 CheckBoxWidget::computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const
    {
        const UIWidgetSkin::CheckBox& skin    = UIWidgetSkin::get()._checkBox;
        const float32                 boxSpan = skin._boxSize._x + skin._labelGap;
        if ( getChildCount() == 0 )
            return skin._boxSize;
        const float2 childAvailable{ UILayoutPass::computeRemaining( availableSize._x, boxSpan ), availableSize._y };
        const float2 desired = UILayoutPass::measure( *getChild( 0 ), context, childAvailable );
        return float2{ boxSpan + desired._x, MathUtil::max( skin._boxSize._y, desired._y ) };
    }

    void CheckBoxWidget::arrangeChildren( const UILayoutContext& context, const float2& size )
    {
        if ( getChildCount() == 0 )
            return;
        // 왼쪽에서 오른쪽으로 배치한다. 오른쪽에서 왼쪽이면 arrangeChild 가 거울 배치해 텍스트가 상자 왼쪽에 간다.
        const UIWidgetSkin::CheckBox& skin    = UIWidgetSkin::get()._checkBox;
        const float32                 boxSpan = skin._boxSize._x + skin._labelGap;
        arrangeChild( context, *getChild( 0 ), float2{ boxSpan, 0.0f }, float2{ MathUtil::max( 0.0f, size._x - boxSpan ), size._y } );
    }

    void CheckBoxWidget::paint( CanvasPainter& painter, const UIPaintContext& context ) const
    {
        using Internal = CheckBoxWidgetInternal;
        ButtonWidget::paint( painter, context ); // 호버와 누름 바탕
        const UIWidgetSkin::CheckBox& skin = UIWidgetSkin::get()._checkBox;
        const float2&                 size = getGeometry()._size;
        const float2                  boxPosition{ isRightToLeft() ? size._x - skin._boxSize._x : 0.0f, ( size._y - skin._boxSize._y ) * 0.5f };
        const bool                    bEnabled = isEnabledInHierarchy();
        CanvasBrush                   box{};
        box._color        = Internal::makeStateColor( _boxColor, bEnabled, skin._disabledAlpha );
        box._borderColor  = Internal::makeStateColor( _boxBorderColor, bEnabled, skin._disabledAlpha );
        box._borderWidth  = skin._box._borderWidth;
        box._cornerRadius = skin._box._cornerRadius;
        painter.fillRect( boxPosition, skin._boxSize, box );
        if ( _bChecked == false )
            return;
        const float4& inset = skin._boxPadding;
        CanvasBrush   mark{};
        mark._color        = Internal::makeStateColor( _markColor, bEnabled, skin._disabledAlpha );
        mark._cornerRadius = skin._mark._cornerRadius;
        painter.fillRect( float2{ boxPosition._x + inset._x, boxPosition._y + inset._y }, float2{ skin._boxSize._x - inset._x - inset._z, skin._boxSize._y - inset._y - inset._w }, mark );
    }
} // namespace sw

namespace sw
{
    void CheckBoxWidget::describeAccessible( AccessibleInfo& outInfo )
    {
        ButtonWidget::describeAccessible( outInfo );
        outInfo._role = AccessibleRole::CheckBox;
        if ( _bChecked )
            outInfo._states |= AccessibleState::kChecked;
    }
} // namespace sw
