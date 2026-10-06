#include "pch.h"

#include "Engine/UI/UiSystem.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
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
            /** @brief UI 행동 맵의 레이어 이름입니다(활성 화면이 있을 때만 켠다). */
            static constexpr utf8 kUiLayerName[] = "UI";
            /** @brief 스틱 탐색이 한 칸 옮기는 기울기 문턱입니다. */
            static constexpr float32 kStickNavigateMagnitude = 0.5f;
            /** @brief 패드 축 사건이 "패드를 쓴다" 로 세는 크기입니다(손을 떼어 둔 스틱의 떨림은 세지 않는다). */
            static constexpr float32 kGamepadAxisActivity = 0.5f;

            /** @struct NavigationAction @brief 탐색 행동 이름과 방향입니다. */
            struct NavigationAction
            {
                const utf8*           _pAction;
                UiNavigationDirection _direction;
            };
            static constexpr NavigationAction kArrNavigationAction[] = {
                {   "UI.NavigateUp",       UiNavigationDirection::Up},
                { "UI.NavigateDown",     UiNavigationDirection::Down},
                { "UI.NavigateLeft",     UiNavigationDirection::Left},
                {"UI.NavigateRight",    UiNavigationDirection::Right},
                {    "UI.FocusNext",     UiNavigationDirection::Next},
                {"UI.FocusPrevious", UiNavigationDirection::Previous},
            };
            static constexpr utf8 kAcceptAction[]      = "UI.Accept";
            static constexpr utf8 kBackAction[]        = "UI.Back";
            static constexpr utf8 kTabNextAction[]     = "UI.TabNext";
            static constexpr utf8 kTabPreviousAction[] = "UI.TabPrevious";
            static constexpr utf8 kStickAction[]       = "UI.NavigateStick";

            /** @brief 원시 사건 하나가 가리키는 입력 방식입니다. 방식과 무관한 사건이면 false 입니다. */
            [[nodiscard]] static bool tryGetInputMode( const RawInputEvent& rawEvent, UiInputMode& outMode )
            {
                switch ( rawEvent._type )
                {
                    case RawInputEventType::KeyDown:
                    case RawInputEventType::GamepadButtonDown:
                    {
                        outMode = UiInputMode::Navigation;
                        return true;
                    }
                    case RawInputEventType::GamepadAxis:
                    {
                        if ( MathUtil::abs( rawEvent._payload._gamepadData._axisValue ) < kGamepadAxisActivity )
                            return false;
                        outMode = UiInputMode::Navigation;
                        return true;
                    }
                    case RawInputEventType::MouseMove:
                    case RawInputEventType::MouseButtonDown:
                    case RawInputEventType::MouseWheel:
                    {
                        outMode = UiInputMode::Pointer;
                        return true;
                    }
                    default:
                    {
                        return false;
                    }
                }
            }

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
        , _consumption{}
        , _uiInputMap{}
        , _pInput{ nullptr }
        , _pFontSystem{ nullptr }
        , _viewport{}
        , _lastPointerPosition{}
        , _stickRepeatSeconds{ 0.0f }
        , _stickDirection{ UiNavigationDirection::Next }
        , _activeScreen{ kInvalidUiScreenHandle }
        , _nextScreenHandle{ 1 }
        , _nextPushOrder{ 0 }
        , _inputMode{ UiInputMode::Pointer }
        , _bPauseRequested{ SW_FALSE }
        , _bPendingClose{ SW_FALSE }
        , _bPointerKnown{ SW_FALSE }
        , _bStickHeld{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    UiSystem::~UiSystem()
    {
        shutdown();
    }

    bool UiSystem::initialize( InputManager& inputManager, FontSystem* pFontSystem, string_view uiInputMapPath )
    {
        _pInput        = &inputManager;
        _pFontSystem   = pFontSystem;
        _bPointerKnown = SW_FALSE;
        _consumption.clear();
        _pInput->setTextInputCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &UiSystem::onTextInput, this ), InputKeyboardFocus::Ui );
        _pInput->setTextCompositionCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &UiSystem::onTextComposition, this ), InputKeyboardFocus::Ui );
        if ( uiInputMapPath.empty() )
            return true;
        _uiInputMap = make_unique<InputMap>();
        // 글 입력 칸이 키보드 포커스(Ui)를 쥔 동안에도 Back · Tab 은 UI 가 받는다 — 그 밖의 행동은 UiSystem 이 거른다.
        _uiInputMap->setKeyboardFocusIgnored( true );
        _uiInputMap->setSuppressBaseActionOnChord( true ); // Shift+Tab 이 Tab 도 함께 발화하지 않게
        if ( _uiInputMap->loadFromResource( uiInputMapPath ) == false )
        {
            SW_LOG_ERROR( "[Ui] UI input map '%#' could not be loaded - UI navigation actions stay unbound", uiInputMapPath );
            _uiInputMap.reset();
            return false;
        }
        _uiInputMap->setInputManager( _pInput );
        _uiInputMap->setLayerEnabled( UiSystemInternal::kUiLayerName, getActiveScreen() != nullptr );
        return true;
    }

    void UiSystem::shutdown()
    {
        _focus.clearFocus();
        while ( _listScreen.empty() == false )
            destroyScreenAt( static_cast<uint32>( _listScreen.size() ) - 1 );
        _bPendingClose = SW_FALSE;
        refreshActiveScreen();
        if ( _pInput != nullptr )
        {
            if ( _pInput->getKeyboardFocus() == InputKeyboardFocus::Ui )
                _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
            _pInput->setTextInputCallback( {}, InputKeyboardFocus::Ui );
            _pInput->setTextCompositionCallback( {}, InputKeyboardFocus::Ui );
        }
        _uiInputMap.reset();
        _consumption.clear();
        _pInput      = nullptr;
        _pFontSystem = nullptr;
    }

    void UiSystem::processInput( float32 deltaSeconds )
    {
        if ( _pInput == nullptr )
            return;
        _consumption.update( *_pInput );
        if ( _uiInputMap != nullptr )
            _uiInputMap->update( deltaSeconds );
        updateInputMode();
        processPointer();
        processActions( deltaSeconds );
        applyPendingCloses();
        updateKeyboardFocus();
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

    bool UiSystem::isActionConsumed( const InputMap& inputMap, const hashed_string& action ) const
    {
        return _pInput != nullptr && _consumption.isActionConsumed( inputMap, *_pInput, action );
    }

    void UiSystem::consumeAction( const InputMap& inputMap, const hashed_string& action )
    {
        if ( _pInput != nullptr )
            _consumption.consumeAction( inputMap, *_pInput, action );
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
            // 위젯이 처리한 버튼은 먹는다 — 게임의 "왼쪽 클릭 = 사격" 이 메뉴 클릭을 같이 받지 않게. 빈 곳 클릭은 게임으로 간다.
            if ( pMouse->wasButtonPressed( button ) )
            {
                event._kind = UiPointerEventKind::Down;
                if ( dispatchPointerEvent( event ) )
                    _consumption.consumeMouseButton( button );
            }
            if ( pMouse->wasButtonReleased( button ) )
            {
                event._kind = UiPointerEventKind::Up;
                if ( dispatchPointerEvent( event ) )
                    _consumption.consumeMouseButton( button );
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

        // UI 행동은 활성 화면이 있을 때만 — HUD 만 있으면 패드 A 는 게임의 것이다.
        if ( _uiInputMap != nullptr )
            _uiInputMap->setLayerEnabled( UiSystemInternal::kUiLayerName, pNewActive != nullptr );

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

    void UiSystem::updateInputMode()
    {
        UiInputMode mode    = _inputMode;
        bool        bChange = false;
        for ( const RawInputEvent& rawEvent : _pInput->getLastFrameEvents() )
        {
            UiInputMode eventMode = mode;
            if ( UiSystemInternal::tryGetInputMode( rawEvent, eventMode ) )
            {
                mode    = eventMode;
                bChange = true;
            }
        }
        if ( bChange )
            setInputMode( mode );
    }

    void UiSystem::processActions( float32 deltaSeconds )
    {
        UiScreen* pActive = getActiveScreen();
        if ( pActive == nullptr || _uiInputMap == nullptr )
        {
            _bStickHeld = SW_FALSE;
            return;
        }
        // 개발 콘솔이 키보드를 쥐었으면 UI 는 행동을 받지 않는다. 글 입력 칸이 쥐었으면 Back · Tab 만.
        const InputKeyboardFocus keyboardFocus = _pInput->getKeyboardFocus();
        if ( keyboardFocus == InputKeyboardFocus::DevConsole )
            return;
        const bool      bTextFocus = keyboardFocus == InputKeyboardFocus::Ui;
        const InputMap& uiMap      = *_uiInputMap;

        for ( const UiSystemInternal::NavigationAction& entry : UiSystemInternal::kArrNavigationAction )
        {
            const hashed_string action( entry._pAction );
            const bool          bTab = entry._direction == UiNavigationDirection::Next || entry._direction == UiNavigationDirection::Previous;
            if ( ( bTextFocus && bTab == false ) || uiMap.wasActionTriggered( action ) == false )
                continue;
            if ( handleNavigation( *pActive, action, entry._direction ) )
                consumeAction( uiMap, action );
        }
        if ( bTextFocus == false )
            processStickNavigation( *pActive, deltaSeconds );

        const hashed_string arrRoutedAction[] = { hashed_string( UiSystemInternal::kAcceptAction ), hashed_string( UiSystemInternal::kTabNextAction ),
                                                  hashed_string( UiSystemInternal::kTabPreviousAction ) };
        for ( const hashed_string& action : arrRoutedAction )
        {
            if ( bTextFocus || uiMap.wasActionTriggered( action ) == false )
                continue;
            if ( routeAction( *pActive, action, float2{} ) )
                consumeAction( uiMap, action );
        }

        const hashed_string backAction( UiSystemInternal::kBackAction );
        if ( uiMap.wasActionTriggered( backAction ) )
        {
            // 위젯이 먼저(펼친 목록을 접는 콤보 상자), 아무도 안 먹으면 화면이(기본은 닫기).
            const bool bHandled = routeAction( *pActive, backAction, float2{} ) || pActive->onBack();
            if ( bHandled )
                consumeAction( uiMap, backAction );
        }
    }

    bool UiSystem::handleNavigation( UiScreen& screen, const hashed_string& action, UiNavigationDirection direction )
    {
        WidgetTree& tree = screen.getTree();
        // 포커스가 없으면 첫 탐색 입력은 기본 포커스를 잡는 데 쓴다(어디로 갈지 모르는 채 옮기지 않는다).
        if ( _focus.getFocusedTree() != &tree || _focus.getFocusedWidget() == kInvalidWidgetId )
        {
            restoreFocus( screen );
            return _focus.getFocusedTree() == &tree;
        }
        if ( routeAction( screen, action, float2{} ) )
            return true;
        return _focus.navigate( tree, direction );
    }

    void UiSystem::processStickNavigation( UiScreen& screen, float32 deltaSeconds )
    {
        const hashed_string stickAction( UiSystemInternal::kStickAction );
        const float2        value = _uiInputMap->getVector2D( stickAction );
        const float32       absX  = MathUtil::abs( value._x );
        const float32       absY  = MathUtil::abs( value._y );
        if ( MathUtil::max( absX, absY ) < UiSystemInternal::kStickNavigateMagnitude )
        {
            _bStickHeld = SW_FALSE;
            return;
        }
        // 큰 축 방향 한 칸 — 스틱 위(+y)는 화면 위다.
        const UiNavigationDirection direction = absX >= absY ? ( value._x > 0.0f ? UiNavigationDirection::Right : UiNavigationDirection::Left )
                                                             : ( value._y > 0.0f ? UiNavigationDirection::Up : UiNavigationDirection::Down );
        bool                        bStep     = false;
        if ( _bStickHeld == SW_FALSE || direction != _stickDirection )
        {
            _bStickHeld         = SW_TRUE;
            _stickDirection     = direction;
            _stickRepeatSeconds = _uiInputMap->getNavRepeatDelay();
            bStep               = true;
        }
        else
        {
            _stickRepeatSeconds -= deltaSeconds;
            if ( _stickRepeatSeconds <= 0.0f )
            {
                _stickRepeatSeconds += _uiInputMap->getNavRepeatRate();
                bStep = true;
            }
        }
        if ( bStep )
            (void)handleNavigation( screen, stickAction, direction );
        consumeAction( *_uiInputMap, stickAction ); // 기운 동안(반복을 기다리는 동안에도) 스틱은 UI 의 것이다
    }

    bool UiSystem::routeAction( UiScreen& screen, const hashed_string& action, const float2& value )
    {
        UiWidgetPath path{};
        _focus.makeFocusPath( screen.getTree(), path );
        if ( path.isEmpty() )
            return false;
        UiActionEvent event{};
        event._action = action;
        event._value  = value;
        WidgetId      handler{ kInvalidWidgetId };
        const UiReply reply = UiEventRouter::routeActionEvent( screen.getTree(), path, event, handler );
        if ( reply.isHandled() && reply._focusRequest != kInvalidWidgetId )
            (void)_focus.setFocus( screen.getTree(), reply._focusRequest );
        return reply.isHandled();
    }

    void UiSystem::updateKeyboardFocus()
    {
        const Widget* pFocused = nullptr;
        if ( _focus.getFocusedTree() != nullptr )
            pFocused = _focus.getFocusedTree()->findWidgetById( _focus.getFocusedWidget() );
        const bool               bWantsText = pFocused != nullptr && pFocused->supportsTextInput();
        const InputKeyboardFocus current    = _pInput->getKeyboardFocus();
        if ( bWantsText && current == InputKeyboardFocus::Game )
            _pInput->setKeyboardFocus( InputKeyboardFocus::Ui );
        else if ( bWantsText == false && current == InputKeyboardFocus::Ui )
            _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
    }

    void UiSystem::onTextInput( string_view text )
    {
        dispatchTextEvent( text, false );
    }

    void UiSystem::onTextComposition( string_view text )
    {
        dispatchTextEvent( text, true );
    }

    void UiSystem::dispatchTextEvent( string_view text, bool bComposition )
    {
        WidgetTree* pTree   = _focus.getFocusedTree();
        Widget*     pWidget = pTree != nullptr ? pTree->findWidgetById( _focus.getFocusedWidget() ) : nullptr;
        if ( pWidget == nullptr || pWidget->supportsTextInput() == false )
            return;
        UiTextEvent event{};
        event._text         = string{ text };
        event._bComposition = bComposition ? SW_TRUE : SW_FALSE;
        (void)pWidget->onTextEvent( event );
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
