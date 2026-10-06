#include "pch.h"

#include "Engine/UI/Layout/BoxPanel.h"

#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UiLayoutPass.h"

namespace sw
{
    BoxPanel::BoxPanel()
        : PanelWidget{}
        , _spacing{ 0.0f }
        , _orientation{ UiOrientation::Horizontal }
    {
    }

    BoxPanel::~BoxPanel() = default;

    const TypeInfo* BoxPanel::getTypeInfo() const
    {
        return StaticType();
    }

    void BoxPanel::setOrientation( UiOrientation orientation )
    {
        if ( _orientation == orientation )
            return;
        _orientation = orientation;
        invalidate( WidgetDirty::kLayout );
    }

    void BoxPanel::setSpacing( float32 spacing )
    {
        if ( _spacing == spacing )
            return;
        _spacing = spacing;
        invalidate( WidgetDirty::kLayout );
    }

    float2 BoxPanel::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        // 1) Auto 자식은 주축 무한으로 잰다(원하는 만큼). 2) 남은 주축을 Fill 자식에게 비율대로 주고 그 크기로 잰다 —
        //    줄 바꿈 글이 Fill 칸에 들어가면 그 칸 너비로 높이를 정한다(한 프레임 늦지 않다).
        float32       mainUsed       = 0.0f;
        float32       crossMax       = 0.0f;
        float32       fillWeight     = 0.0f;
        uint32        visibleCount   = 0;
        const float32 crossAvailable = UiLayoutPass::getCrossAxis( _orientation, availableSize );
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            ++visibleCount;
            const WidgetLayoutSlot& slot = child.getLayoutSlot();
            if ( slot._sizeRule == UiSizeRule::Fill )
            {
                fillWeight += slot._fillWeight;
                continue;
            }
            const float32 crossPadding = UiLayoutPass::getCrossPadding( _orientation, slot._padding );
            const float2  desired      = UiLayoutPass::measure(
                child, context, UiLayoutPass::makeAxisVector( _orientation, kUiUnbounded, UiLayoutPass::computeRemaining( crossAvailable, crossPadding ) ) );
            mainUsed += UiLayoutPass::getMainAxis( _orientation, desired ) + UiLayoutPass::getMainPadding( _orientation, slot._padding );
            crossMax = MathUtil::max( crossMax, UiLayoutPass::getCrossAxis( _orientation, desired ) + crossPadding );
        }
        const float32 spacingTotal  = visibleCount > 1 ? _spacing * static_cast<float32>( visibleCount - 1 ) : 0.0f;
        const float32 mainAvailable = UiLayoutPass::getMainAxis( _orientation, availableSize );
        const float32 remaining     = UiLayoutPass::computeRemaining( mainAvailable, mainUsed + spacingTotal );
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget&                 child = *getChild( index );
            const WidgetLayoutSlot& slot  = child.getLayoutSlot();
            if ( child.getVisibility() == WidgetVisibility::Collapsed || slot._sizeRule != UiSizeRule::Fill )
                continue;
            const bool    bUnbounded   = UiLayoutPass::isUnbounded( remaining ) || fillWeight <= 0.0f;
            const float32 share        = bUnbounded ? kUiUnbounded : remaining * slot._fillWeight / fillWeight;
            const float32 mainPadding  = UiLayoutPass::getMainPadding( _orientation, slot._padding );
            const float32 crossPadding = UiLayoutPass::getCrossPadding( _orientation, slot._padding );
            const float2  desired      = UiLayoutPass::measure( child, context,
                                                                UiLayoutPass::makeAxisVector( _orientation, UiLayoutPass::computeRemaining( share, mainPadding ),
                                                                                              UiLayoutPass::computeRemaining( crossAvailable, crossPadding ) ) );
            mainUsed += UiLayoutPass::getMainAxis( _orientation, desired ) + mainPadding;
            crossMax = MathUtil::max( crossMax, UiLayoutPass::getCrossAxis( _orientation, desired ) + crossPadding );
        }
        return UiLayoutPass::makeAxisVector( _orientation, mainUsed + spacingTotal, crossMax );
    }

    void BoxPanel::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        float32 autoTotal    = 0.0f;
        float32 fillWeight   = 0.0f;
        uint32  visibleCount = 0;
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            const Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            ++visibleCount;
            const WidgetLayoutSlot& slot = child.getLayoutSlot();
            if ( slot._sizeRule == UiSizeRule::Fill )
                fillWeight += slot._fillWeight;
            else
                autoTotal += UiLayoutPass::getMainAxis( _orientation, child.getDesiredSize() ) + UiLayoutPass::getMainPadding( _orientation, slot._padding );
        }
        const float32 spacingTotal = visibleCount > 1 ? _spacing * static_cast<float32>( visibleCount - 1 ) : 0.0f;
        const float32 mainSize     = UiLayoutPass::getMainAxis( _orientation, size );
        const float32 crossSize    = UiLayoutPass::getCrossAxis( _orientation, size );
        const float32 remaining    = MathUtil::max( 0.0f, mainSize - autoTotal - spacingTotal );
        float32       cursor       = 0.0f;
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const WidgetLayoutSlot& slot      = child.getLayoutSlot();
            float32                 childMain = 0.0f;
            if ( slot._sizeRule == UiSizeRule::Fill )
                childMain = fillWeight > 0.0f ? remaining * slot._fillWeight / fillWeight : 0.0f;
            else
                childMain = UiLayoutPass::getMainAxis( _orientation, child.getDesiredSize() ) + UiLayoutPass::getMainPadding( _orientation, slot._padding );
            arrangeChild( context, child, UiLayoutPass::makeAxisVector( _orientation, cursor, 0.0f ), UiLayoutPass::makeAxisVector( _orientation, childMain, crossSize ) );
            cursor += childMain + _spacing;
        }
    }
} // namespace sw
