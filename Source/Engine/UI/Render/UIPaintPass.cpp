#include "pch.h"

#include "Engine/UI/Render/UIPaintPass.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Style/WidgetStyle.h"

#include <cstdlib>

namespace sw
{
    SW_LOG_CALLER( "UIPaintPass" );

    namespace
    {
        struct UIPaintPassInternal
        {
            /** @brief 이 위젯만 다시 칠하게 하는 무효화입니다. `kStyle` 은 스타일 순회가 먼저 비우고 필요하면 `kPaint` 를 겁니다. 순회 없이 칠하는 쪽(테스트)에서는 이 비트가 남아 있으므로 그리기가 처리합니다. */
            static constexpr uint32 kSelfPaintBits = WidgetDirty::kPaint | WidgetDirty::kTransform | WidgetDirty::kStyle | WidgetDirty::kVisibility;
            /** @brief 자손까지 다시 칠하게 하는 무효화입니다. 불투명도와 렌더 변환은 자손 캐시에 반영되어 있기 때문입니다. */
            static constexpr uint32 kSubtreePaintBits = WidgetDirty::kTransform | WidgetDirty::kVisibility;
            /** @brief 자르기 밖이라 순회하지 않은 위젯에 남기는 비트입니다. 다시 보일 때 자기와 자손을 다시 칠합니다(배율이나 아틀라스가 바뀐 뒤의 옛 캐시). */
            static constexpr uint32 kCulledRepaintBits = WidgetDirty::kTransform;
            /** @brief 순회가 끝나면 지우는 비트입니다. */
            static constexpr uint32 kClearBits = kSelfPaintBits;

            /** @brief 포커스 테두리 색입니다(위젯의 계산된 스타일이 `_focusRingColor` 를 정하지 않았을 때). */
            static constexpr float32 kFocusRingRed   = 1.0f;
            static constexpr float32 kFocusRingGreen = 0.78f;
            static constexpr float32 kFocusRingBlue  = 0.2f;
            /** @brief 포커스 테두리 두께, 바깥으로 넓히는 길이, 모서리 반지름(UI 단위)입니다. */
            static constexpr float32 kFocusRingWidth  = 2.0f;
            static constexpr float32 kFocusRingOutset = 3.0f;
            static constexpr float32 kFocusRingRadius = 6.0f;
            /** @brief 보이는 자식 범위를 이분 탐색으로 찾는 자식 수 임계값입니다. 그보다 적으면 자식마다 자르기 검사하는 편이 더 쌉니다. */
            static constexpr uint32 kVisibleRangeSearchMinChildCount = 64;

            /**
             * @brief 자식 슬롯이 위에서 아래로 놓인 패널에서 자르기 안에 들 수 있는 자식 범위 [@p outBegin, @p outEnd) 를 찾습니다.
             * @details 슬롯 위 변(`_lastSlotPosition._y`, 패널 로컬)이 자식 순서대로 같거나 커지므로, 위 변이 (자르기 위 − 최대 슬롯 높이) 보다 작은 자식은
             *          아래 변도 자르기 위에 있다. 끝은 위 변이 자르기 아래를 넘는 첫 자식이다. 탐색이 Collapsed 자식(지난 슬롯이 낡았다)을 만나면
             *          범위를 줄이지 않는다(0 부터 자식 수까지). 패널 기하가 축 정렬이 아니거나 자르기가 없으면 false 를 반환한다.
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

                // 첫 자식: 슬롯 위 변이 localTop 이상인 첫 위치.
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
                // 끝: 슬롯 위 변이 localBottom 을 넘는 첫 위치.
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
    bool UIPaintPass::canReuseOutput( const WidgetTree& tree, const UIPaintContext& context, const float2& targetSize )
    {
        if ( tree._paintOutput == nullptr )
            return false;
        const bool bSameScaleAndAtlas = tree._paintUIScale == context._uiScale && tree._paintAtlasGeneration == context._atlasGeneration;
        return tree._bPaintOutputStale == SW_FALSE && bSameScaleAndAtlas && tree._paintOutput->_targetSize == targetSize;
    }

    uint32 UIPaintPass::paint( WidgetTree& tree, const UIPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas )
    {
        // 스크래치는 칠하는 쪽이 소유해 넘기는 필수 값이다. 없으면 z 순서 패널을 칠할 수 없으므로 멈춘다.
        if ( context._pScratch == nullptr )
        {
            SW_LOG_ERROR( "UIPaintPass::paint needs UIPaintContext::_pScratch - the painting owner must pass its scratch" );
            std::abort();
        }
        // 지난 순회 뒤 트리에 무효화가 하나도 없고 배율과 아틀라스도 그대로면 순회하지 않고 지난 목록을 낸다(멈춘 HUD 나 메뉴는 위젯 수와 무관하게 이어 붙이기 한 번으로 끝난다).
        const bool bReusePaintOutput = canReuseOutput( tree, context, outCanvas._targetSize );
        const bool bScaleChanged     = tree._paintUIScale != context._uiScale;
        const bool bAtlasChanged     = tree._paintAtlasGeneration != context._atlasGeneration;
        tree._paintUIScale           = context._uiScale;
        tree._paintAtlasGeneration   = context._atlasGeneration;
        if ( tree._paintOutput == nullptr )
            tree._paintOutput = make_unique<CanvasDrawList>();
        CanvasDrawList& output = *tree._paintOutput;
        if ( bReusePaintOutput )
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
        // 그리기와 스타일 목록은 이번 순회가 다 봤다(안 보이는 위젯 아래는 비트가 남아 다시 보일 때 칠한다).
        tree._listPaintDirty.clear();
        tree._listStyleDirty.clear();
        // 글리프 래스터화 예산이 차서 글리프를 미룬 위젯은 다음 프레임에 다시 칠한다. 위 비움 뒤에 무효화해야 목록과 트리의 "바뀜" 표시가 남는다.
        vector<WidgetID>& listRetry = context._pScratch->_listGlyphRetryWidget;
        for ( const WidgetID retryID : listRetry )
        {
            Widget* pRetry = tree.findWidgetByID( retryID );
            if ( pRetry != nullptr )
                pRetry->invalidate( WidgetDirty::kPaint );
        }
        listRetry.clear();
        return paintedCount;
    }

    void UIPaintPass::paintFocusRing( const Widget& widget, CanvasPainter& painter )
    {
        using Internal                 = UIPaintPassInternal;
        const WidgetGeometry& geometry = widget.getGeometry();
        CanvasBrush           ring{};
        ring._color                   = float4{};
        const UIComputedStyle* pStyle = widget.getComputedStyle();
        ring._borderColor             = pStyle != nullptr && pStyle->has( UIStyleField::FocusRingColor )
                                          ? pStyle->_value._focusRingColor
                                          : float4{ Internal::kFocusRingRed, Internal::kFocusRingGreen, Internal::kFocusRingBlue, 1.0f };
        ring._borderWidth             = Internal::kFocusRingWidth;
        ring._cornerRadius            = float4{ Internal::kFocusRingRadius, Internal::kFocusRingRadius, Internal::kFocusRingRadius, Internal::kFocusRingRadius };
        painter.pushTransform( makeWidgetTransform( geometry ) );
        painter.fillRect( float2{ -Internal::kFocusRingOutset, -Internal::kFocusRingOutset },
                          float2{ geometry._size._x + 2.0f * Internal::kFocusRingOutset, geometry._size._y + 2.0f * Internal::kFocusRingOutset }, ring );
        painter.popTransform();
    }

    CanvasTransform UIPaintPass::makeWidgetTransform( const WidgetGeometry& geometry )
    {
        CanvasTransform transform{};
        transform._axisX       = geometry._axisX;
        transform._axisY       = geometry._axisY;
        transform._translation = geometry._translation;
        return transform;
    }

    uint32 UIPaintPass::paintWidget( Widget& widget, const UIPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas, bool bForce,
                                     bool bAtlasChanged )
    {
        using Internal = UIPaintPassInternal;
        if ( widget.isVisible() == false )
            return 0;

        const uint32 flags       = widget._dirtyFlags;
        const bool   bNoCache    = widget._paintCache == nullptr;
        const bool   bSelfDirty  = bForce || bNoCache || ( flags & Internal::kSelfPaintBits ) != 0 || ( bAtlasChanged && widget.usesGlyphAtlas() );
        const bool   bChildForce = bForce || ( flags & Internal::kSubtreePaintBits ) != 0;
        if ( bNoCache )
            widget._paintCache = make_unique<WidgetPaintCache>();
        WidgetPaintCache& cache          = *widget._paintCache;
        uint32            paintedCount   = 0;
        bool              bGlyphDeferred = false; // 이 위젯이 자기 캐시를 칠하다 글리프를 미뤘는지(자손이 미룬 것은 자손이 적는다)

        // 화면 앵커 — 이 아래 사각형은 앵커 점 기준이다. 대상 픽셀의 가위는 앵커를 따라가지 않으므로 앵커 위와 아래에 자르기가 없어야 한다.
        const uint32 previousAnchor = painter.getAnchor();
        if ( widget._paintAnchor != invalid_index::kUint32 )
        {
            SW_ASSERT( painter.hasClip() == false );
            painter.setAnchor( widget._paintAnchor );
        }
        painter.pushOpacity( widget.computeEffectiveOpacity() );
        if ( bSelfDirty )
        {
            const uint32 deferredBefore = painter.getDeferredGlyphCount();
            repaintCache( widget, context, painter, outCanvas, cache._under, false );
            bGlyphDeferred = painter.getDeferredGlyphCount() != deferredBefore;
            ++paintedCount;
        }
        if ( cache._under.isEmpty() == false )
            outCanvas.appendDrawList( cache._under );

        PanelWidget* const pPanel = widget.asPanel();
        if ( pPanel != nullptr && pPanel->getChildCount() > 0 )
        {
            const bool bClip = pPanel->clipsChildren();
            SW_ASSERT( bClip == false || painter.getAnchor() == invalid_index::kUint32 );
            if ( bClip )
            {
                painter.pushTransform( makeWidgetTransform( widget.getGeometry() ) );
                painter.pushClip( float2{}, widget.getGeometry()._size, 0.0f );
                painter.popTransform();
            }
            if ( pPanel->hasCustomPaintOrder() )
            {
                // 바깥 배열이 재귀 중에 커지면 안쪽 목록이 옮겨지므로 참조를 들고 있지 않고 매번 인덱스로 읽는다.
                UIPaintScratch& scratch = *context._pScratch; // `paint` 가 입구에서 확인했다
                const uint32    depth   = scratch._paintOrderDepth;
                if ( scratch._listPaintOrderByDepth.size() <= depth )
                    scratch._listPaintOrderByDepth.resize( depth + 1 );
                pPanel->collectPaintOrder( scratch._listPaintOrderByDepth[depth] );
                const uint32 orderCount = static_cast<uint32>( scratch._listPaintOrderByDepth[depth].size() );
                ++scratch._paintOrderDepth;
                for ( uint32 order = 0; order < orderCount; ++order )
                {
                    const uint32 index = scratch._listPaintOrderByDepth[depth][order];
                    paintedCount += paintChild( *pPanel->getChild( index ), context, painter, outCanvas, bChildForce, bAtlasChanged );
                }
                --scratch._paintOrderDepth;
            }
            else
            {
                // 범위 밖 자식은 순회하지 않는다. 자르기 밖 자식과 같게 강제 칠하기를 비트로 남긴다.
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
        {
            const uint32 deferredBefore = painter.getDeferredGlyphCount();
            repaintCache( widget, context, painter, outCanvas, cache._over, true );
            bGlyphDeferred = bGlyphDeferred || painter.getDeferredGlyphCount() != deferredBefore;
        }
        if ( cache._over.isEmpty() == false )
            outCanvas.appendDrawList( cache._over );
        painter.popOpacity();
        painter.setAnchor( previousAnchor );
        widget._dirtyFlags &= ~Internal::kClearBits;
        if ( bGlyphDeferred )
            context._pScratch->_listGlyphRetryWidget.push_back( widget.getID() );
        return paintedCount;
    }

    uint32 UIPaintPass::paintChild( Widget& widget, const UIPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas, bool bForce,
                                    bool bAtlasChanged )
    {
        // 자르는 조상 밖에 통째로 있는 자식은 순회하지 않는다. 더티 비트와 강제 칠하기는 위젯에 남아 다시 보일 때 칠한다(스크롤 목록 1 만 칸 중 보이는 칸만큼만 처리한다).
        // 강제(배율이나 아틀라스가 바뀜)는 순회하지 않은 위젯에 비트로 남긴다. 다음에 보일 때 그 그리기 캐시는 옛 배율이므로 다시 칠해야 한다.
        const WidgetGeometry& geometry = widget.getGeometry();
        const bool            bOutside = painter.isOutsideClip( makeWidgetTransform( geometry ), geometry._size );
        if ( bOutside )
        {
            if ( bForce || bAtlasChanged )
                widget._dirtyFlags |= UIPaintPassInternal::kCulledRepaintBits;
            return 0;
        }
        return paintWidget( widget, context, painter, outCanvas, bForce, bAtlasChanged );
    }

    void UIPaintPass::repaintCache( const Widget& widget, const UIPaintContext& context, CanvasPainter& painter, const CanvasDrawList& outCanvas,
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
