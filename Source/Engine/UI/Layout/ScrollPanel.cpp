#include "pch.h"

#include "Engine/UI/Layout/ScrollPanel.h"

#include "Core/Math/MathUtil.h"

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
        , _navigationMargin{ 8.0f }
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
        invalidate( WidgetDirty::kArrange );
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
        _viewportSize = size;
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
    }
} // namespace sw
