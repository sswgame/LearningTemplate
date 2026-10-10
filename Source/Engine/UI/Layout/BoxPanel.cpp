#include "pch.h"

#include "Engine/UI/Layout/BoxPanel.h"

#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UILayoutPass.h"

namespace sw
{
    BoxPanel::BoxPanel()
        : PanelWidget{}
        , _spacing{ 0.0f }
        , _orientation{ UIOrientation::Horizontal }
    {
    }

    BoxPanel::~BoxPanel() = default;

    const TypeInfo* BoxPanel::getTypeInfo() const
    {
        return StaticType();
    }

    void BoxPanel::setOrientation( UIOrientation orientation )
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

    float2 BoxPanel::computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const
    {
        // 1) Auto 자식은 주축 무한으로 잰다(원하는 만큼). 2) 남은 주축을 Fill 자식에게 비율대로 주고 그 크기로 잰다 —
        //    줄 바꿈 글이 Fill 칸에 들어가면 그 칸 너비로 높이를 정한다(한 프레임 늦지 않다).
        float32       mainUsed       = 0.0f;
        float32       crossMax       = 0.0f;
        float32       fillWeight     = 0.0f;
        uint32        visibleCount   = 0;
        const float32 crossAvailable = UILayoutPass::getCrossAxis( _orientation, availableSize );
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            ++visibleCount;
            const WidgetLayoutSlot& slot = child.getLayoutSlot();
            if ( slot._sizeRule == UISizeRule::Fill )
            {
                fillWeight += slot._fillWeight;
                continue;
            }
            const float32 crossPadding = UILayoutPass::getCrossPadding( _orientation, slot._padding );
            const float2  desired      = UILayoutPass::measure(
                child, context, UILayoutPass::makeAxisVector( _orientation, kUIUnbounded, UILayoutPass::computeRemaining( crossAvailable, crossPadding ) ) );
            mainUsed += UILayoutPass::getMainAxis( _orientation, desired ) + UILayoutPass::getMainPadding( _orientation, slot._padding );
            crossMax = MathUtil::max( crossMax, UILayoutPass::getCrossAxis( _orientation, desired ) + crossPadding );
        }
        const float32 spacingTotal  = visibleCount > 1 ? _spacing * static_cast<float32>( visibleCount - 1 ) : 0.0f;
        const float32 mainAvailable = UILayoutPass::getMainAxis( _orientation, availableSize );
        const float32 remaining     = UILayoutPass::computeRemaining( mainAvailable, mainUsed + spacingTotal );
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget&                 child = *getChild( index );
            const WidgetLayoutSlot& slot  = child.getLayoutSlot();
            if ( child.getVisibility() == WidgetVisibility::Collapsed || slot._sizeRule != UISizeRule::Fill )
                continue;
            const bool    bUnbounded   = UILayoutPass::isUnbounded( remaining ) || fillWeight <= 0.0f;
            const float32 share        = bUnbounded ? kUIUnbounded : remaining * slot._fillWeight / fillWeight;
            const float32 mainPadding  = UILayoutPass::getMainPadding( _orientation, slot._padding );
            const float32 crossPadding = UILayoutPass::getCrossPadding( _orientation, slot._padding );
            const float2  desired      = UILayoutPass::measure( child, context,
                                                                UILayoutPass::makeAxisVector( _orientation, UILayoutPass::computeRemaining( share, mainPadding ),
                                                                                              UILayoutPass::computeRemaining( crossAvailable, crossPadding ) ) );
            mainUsed += UILayoutPass::getMainAxis( _orientation, desired ) + mainPadding;
            crossMax = MathUtil::max( crossMax, UILayoutPass::getCrossAxis( _orientation, desired ) + crossPadding );
        }
        return UILayoutPass::makeAxisVector( _orientation, mainUsed + spacingTotal, crossMax );
    }

    void BoxPanel::arrangeChildren( const UILayoutContext& context, const float2& size )
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
            if ( slot._sizeRule == UISizeRule::Fill )
                fillWeight += slot._fillWeight;
            else
                autoTotal += UILayoutPass::getMainAxis( _orientation, child.getDesiredSize() ) + UILayoutPass::getMainPadding( _orientation, slot._padding );
        }
        const float32 spacingTotal = visibleCount > 1 ? _spacing * static_cast<float32>( visibleCount - 1 ) : 0.0f;
        const float32 mainSize     = UILayoutPass::getMainAxis( _orientation, size );
        const float32 crossSize    = UILayoutPass::getCrossAxis( _orientation, size );
        const float32 remaining    = MathUtil::max( 0.0f, mainSize - autoTotal - spacingTotal );
        float32       cursor       = 0.0f;
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const WidgetLayoutSlot& slot      = child.getLayoutSlot();
            float32                 childMain = 0.0f;
            if ( slot._sizeRule == UISizeRule::Fill )
                childMain = fillWeight > 0.0f ? remaining * slot._fillWeight / fillWeight : 0.0f;
            else
                childMain = UILayoutPass::getMainAxis( _orientation, child.getDesiredSize() ) + UILayoutPass::getMainPadding( _orientation, slot._padding );
            arrangeChild( context, child, UILayoutPass::makeAxisVector( _orientation, cursor, 0.0f ), UILayoutPass::makeAxisVector( _orientation, childMain, crossSize ) );
            cursor += childMain + _spacing;
        }
    }
} // namespace sw
