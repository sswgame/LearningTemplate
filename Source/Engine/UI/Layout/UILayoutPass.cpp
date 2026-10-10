#include "pch.h"

#include "Engine/UI/Layout/UILayoutPass.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    namespace
    {
        /** @brief 레이아웃 걷기 번호입니다(0 은 "잰 적 없음"). 게임 스레드만 — 걷기마다 하나 늘어 같은 걷기 안의 다시 재기를 막는다. */
        uint32 s_layoutSerial = 0;
        /** @brief `computeDesiredSize` 호출 수(누적) — `update` 가 앞뒤 차로 이번 걷기의 수를 낸다. */
        uint32 s_measureCount = 0;

        struct UILayoutPassInternal
        {
            static constexpr uint32 kLayoutBits = WidgetDirty::kLayout | WidgetDirty::kChildLayout;
            static constexpr uint32 kClearBits  = WidgetDirty::kLayout | WidgetDirty::kChildLayout | WidgetDirty::kArrange | WidgetDirty::kLayoutRoot;

            /** @brief 한 축의 슬롯 적용 — 크기와 앞쪽 띄움을 정합니다. */
            static void applySlotAxis( UIAlignment alignment, float32 overrideLength, float32 maxLength, float32 desired, float32 inner, float32& outOffset,
                                       float32& outLength )
            {
                const bool bFixed = overrideLength > 0.0f;
                float32    length = ( alignment == UIAlignment::Fill && bFixed == false ) ? inner : MathUtil::min( desired, inner );
                if ( bFixed )
                    length = MathUtil::min( overrideLength, inner );
                if ( maxLength > 0.0f )
                    length = MathUtil::min( length, maxLength );
                const UIAlignment placeAlignment = alignment == UIAlignment::Fill ? UIAlignment::Start : alignment;
                outOffset                        = UILayoutPass::computeAlignmentOffset( placeAlignment, inner - length );
                outLength                        = length;
            }

            /** @brief 오른쪽에서 왼쪽 부모가 읽는 슬롯입니다 — 여백 왼 ↔ 오, 가로 정렬 Start ↔ End(위치 거울은 `PanelWidget::arrangeChild` 가 한다). */
            static WidgetLayoutSlot makeMirroredSlot( const WidgetLayoutSlot& slot )
            {
                WidgetLayoutSlot mirrored = slot;
                mirrored._padding._x      = slot._padding._z;
                mirrored._padding._z      = slot._padding._x;
                if ( slot._horizontalAlignment == UIAlignment::Start )
                    mirrored._horizontalAlignment = UIAlignment::End;
                else if ( slot._horizontalAlignment == UIAlignment::End )
                    mirrored._horizontalAlignment = UIAlignment::Start;
                return mirrored;
            }

            /** @brief 물리 픽셀에 맞춘 변 두 개로 한 축을 고칩니다(@p origin 은 부모 로컬 원점의 화면 좌표). */
            static void snapAxis( float32 origin, float32 uiScale, float32& inoutPosition, float32& inoutLength )
            {
                const float32 begin = MathUtil::round( ( origin + inoutPosition ) * uiScale ) / uiScale;
                const float32 end   = MathUtil::round( ( origin + inoutPosition + inoutLength ) * uiScale ) / uiScale;
                inoutPosition       = begin - origin;
                inoutLength         = MathUtil::max( 0.0f, end - begin );
            }

            /** @brief 부모 기하 안 로컬 사각형의 기하를 만듭니다(렌더 변환 적용 전). */
            static WidgetGeometry composeGeometry( const WidgetGeometry& parent, const float2& localPosition, const float2& size )
            {
                WidgetGeometry placed{};
                placed._position    = float2{ parent._position._x + localPosition._x, parent._position._y + localPosition._y };
                placed._size        = size;
                placed._axisX       = parent._axisX;
                placed._axisY       = parent._axisY;
                placed._translation = parent.transformPoint( localPosition );
                return placed;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 UILayoutPass::update( WidgetTree& tree, const UILayoutContext& context )
    {
        ++s_layoutSerial;
        if ( s_layoutSerial == 0 )
            s_layoutSerial = 1;
        const uint32 measureCountBefore = s_measureCount;

        const bool bScaleChanged = tree._layoutUIScale != context._uiScale || tree._layoutTextScale != context._textScale;
        if ( bScaleChanged )
        {
            invalidateAllLayout( tree );
            tree._layoutUIScale   = context._uiScale;
            tree._layoutTextScale = context._textScale;
        }

        Widget* const pRoot = tree.getRoot();
        if ( pRoot == nullptr )
        {
            tree._listLayoutDirtyRoot.clear();
            return 0;
        }
        // 문화권 방향이 바뀌어 루트의 방향이 달라지면 트리 전체를 다시 놓는다(크기는 그대로 — measure 는 캐시).
        if ( pRoot->_layoutSerial != 0 && pRoot->_bRightToLeft != resolveRightToLeft( pRoot->getFlowDirection(), context._bRightToLeft ) )
            pRoot->_dirtyFlags |= WidgetDirty::kArrange;

        // 뿌리 목록을 먼저 떼어 둔다 — 걷는 동안 그리기 더러움만 생긴다(arrange 의 기하 변경). 목록 순서대로 처리하고, 이미 위에서 다시 잰 뿌리는
        // 비트가 지워져 건너뛴다. 처리 순서가 어떻든 마지막에 놓인 쪽이 맞다 — 조상이 나중이면 조상이 다시 놓는다.
        vector<WidgetID> listRoot;
        listRoot.swap( tree._listLayoutDirtyRoot );

        const bool bInsetsChanged = tree._layoutSafeInsets != context._safeInsets;
        tree._layoutSafeInsets    = context._safeInsets;
        const bool bRootPending   = bInsetsChanged || pRoot->_layoutSerial == 0 || pRoot->_lastSlotSize != context._viewportSize ||
                                  ( pRoot->_dirtyFlags & UILayoutPassInternal::kClearBits ) != 0;
        if ( bRootPending )
            layoutTreeRoot( *pRoot, context );

        for ( const WidgetID id : listRoot )
        {
            Widget* const pWidget = tree.findWidgetByID( id );
            if ( pWidget == nullptr || pWidget == pRoot || ( pWidget->_dirtyFlags & WidgetDirty::kLayoutRoot ) == 0 )
                continue;
            if ( pWidget->_layoutSerial == 0 || pWidget->getParent() == nullptr )
                continue; // 아직 놓인 적이 없다 — 조상의 걷기가 처음 잰다
            measure( *pWidget, context, pWidget->_lastAvailableSize );
            arrange( *pWidget, context, pWidget->getParent()->getGeometry(), pWidget->_lastSlotPosition, pWidget->_lastSlotSize );
            clearLayoutFlags( *pWidget );
        }
        return s_measureCount - measureCountBefore;
    }

    float2 UILayoutPass::measure( Widget& widget, const UILayoutContext& context, const float2& availableSize )
    {
        if ( widget.getVisibility() == WidgetVisibility::Collapsed )
            return float2{};

        const WidgetLayoutSlot& slot  = widget.getLayoutSlot();
        float2                  inner = availableSize;
        if ( slot._widthOverride > 0.0f )
            inner._x = slot._widthOverride;
        else if ( slot._maxSize._x > 0.0f )
            inner._x = MathUtil::min( inner._x, slot._maxSize._x );
        if ( slot._heightOverride > 0.0f )
            inner._y = slot._heightOverride;
        else if ( slot._maxSize._y > 0.0f )
            inner._y = MathUtil::min( inner._y, slot._maxSize._y );

        const bool bCached = isMeasureStale( widget ) == false && widget._lastAvailableSize == inner;
        if ( bCached )
            return widget._desiredSize;

        float2 desired = widget.computeDesiredSize( context, inner );
        ++s_measureCount;
        if ( slot._widthOverride > 0.0f )
            desired._x = slot._widthOverride;
        if ( slot._heightOverride > 0.0f )
            desired._y = slot._heightOverride;
        desired._x = MathUtil::max( desired._x, slot._minSize._x );
        desired._y = MathUtil::max( desired._y, slot._minSize._y );
        if ( slot._maxSize._x > 0.0f )
            desired._x = MathUtil::min( desired._x, slot._maxSize._x );
        if ( slot._maxSize._y > 0.0f )
            desired._y = MathUtil::min( desired._y, slot._maxSize._y );

        widget._desiredSize       = desired;
        widget._lastAvailableSize = inner;
        widget._layoutSerial      = s_layoutSerial;
        return desired;
    }

    void UILayoutPass::arrange( Widget& widget, const UILayoutContext& context, const WidgetGeometry& parentGeometry, const float2& slotPosition,
                                const float2& slotSize )
    {
        if ( widget.getVisibility() == WidgetVisibility::Collapsed )
            return;
        // 이번 걷기에서 재지 않은 더러운 위젯(배치만 다시 하는 조상 아래의 경계)은 놓기 전에 지난 가용 크기로 다시 잰다.
        if ( isMeasureStale( widget ) )
            measure( widget, context, widget._layoutSerial == 0 ? slotSize : widget._lastAvailableSize );

        widget._lastSlotPosition = slotPosition;
        widget._lastSlotSize     = slotSize;

        // 흐름 방향: 자기 슬롯(여백 · 정렬)은 부모의 방향으로 읽고, 자기 방향은 자식을 놓을 때 쓴다.
        const PanelWidget* const pParent            = widget.getParent();
        const bool               bParentRightToLeft = pParent != nullptr ? pParent->isRightToLeft() : context._bRightToLeft;
        const bool               bRightToLeft       = resolveRightToLeft( widget.getFlowDirection(), bParentRightToLeft );
        if ( widget._bRightToLeft != bRightToLeft )
        {
            widget._bRightToLeft = bRightToLeft;
            widget.invalidate( WidgetDirty::kPaint ); // 기하가 그대로여도 그림이 방향을 따른다(글 문단 방향 · 거울 그림)
        }

        float2 position{};
        float2 size{};
        if ( bParentRightToLeft )
            applySlot( UILayoutPassInternal::makeMirroredSlot( widget.getLayoutSlot() ), widget.getDesiredSize(), slotPosition, slotSize, position, size );
        else
            applySlot( widget.getLayoutSlot(), widget.getDesiredSize(), slotPosition, slotSize, position, size );
        if ( parentGeometry.isAxisAligned() && context._uiScale > 0.0f )
        {
            UILayoutPassInternal::snapAxis( parentGeometry._translation._x, context._uiScale, position._x, size._x );
            UILayoutPassInternal::snapAxis( parentGeometry._translation._y, context._uiScale, position._y, size._y );
        }

        const WidgetGeometry placed = UILayoutPassInternal::composeGeometry( parentGeometry, position, size );
        widget.setArrangedGeometry( widget.getRenderTransform().applyTo( placed ) );

        PanelWidget* const pPanel = castTo<PanelWidget>( &widget );
        if ( pPanel != nullptr )
            pPanel->arrangeChildren( context, size );
    }

    void UILayoutPass::invalidateAllLayout( WidgetTree& tree )
    {
        for ( const auto& [id, pWidget] : tree._mapIDToWidget )
        {
            pWidget->_dirtyFlags |= UILayoutPassInternal::kLayoutBits;
        }
        if ( tree.getRoot() != nullptr )
            tree.addLayoutRoot( *tree.getRoot() );
    }

    bool UILayoutPass::isCultureRightToLeft( const LocalizationManager* pLocalization )
    {
        const LocalizationManager* pCulture = pLocalization != nullptr ? pLocalization : engine::getBoundEngineServices()._pLocalizationManager;
        return pCulture != nullptr && pCulture->isRightToLeft();
    }

    bool UILayoutPass::isMeasureStale( const Widget& widget )
    {
        return widget._layoutSerial == 0 || ( ( widget._dirtyFlags & UILayoutPassInternal::kLayoutBits ) != 0 && widget._layoutSerial != s_layoutSerial );
    }

    void UILayoutPass::clearLayoutFlags( Widget& widget )
    {
        if ( widget.getVisibility() == WidgetVisibility::Collapsed )
        {
            // 접힌 위젯은 재지 않았다 — 자기 kLayout 은 지워(다시 보이게 될 때 위로 번지게) 두고, kChildLayout 을 남겨 그때 그 아래를 다시 재게 한다.
            // 접힌 동안 자손이 바뀌면 번짐이 여기서 멈춘다(이미 kChildLayout).
            if ( ( widget._dirtyFlags & UILayoutPassInternal::kClearBits ) != 0 )
                widget._dirtyFlags = ( widget._dirtyFlags & ~UILayoutPassInternal::kClearBits ) | WidgetDirty::kChildLayout;
            return;
        }
        if ( ( widget._dirtyFlags & UILayoutPassInternal::kClearBits ) == 0 )
            return;
        widget._dirtyFlags &= ~UILayoutPassInternal::kClearBits;
        const PanelWidget* pPanel = castTo<const PanelWidget>( &widget );
        if ( pPanel == nullptr )
            return;
        for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
        {
            clearLayoutFlags( *pPanel->getChild( index ) );
        }
    }

    void UILayoutPass::layoutTreeRoot( Widget& root, const UILayoutContext& context )
    {
        const WidgetGeometry viewport = WidgetGeometry::makeAxisAligned( float2{}, context._viewportSize );
        measure( root, context, context._viewportSize );
        arrange( root, context, viewport, float2{}, context._viewportSize );
        clearLayoutFlags( root );
    }

    float32 UILayoutPass::computeRemaining( float32 available, float32 used )
    {
        if ( isUnbounded( available ) )
            return kUIUnbounded;
        return MathUtil::max( 0.0f, available - used );
    }

    void UILayoutPass::applySlot( const WidgetLayoutSlot& slot, const float2& desired, const float2& slotPosition, const float2& slotSize, float2& outPosition,
                                  float2& outSize )
    {
        const float32 innerWidth  = MathUtil::max( 0.0f, slotSize._x - slot._padding._x - slot._padding._z );
        const float32 innerHeight = MathUtil::max( 0.0f, slotSize._y - slot._padding._y - slot._padding._w );
        float32       offsetX     = 0.0f;
        float32       offsetY     = 0.0f;
        UILayoutPassInternal::applySlotAxis( slot._horizontalAlignment, slot._widthOverride, slot._maxSize._x, desired._x, innerWidth, offsetX, outSize._x );
        UILayoutPassInternal::applySlotAxis( slot._verticalAlignment, slot._heightOverride, slot._maxSize._y, desired._y, innerHeight, offsetY, outSize._y );
        outPosition._x = slotPosition._x + slot._padding._x + offsetX;
        outPosition._y = slotPosition._y + slot._padding._y + offsetY;
    }

    float32 UILayoutPass::computeAlignmentOffset( UIAlignment alignment, float32 freeSpace )
    {
        switch ( alignment )
        {
            case UIAlignment::Fill:
            case UIAlignment::Start:
                return 0.0f;
            case UIAlignment::Center:
                return freeSpace * 0.5f;
            case UIAlignment::End:
                return freeSpace;
        }
        return 0.0f;
    }
} // namespace sw
