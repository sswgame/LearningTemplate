#include "pch.h"

#include "Engine/UI/Widgets/BorderPanel.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/UI/Layout/UiLayoutPass.h"

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

    float2 BorderPanel::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        const float32 padX = _contentPadding._x + _contentPadding._z;
        const float32 padY = _contentPadding._y + _contentPadding._w;
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
        const float2 inner{ MathUtil::max( 0.0f, size._x - _contentPadding._x - _contentPadding._z ),
                            MathUtil::max( 0.0f, size._y - _contentPadding._y - _contentPadding._w ) };
        arrangeChild( context, *getChild( 0 ), float2{ _contentPadding._x, _contentPadding._y }, inner );
    }

    void BorderPanel::paint( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        (void)context;
        const float2&  size  = getGeometry()._size;
        const UiBrush& brush = getBackgroundBrush();
        if ( _shadowColor._w > 0.0f )
            painter.drawShadow( float2{}, size, brush._cornerRadius, _shadowColor, _shadowBlur, _shadowOffset );
        if ( brush.isInvisible() == false )
            painter.fillRect( float2{}, size, brush.makeCanvasBrush() );
    }
} // namespace sw
