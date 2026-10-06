#include "pch.h"

#include "Engine/UI/Layout/OverlayPanel.h"

#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UiLayoutPass.h"

namespace sw
{
    OverlayPanel::OverlayPanel()
        : PanelWidget{}
    {
    }

    OverlayPanel::~OverlayPanel() = default;

    const TypeInfo* OverlayPanel::getTypeInfo() const
    {
        return StaticType();
    }

    float2 OverlayPanel::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        float2 desiredMax{};
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const float4& padding  = child.getLayoutSlot()._padding;
            const float32 padX     = padding._x + padding._z;
            const float32 padY     = padding._y + padding._w;
            const float2  childMax = float2{ UiLayoutPass::computeRemaining( availableSize._x, padX ), UiLayoutPass::computeRemaining( availableSize._y, padY ) };
            const float2  desired  = UiLayoutPass::measure( child, context, childMax );
            desiredMax._x          = MathUtil::max( desiredMax._x, desired._x + padX );
            desiredMax._y          = MathUtil::max( desiredMax._y, desired._y + padY );
        }
        return desiredMax;
    }

    void OverlayPanel::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        for ( uint32 index = 0; index < getChildCount(); ++index )
            arrangeChild( context, *getChild( index ), float2{}, size );
    }
} // namespace sw
