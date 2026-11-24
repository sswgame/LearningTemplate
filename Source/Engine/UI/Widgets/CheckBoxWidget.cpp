#include "pch.h"

#include "Engine/UI/Widgets/CheckBoxWidget.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"
#include "Engine/UI/Layout/UILayoutPass.h"

namespace sw
{
    namespace
    {
        struct CheckBoxWidgetInternal
        {
            static constexpr float32 kBoxBorderWidth   = 2.0f;
            static constexpr float32 kBoxCornerRadius  = 4.0f;
            static constexpr float32 kMarkInset        = 5.0f;
            static constexpr float32 kMarkCornerRadius = 2.0f;
            /** @brief 꺼진 위젯의 색이 넘지 않는 불투명도입니다. */
            static constexpr float32 kDisabledAlpha = 0.4f;

            static float4 makeStateColor( const float4& color, bool bEnabled )
            {
                return float4{ color._x, color._y, color._z, bEnabled ? color._w : MathUtil::min( color._w, kDisabledAlpha ) };
            }
        };
    } // namespace

    CheckBoxWidget::CheckBoxWidget()
        : ButtonWidget{}
        , _onCheckedChanged{}
        , _boxColor{ 0.08f, 0.09f, 0.12f, 0.9f }
        , _boxBorderColor{ 0.85f, 0.88f, 0.95f, 1.0f }
        , _markColor{ 0.95f, 0.75f, 0.2f, 1.0f }
        , _bChecked{ false }
    {
        // 상자가 겉모습이다. 바탕은 투명하고 호버와 누름에서만 옅게 칠한다.
        setBackground( UIBrush::makeSolid( float4{}, 0.0f ) );
        setStateBrushes( UIBrush::makeSolid( float4{ 1.0f, 1.0f, 1.0f, 0.06f }, 4.0f ), UIBrush::makeSolid( float4{ 1.0f, 1.0f, 1.0f, 0.12f }, 4.0f ),
                         UIBrush::makeSolid( float4{}, 0.0f ) );
        setContentPadding( float4{} );
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
        const float32 boxSpan = kBoxSize + kBoxGap;
        if ( getChildCount() == 0 )
            return float2{ kBoxSize, kBoxSize };
        const float2 childAvailable{ UILayoutPass::computeRemaining( availableSize._x, boxSpan ), availableSize._y };
        const float2 desired = UILayoutPass::measure( *getChild( 0 ), context, childAvailable );
        return float2{ boxSpan + desired._x, MathUtil::max( kBoxSize, desired._y ) };
    }

    void CheckBoxWidget::arrangeChildren( const UILayoutContext& context, const float2& size )
    {
        if ( getChildCount() == 0 )
            return;
        // 왼쪽에서 오른쪽으로 배치한다. 오른쪽에서 왼쪽이면 arrangeChild 가 거울 배치해 텍스트가 상자 왼쪽에 간다.
        const float32 boxSpan = kBoxSize + kBoxGap;
        arrangeChild( context, *getChild( 0 ), float2{ boxSpan, 0.0f }, float2{ MathUtil::max( 0.0f, size._x - boxSpan ), size._y } );
    }

    void CheckBoxWidget::paint( CanvasPainter& painter, const UIPaintContext& context ) const
    {
        using Internal = CheckBoxWidgetInternal;
        ButtonWidget::paint( painter, context ); // 호버와 누름 바탕
        const float2& size = getGeometry()._size;
        const float2  boxPosition{ isRightToLeft() ? size._x - kBoxSize : 0.0f, ( size._y - kBoxSize ) * 0.5f };
        const bool    bEnabled = isEnabledInHierarchy();
        CanvasBrush   box{};
        box._color        = Internal::makeStateColor( _boxColor, bEnabled );
        box._borderColor  = Internal::makeStateColor( _boxBorderColor, bEnabled );
        box._borderWidth  = Internal::kBoxBorderWidth;
        box._cornerRadius = float4{ Internal::kBoxCornerRadius, Internal::kBoxCornerRadius, Internal::kBoxCornerRadius, Internal::kBoxCornerRadius };
        painter.fillRect( boxPosition, float2{ kBoxSize, kBoxSize }, box );
        if ( _bChecked == false )
            return;
        const float32 markSize = kBoxSize - 2.0f * Internal::kMarkInset;
        CanvasBrush   mark{};
        mark._color        = Internal::makeStateColor( _markColor, bEnabled );
        mark._cornerRadius = float4{ Internal::kMarkCornerRadius, Internal::kMarkCornerRadius, Internal::kMarkCornerRadius, Internal::kMarkCornerRadius };
        painter.fillRect( float2{ boxPosition._x + Internal::kMarkInset, boxPosition._y + Internal::kMarkInset }, float2{ markSize, markSize },
                          mark );
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
