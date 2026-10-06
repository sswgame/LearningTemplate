#include "pch.h"

#include "Engine/UI/Render/UiPaintPass.h"

#include "Core/Container/vector.h"

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

        uint32  paintedCount = 0;
        Widget* pRoot        = tree.getRoot();
        if ( pRoot != nullptr )
            paintedCount = paintWidget( *pRoot, context, painter, outCanvas, bScaleChanged, bAtlasChanged );
        painter.setDrawList( outCanvas );
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
                    paintedCount += paintWidget( *pPanel->getChild( index ), context, painter, outCanvas, bChildForce, bAtlasChanged );
            }
            else
            {
                for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
                    paintedCount += paintWidget( *pPanel->getChild( index ), context, painter, outCanvas, bChildForce, bAtlasChanged );
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
