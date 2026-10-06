#include "pch.h"

#include "Engine/UI/Layout/SafeZonePanel.h"

#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UiLayoutPass.h"

namespace sw
{
    SafeZonePanel::SafeZonePanel()
        : PanelWidget{}
    {
    }

    SafeZonePanel::~SafeZonePanel() = default;

    const TypeInfo* SafeZonePanel::getTypeInfo() const
    {
        return StaticType();
    }

    float2 SafeZonePanel::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        float2 desiredMax{};
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const float4& padding = child.getLayoutSlot()._padding;
            const float32 padX    = padding._x + padding._z;
            const float32 padY    = padding._y + padding._w;
            const float2  desired = UiLayoutPass::measure(
                child, context, float2{ UiLayoutPass::computeRemaining( availableSize._x, padX ), UiLayoutPass::computeRemaining( availableSize._y, padY ) } );
            desiredMax._x = MathUtil::max( desiredMax._x, desired._x + padX );
            desiredMax._y = MathUtil::max( desiredMax._y, desired._y + padY );
        }
        return desiredMax;
    }

    void SafeZonePanel::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        // 안전 사각형(화면 UI 단위)과 이 패널 사각형이 겹치는 만큼만 안쪽으로 민다 — 화면 가운데 패널은 그대로다.
        const float2& origin      = getGeometry()._position;
        const float32 safeLeft    = context._safeInsets._x;
        const float32 safeTop     = context._safeInsets._y;
        const float32 safeRight   = context._viewportSize._x - context._safeInsets._z;
        const float32 safeBottom  = context._viewportSize._y - context._safeInsets._w;
        const float32 insetLeft   = MathUtil::max( 0.0f, safeLeft - origin._x );
        const float32 insetTop    = MathUtil::max( 0.0f, safeTop - origin._y );
        const float32 insetRight  = MathUtil::max( 0.0f, origin._x + size._x - safeRight );
        const float32 insetBottom = MathUtil::max( 0.0f, origin._y + size._y - safeBottom );
        const float2  innerSize{ MathUtil::max( 0.0f, size._x - insetLeft - insetRight ), MathUtil::max( 0.0f, size._y - insetTop - insetBottom ) };
        for ( uint32 index = 0; index < getChildCount(); ++index )
            arrangeChild( context, *getChild( index ), float2{ insetLeft, insetTop }, innerSize );
    }
} // namespace sw
