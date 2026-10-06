#include "pch.h"

#include "Engine/UI/Core/UiFocusManager.h"

#include "Engine/UI/Core/UiEventRouter.h"
#include "Engine/UI/Core/UiNavigationSolver.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTree.h"

namespace sw
{
    UiFocusManager::UiFocusManager()
        : _pFocusedTree{ nullptr }
    {
    }

    UiFocusManager::~UiFocusManager()
    {
        if ( _pFocusedTree != nullptr )
            _pFocusedTree->_pFocusManager = nullptr;
    }

    bool UiFocusManager::setFocus( WidgetTree& tree, WidgetId widget )
    {
        Widget* pNew = tree.findWidgetById( widget );
        if ( pNew == nullptr || UiNavigationSolver::canReceiveFocus( *pNew ) == false )
            return false;
        if ( _pFocusedTree == &tree && tree._focusedWidget == widget )
            return true;
        clearFocus();
        tree._focusedWidget = widget;
        tree._pFocusManager = this;
        _pFocusedTree       = &tree;
        pNew->invalidate( WidgetDirty::kStyle );
        pNew->onFocusChanged( true );
        return true;
    }

    void UiFocusManager::clearFocus()
    {
        if ( _pFocusedTree == nullptr )
            return;
        WidgetTree&    tree     = *_pFocusedTree;
        const WidgetId previous = tree._focusedWidget;
        tree._focusedWidget     = kInvalidWidgetId;
        tree._pFocusManager     = nullptr;
        _pFocusedTree           = nullptr;
        Widget* pOld            = tree.findWidgetById( previous );
        if ( pOld == nullptr )
            return;
        pOld->invalidate( WidgetDirty::kStyle );
        pOld->onFocusChanged( false );
    }

    WidgetId UiFocusManager::getFocusedWidget() const
    {
        return _pFocusedTree != nullptr ? _pFocusedTree->_focusedWidget : kInvalidWidgetId;
    }

    bool UiFocusManager::navigate( WidgetTree& tree, UiNavigationDirection direction )
    {
        if ( _pFocusedTree != &tree || tree._focusedWidget == kInvalidWidgetId )
            return false;
        const WidgetId next = UiNavigationSolver::findNextWidget( tree, tree._focusedWidget, direction );
        if ( next == kInvalidWidgetId || next == tree._focusedWidget )
            return false;
        return setFocus( tree, next );
    }

    void UiFocusManager::makeFocusPath( const WidgetTree& tree, UiWidgetPath& outPath ) const
    {
        outPath._listWidget.clear();
        if ( _pFocusedTree != &tree || tree._focusedWidget == kInvalidWidgetId )
            return;
        (void)UiEventRouter::makePathTo( tree, tree._focusedWidget, outPath );
    }

    void UiFocusManager::forgetTree( const WidgetTree& tree )
    {
        if ( _pFocusedTree != &tree )
            return;
        _pFocusedTree->_focusedWidget = kInvalidWidgetId;
        _pFocusedTree->_pFocusManager = nullptr;
        _pFocusedTree                 = nullptr;
    }
} // namespace sw
