#include "pch.h"

#include "Engine/UI/WidgetTree/PanelWidget.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    DetachedWidget::DetachedWidget()
        : _widget{}
        , _pRetireTree{ nullptr }
    {
    }

    DetachedWidget::DetachedWidget( unique_ptr<Widget> widget, WidgetTree* pRetireTree )
        : _widget{ std::move( widget ) }
        , _pRetireTree{ pRetireTree }
    {
    }

    DetachedWidget::~DetachedWidget()
    {
        if ( _pRetireTree != nullptr )
            _pRetireTree->retireWidget( std::move( _widget ) );
    }

    DetachedWidget::DetachedWidget( DetachedWidget&& other ) noexcept
        : _widget{ std::move( other._widget ) }
        , _pRetireTree{ other._pRetireTree }
    {
        other._pRetireTree = nullptr;
    }

    DetachedWidget& DetachedWidget::operator=( DetachedWidget&& other ) noexcept
    {
        if ( this == &other )
            return *this;
        if ( _pRetireTree != nullptr )
            _pRetireTree->retireWidget( std::move( _widget ) );
        _widget            = std::move( other._widget );
        _pRetireTree       = other._pRetireTree;
        other._pRetireTree = nullptr;
        return *this;
    }

    unique_ptr<Widget> DetachedWidget::release()
    {
        _pRetireTree = nullptr;
        return std::move( _widget );
    }
} // namespace sw

namespace sw
{
    PanelWidget::PanelWidget()
        : Widget{}
        , _listChild{}
        , _bClipChildren{ false }
        , _maxChildSlotHeight{ 0.0f }
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

    DetachedWidget PanelWidget::removeChild( Widget* pChild )
    {
        const optional<uint32> index = findChildIndex( pChild );
        if ( index.has_value() == false )
            return {};
        WidgetTree* const  pTree = getTree();
        unique_ptr<Widget> child = std::move( _listChild[*index] );
        _listChild.erase( _listChild.begin() + *index );
        child->detachFromTree();
        child->_pParent = nullptr;
        invalidate( WidgetDirty::kLayout );
        const bool bDispatching = pTree != nullptr && pTree->isDispatching();
        return DetachedWidget{ std::move( child ), bDispatching ? pTree : nullptr };
    }

    void PanelWidget::clearChildren()
    {
        if ( _listChild.empty() )
            return;
        WidgetTree* const          pTree = getTree();
        vector<unique_ptr<Widget>> listChild;
        listChild.swap( _listChild );
        for ( unique_ptr<Widget>& child : listChild )
        {
            child->detachFromTree();
            child->_pParent = nullptr;
            if ( pTree != nullptr )
                pTree->retireWidget( std::move( child ) ); // 전달 중이면 트리가 맡아 전달이 끝난 뒤 지운다
        }
        invalidate( WidgetDirty::kLayout );
    }

    bool PanelWidget::scrollIntoView( const Widget& widget )
    {
        (void)widget;
        return false;
    }

    void PanelWidget::collectPaintOrder( vector<uint32>& outListIndex ) const
    {
        outListIndex.clear();
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            outListIndex.push_back( index );
        }
    }

    optional<uint32> PanelWidget::findChildIndex( const Widget* pChild ) const
    {
        for ( uint32 index = 0; index < static_cast<uint32>( _listChild.size() ); ++index )
        {
            if ( _listChild[index].get() == pChild )
                return index;
        }
        return nullopt;
    }

    void PanelWidget::setClipChildren( bool bClip )
    {
        if ( _bClipChildren == bClip )
            return;
        _bClipChildren = bClip;
        invalidate( WidgetDirty::kPaint );
    }

    void PanelWidget::arrangeChildren( const UILayoutContext& context, const float2& size )
    {
        (void)context;
        (void)size;
    }

    void PanelWidget::arrangeChild( const UILayoutContext& context, Widget& child, const float2& localPosition, const float2& size )
    {
        _maxChildSlotHeight = MathUtil::max( _maxChildSlotHeight, size._y );
        if ( isRightToLeft() && shouldMirrorChildrenInRightToLeft() )
        {
            const float2 mirrored{ getGeometry()._size._x - localPosition._x - size._x, localPosition._y };
            UILayoutPass::arrange( child, context, getGeometry(), mirrored, size );
            return;
        }
        UILayoutPass::arrange( child, context, getGeometry(), localPosition, size );
    }
} // namespace sw
