#include "pch.h"

#include "Engine/UI/Core/WidgetTree.h"

#include "Core/Log/Logger.h"

#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/UiFocusManager.h"

namespace sw
{
    SW_LOG_CALLER( "WidgetTree" );

    namespace
    {
        struct WidgetTreeInternal
        {
            static void collectRecursive( Widget& widget, vector<Widget*>& inoutListWidget )
            {
                inoutListWidget.push_back( &widget );
                const PanelWidget* pPanel = castTo<const PanelWidget>( &widget );
                if ( pPanel == nullptr )
                    return;
                for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
                    collectRecursive( *pPanel->getChild( index ), inoutListWidget );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    WidgetTree::WidgetTree()
        : _root{}
        , _mapIdToWidget{}
        , _mapNameToWidget{}
        , _listLayoutDirtyRoot{}
        , _listPaintDirty{}
        , _listStyleDirty{}
        , _pFocusManager{ nullptr }
        , _focusedWidget{ kInvalidWidgetId }
        , _layoutUiScale{ 0.0f }
        , _layoutTextScale{ 0.0f }
    {
    }

    WidgetTree::~WidgetTree()
    {
        if ( _pFocusManager != nullptr )
            _pFocusManager->forgetTree( *this );
        setRoot( nullptr );
    }

    void WidgetTree::setRoot( unique_ptr<Widget> root )
    {
        if ( _root != nullptr )
        {
            _root->detachFromTree();
            _root.reset();
        }
        _listLayoutDirtyRoot.clear();
        _listPaintDirty.clear();
        _listStyleDirty.clear();
        _root = std::move( root );
        if ( _root != nullptr )
        {
            SW_ASSERT( _root->getParent() == nullptr && _root->getTree() == nullptr );
            _root->attachToTree( this, nullptr );
        }
    }

    Widget* WidgetTree::findWidgetByName( const hashed_string& name ) const
    {
        const auto iter = _mapNameToWidget.find( name );
        return iter != _mapNameToWidget.end() ? iter->second : nullptr;
    }

    Widget* WidgetTree::findWidgetById( WidgetId id ) const
    {
        const auto iter = _mapIdToWidget.find( id );
        return iter != _mapIdToWidget.end() ? iter->second : nullptr;
    }

    void WidgetTree::notifyDirty( Widget& widget, uint32 dirtyReason )
    {
        const uint32 previous = widget._dirtyFlags;
        widget._dirtyFlags |= dirtyReason;
        if ( ( dirtyReason & WidgetDirty::kVisibility ) != 0 )
            widget._dirtyFlags |= WidgetDirty::kLayout | WidgetDirty::kPaint; // Collapsed 를 오가면 자리가 바뀐다 — 보수적으로 레이아웃
        const uint32 kPaintLike = WidgetDirty::kPaint | WidgetDirty::kTransform;
        if ( ( widget._dirtyFlags & kPaintLike ) != 0 && ( previous & kPaintLike ) == 0 )
            _listPaintDirty.push_back( widget._id );
        if ( ( dirtyReason & WidgetDirty::kStyle ) != 0 && ( previous & WidgetDirty::kStyle ) == 0 )
            _listStyleDirty.push_back( widget._id );
        // 배치만: 크기는 그대로라 위로 번지지 않는다 — 이 위젯을 지난 자리에 다시 놓을 뿌리로 적는다(스크롤 오프셋).
        if ( ( dirtyReason & WidgetDirty::kArrange ) != 0 && ( previous & WidgetDirty::kArrange ) == 0 )
            addLayoutRoot( widget );
        if ( ( widget._dirtyFlags & WidgetDirty::kLayout ) == 0 || ( previous & WidgetDirty::kLayout ) != 0 )
            return;
        // 레이아웃: 부모 쪽으로 kChildLayout 을 올린다. 부모의 크기가 자식에 기대지 않으면(레이아웃 경계) 거기서 멈추고
        // 그 부모를 "다시 잴 뿌리" 로 적는다. 이미 kChildLayout 인 조상을 만나면 그 위는 이미 표시됐다.
        Widget* pCurrent = &widget;
        while ( pCurrent->_pParent != nullptr )
        {
            PanelWidget* pParent = pCurrent->_pParent;
            if ( ( pParent->_dirtyFlags & WidgetDirty::kChildLayout ) != 0 )
                return;
            pParent->_dirtyFlags |= WidgetDirty::kChildLayout;
            if ( pParent->isLayoutBoundary() )
            {
                addLayoutRoot( *pParent );
                return;
            }
            pCurrent = pParent;
        }
        addLayoutRoot( *pCurrent ); // 루트까지 올라갔다
    }

    void WidgetTree::addLayoutRoot( Widget& widget )
    {
        if ( ( widget._dirtyFlags & WidgetDirty::kLayoutRoot ) != 0 )
            return;
        widget._dirtyFlags |= WidgetDirty::kLayoutRoot;
        _listLayoutDirtyRoot.push_back( widget._id );
    }

    bool WidgetTree::hasPendingWork() const
    {
        return _listLayoutDirtyRoot.empty() == false || _listPaintDirty.empty() == false || _listStyleDirty.empty() == false;
    }

    void WidgetTree::clearAllDirty()
    {
        for ( const auto& [id, pWidget] : _mapIdToWidget )
            pWidget->_dirtyFlags = WidgetDirty::kNone;
        _listLayoutDirtyRoot.clear();
        _listPaintDirty.clear();
        _listStyleDirty.clear();
    }

    void WidgetTree::collectWidgetsInDocumentOrder( vector<Widget*>& outListWidget ) const
    {
        outListWidget.clear();
        if ( _root != nullptr )
            WidgetTreeInternal::collectRecursive( *_root, outListWidget );
    }

    void WidgetTree::registerWidget( Widget& widget )
    {
        _mapIdToWidget[widget._id] = &widget;
        if ( widget._name.empty() == false )
            registerName( widget );
    }

    void WidgetTree::unregisterWidget( Widget& widget )
    {
        _mapIdToWidget.erase( widget._id );
        if ( widget._name.empty() == false )
            unregisterName( widget );
        if ( _focusedWidget == widget._id )
            _focusedWidget = kInvalidWidgetId;
    }

    void WidgetTree::registerName( Widget& widget )
    {
        if ( widget._name.empty() )
            return;
        const auto iter = _mapNameToWidget.find( widget._name );
        if ( iter != _mapNameToWidget.end() && iter->second != &widget )
        {
            SW_LOG_WARNING( "[Ui] Widget name '%#' is used twice in one tree - findWidget returns the first one", widget._name.c_str() );
            return;
        }
        _mapNameToWidget[widget._name] = &widget;
    }

    void WidgetTree::unregisterName( Widget& widget )
    {
        if ( widget._name.empty() )
            return;
        const auto iter = _mapNameToWidget.find( widget._name );
        if ( iter != _mapNameToWidget.end() && iter->second == &widget )
            _mapNameToWidget.erase( iter );
    }
} // namespace sw
