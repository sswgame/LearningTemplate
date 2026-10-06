#include "pch.h"

#include "Engine/UI/Widgets/CheckBoxWidget.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Layout/UiLayoutPass.h"

namespace sw
{
    CheckBoxWidget::CheckBoxWidget()
        : ButtonWidget{}
        , _onCheckedChanged{}
        , _bChecked{ false }
    {
        // 상자가 겉모습이다 — 바탕은 투명, 호버 · 누름만 옅게.
        setBackground( UiBrush::makeSolid( float4{}, 0.0f ) );
        setStateBrushes( UiBrush::makeSolid( float4{ 1.0f, 1.0f, 1.0f, 0.06f }, 4.0f ), UiBrush::makeSolid( float4{ 1.0f, 1.0f, 1.0f, 0.12f }, 4.0f ),
                         UiBrush::makeSolid( float4{}, 0.0f ) );
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
        return ButtonWidget::computeStyleStates() | ( _bChecked ? UiStyleState::kChecked : UiStyleState::kNone );
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

    float2 CheckBoxWidget::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        const float32 boxSpan = kBoxSize + kBoxGap;
        if ( getChildCount() == 0 )
            return float2{ kBoxSize, kBoxSize };
        const float2 childAvailable{ UiLayoutPass::computeRemaining( availableSize._x, boxSpan ), availableSize._y };
        const float2 desired = UiLayoutPass::measure( *getChild( 0 ), context, childAvailable );
        return float2{ boxSpan + desired._x, MathUtil::max( kBoxSize, desired._y ) };
    }

    void CheckBoxWidget::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        if ( getChildCount() == 0 )
            return;
        // 왼쪽에서 오른쪽으로 놓는다 — 오른쪽에서 왼쪽이면 arrangeChild 가 거울로 놓아 글이 상자 왼쪽에 간다.
        const float32 boxSpan = kBoxSize + kBoxGap;
        arrangeChild( context, *getChild( 0 ), float2{ boxSpan, 0.0f }, float2{ MathUtil::max( 0.0f, size._x - boxSpan ), size._y } );
    }

    void CheckBoxWidget::paint( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        ButtonWidget::paint( painter, context ); // 호버 · 누름 바탕
        const float2& size = getGeometry()._size;
        const float2  boxPosition{ isRightToLeft() ? size._x - kBoxSize : 0.0f, ( size._y - kBoxSize ) * 0.5f };
        const bool    bEnabled = isEnabledInHierarchy();
        CanvasBrush   box{};
        box._color        = float4{ 0.08f, 0.09f, 0.12f, bEnabled ? 0.9f : 0.4f };
        box._borderColor  = float4{ 0.85f, 0.88f, 0.95f, bEnabled ? 1.0f : 0.4f };
        box._borderWidth  = 2.0f;
        box._cornerRadius = float4{ 4.0f, 4.0f, 4.0f, 4.0f };
        painter.fillRect( boxPosition, float2{ kBoxSize, kBoxSize }, box );
        if ( _bChecked == false )
            return;
        constexpr float32 kMarkInset = 5.0f;
        CanvasBrush       mark{};
        mark._color        = float4{ 0.95f, 0.75f, 0.2f, bEnabled ? 1.0f : 0.4f };
        mark._cornerRadius = float4{ 2.0f, 2.0f, 2.0f, 2.0f };
        painter.fillRect( float2{ boxPosition._x + kMarkInset, boxPosition._y + kMarkInset }, float2{ kBoxSize - 2.0f * kMarkInset, kBoxSize - 2.0f * kMarkInset },
                          mark );
    }
} // namespace sw
