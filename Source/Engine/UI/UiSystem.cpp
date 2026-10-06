#include "pch.h"

#include "Engine/UI/UiSystem.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/UI/Core/UiEventRouter.h"
#include "Engine/UI/Core/UiNavigationSolver.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/Utility/GameTimeScale.h"

namespace sw
{
    SW_LOG_CALLER( "UiSystem" );

    namespace
    {
        struct UiSystemInternal
        {
            /** @brief 마우스 버튼 중 UI 가 사건으로 받는 것(왼쪽 · 오른쪽 · 가운데). */
            static constexpr MouseButton kArrPointerButton[] = { MouseButton::Left, MouseButton::Right, MouseButton::Middle };

            /** @brief 그리기 순서의 앞뒤입니다 — 층이 먼저, 같은 층이면 쌓인 순서. */
            static bool isDrawnBefore( const UiScreen& lhs, const UiScreen& rhs )
            {
                if ( lhs.getDesc()._layer != rhs.getDesc()._layer )
                    return static_cast<uint8>( lhs.getDesc()._layer ) < static_cast<uint8>( rhs.getDesc()._layer );
                return false; // 같은 층이면 뒤에 올린 것이 위 — 끼울 자리는 같은 층의 끝
            }

            /** @brief vtable 이 범위 안인 화면 · 위젯이 하나라도 있으면 true 입니다. */
            static bool usesCodeWithin( const UiScreen& screen, const void* pBegin, const void* pEnd )
            {
                if ( IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( &screen ), pBegin, pEnd ) )
                    return true;
                vector<Widget*> listWidget;
                screen.getTree().collectWidgetsInDocumentOrder( listWidget );
                for ( const Widget* pWidget : listWidget )
                {
                    if ( IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( pWidget ), pBegin, pEnd ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiSystem::UiSystem()
        : IModuleUnloadListener{}
        , _listScreen{}
        , _focus{}
        , _pointer{}
        , _pInput{ nullptr }
        , _pFontSystem{ nullptr }
        , _viewport{}
        , _lastPointerPosition{}
        , _activeScreen{ kInvalidUiScreenHandle }
        , _nextScreenHandle{ 1 }
        , _nextPushOrder{ 0 }
        , _inputMode{ UiInputMode::Pointer }
        , _bPauseRequested{ SW_FALSE }
        , _bPendingClose{ SW_FALSE }
        , _bPointerKnown{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    UiSystem::~UiSystem()
    {
        shutdown();
    }

    bool UiSystem::initialize( InputManager& inputManager, FontSystem* pFontSystem )
    {
        _pInput        = &inputManager;
        _pFontSystem   = pFontSystem;
        _bPointerKnown = SW_FALSE;
        return true;
    }

    void UiSystem::shutdown()
    {
        _focus.clearFocus();
        while ( _listScreen.empty() == false )
            destroyScreenAt( static_cast<uint32>( _listScreen.size() ) - 1 );
        _bPendingClose = SW_FALSE;
        refreshActiveScreen();
        _pInput      = nullptr;
        _pFontSystem = nullptr;
    }

    void UiSystem::processInput( float32 deltaSeconds )
    {
        (void)deltaSeconds;
        if ( _pInput == nullptr )
            return;
        processPointer();
        applyPendingCloses();
    }

    void UiSystem::update( float32 deltaSeconds, const UiViewport& viewport )
    {
        (void)deltaSeconds;
        _viewport = viewport;
        applyPendingCloses();
        // 스타일 · 레이아웃 · 그리기 걷기가 생기기 전까지는 무효화를 프레임마다 처리한 것으로 둔다(목록이 쌓이지 않게).
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
            screen->getTree().clearAllDirty();
    }

    UiScreenHandle UiSystem::pushScreen( unique_ptr<UiScreen> screen )
    {
        if ( screen == nullptr )
            return kInvalidUiScreenHandle;
        SW_ASSERT( screen->_pUiSystem == nullptr );
        screen->_pUiSystem = this;
        screen->_handle    = _nextScreenHandle++;
        screen->_pushOrder = _nextPushOrder++;
        screen->_bClosing  = SW_FALSE;
        // 같은 층의 끝(그 층에서 맨 위)에 끼운다.
        uint32 at = static_cast<uint32>( _listScreen.size() );
        while ( at > 0 && UiSystemInternal::isDrawnBefore( *screen, *_listScreen[at - 1] ) )
            --at;
        const UiScreenHandle handle = screen->_handle;
        _listScreen.insert( _listScreen.begin() + at, std::move( screen ) );
        refreshActiveScreen();
        return handle;
    }

    void UiSystem::closeScreen( UiScreenHandle handle )
    {
        UiScreen* pScreen = findScreen( handle );
        if ( pScreen == nullptr || pScreen->_bClosing == SW_TRUE )
            return;
        pScreen->_bClosing = SW_TRUE;
        _bPendingClose     = SW_TRUE;
    }

    UiScreen* UiSystem::findScreen( UiScreenHandle handle ) const
    {
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->_handle == handle )
                return screen.get();
        }
        return nullptr;
    }

    UiScreen* UiSystem::getActiveScreen() const
    {
        return findScreen( _activeScreen );
    }

    bool UiSystem::isGameInputBlocked() const
    {
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->isClosing() == false && screen->blocksLowerInput() )
                return true;
        }
        return false;
    }

    bool UiSystem::wantsCursor() const
    {
        const UiScreen* pActive = getActiveScreen();
        return pActive != nullptr && pActive->getDesc()._bShowCursor;
    }

    void UiSystem::setInputMode( UiInputMode mode )
    {
        if ( _inputMode == mode )
            return;
        _inputMode        = mode;
        UiScreen* pActive = getActiveScreen();
        if ( mode == UiInputMode::Navigation && pActive != nullptr && _focus.getFocusedTree() != &pActive->getTree() )
            restoreFocus( *pActive );
    }

    uint32 UiSystem::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        uint32 closedCount = 0;
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            if ( UiSystemInternal::usesCodeWithin( *_listScreen[index - 1], pBegin, pEnd ) == false )
                continue;
            SW_LOG_WARNING( "[Ui] Screen %# closed for module reload - it holds widget code from the unloading module", _listScreen[index - 1]->_handle );
            destroyScreenAt( index - 1 );
            ++closedCount;
        }
        if ( closedCount > 0 )
            refreshActiveScreen();
        return closedCount;
    }

    void UiSystem::processPointer()
    {
        const MouseDevice* pMouse = _pInput->getMouse();
        if ( pMouse == nullptr )
            return;
        // 게임이 포인터를 잠가 쥐고 있으면(1 인칭 시점) 커서는 화면 가운데에 묶여 있다 — UI 는 포인터를 보지 않는다.
        if ( _pInput->isMouseLockActive() )
        {
            _pointer.clearHover();
            _bPointerKnown = SW_FALSE;
            return;
        }
        // UI 단위 = 창 픽셀(UI 배율이 생기면 여기서 나눈다).
        const int2     pixel = pMouse->getPosition();
        const float2   position{ static_cast<float32>( pixel._x ), static_cast<float32>( pixel._y ) };
        UiPointerEvent event{};
        event._position      = position;
        event._delta         = _bPointerKnown == SW_TRUE ? float2{ position._x - _lastPointerPosition._x, position._y - _lastPointerPosition._y } : float2{};
        const bool bMoved    = _bPointerKnown == SW_FALSE || event._delta._x != 0.0f || event._delta._y != 0.0f;
        _lastPointerPosition = position;
        _bPointerKnown       = SW_TRUE;
        if ( bMoved )
        {
            event._kind = UiPointerEventKind::Move;
            (void)dispatchPointerEvent( event );
        }
        for ( const MouseButton button : UiSystemInternal::kArrPointerButton )
        {
            event._button = button;
            if ( pMouse->wasButtonPressed( button ) )
            {
                event._kind = UiPointerEventKind::Down;
                (void)dispatchPointerEvent( event );
            }
            if ( pMouse->wasButtonReleased( button ) )
            {
                event._kind = UiPointerEventKind::Up;
                (void)dispatchPointerEvent( event );
            }
        }
        const float32 wheel = pMouse->getMouseWheel();
        if ( wheel != 0.0f )
        {
            event._kind  = UiPointerEventKind::Wheel;
            event._wheel = wheel;
            (void)dispatchPointerEvent( event );
        }
    }

    bool UiSystem::dispatchPointerEvent( const UiPointerEvent& event )
    {
        // 잡은 위젯이 있으면 그 트리로, 없으면 점 아래 맨 위 화면으로.
        UiScreen* pTarget = nullptr;
        if ( _pointer.getCaptureTree() != nullptr )
        {
            for ( const unique_ptr<UiScreen>& screen : _listScreen )
            {
                if ( &screen->getTree() == _pointer.getCaptureTree() )
                    pTarget = screen.get();
            }
        }
        if ( pTarget == nullptr )
            pTarget = findPointerScreen( event._position );
        if ( pTarget == nullptr )
        {
            _pointer.clearHover();
            return false;
        }
        const UiPointerResult result = _pointer.process( pTarget->getTree(), event );
        if ( result._focusRequest != kInvalidWidgetId && pTarget->takesFocus() )
            (void)_focus.setFocus( pTarget->getTree(), result._focusRequest );
        return result._bHandled == SW_TRUE;
    }

    UiScreen* UiSystem::findPointerScreen( const float2& point ) const
    {
        UiWidgetPath path{};
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            UiScreen* pScreen = _listScreen[index - 1].get();
            if ( pScreen->isClosing() || pScreen->receivesPointer() == false )
                continue;
            if ( UiEventRouter::hitTest( pScreen->getTree(), point, path ) )
                return pScreen;
            if ( pScreen->blocksLowerInput() )
                return nullptr; // 모달 아래 화면은 클릭을 받지 않는다
        }
        return nullptr;
    }

    void UiSystem::applyPendingCloses()
    {
        if ( _bPendingClose == SW_FALSE )
            return;
        _bPendingClose = SW_FALSE;
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            if ( _listScreen[index - 1]->_bClosing == SW_TRUE )
                destroyScreenAt( index - 1 );
        }
        refreshActiveScreen();
    }

    void UiSystem::destroyScreenAt( uint32 index )
    {
        unique_ptr<UiScreen> screen = std::move( _listScreen[index] );
        _listScreen.erase( _listScreen.begin() + index );
        _pointer.forgetTree( screen->getTree() );
        if ( _focus.getFocusedTree() == &screen->getTree() )
            _focus.clearFocus();
        screen->_pUiSystem = nullptr;
        // 트리가 지워질 때 포커스 관리자에게 알린다 — 위에서 이미 풀었다.
    }

    void UiSystem::refreshActiveScreen()
    {
        // 활성 화면 = 맨 위의 포커스 받는 화면. 막는 화면(모달 · 로딩)을 지나 내려가지 않는다.
        UiScreen* pNewActive = nullptr;
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            UiScreen* pScreen = _listScreen[index - 1].get();
            if ( pScreen->isClosing() )
                continue;
            if ( pScreen->takesFocus() )
            {
                pNewActive = pScreen;
                break;
            }
            if ( pScreen->blocksLowerInput() )
                break;
        }

        UiScreen* pOldActive = findScreen( _activeScreen );
        if ( pNewActive != pOldActive )
        {
            if ( pOldActive != nullptr && _focus.getFocusedTree() == &pOldActive->getTree() )
            {
                pOldActive->_lastFocused = _focus.getFocusedWidget();
                _focus.clearFocus();
            }
            _activeScreen = pNewActive != nullptr ? pNewActive->_handle : kInvalidUiScreenHandle;
            if ( pNewActive != nullptr )
                restoreFocus( *pNewActive );
        }

        // 게임 정지 — 정지 화면이 하나라도 있으면 요청을 걸어 둔다.
        bool bPause = false;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
            bPause = bPause || ( screen->isClosing() == false && screen->getDesc()._bPausesGame );
        if ( bPause && _bPauseRequested == SW_FALSE )
        {
            GameTimeScale::addPauseRequest();
            _bPauseRequested = SW_TRUE;
        }
        else if ( bPause == false && _bPauseRequested == SW_TRUE )
        {
            GameTimeScale::removePauseRequest();
            _bPauseRequested = SW_FALSE;
        }
    }

    void UiSystem::restoreFocus( UiScreen& screen )
    {
        WidgetTree& tree = screen.getTree();
        if ( screen._lastFocused != kInvalidWidgetId && _focus.setFocus( tree, screen._lastFocused ) )
            return;
        if ( _inputMode != UiInputMode::Navigation || tree.getRoot() == nullptr )
            return;
        const Widget*  pDefault = screen.getDesc()._defaultFocus.empty() ? nullptr : tree.findWidgetByName( screen.getDesc()._defaultFocus );
        const WidgetId target   = pDefault != nullptr ? pDefault->getId() : UiNavigationSolver::findFirstFocusable( *tree.getRoot() );
        if ( target != kInvalidWidgetId )
            (void)_focus.setFocus( tree, target );
    }
} // namespace sw
