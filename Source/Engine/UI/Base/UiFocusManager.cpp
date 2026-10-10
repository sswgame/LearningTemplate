#include "pch.h"

#include "Engine/UI/Base/UiFocusManager.h"

#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/UiEventRouter.h"
#include "Engine/UI/Base/UiNavigationSolver.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"

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

    bool UiFocusManager::setFocus( WidgetTree& tree, WidgetID widget )
    {
        Widget* pNew = tree.findWidgetByID( widget );
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
        const WidgetID previous = tree._focusedWidget;
        tree._focusedWidget     = kInvalidWidgetID;
        tree._pFocusManager     = nullptr;
        _pFocusedTree           = nullptr;
        Widget* pOld            = tree.findWidgetByID( previous );
        if ( pOld == nullptr )
            return;
        pOld->invalidate( WidgetDirty::kStyle );
        pOld->onFocusChanged( false );
    }

    WidgetID UiFocusManager::getFocusedWidget() const
    {
        return _pFocusedTree != nullptr ? _pFocusedTree->_focusedWidget : kInvalidWidgetID;
    }

    bool UiFocusManager::navigate( WidgetTree& tree, UiNavigationDirection direction )
    {
        if ( _pFocusedTree != &tree || tree._focusedWidget == kInvalidWidgetID )
            return false;
        const WidgetID next = UiNavigationSolver::findNextWidget( tree, tree._focusedWidget, direction );
        if ( next == kInvalidWidgetID || next == tree._focusedWidget )
            return false;
        if ( setFocus( tree, next ) == false )
            return false;
        // 자른 조상(스크롤 패널)이 새 포커스를 보이게 옮긴다 — 안쪽부터. 지난 배치의 기하로 세고, 옮긴 자리는 다음 레이아웃 걷기가 놓는다.
        const Widget* pFocused = tree.findWidgetByID( next );
        for ( PanelWidget* pAncestor = pFocused != nullptr ? pFocused->getParent() : nullptr; pAncestor != nullptr; pAncestor = pAncestor->getParent() )
        {
            (void)pAncestor->scrollIntoView( *pFocused );
        }
        return true;
    }

    void UiFocusManager::makeFocusPath( const WidgetTree& tree, UiWidgetPath& outPath ) const
    {
        outPath._listWidget.clear();
        if ( _pFocusedTree != &tree || tree._focusedWidget == kInvalidWidgetID )
            return;
        (void)UiEventRouter::makePathTo( tree, tree._focusedWidget, outPath );
    }

    void UiFocusManager::forgetTree( const WidgetTree& tree )
    {
        if ( _pFocusedTree != &tree )
            return;
        _pFocusedTree->_focusedWidget = kInvalidWidgetID;
        _pFocusedTree->_pFocusManager = nullptr;
        _pFocusedTree                 = nullptr;
    }
} // namespace sw
