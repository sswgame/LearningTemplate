#include "pch.h"

#include "Engine/UI/Core/UiFocusManager.h"

#include "Engine/UI/Core/PanelWidget.h"
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
        if ( setFocus( tree, next ) == false )
            return false;
        // 자른 조상(스크롤 패널)이 새 포커스를 보이게 옮긴다 — 안쪽부터. 지난 배치의 기하로 세고, 옮긴 자리는 다음 레이아웃 걷기가 놓는다.
        const Widget* pFocused = tree.findWidgetById( next );
        for ( PanelWidget* pAncestor = pFocused != nullptr ? pFocused->getParent() : nullptr; pAncestor != nullptr; pAncestor = pAncestor->getParent() )
            (void)pAncestor->scrollIntoView( *pFocused );
        return true;
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
