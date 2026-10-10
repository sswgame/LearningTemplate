#include "pch.h"

#include "Engine/UI/Widgets/ListViewWidget.h"

#include "Core/Common/Defines.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UILayoutPass.h"

namespace sw
{
    ListViewWidget::ListViewWidget()
        : PanelWidget{}
        , _rowFactory{}
        , _rowBinder{}
        , _listRowItem{}
        , _scrollOffset{ 0.0f }
        , _viewportHeight{ 0.0f }
        , _itemCount{ 0 }
        , _bindCount{ 0 }
        , _rowHeight{ 32.0f }
        , _wheelStep{ 96.0f }
    {
        setClipChildren( true );
    }

    ListViewWidget::~ListViewWidget() = default;

    const TypeInfo* ListViewWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void ListViewWidget::setItemCount( uint32 itemCount )
    {
        _itemCount = itemCount;
        for ( uint32& item : _listRowItem )
        {
            item = invalid_index::kUint32; // 모두 다시 묶는다
        }
        _scrollOffset = MathUtil::clamp( _scrollOffset, 0.0f, getMaxScrollOffset() );
        invalidate( WidgetDirty::kLayout );
    }

    void ListViewWidget::setRowHeight( float32 rowHeight )
    {
        const float32 clamped = MathUtil::max( 1.0f, rowHeight );
        if ( _rowHeight == clamped )
            return;
        _rowHeight = clamped;
        invalidate( WidgetDirty::kLayout );
    }

    float32 ListViewWidget::getMaxScrollOffset() const
    {
        return MathUtil::max( 0.0f, static_cast<float32>( _itemCount ) * _rowHeight - _viewportHeight );
    }

    void ListViewWidget::setScrollOffset( float32 scrollOffset )
    {
        const float32 clamped = MathUtil::clamp( scrollOffset, 0.0f, getMaxScrollOffset() );
        if ( clamped == _scrollOffset )
            return;
        _scrollOffset = clamped;
        invalidate( WidgetDirty::kArrange );
    }

    void ListViewWidget::scrollToItem( uint32 itemIndex )
    {
        if ( itemIndex >= _itemCount )
            return;
        const float32 top    = static_cast<float32>( itemIndex ) * _rowHeight;
        const float32 bottom = top + _rowHeight;
        if ( top < _scrollOffset )
            setScrollOffset( top );
        else if ( bottom > _scrollOffset + _viewportHeight )
            setScrollOffset( bottom - _viewportHeight );
    }

    uint32 ListViewWidget::findItemIndex( const Widget& row ) const
    {
        const uint32 rowIndex = findChildIndex( &row );
        return rowIndex < _listRowItem.size() ? _listRowItem[rowIndex] : invalid_index::kUint32;
    }

    bool ListViewWidget::scrollIntoView( const Widget& widget )
    {
        // 그 위젯을 든 줄(이 패널의 자식)을 찾아 그 줄의 항목이 보이게.
        const Widget* pRow = &widget;
        while ( pRow != nullptr && pRow->getParent() != this )
        {
            pRow = pRow->getParent();
        }
        if ( pRow == nullptr )
            return false;
        const uint32  itemIndex = findItemIndex( *pRow );
        const float32 before    = _scrollOffset;
        scrollToItem( itemIndex );
        return before != _scrollOffset;
    }

    UIReply ListViewWidget::onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase )
    {
        if ( event._kind != UIPointerEventKind::Wheel || phase != UIRoutePhase::Bubble )
            return UIReply::makeUnhandled();
        const float32 before = _scrollOffset;
        setScrollOffset( _scrollOffset - event._wheel * _wheelStep );
        return before != _scrollOffset ? UIReply::makeHandled() : UIReply::makeUnhandled();
    }

    float2 ListViewWidget::computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const
    {
        (void)context;
        (void)availableSize;
        return float2{ 0.0f, static_cast<float32>( _itemCount ) * _rowHeight };
    }

    void ListViewWidget::ensureRowCount( uint32 rowCount )
    {
        if ( _rowFactory.isBound() == false )
            return;
        while ( _listRowItem.size() < rowCount )
        {
            unique_ptr<Widget> row = _rowFactory();
            if ( row == nullptr )
                return;
            (void)addChild( std::move( row ) );
            _listRowItem.push_back( invalid_index::kUint32 );
        }
    }

    void ListViewWidget::arrangeChildren( const UILayoutContext& context, const float2& size )
    {
        _viewportHeight          = size._y;
        _scrollOffset            = MathUtil::clamp( _scrollOffset, 0.0f, getMaxScrollOffset() );
        const uint32 visibleRows = static_cast<uint32>( MathUtil::ceil( size._y / _rowHeight ) ) + 1;
        ensureRowCount( MathUtil::min( visibleRows, _itemCount ) );

        const uint32 rowCount  = static_cast<uint32>( _listRowItem.size() );
        const uint32 firstItem = static_cast<uint32>( _scrollOffset / _rowHeight );
        for ( uint32 rowIndex = 0; rowIndex < rowCount; ++rowIndex )
        {
            Widget& row = *getChild( rowIndex );
            // 항목 k 는 줄 k % 줄 수가 맡는다 — 보이는 창 [first, first + 줄 수) 안에서 이 줄이 맡을 항목.
            const uint32 offsetInWindow = ( rowIndex + rowCount - firstItem % rowCount ) % rowCount;
            const uint32 itemIndex      = firstItem + offsetInWindow;
            if ( itemIndex >= _itemCount )
            {
                row.setVisibility( WidgetVisibility::Collapsed );
                _listRowItem[rowIndex] = invalid_index::kUint32;
                continue;
            }
            if ( row.getVisibility() == WidgetVisibility::Collapsed )
                row.setVisibility( WidgetVisibility::Visible );
            if ( _listRowItem[rowIndex] != itemIndex )
            {
                _listRowItem[rowIndex] = itemIndex;
                ++_bindCount;
                if ( _rowBinder.isBound() )
                    _rowBinder( row, itemIndex );
            }
            const float32 top = static_cast<float32>( itemIndex ) * _rowHeight - _scrollOffset;
            arrangeChild( context, row, float2{ 0.0f, top }, float2{ size._x, _rowHeight } );
        }
    }
} // namespace sw
