#include "pch.h"

#include "Engine/UI/Style/UiStylePass.h"

#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Animation/UiStyleTransition.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Style/UiStyleSet.h"
#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    uint32 UiStylePass::makeDirtyReason( uint32 changedFields )
    {
        if ( changedFields == 0 )
            return WidgetDirty::kNone;
        uint32 reason = WidgetDirty::kPaint;
        for ( uint32 index = 0; index < static_cast<uint32>( UiStyleField::Count ); ++index )
        {
            const UiStyleField field = static_cast<UiStyleField>( index );
            if ( ( changedFields & UiStyleFieldTable::makeBit( field ) ) == 0 )
                continue;
            const uint32 flags = UiStyleFieldTable::getEntry( field )._flags;
            if ( ( flags & UiStyleFieldTable::kAffectsLayout ) != 0 )
                reason |= WidgetDirty::kLayout;
            if ( ( flags & UiStyleFieldTable::kAffectsSubtree ) != 0 )
                reason |= WidgetDirty::kTransform;
        }
        return reason;
    }

    uint32 UiStylePass::update( WidgetTree& tree, UiStyleSet& styleSet, bool bNavigationMode )
    {
        if ( tree._listStyleDirty.empty() )
            return 0;
        // 목록을 떼어 걷는다 — 다시 맞추며 거는 무효화(kLayout · kPaint)가 목록을 건드리지 않게.
        vector<WidgetId> listDirty;
        listDirty.swap( tree._listStyleDirty );
        uint32 restyledCount = 0;
        for ( const WidgetId id : listDirty )
        {
            Widget* pWidget = tree.findWidgetById( id );
            if ( pWidget == nullptr || ( pWidget->_dirtyFlags & WidgetDirty::kStyle ) == 0 )
                continue; // 떨어졌거나 이미 조상과 함께 맞췄다
            // 가장 위의 스타일 더러운 조상부터 — 부모 스타일이 먼저 정해져야 한다.
            Widget* pStart = pWidget;
            for ( Widget* pAncestor = pWidget->getParent(); pAncestor != nullptr; pAncestor = pAncestor->getParent() )
            {
                if ( ( pAncestor->_dirtyFlags & WidgetDirty::kStyle ) != 0 )
                    pStart = pAncestor;
            }
            restyledCount += restyle( *pStart, styleSet, bNavigationMode );
        }
        return restyledCount;
    }

    uint32 UiStylePass::restyle( Widget& widget, UiStyleSet& styleSet, bool bNavigationMode )
    {
        const bool bSelfDirty = ( widget._dirtyFlags & WidgetDirty::kStyle ) != 0;
        // 부모는 목표(나눠 쓰는) 스타일로 — 전환 중인 보간 값을 물려받으면 나눠 쓰기 열쇠가 프레임마다 바뀐다.
        const UiComputedStyle*                  pParentStyle = widget.getParent() != nullptr ? widget.getParent()->_computedStyle.get() : nullptr;
        uint64                                  ancestorKey{ 0 };
        const shared_ptr<const UiComputedStyle> style         = styleSet.computeStyle( widget, pParentStyle, bNavigationMode, ancestorKey );
        const uint32                            changedFields = UiComputedStyle::computeChangedFields( widget._computedStyle.get(), style.get() );
        const bool                              bPropagate    = style != widget._computedStyle || ancestorKey != widget._styleAncestorKey;
        if ( changedFields != 0 )
            UiStyleTransition::onStyleChanged( widget, widget._computedStyle.get(), style.get(), changedFields );
        widget._computedStyle    = style;
        widget._styleAncestorKey = ancestorKey;
        widget._dirtyFlags &= ~WidgetDirty::kStyle;

        uint32 reason = makeDirtyReason( changedFields );
        if ( bSelfDirty )
            reason |= WidgetDirty::kPaint;
        if ( reason != WidgetDirty::kNone )
            widget.invalidate( reason );

        uint32             restyledCount = 1;
        const PanelWidget* pPanel        = castTo<const PanelWidget>( &widget );
        if ( pPanel == nullptr )
            return restyledCount;
        for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
        {
            Widget& child = *pPanel->getChild( index );
            if ( bPropagate || ( child._dirtyFlags & WidgetDirty::kStyle ) != 0 )
                restyledCount += restyle( child, styleSet, bNavigationMode );
        }
        return restyledCount;
    }
} // namespace sw
