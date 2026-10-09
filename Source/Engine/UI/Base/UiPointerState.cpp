#include "pch.h"

#include "Engine/UI/Base/UiPointerState.h"

#include "Core/Math/MathUtil.h"

#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"

namespace sw
{
    UiPointerState::UiPointerState()
        : _hoverPath{}
        , _scratchPath{}
        , _pHoverTree{ nullptr }
        , _pCaptureTree{ nullptr }
        , _captured{ kInvalidWidgetId }
    {
    }

    UiPointerResult UiPointerState::process( WidgetTree& tree, const UiPointerEvent& event )
    {
        UiPointerResult result{};
        // 잡은 위젯이 떨어졌으면 잡기를 푼다.
        if ( _captured != kInvalidWidgetId && ( _pCaptureTree == nullptr || _pCaptureTree->findWidgetById( _captured ) == nullptr ) )
        {
            _captured     = kInvalidWidgetId;
            _pCaptureTree = nullptr;
        }

        const bool bCapturedHere = _captured != kInvalidWidgetId && _pCaptureTree == &tree;
        if ( bCapturedHere )
        {
            (void)UiEventRouter::makePathTo( tree, _captured, _scratchPath );
            result._bHitWidget = SW_TRUE;
        }
        else
        {
            result._bHitWidget = UiEventRouter::hitTest( tree, event._position, _scratchPath ) ? SW_TRUE : SW_FALSE;
        }
        updateHover( &tree, _scratchPath );

        WidgetId      handler = kInvalidWidgetId;
        const UiReply reply   = UiEventRouter::routePointerEvent( tree, _scratchPath, event, handler );
        if ( reply.isHandled() == false )
            return result;
        result._bHandled     = SW_TRUE;
        result._handler      = handler;
        result._focusRequest = reply._focusRequest;
        if ( reply._bCapturePointer == SW_TRUE )
        {
            _captured     = handler;
            _pCaptureTree = &tree;
        }
        else if ( reply._bReleasePointer == SW_TRUE && _captured == handler )
        {
            _captured     = kInvalidWidgetId;
            _pCaptureTree = nullptr;
        }
        return result;
    }

    void UiPointerState::clearHover()
    {
        _scratchPath._listWidget.clear();
        updateHover( nullptr, _scratchPath );
    }

    void UiPointerState::forgetTree( const WidgetTree& tree )
    {
        if ( _pHoverTree == &tree )
        {
            _pHoverTree = nullptr;
            _hoverPath._listWidget.clear();
        }
        if ( _pCaptureTree == &tree )
        {
            _pCaptureTree = nullptr;
            _captured     = kInvalidWidgetId;
        }
    }

    void UiPointerState::updateHover( WidgetTree* pTree, const UiWidgetPath& newPath )
    {
        // 다른 트리로 옮겼으면 공통 조상이 없다.
        uint32 common = 0;
        if ( pTree == _pHoverTree )
        {
            const uint32 limit = static_cast<uint32>( MathUtil::min( _hoverPath._listWidget.size(), newPath._listWidget.size() ) );
            while ( common < limit && _hoverPath._listWidget[common] == newPath._listWidget[common] )
            {
                ++common;
            }
        }
        // 빠진 쪽: 잎부터 Leave.
        if ( _pHoverTree != nullptr )
        {
            for ( uint32 index = static_cast<uint32>( _hoverPath._listWidget.size() ); index > common; --index )
            {
                Widget* pWidget = _pHoverTree->findWidgetById( _hoverPath._listWidget[index - 1] );
                if ( pWidget == nullptr )
                    continue;
                pWidget->_bHovered = false;
                pWidget->invalidate( WidgetDirty::kStyle );
                pWidget->onHoverChanged( false );
            }
        }
        // 새로 든 쪽: 뿌리부터 Enter.
        if ( pTree != nullptr )
        {
            for ( uint32 index = common; index < static_cast<uint32>( newPath._listWidget.size() ); ++index )
            {
                Widget* pWidget = pTree->findWidgetById( newPath._listWidget[index] );
                if ( pWidget == nullptr )
                    continue;
                pWidget->_bHovered = true;
                pWidget->invalidate( WidgetDirty::kStyle );
                pWidget->onHoverChanged( true );
            }
        }
        _pHoverTree = newPath.isEmpty() ? nullptr : pTree;
        _hoverPath  = newPath;
    }
} // namespace sw
