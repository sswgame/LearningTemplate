#include "pch.h"

#include "Engine/UI/Layout/UiLayoutPass.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    namespace
    {
        /** @brief 레이아웃 걷기 번호입니다(0 은 "잰 적 없음"). 게임 스레드만 — 걷기마다 하나 늘어 같은 걷기 안의 다시 재기를 막는다. */
        uint32 s_layoutSerial = 0;
        /** @brief `computeDesiredSize` 호출 수(누적) — `update` 가 앞뒤 차로 이번 걷기의 수를 낸다. */
        uint32 s_measureCount = 0;

        struct UiLayoutPassInternal
        {
            static constexpr uint32 kLayoutBits = WidgetDirty::kLayout | WidgetDirty::kChildLayout;
            static constexpr uint32 kClearBits  = WidgetDirty::kLayout | WidgetDirty::kChildLayout | WidgetDirty::kArrange | WidgetDirty::kLayoutRoot;

            /** @brief 한 축의 슬롯 적용 — 크기와 앞쪽 띄움을 정합니다. */
            static void applySlotAxis( UiAlignment alignment, float32 overrideLength, float32 maxLength, float32 desired, float32 inner, float32& outOffset,
                                       float32& outLength )
            {
                const bool bFixed = overrideLength > 0.0f;
                float32    length = ( alignment == UiAlignment::Fill && bFixed == false ) ? inner : MathUtil::min( desired, inner );
                if ( bFixed )
                    length = MathUtil::min( overrideLength, inner );
                if ( maxLength > 0.0f )
                    length = MathUtil::min( length, maxLength );
                const UiAlignment placeAlignment = alignment == UiAlignment::Fill ? UiAlignment::Start : alignment;
                outOffset                        = UiLayoutPass::computeAlignmentOffset( placeAlignment, inner - length );
                outLength                        = length;
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
    uint32 UiLayoutPass::update( WidgetTree& tree, const UiLayoutContext& context )
    {
        ++s_layoutSerial;
        if ( s_layoutSerial == 0 )
            s_layoutSerial = 1;
        const uint32 measureCountBefore = s_measureCount;

        const bool bScaleChanged = tree._layoutUiScale != context._uiScale || tree._layoutTextScale != context._textScale;
        if ( bScaleChanged )
        {
            invalidateAllLayout( tree );
            tree._layoutUiScale   = context._uiScale;
            tree._layoutTextScale = context._textScale;
        }

        Widget* const pRoot = tree.getRoot();
        if ( pRoot == nullptr )
        {
            tree._listLayoutDirtyRoot.clear();
            return 0;
        }

        // 뿌리 목록을 먼저 떼어 둔다 — 걷는 동안 그리기 더러움만 생긴다(arrange 의 기하 변경). 목록 순서대로 처리하고, 이미 위에서 다시 잰 뿌리는
        // 비트가 지워져 건너뛴다. 처리 순서가 어떻든 마지막에 놓인 쪽이 맞다 — 조상이 나중이면 조상이 다시 놓는다.
        vector<WidgetId> listRoot;
        listRoot.swap( tree._listLayoutDirtyRoot );

        const bool bRootPending = pRoot->_layoutSerial == 0 || pRoot->_lastSlotSize != context._viewportSize ||
                                  ( pRoot->_dirtyFlags & UiLayoutPassInternal::kClearBits ) != 0;
        if ( bRootPending )
            layoutTreeRoot( *pRoot, context );

        for ( const WidgetId id : listRoot )
        {
            Widget* const pWidget = tree.findWidgetById( id );
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

    float2 UiLayoutPass::measure( Widget& widget, const UiLayoutContext& context, const float2& availableSize )
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

    void UiLayoutPass::arrange( Widget& widget, const UiLayoutContext& context, const WidgetGeometry& parentGeometry, const float2& slotPosition,
                                const float2& slotSize )
    {
        if ( widget.getVisibility() == WidgetVisibility::Collapsed )
            return;
        // 이번 걷기에서 재지 않은 더러운 위젯(배치만 다시 하는 조상 아래의 경계)은 놓기 전에 지난 가용 크기로 다시 잰다.
        if ( isMeasureStale( widget ) )
            measure( widget, context, widget._layoutSerial == 0 ? slotSize : widget._lastAvailableSize );

        widget._lastSlotPosition = slotPosition;
        widget._lastSlotSize     = slotSize;

        float2 position{};
        float2 size{};
        applySlot( widget.getLayoutSlot(), widget.getDesiredSize(), slotPosition, slotSize, position, size );
        if ( parentGeometry.isAxisAligned() && context._uiScale > 0.0f )
        {
            UiLayoutPassInternal::snapAxis( parentGeometry._translation._x, context._uiScale, position._x, size._x );
            UiLayoutPassInternal::snapAxis( parentGeometry._translation._y, context._uiScale, position._y, size._y );
        }

        const WidgetGeometry placed = UiLayoutPassInternal::composeGeometry( parentGeometry, position, size );
        widget.setArrangedGeometry( widget.getRenderTransform().applyTo( placed ) );

        PanelWidget* const pPanel = castTo<PanelWidget>( &widget );
        if ( pPanel != nullptr )
            pPanel->arrangeChildren( context, size );
    }

    void UiLayoutPass::invalidateAllLayout( WidgetTree& tree )
    {
        for ( const auto& [id, pWidget] : tree._mapIdToWidget )
            pWidget->_dirtyFlags |= UiLayoutPassInternal::kLayoutBits;
        if ( tree.getRoot() != nullptr )
            tree.addLayoutRoot( *tree.getRoot() );
    }

    bool UiLayoutPass::isMeasureStale( const Widget& widget )
    {
        return widget._layoutSerial == 0 || ( ( widget._dirtyFlags & UiLayoutPassInternal::kLayoutBits ) != 0 && widget._layoutSerial != s_layoutSerial );
    }

    void UiLayoutPass::clearLayoutFlags( Widget& widget )
    {
        if ( widget.getVisibility() == WidgetVisibility::Collapsed )
        {
            // 접힌 위젯은 재지 않았다 — 자기 kLayout 은 지워(다시 보이게 될 때 위로 번지게) 두고, kChildLayout 을 남겨 그때 그 아래를 다시 재게 한다.
            // 접힌 동안 자손이 바뀌면 번짐이 여기서 멈춘다(이미 kChildLayout).
            if ( ( widget._dirtyFlags & UiLayoutPassInternal::kClearBits ) != 0 )
                widget._dirtyFlags = ( widget._dirtyFlags & ~UiLayoutPassInternal::kClearBits ) | WidgetDirty::kChildLayout;
            return;
        }
        if ( ( widget._dirtyFlags & UiLayoutPassInternal::kClearBits ) == 0 )
            return;
        widget._dirtyFlags &= ~UiLayoutPassInternal::kClearBits;
        const PanelWidget* pPanel = castTo<const PanelWidget>( &widget );
        if ( pPanel == nullptr )
            return;
        for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
            clearLayoutFlags( *pPanel->getChild( index ) );
    }

    void UiLayoutPass::layoutTreeRoot( Widget& root, const UiLayoutContext& context )
    {
        const WidgetGeometry viewport = WidgetGeometry::makeAxisAligned( float2{}, context._viewportSize );
        measure( root, context, context._viewportSize );
        arrange( root, context, viewport, float2{}, context._viewportSize );
        clearLayoutFlags( root );
    }

    float32 UiLayoutPass::computeRemaining( float32 available, float32 used )
    {
        if ( isUnbounded( available ) )
            return kUiUnbounded;
        return MathUtil::max( 0.0f, available - used );
    }

    void UiLayoutPass::applySlot( const WidgetLayoutSlot& slot, const float2& desired, const float2& slotPosition, const float2& slotSize, float2& outPosition,
                                  float2& outSize )
    {
        const float32 innerWidth  = MathUtil::max( 0.0f, slotSize._x - slot._padding._x - slot._padding._z );
        const float32 innerHeight = MathUtil::max( 0.0f, slotSize._y - slot._padding._y - slot._padding._w );
        float32       offsetX     = 0.0f;
        float32       offsetY     = 0.0f;
        UiLayoutPassInternal::applySlotAxis( slot._horizontalAlignment, slot._widthOverride, slot._maxSize._x, desired._x, innerWidth, offsetX, outSize._x );
        UiLayoutPassInternal::applySlotAxis( slot._verticalAlignment, slot._heightOverride, slot._maxSize._y, desired._y, innerHeight, offsetY, outSize._y );
        outPosition._x = slotPosition._x + slot._padding._x + offsetX;
        outPosition._y = slotPosition._y + slot._padding._y + offsetY;
    }

    float32 UiLayoutPass::computeAlignmentOffset( UiAlignment alignment, float32 freeSpace )
    {
        switch ( alignment )
        {
            case UiAlignment::Fill:
            case UiAlignment::Start:
                return 0.0f;
            case UiAlignment::Center:
                return freeSpace * 0.5f;
            case UiAlignment::End:
                return freeSpace;
        }
        return 0.0f;
    }
} // namespace sw
