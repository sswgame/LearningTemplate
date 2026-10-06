#include "pch.h"

#include "Engine/UI/Widgets/BorderPanel.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/UI/Layout/UiLayoutPass.h"
#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    BorderPanel::BorderPanel()
        : PanelWidget{}
        , _background{}
        , _contentPadding{}
        , _shadowColor{}
        , _shadowOffset{}
        , _shadowBlur{ 0.0f }
    {
    }

    BorderPanel::~BorderPanel() = default;

    const TypeInfo* BorderPanel::getTypeInfo() const
    {
        return StaticType();
    }

    void BorderPanel::setBackground( const UiBrush& brush )
    {
        _background = brush;
        invalidate( WidgetDirty::kPaint );
    }

    void BorderPanel::setContentPadding( const float4& padding )
    {
        if ( _contentPadding == padding )
            return;
        _contentPadding = padding;
        invalidate( WidgetDirty::kLayout );
    }

    void BorderPanel::setShadow( const float4& color, float32 blur, const float2& offset )
    {
        _shadowColor  = color;
        _shadowBlur   = blur;
        _shadowOffset = offset;
        invalidate( WidgetDirty::kPaint );
    }

    float4 BorderPanel::computeEffectivePadding() const
    {
        const UiComputedStyle* pStyle = getComputedStyle();
        return pStyle != nullptr && pStyle->has( UiStyleField::Padding ) ? pStyle->_value._padding : _contentPadding;
    }

    UiBrush BorderPanel::computeEffectiveBrush() const
    {
        UiBrush                brush  = getBackgroundBrush();
        const UiComputedStyle* pStyle = getComputedStyle();
        if ( pStyle == nullptr )
            return brush;
        if ( pStyle->has( UiStyleField::BackgroundColor ) )
            brush._color = pStyle->_value._backgroundColor;
        if ( pStyle->has( UiStyleField::CornerRadius ) )
            brush._cornerRadius = pStyle->_value._cornerRadius;
        if ( pStyle->has( UiStyleField::BorderWidth ) )
            brush._borderWidth = pStyle->_value._borderWidth;
        if ( pStyle->has( UiStyleField::BorderColor ) )
            brush._borderColor = pStyle->_value._borderColor;
        return brush;
    }

    float2 BorderPanel::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        const float4  padding = computeEffectivePadding();
        const float32 padX    = padding._x + padding._z;
        const float32 padY    = padding._y + padding._w;
        if ( getChildCount() == 0 )
            return float2{ padX, padY };
        Widget&       child        = *getChild( 0 );
        const float4& childPadding = child.getLayoutSlot()._padding;
        const float32 childPadX    = childPadding._x + childPadding._z;
        const float32 childPadY    = childPadding._y + childPadding._w;
        const float2  childAvailable{ UiLayoutPass::computeRemaining( availableSize._x, padX + childPadX ),
                                     UiLayoutPass::computeRemaining( availableSize._y, padY + childPadY ) };
        const float2  desired = UiLayoutPass::measure( child, context, childAvailable );
        return float2{ desired._x + childPadX + padX, desired._y + childPadY + padY };
    }

    void BorderPanel::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        if ( getChildCount() == 0 )
            return;
        const float4 padding = computeEffectivePadding();
        const float2 inner{ MathUtil::max( 0.0f, size._x - padding._x - padding._z ), MathUtil::max( 0.0f, size._y - padding._y - padding._w ) };
        arrangeChild( context, *getChild( 0 ), float2{ padding._x, padding._y }, inner );
    }

    void BorderPanel::paint( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        (void)context;
        const float2&          size        = getGeometry()._size;
        const UiBrush          brush       = computeEffectiveBrush();
        const UiComputedStyle* pStyle      = getComputedStyle();
        const bool             bHasStyle   = pStyle != nullptr;
        const float4           shadowColor = bHasStyle && pStyle->has( UiStyleField::ShadowColor ) ? pStyle->_value._shadowColor : _shadowColor;
        const float32          shadowBlur  = bHasStyle && pStyle->has( UiStyleField::ShadowBlur ) ? pStyle->_value._shadowBlur : _shadowBlur;
        const float2           shadowShift = bHasStyle && pStyle->has( UiStyleField::ShadowOffset ) ? pStyle->_value._shadowOffset : _shadowOffset;
        if ( shadowColor._w > 0.0f )
            painter.drawShadow( float2{}, size, brush._cornerRadius, shadowColor, shadowBlur, shadowShift );
        if ( brush.isInvisible() == false )
            painter.fillRect( float2{}, size, brush.makeCanvasBrush() );
    }
} // namespace sw
