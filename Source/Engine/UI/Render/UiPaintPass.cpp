#include "pch.h"

#include "Engine/UI/Render/UiPaintPass.h"

#include "Core/Container/vector.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Core/WidgetTypes.h"
#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    namespace
    {
        struct UiPaintPassInternal
        {
            /** @brief 이 위젯만 다시 칠하게 하는 무효화입니다. `kStyle` 은 스타일 걷기가 먼저 비우고 필요하면 `kPaint` 를 건다 — 걷기 없이 칠하는 쪽(시험)의 몫으로 남긴다. */
            static constexpr uint32 kSelfPaintBits = WidgetDirty::kPaint | WidgetDirty::kTransform | WidgetDirty::kStyle | WidgetDirty::kVisibility;
            /** @brief 자손까지 다시 칠하게 하는 무효화입니다 — 불투명도 · 렌더 변환은 자손 캐시에 구워져 있다. */
            static constexpr uint32 kSubtreePaintBits = WidgetDirty::kTransform | WidgetDirty::kVisibility;
            /** @brief 자르기 밖이라 걷지 않은 위젯에 남기는 비트 — 다시 보일 때 자기와 자손을 다시 칠한다(배율 · 아틀라스가 바뀐 뒤의 옛 캐시). */
            static constexpr uint32 kCulledRepaintBits = WidgetDirty::kTransform;
            /** @brief 걷기가 끝나면 지우는 비트입니다. */
            static constexpr uint32 kClearBits = kSelfPaintBits;

            /** @brief 포커스 테두리 색입니다(위젯의 계산된 스타일이 `_focusRingColor` 를 정하지 않았을 때). */
            static constexpr float32 kFocusRingRed   = 1.0f;
            static constexpr float32 kFocusRingGreen = 0.78f;
            static constexpr float32 kFocusRingBlue  = 0.2f;
            /** @brief 포커스 테두리 두께 · 바깥으로 넓히는 길이 · 모서리 반지름(UI 단위)입니다. */
            static constexpr float32 kFocusRingWidth  = 2.0f;
            static constexpr float32 kFocusRingOutset = 3.0f;
            static constexpr float32 kFocusRingRadius = 6.0f;
            /** @brief 보이는 자식 범위를 이분 탐색으로 찾는 자식 수 문턱입니다 — 그보다 적으면 자식마다 자르기 검사가 더 싸다. */
            static constexpr uint32 kVisibleRangeSearchMinChildCount = 64;

            /**
             * @brief 자식 슬롯이 위에서 아래로 놓인 패널에서 자르기 안에 들 수 있는 자식 범위 [@p outBegin, @p outEnd) 를 찾습니다.
             * @details 슬롯 위 변(`_lastSlotPosition._y`, 패널 로컬)이 자식 순서대로 같거나 커지므로, 위 변이 (자르기 위 − 최대 슬롯 높이) 보다 작은 자식은
             *          아래 변도 자르기 위에 있다. 끝은 위 변이 자르기 아래를 넘는 첫 자식이다. 탐색이 Collapsed 자식(지난 슬롯이 낡았다)을 만나면
             *          범위를 줄이지 않는다(0 · 자식 수). 패널 기하가 축 정렬이 아니거나 자르기가 없으면 false 입니다.
             */
            static bool findVisibleChildRange( const PanelWidget& panel, const CanvasPainter& painter, uint32& outBegin, uint32& outEnd )
            {
                outBegin           = 0;
                outEnd             = panel.getChildCount();
                float32 clipTop    = 0.0f;
                float32 clipBottom = 0.0f;
                if ( panel.getChildCount() < kVisibleRangeSearchMinChildCount || panel.isChildOrderTopToBottom() == false )
                    return false;
                const WidgetGeometry& geometry = panel.getGeometry();
                if ( geometry.isAxisAligned() == false || painter.findClipVerticalRange( clipTop, clipBottom ) == false )
                    return false;
                const float32 localTop    = clipTop - geometry._translation._y - panel.getMaxChildSlotHeight();
                const float32 localBottom = clipBottom - geometry._translation._y;

                // 첫 자식: 슬롯 위 변이 localTop 이상인 첫 자리.
                uint32 low  = 0;
                uint32 high = panel.getChildCount();
                while ( low < high )
                {
                    const uint32  middle = low + ( high - low ) / 2;
                    const Widget& child  = *panel.getChild( middle );
                    if ( child.getVisibility() == WidgetVisibility::Collapsed )
                        return false;
                    if ( child.getSlotPosition()._y < localTop )
                        low = middle + 1;
                    else
                        high = middle;
                }
                const uint32 begin = low;
                // 끝: 슬롯 위 변이 localBottom 을 넘는 첫 자리.
                high = panel.getChildCount();
                while ( low < high )
                {
                    const uint32  middle = low + ( high - low ) / 2;
                    const Widget& child  = *panel.getChild( middle );
                    if ( child.getVisibility() == WidgetVisibility::Collapsed )
                        return false;
                    if ( child.getSlotPosition()._y <= localBottom )
                        low = middle + 1;
                    else
                        high = middle;
                }
                outBegin = begin;
                outEnd   = low;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 UiPaintPass::paint( WidgetTree& tree, const UiPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas )
    {
        const bool bScaleChanged   = tree._paintUiScale != context._uiScale;
        const bool bAtlasChanged   = tree._paintAtlasGeneration != context._atlasGeneration;
        tree._paintUiScale         = context._uiScale;
        tree._paintAtlasGeneration = context._atlasGeneration;

        // 지난 걷기 뒤 트리에 무효화가 하나도 없고 배율 · 아틀라스도 그대로면 걷지 않고 지난 목록을 낸다(멈춘 HUD · 메뉴가 위젯 수와 무관하게 이어 붙이기 한 번).
        if ( tree._paintOutput == nullptr )
            tree._paintOutput = make_unique<CanvasDrawList>();
        CanvasDrawList& output = *tree._paintOutput;
        if ( tree._bPaintOutputStale == SW_FALSE && bScaleChanged == false && bAtlasChanged == false && output._targetSize == outCanvas._targetSize )
        {
            outCanvas.appendDrawList( output );
            return 0;
        }
        output.clear();
        output._targetSize   = outCanvas._targetSize;
        uint32  paintedCount = 0;
        Widget* pRoot        = tree.getRoot();
        if ( pRoot != nullptr )
            paintedCount = paintWidget( *pRoot, context, painter, output, bScaleChanged, bAtlasChanged );
        painter.setDrawList( outCanvas );
        outCanvas.appendDrawList( output );
        tree._bPaintOutputStale = SW_FALSE;
        // 그리기 · 스타일 목록은 이번 걷기가 다 봤다(안 보이는 아래는 비트가 남아 다시 보일 때 칠한다).
        tree._listPaintDirty.clear();
        tree._listStyleDirty.clear();
        return paintedCount;
    }

    void UiPaintPass::paintFocusRing( const Widget& widget, CanvasPainter& painter )
    {
        using Internal                 = UiPaintPassInternal;
        const WidgetGeometry& geometry = widget.getGeometry();
        CanvasBrush           ring{};
        ring._color                   = float4{};
        const UiComputedStyle* pStyle = widget.getComputedStyle();
        ring._borderColor             = pStyle != nullptr && pStyle->has( UiStyleField::FocusRingColor )
                                          ? pStyle->_value._focusRingColor
                                          : float4{ Internal::kFocusRingRed, Internal::kFocusRingGreen, Internal::kFocusRingBlue, 1.0f };
        ring._borderWidth             = Internal::kFocusRingWidth;
        ring._cornerRadius            = float4{ Internal::kFocusRingRadius, Internal::kFocusRingRadius, Internal::kFocusRingRadius, Internal::kFocusRingRadius };
        painter.pushTransform( makeWidgetTransform( geometry ) );
        painter.fillRect( float2{ -Internal::kFocusRingOutset, -Internal::kFocusRingOutset },
                          float2{ geometry._size._x + 2.0f * Internal::kFocusRingOutset, geometry._size._y + 2.0f * Internal::kFocusRingOutset }, ring );
        painter.popTransform();
    }

    CanvasTransform UiPaintPass::makeWidgetTransform( const WidgetGeometry& geometry )
    {
        CanvasTransform transform{};
        transform._axisX       = geometry._axisX;
        transform._axisY       = geometry._axisY;
        transform._translation = geometry._translation;
        return transform;
    }

    uint32 UiPaintPass::paintWidget( Widget& widget, const UiPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas, bool bForce,
                                     bool bAtlasChanged )
    {
        using Internal = UiPaintPassInternal;
        if ( widget.isVisible() == false )
            return 0;

        const uint32 flags       = widget._dirtyFlags;
        const bool   bNoCache    = widget._paintCache == nullptr;
        const bool   bSelfDirty  = bForce || bNoCache || ( flags & Internal::kSelfPaintBits ) != 0 || ( bAtlasChanged && widget.usesGlyphAtlas() );
        const bool   bChildForce = bForce || ( flags & Internal::kSubtreePaintBits ) != 0;
        if ( bNoCache )
            widget._paintCache = make_unique<WidgetPaintCache>();
        WidgetPaintCache& cache        = *widget._paintCache;
        uint32            paintedCount = 0;

        painter.pushOpacity( widget.computeEffectiveOpacity() );
        if ( bSelfDirty )
        {
            repaintCache( widget, context, painter, outCanvas, cache._under, false );
            ++paintedCount;
        }
        outCanvas.appendDrawList( cache._under );

        PanelWidget* const pPanel = castTo<PanelWidget>( &widget );
        if ( pPanel != nullptr && pPanel->getChildCount() > 0 )
        {
            const bool bClip = pPanel->clipsChildren();
            if ( bClip )
            {
                painter.pushTransform( makeWidgetTransform( widget.getGeometry() ) );
                painter.pushClip( float2{}, widget.getGeometry()._size, 0.0f );
                painter.popTransform();
            }
            if ( pPanel->hasCustomPaintOrder() )
            {
                vector<uint32> listOrder;
                pPanel->collectPaintOrder( listOrder );
                for ( const uint32 index : listOrder )
                {
                    paintedCount += paintChild( *pPanel->getChild( index ), context, painter, outCanvas, bChildForce, bAtlasChanged );
                }
            }
            else
            {
                // 범위 밖 자식은 걷지 않는다 — 자르기 밖 자식과 같게 강제 칠하기를 비트로 남긴다.
                uint32 begin = 0;
                uint32 end   = pPanel->getChildCount();
                if ( Internal::findVisibleChildRange( *pPanel, painter, begin, end ) && ( bChildForce || bAtlasChanged ) )
                {
                    for ( uint32 index = 0; index < begin; ++index )
                    {
                        pPanel->getChild( index )->_dirtyFlags |= Internal::kCulledRepaintBits;
                    }
                    for ( uint32 index = end; index < pPanel->getChildCount(); ++index )
                    {
                        pPanel->getChild( index )->_dirtyFlags |= Internal::kCulledRepaintBits;
                    }
                }
                for ( uint32 index = begin; index < end; ++index )
                {
                    paintedCount += paintChild( *pPanel->getChild( index ), context, painter, outCanvas, bChildForce, bAtlasChanged );
                }
            }
            if ( bClip )
                painter.popClip();
        }

        if ( bSelfDirty )
            repaintCache( widget, context, painter, outCanvas, cache._over, true );
        outCanvas.appendDrawList( cache._over );
        painter.popOpacity();
        widget._dirtyFlags &= ~Internal::kClearBits;
        return paintedCount;
    }

    uint32 UiPaintPass::paintChild( Widget& widget, const UiPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas, bool bForce,
                                    bool bAtlasChanged )
    {
        // 자르는 조상 밖에 통째로 있는 자식은 걷지 않는다 — 더러운 비트 · 강제 칠하기는 위젯에 남아 다시 보일 때 칠한다(스크롤 목록 1 만 칸이 보이는 칸만큼만 든다).
        // 강제(배율 · 아틀라스가 바뀜)는 걷지 않은 위젯에 비트로 남긴다 — 다음에 보일 때 그 그림 캐시는 옛 배율이라 다시 칠해야 한다.
        const WidgetGeometry& geometry = widget.getGeometry();
        const bool            bOutside = painter.isOutsideClip( makeWidgetTransform( geometry ), geometry._size );
        if ( bOutside )
        {
            if ( bForce || bAtlasChanged )
                widget._dirtyFlags |= UiPaintPassInternal::kCulledRepaintBits;
            return 0;
        }
        return paintWidget( widget, context, painter, outCanvas, bForce, bAtlasChanged );
    }

    void UiPaintPass::repaintCache( const Widget& widget, const UiPaintContext& context, CanvasPainter& painter, const CanvasDrawList& outCanvas,
                                    CanvasDrawList& outCache, bool bOver )
    {
        outCache.clear();
        outCache._targetSize = outCanvas._targetSize;
        painter.setDrawList( outCache );
        painter.pushTransform( makeWidgetTransform( widget.getGeometry() ) );
        if ( bOver )
            widget.paintOverChildren( painter, context );
        else
            widget.paint( painter, context );
        painter.popTransform();
    }
} // namespace sw
