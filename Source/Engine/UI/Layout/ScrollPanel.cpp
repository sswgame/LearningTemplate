#include "pch.h"

#include "Engine/UI/Layout/ScrollPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/UI/Core/UiEvents.h"
#include "Engine/UI/Layout/UiLayoutPass.h"

namespace sw
{
    namespace
    {
        struct ScrollPanelInternal
        {
            /** @brief 한 축에서 [begin, end) 가 [margin, view − margin] 에 들도록 오프셋에 더할 길이입니다(0 이면 이미 보인다). */
            static float32 computeRevealDelta( float32 begin, float32 end, float32 viewLength, float32 margin )
            {
                if ( begin < margin )
                    return begin - margin;
                if ( end > viewLength - margin )
                    return MathUtil::min( end - ( viewLength - margin ), begin - margin );
                return 0.0f;
            }

            /** @brief 점이 로컬 사각형 안이면 true 입니다(왼쪽 · 위 변 포함). */
            static bool isInside( const float2& point, const float2& position, const float2& size )
            {
                return position._x <= point._x && point._x < position._x + size._x && position._y <= point._y && point._y < position._y + size._y;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ScrollPanel::ScrollPanel()
        : PanelWidget{}
        , _scrollOffset{}
        , _contentSize{}
        , _viewportSize{}
        , _dragStartLocal{}
        , _dragStartOffset{ 0.0f }
        , _dragAxis{ UiOrientation::Vertical }
        , _bDraggingBar{ false }
        , _navigationMargin{ 8.0f }
        , _wheelStep{ 48.0f }
        , _stickSpeed{ 1200.0f }
        , _scrollBarThickness{ 6.0f }
        , _minThumbLength{ 24.0f }
        , _bScrollHorizontal{ false }
        , _bScrollVertical{ true }
    {
        setClipChildren( true );
    }

    ScrollPanel::~ScrollPanel() = default;

    const TypeInfo* ScrollPanel::getTypeInfo() const
    {
        return StaticType();
    }

    void ScrollPanel::setScrollAxes( bool bHorizontal, bool bVertical )
    {
        if ( _bScrollHorizontal == bHorizontal && _bScrollVertical == bVertical )
            return;
        _bScrollHorizontal = bHorizontal;
        _bScrollVertical   = bVertical;
        invalidate( WidgetDirty::kLayout );
    }

    void ScrollPanel::setScrollOffset( const float2& scrollOffset )
    {
        const float2 clamped = clampOffset( scrollOffset );
        if ( clamped == _scrollOffset )
            return;
        _scrollOffset = clamped;
        invalidate( WidgetDirty::kArrange | WidgetDirty::kPaint ); // 막대 엄지 자리
    }

    void ScrollPanel::scrollBy( const float2& delta )
    {
        setScrollOffset( float2{ _scrollOffset._x + delta._x, _scrollOffset._y + delta._y } );
    }

    float2 ScrollPanel::getMaxScrollOffset() const
    {
        return float2{ MathUtil::max( 0.0f, _contentSize._x - _viewportSize._x ), MathUtil::max( 0.0f, _contentSize._y - _viewportSize._y ) };
    }

    bool ScrollPanel::scrollIntoView( const Widget& widget )
    {
        bool bDescendant = false;
        for ( const Widget* pAncestor = widget.getParent(); pAncestor != nullptr; pAncestor = pAncestor->getParent() )
        {
            if ( pAncestor == this )
            {
                bDescendant = true;
                break;
            }
        }
        if ( bDescendant == false )
            return false;
        // 지난 배치의 화면 사각형을 이 패널 기준으로 — 지금 오프셋은 이미 들어 있다.
        const WidgetGeometry& view   = getGeometry();
        const WidgetGeometry& target = widget.getGeometry();
        const float32         left   = target._position._x - view._position._x;
        const float32         top    = target._position._y - view._position._y;
        float2                delta{};
        if ( _bScrollHorizontal )
            delta._x = ScrollPanelInternal::computeRevealDelta( left, left + target._size._x, view._size._x, _navigationMargin );
        if ( _bScrollVertical )
            delta._y = ScrollPanelInternal::computeRevealDelta( top, top + target._size._y, view._size._y, _navigationMargin );
        const float2 before = _scrollOffset;
        scrollBy( delta );
        return before != _scrollOffset;
    }

    bool ScrollPanel::computeScrollBar( UiOrientation axis, ScrollBarLayout& outLayout ) const
    {
        const bool    bVertical  = axis == UiOrientation::Vertical;
        const bool    bScrolls   = bVertical ? _bScrollVertical : _bScrollHorizontal;
        const float2  maxOffset  = getMaxScrollOffset();
        const float32 axisMax    = bVertical ? maxOffset._y : maxOffset._x;
        const float32 viewLength = bVertical ? _viewportSize._y : _viewportSize._x;
        const float32 content    = bVertical ? _contentSize._y : _contentSize._x;
        if ( bScrolls == false || axisMax <= 0.0f || viewLength <= 0.0f || content <= 0.0f )
            return false;
        const float32 trackLength = viewLength;
        const float32 thumbLength = MathUtil::min( trackLength, MathUtil::max( _minThumbLength, trackLength * viewLength / content ) );
        const float32 offset      = bVertical ? _scrollOffset._y : _scrollOffset._x;
        const float32 thumbStart  = ( trackLength - thumbLength ) * ( offset / axisMax );
        if ( bVertical )
        {
            outLayout._trackPosition = float2{ _viewportSize._x - _scrollBarThickness, 0.0f };
            outLayout._trackSize     = float2{ _scrollBarThickness, trackLength };
            outLayout._thumbPosition = float2{ outLayout._trackPosition._x, thumbStart };
            outLayout._thumbSize     = float2{ _scrollBarThickness, thumbLength };
        }
        else
        {
            outLayout._trackPosition = float2{ 0.0f, _viewportSize._y - _scrollBarThickness };
            outLayout._trackSize     = float2{ trackLength, _scrollBarThickness };
            outLayout._thumbPosition = float2{ thumbStart, outLayout._trackPosition._y };
            outLayout._thumbSize     = float2{ thumbLength, _scrollBarThickness };
        }
        return true;
    }

    UiReply ScrollPanel::onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase )
    {
        float2 local{};
        if ( getGeometry().inverseTransformPoint( event._position, local ) == false )
            return UiReply::makeUnhandled();
        switch ( event._kind )
        {
            case UiPointerEventKind::Wheel:
            {
                // 버블 — 안쪽 위젯(콤보 목록 · 안쪽 스크롤)이 먼저. 휠 위(+)는 내용을 내린다(오프셋이 준다). 세로가 없으면 가로로.
                if ( phase != UiRoutePhase::Bubble )
                    return UiReply::makeUnhandled();
                const float32 distance = -event._wheel * _wheelStep;
                const float2  delta    = _bScrollVertical ? float2{ 0.0f, distance } : float2{ distance, 0.0f };
                return applyScrollDelta( delta ) ? UiReply::makeHandled() : UiReply::makeUnhandled();
            }
            case UiPointerEventKind::Down:
            {
                // 터널 — 막대는 내용 위에 겹쳐 그리므로 막대 위 누름은 내용(버튼)보다 먼저 받는다.
                if ( phase != UiRoutePhase::Tunnel || event._button != MouseButton::Left || _bDraggingBar )
                    return UiReply::makeUnhandled();
                return beginScrollBarDrag( local );
            }
            case UiPointerEventKind::Move:
            {
                if ( _bDraggingBar == false || phase != UiRoutePhase::Tunnel )
                    return UiReply::makeUnhandled();
                ScrollBarLayout bar{};
                if ( computeScrollBar( _dragAxis, bar ) == false )
                    return UiReply::makeHandled();
                const bool    bVertical = _dragAxis == UiOrientation::Vertical;
                const float32 moved     = bVertical ? local._y - _dragStartLocal._y : local._x - _dragStartLocal._x;
                const float32 freeTrack = bVertical ? bar._trackSize._y - bar._thumbSize._y : bar._trackSize._x - bar._thumbSize._x;
                const float2  maxOffset = getMaxScrollOffset();
                const float32 axisMax   = bVertical ? maxOffset._y : maxOffset._x;
                const float32 offset    = freeTrack > 0.0f ? _dragStartOffset + moved * axisMax / freeTrack : _dragStartOffset;
                setScrollOffset( bVertical ? float2{ _scrollOffset._x, offset } : float2{ offset, _scrollOffset._y } );
                return UiReply::makeHandled();
            }
            case UiPointerEventKind::Up:
            {
                if ( _bDraggingBar == false || event._button != MouseButton::Left )
                    return UiReply::makeUnhandled();
                _bDraggingBar = false;
                invalidate( WidgetDirty::kPaint ); // 막대 누름 상태
                return UiReply::makeHandled().releasePointer();
            }
        }
        return UiReply::makeUnhandled();
    }

    UiReply ScrollPanel::onActionEvent( const UiActionEvent& event, UiRoutePhase phase )
    {
        // 오른쪽 스틱(위 · 오른쪽이 +) — 버블이라 포커스 위젯이 먼저 쓸 수 있다(값 바꾸는 슬라이더 등). 위로 기울이면 내용이 내려온다.
        if ( phase != UiRoutePhase::Bubble || event._action != hashed_string( UiActionName::kScroll ) )
            return UiReply::makeUnhandled();
        const float32 distance = _stickSpeed * event._deltaSeconds;
        const float2  delta{ event._value._x * distance, -event._value._y * distance };
        return applyScrollDelta( delta ) ? UiReply::makeHandled() : UiReply::makeUnhandled();
    }

    void ScrollPanel::paintOverChildren( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        (void)context;
        const UiOrientation arrAxis[] = { UiOrientation::Vertical, UiOrientation::Horizontal };
        for ( const UiOrientation axis : arrAxis )
        {
            ScrollBarLayout bar{};
            if ( computeScrollBar( axis, bar ) == false )
                continue;
            const float32 radius = _scrollBarThickness * 0.5f;
            CanvasBrush   thumb{};
            thumb._color        = float4{ 1.0f, 1.0f, 1.0f, _bDraggingBar && _dragAxis == axis ? 0.65f : 0.35f };
            thumb._cornerRadius = float4{ radius, radius, radius, radius };
            painter.fillRect( bar._thumbPosition, bar._thumbSize, thumb );
        }
    }

    bool ScrollPanel::applyScrollDelta( const float2& delta )
    {
        const float2 before = _scrollOffset;
        scrollBy( delta );
        return before != _scrollOffset;
    }

    UiReply ScrollPanel::beginScrollBarDrag( const float2& local )
    {
        const UiOrientation arrAxis[] = { UiOrientation::Vertical, UiOrientation::Horizontal };
        for ( const UiOrientation axis : arrAxis )
        {
            ScrollBarLayout bar{};
            if ( computeScrollBar( axis, bar ) == false || ScrollPanelInternal::isInside( local, bar._trackPosition, bar._trackSize ) == false )
                continue;
            const bool bVertical = axis == UiOrientation::Vertical;
            if ( ScrollPanelInternal::isInside( local, bar._thumbPosition, bar._thumbSize ) == false )
            {
                // 트랙 — 누른 쪽으로 한 화면(Godot · 브라우저와 같다).
                const float32 thumbStart = bVertical ? bar._thumbPosition._y : bar._thumbPosition._x;
                const float32 point      = bVertical ? local._y : local._x;
                const float32 page       = ( bVertical ? _viewportSize._y : _viewportSize._x ) * ( point < thumbStart ? -1.0f : 1.0f );
                (void)applyScrollDelta( bVertical ? float2{ 0.0f, page } : float2{ page, 0.0f } );
                return UiReply::makeHandled();
            }
            _bDraggingBar    = true;
            _dragAxis        = axis;
            _dragStartLocal  = local;
            _dragStartOffset = bVertical ? _scrollOffset._y : _scrollOffset._x;
            invalidate( WidgetDirty::kPaint );
            return UiReply::makeHandled().capturePointer();
        }
        return UiReply::makeUnhandled();
    }

    float2 ScrollPanel::clampOffset( const float2& scrollOffset ) const
    {
        const float2 maxOffset = getMaxScrollOffset();
        return float2{ _bScrollHorizontal ? MathUtil::clamp( scrollOffset._x, 0.0f, maxOffset._x ) : 0.0f,
                       _bScrollVertical ? MathUtil::clamp( scrollOffset._y, 0.0f, maxOffset._y ) : 0.0f };
    }

    float2 ScrollPanel::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        if ( getChildCount() == 0 )
            return float2{};
        Widget&       content = *getChild( 0 );
        const float4& padding = content.getLayoutSlot()._padding;
        const float32 padX    = padding._x + padding._z;
        const float32 padY    = padding._y + padding._w;
        const float2  contentAvailable{ _bScrollHorizontal ? kUiUnbounded : UiLayoutPass::computeRemaining( availableSize._x, padX ),
                                       _bScrollVertical ? kUiUnbounded : UiLayoutPass::computeRemaining( availableSize._y, padY ) };
        const float2 desired = UiLayoutPass::measure( content, context, contentAvailable );
        float2       result{ desired._x + padX, desired._y + padY };
        // 스크롤 축은 가용 크기를 넘겨 원하지 않는다 — 넘치는 만큼이 스크롤 거리다.
        if ( _bScrollHorizontal && UiLayoutPass::isUnbounded( availableSize._x ) == false )
            result._x = MathUtil::min( result._x, availableSize._x );
        if ( _bScrollVertical && UiLayoutPass::isUnbounded( availableSize._y ) == false )
            result._y = MathUtil::min( result._y, availableSize._y );
        return result;
    }

    void ScrollPanel::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        const float2 previousContent  = _contentSize;
        const float2 previousViewport = _viewportSize;
        const float2 previousOffset   = _scrollOffset;
        _viewportSize                 = size;
        if ( getChildCount() == 0 )
        {
            _contentSize  = float2{};
            _scrollOffset = float2{};
            return;
        }
        Widget&       content = *getChild( 0 );
        const float4& padding = content.getLayoutSlot()._padding;
        const float2  desired = content.getDesiredSize();
        _contentSize          = float2{ _bScrollHorizontal ? MathUtil::max( desired._x + padding._x + padding._z, size._x ) : size._x,
                               _bScrollVertical ? MathUtil::max( desired._y + padding._y + padding._w, size._y ) : size._y };
        _scrollOffset = clampOffset( _scrollOffset ); // 내용이 줄었으면 끝에 붙는다(무효화 없이 — 지금 놓는 중이다)
        arrangeChild( context, content, float2{ -_scrollOffset._x, -_scrollOffset._y }, _contentSize );
        // 막대 엄지의 길이 · 자리는 내용 · 보이는 크기 · 오프셋에서 나온다 — 그리기만 다시(레이아웃 비트는 건드리지 않는다).
        if ( previousContent != _contentSize || previousViewport != _viewportSize || previousOffset != _scrollOffset )
            invalidate( WidgetDirty::kPaint );
    }
} // namespace sw
