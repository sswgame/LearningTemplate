#include "pch.h"

#include "Engine/UI/Core/PanelWidget.h"

#include "Core/Common/Defines.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UI/Core/WidgetTree.h"

namespace sw
{
    PanelWidget::PanelWidget()
        : Widget{}
        , _listChild{}
        , _bClipChildren{ false }
    {
    }

    PanelWidget::~PanelWidget() = default;

    const TypeInfo* PanelWidget::getTypeInfo() const
    {
        return StaticType();
    }

    Widget* PanelWidget::addChild( unique_ptr<Widget> child )
    {
        return insertChild( static_cast<uint32>( _listChild.size() ), std::move( child ) );
    }

    Widget* PanelWidget::insertChild( uint32 index, unique_ptr<Widget> child )
    {
        if ( child == nullptr )
            return nullptr;
        SW_ASSERT( child->_pParent == nullptr && child->getTree() == nullptr );
        Widget* const pChild = child.get();
        const uint32  at     = MathUtil::min( index, static_cast<uint32>( _listChild.size() ) );
        _listChild.insert( _listChild.begin() + at, std::move( child ) );
        pChild->_pParent = this;
        if ( getTree() != nullptr )
            pChild->attachToTree( getTree(), this );
        invalidate( WidgetDirty::kLayout );
        return pChild;
    }

    unique_ptr<Widget> PanelWidget::removeChild( Widget* pChild )
    {
        const uint32 index = findChildIndex( pChild );
        if ( index == invalid_index::kUint32 )
            return nullptr;
        unique_ptr<Widget> child = std::move( _listChild[index] );
        _listChild.erase( _listChild.begin() + index );
        child->detachFromTree();
        child->_pParent = nullptr;
        invalidate( WidgetDirty::kLayout );
        return child;
    }

    void PanelWidget::clearChildren()
    {
        if ( _listChild.empty() )
            return;
        for ( const unique_ptr<Widget>& child : _listChild )
            child->detachFromTree();
        _listChild.clear();
        invalidate( WidgetDirty::kLayout );
    }

    uint32 PanelWidget::findChildIndex( const Widget* pChild ) const
    {
        for ( uint32 index = 0; index < static_cast<uint32>( _listChild.size() ); ++index )
        {
            if ( _listChild[index].get() == pChild )
                return index;
        }
        return invalid_index::kUint32;
    }

    void PanelWidget::setClipChildren( bool bClip )
    {
        if ( _bClipChildren == bClip )
            return;
        _bClipChildren = bClip;
        invalidate( WidgetDirty::kPaint );
    }

    void PanelWidget::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        (void)context;
        (void)size;
    }

    void PanelWidget::arrangeChild( Widget& child, const float2& localPosition, const float2& size )
    {
        // 자식 기하 = 이 패널의 기하 ∘ (자식 위치로 옮김) ∘ (자식 렌더 변환 — 피벗 기준).
        const WidgetGeometry& parent = getGeometry();
        WidgetGeometry        placed{};
        placed._position    = float2{ parent._position._x + localPosition._x, parent._position._y + localPosition._y };
        placed._size        = size;
        placed._axisX       = parent._axisX;
        placed._axisY       = parent._axisY;
        placed._translation = parent.transformPoint( localPosition );
        child.setArrangedGeometry( child.getRenderTransform().applyTo( placed ) );
    }
} // namespace sw
