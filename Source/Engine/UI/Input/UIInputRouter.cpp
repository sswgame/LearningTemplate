#include "pch.h"

#include "Engine/UI/Input/UIInputRouter.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/UIEventRouter.h"
#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/WidgetTree/UINavigationSolver.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    SW_LOG_CALLER( "UIInputRouter" );

    namespace
    {
        struct UIInputRouterInternal
        {
            /** @brief 마우스 버튼 중 UI 가 이벤트로 받는 것(왼쪽, 오른쪽, 가운데)입니다. */
            static constexpr MouseButton kArrPointerButton[] = { MouseButton::Left, MouseButton::Right, MouseButton::Middle };
            /** @brief UI 액션 맵의 레이어 이름입니다(활성 화면이 있을 때만 켭니다). */
            static constexpr utf8 kUILayerName[]       = "UI";
            static constexpr utf8 kUIGlobalLayerName[] = "UIGlobal"; ///< 화면이 없을 때의 UI 액션(일시정지)입니다.
            /** @brief 스틱 탐색이 한 번 이동하는 기울기 임계값입니다. */
            static constexpr float32 kStickNavigateMagnitude = 0.5f;
            /** @brief 패드 축 이벤트를 "패드를 쓴다" 로 판정하는 크기입니다(손을 뗀 스틱의 떨림은 세지 않습니다). */
            static constexpr float32 kGamepadAxisActivity = 0.5f;

            /** @struct NavigationAction @brief 탐색 액션 이름과 방향입니다. */
            struct NavigationAction
            {
                const utf8*           _pAction;
                UINavigationDirection _direction;
            };
            static constexpr NavigationAction kArrNavigationAction[] = {
                {   UIActionName::kNavigateUp,       UINavigationDirection::Up},
                { UIActionName::kNavigateDown,     UINavigationDirection::Down},
                { UIActionName::kNavigateLeft,     UINavigationDirection::Left},
                {UIActionName::kNavigateRight,    UINavigationDirection::Right},
                {    UIActionName::kFocusNext,     UINavigationDirection::Next},
                {UIActionName::kFocusPrevious, UINavigationDirection::Previous},
            };

            /**
             * @struct TextAction
             * @brief 글 입력 위젯이 키보드 포커스를 가진 동안만 보내는 편집 액션과, 그 액션을 가리는 액션입니다.
             * @details Shift 조합은 조합 키 없는 바인딩도 함께 발화하므로(Shift+Left 는 Left 도 누른 것), 가리는 액션이 발화한 프레임에는 이 액션을 보내지 않습니다.
             */
            struct TextAction
            {
                const utf8* _pAction;
                const utf8* _pShadowedBy; ///< 이 액션이 발화한 프레임에 이 액션을 건너뛰게 하는 액션입니다(없으면 nullptr).
            };
            static constexpr TextAction kArrTextAction[] = {
                {  UIActionName::kTextBackspace,                        nullptr},
                {     UIActionName::kTextDelete,                        nullptr},
                {  UIActionName::kTextCaretLeft,  UIActionName::kTextSelectLeft},
                { UIActionName::kTextCaretRight, UIActionName::kTextSelectRight},
                {       UIActionName::kTextHome,  UIActionName::kTextSelectHome},
                {        UIActionName::kTextEnd,   UIActionName::kTextSelectEnd},
                { UIActionName::kTextSelectLeft,                        nullptr},
                {UIActionName::kTextSelectRight,                        nullptr},
                { UIActionName::kTextSelectHome,                        nullptr},
                {  UIActionName::kTextSelectEnd,                        nullptr},
                {  UIActionName::kTextSelectAll,                        nullptr},
                {       UIActionName::kTextCopy,                        nullptr},
                {        UIActionName::kTextCut,                        nullptr},
                {      UIActionName::kTextPaste,                        nullptr},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIInputRouter::UIInputRouter()
        : _focus{}
        , _pointer{}
        , _listFramePointerEvent{}
        , _consumption{}
        , _uiInputMap{}
        , _pInput{ nullptr }
        , _textInputSubscription{}
        , _textCompositionSubscription{}
        , _lastPointerPosition{}
        , _lastMousePixel{}
        , _stickRepeatSeconds{ 0.0f }
        , _inputDeltaSeconds{ 0.0f }
        , _stickDirection{ UINavigationDirection::Next }
        , _inputMode{ UIInputMode::Pointer }
        , _bPointerKnown{ SW_FALSE }
        , _bStickHeld{ SW_FALSE }
        , _bMousePixelKnown{ SW_FALSE }
    {
    }

    UIInputRouter::~UIInputRouter() = default;

    bool UIInputRouter::initialize( InputManager& inputManager, const AssetID& assetID )
    {
        const string_view uiInputMapPath = assetID.view();
        _pInput                          = &inputManager;
        _bPointerKnown                   = SW_FALSE;
        _consumption.clear();
        _textInputSubscription = _pInput->bindTextInputCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &UIInputRouter::onTextInput, this ), InputKeyboardFocus::UI );
        _textCompositionSubscription =
            _pInput->bindTextCompositionCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &UIInputRouter::onTextComposition, this ), InputKeyboardFocus::UI );
        if ( uiInputMapPath.empty() )
            return true;
        _uiInputMap = make_unique<InputMap>();
        // 텍스트 입력 위젯이 키보드 포커스(UI)를 가진 동안에도 Back 과 Tab 은 UI 가 받는다. 그 밖의 액션은 processActions 가 걸러 낸다.
        _uiInputMap->setKeyboardFocusIgnored( true );
        _uiInputMap->setSuppressBaseActionOnChord( true ); // Shift+Tab 이 Tab 도 함께 발화하지 않게
        if ( _uiInputMap->loadFromAsset( assetID ) == false )
        {
            SW_LOG_ERROR( "[UI] UI input map '%#' could not be loaded - UI navigation actions stay unbound", uiInputMapPath );
            _uiInputMap.reset();
            return false;
        }
        _uiInputMap->setInputManager( _pInput );
        return true;
    }

    void UIInputRouter::shutdown()
    {
        if ( _pInput != nullptr )
        {
            if ( _pInput->getKeyboardFocus() == InputKeyboardFocus::UI )
                _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
        }
        _textInputSubscription.reset(); // 이 라우터가 건 UI 포커스 칸만 놓는다
        _textCompositionSubscription.reset();
        _uiInputMap.reset();
        _consumption.clear();
        _pInput = nullptr;
    }

    void UIInputRouter::update( float32 deltaSeconds )
    {
        _inputDeltaSeconds = deltaSeconds;
        _listFramePointerEvent.clear();
        _consumption.update( *_pInput );
        if ( _uiInputMap != nullptr )
            _uiInputMap->update( deltaSeconds );
    }

    bool UIInputRouter::tryGetInputMode( const RawInputEvent& rawEvent, UIInputMode& outMode )
    {
        switch ( rawEvent._type )
        {
            case RawInputEventType::KeyDown:
            case RawInputEventType::GamepadButtonDown:
            {
                outMode = UIInputMode::Navigation;
                return true;
            }
            case RawInputEventType::GamepadAxis:
            {
                if ( MathUtil::abs( rawEvent._payload._gamepadData._axisValue ) < UIInputRouterInternal::kGamepadAxisActivity )
                    return false;
                outMode = UIInputMode::Navigation;
                return true;
            }
            case RawInputEventType::MouseMove:
            case RawInputEventType::MouseButtonDown:
            case RawInputEventType::MouseWheel:
            {
                outMode = UIInputMode::Pointer;
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    bool UIInputRouter::pollInputMode( UIInputMode& outMode )
    {
        UIInputMode mode    = _inputMode;
        bool        bChange = false;
        for ( const RawInputEvent& rawEvent : _pInput->getLastFrameEvents() )
        {
            // 커서 이동은 위치가 실제로 바뀔 때만 포인터 방식으로 본다. 창이 뜰 때나 포커스를 받을 때 OS 가 같은 위치로 보내는 이동 이벤트가
            // 패드로 고른 포커스를 지우지 않게 하기 위해서다(첫 이벤트는 위치만 기억한다).
            if ( rawEvent._type == RawInputEventType::MouseMove )
            {
                const int2 pixel{ rawEvent._payload._mouseData._x, rawEvent._payload._mouseData._y };
                const bool bMoved = _bMousePixelKnown == SW_TRUE && ( pixel._x != _lastMousePixel._x || pixel._y != _lastMousePixel._y );
                _lastMousePixel   = pixel;
                _bMousePixelKnown = SW_TRUE;
                if ( bMoved == false )
                    continue;
            }
            UIInputMode eventMode = mode;
            if ( tryGetInputMode( rawEvent, eventMode ) )
            {
                mode    = eventMode;
                bChange = true;
            }
        }
        outMode = mode;
        return bChange;
    }

    bool UIInputRouter::setInputMode( UIInputMode mode )
    {
        if ( _inputMode == mode )
            return false;
        _inputMode = mode;
        // :focus-visible 은 입력 방식을 따르므로 포커스 위젯을 다시 맞춘다.
        WidgetTree* pFocusTree = _focus.getFocusedTree();
        Widget*     pFocused   = pFocusTree != nullptr ? pFocusTree->findWidgetByID( _focus.getFocusedWidget() ) : nullptr;
        if ( pFocused != nullptr )
            pFocused->invalidate( WidgetDirty::kStyle );
        return true;
    }

    void UIInputRouter::processPointer( const vector<unique_ptr<UIScreen>>& listScreen, const UIViewport& viewport )
    {
        const MouseDevice* pMouse = _pInput->getMouse();
        if ( pMouse == nullptr )
            return;
        // 게임이 포인터를 잠가 두면(1인칭 시점) 커서는 화면 가운데에 묶여 있으므로 UI 는 포인터를 보지 않는다.
        if ( _pInput->isMouseLockActive() )
        {
            _pointer.clearHover();
            _bPointerKnown = SW_FALSE;
            return;
        }
        // 창 픽셀을 UI 단위로 바꾼다(지난 `update` 뷰포트의 자리와 배율을 쓴다. 입력은 이번 프레임 레이아웃 앞이라 지난 프레임에 그린 화면 기준이다).
        // 에디터 게임 뷰면 창 픽셀에서 패널 안 이미지 자리를 빼야 게임 뷰 픽셀이다(`UIViewport::_pointerOrigin`).
        const int2     pixel   = pMouse->getPosition();
        const float32  uiScale = viewport._uiScale > 0.0f ? viewport._uiScale : 1.0f;
        const float2   position{ static_cast<float32>( pixel._x - viewport._pointerOrigin._x ) / uiScale,
                               static_cast<float32>( pixel._y - viewport._pointerOrigin._y ) / uiScale };
        UIPointerEvent event{};
        event._position      = position;
        event._delta         = _bPointerKnown == SW_TRUE ? float2{ position._x - _lastPointerPosition._x, position._y - _lastPointerPosition._y } : float2{};
        const bool bMoved    = _bPointerKnown == SW_FALSE || event._delta._x != 0.0f || event._delta._y != 0.0f;
        _lastPointerPosition = position;
        _bPointerKnown       = SW_TRUE;
        if ( bMoved )
        {
            event._kind = UIPointerEventKind::Move;
            (void)dispatchPointerEvent( listScreen, event );
        }
        for ( const MouseButton button : UIInputRouterInternal::kArrPointerButton )
        {
            event._button = button;
            // 위젯이 처리한 버튼은 소비한다. 게임의 "왼쪽 클릭 = 사격" 이 메뉴 클릭을 같이 받지 않게 하기 위해서다. 빈 곳 클릭은 게임으로 넘어간다.
            if ( pMouse->wasButtonPressed( button ) )
            {
                event._kind = UIPointerEventKind::Down;
                if ( dispatchPointerEvent( listScreen, event ) )
                    _consumption.consumeMouseButton( event );
            }
            if ( pMouse->wasButtonReleased( button ) )
            {
                event._kind = UIPointerEventKind::Up;
                if ( dispatchPointerEvent( listScreen, event ) )
                    _consumption.consumeMouseButton( event );
            }
        }
        const float32 wheel = pMouse->getMouseWheel();
        if ( wheel != 0.0f )
        {
            event._kind  = UIPointerEventKind::Wheel;
            event._wheel = wheel;
            (void)dispatchPointerEvent( listScreen, event );
        }
    }

    bool UIInputRouter::dispatchPointerEvent( const vector<unique_ptr<UIScreen>>& listScreen, const UIPointerEvent& event )
    {
        // 포인터를 잡은 위젯이 있으면 그 트리로, 없으면 점 아래 맨 위 화면으로 보낸다.
        UIScreen* pTarget = nullptr;
        if ( _pointer.getCaptureTree() != nullptr )
        {
            for ( const unique_ptr<UIScreen>& screen : listScreen )
            {
                if ( &screen->getTree() == _pointer.getCaptureTree() )
                    pTarget = screen.get();
            }
        }
        if ( pTarget == nullptr )
            pTarget = findPointerScreen( listScreen, event._position );
        UIRoutedPointerEvent routed{};
        routed._event             = event;
        routed._bOverScreenWidget = pTarget != nullptr ? SW_TRUE : SW_FALSE;
        _listFramePointerEvent.push_back( routed );
        if ( pTarget == nullptr )
        {
            _pointer.clearHover();
            return false;
        }
        const UIPointerResult result = _pointer.process( pTarget->getTree(), event );
        if ( result._focusRequest != kInvalidWidgetID && pTarget->takesFocus() )
            (void)_focus.setFocus( pTarget->getTree(), result._focusRequest );
        return result._bHandled == SW_TRUE;
    }

    UIScreen* UIInputRouter::findPointerScreen( const vector<unique_ptr<UIScreen>>& listScreen, const float2& point )
    {
        UIWidgetPath path{};
        for ( uint32 index = static_cast<uint32>( listScreen.size() ); index > 0; --index )
        {
            UIScreen* pScreen = listScreen[index - 1].get();
            if ( pScreen->isClosing() || pScreen->receivesPointer() == false )
                continue;
            if ( UIEventRouter::hitTest( pScreen->getTree(), point, path ) )
                return pScreen;
            if ( pScreen->blocksLowerInput() )
                return nullptr; // 모달 아래 화면은 클릭을 받지 않는다
        }
        return nullptr;
    }

    bool UIInputRouter::pollPauseAction( bool bHasPauseMenu )
    {
        const bool bPauseNotPollable = _uiInputMap == nullptr || bHasPauseMenu == false || _pInput->getKeyboardFocus() == InputKeyboardFocus::DevConsole;
        if ( bPauseNotPollable )
            return false;
        const hashed_string pauseAction( UIActionName::kPause );
        if ( _uiInputMap->wasActionTriggered( pauseAction ) == false || _consumption.isActionConsumed( *_uiInputMap, *_pInput, pauseAction ) )
            return false;
        // 열지 못해도 소비한다. 일시정지를 누른 Esc 가 게임의 마우스 잠금 토글로 새지 않게 하기 위해서다(오류는 문서 생성 단계가 남긴다).
        consumeAction( *_uiInputMap, pauseAction );
        return true;
    }

    void UIInputRouter::processActions( UIScreen* pActive, float32 deltaSeconds )
    {
        const bool bNoUIActions = pActive == nullptr || _uiInputMap == nullptr || pActive->wantsUIActions() == false;
        if ( bNoUIActions )
        {
            _bStickHeld = SW_FALSE;
            return;
        }
        // 개발 콘솔이 키보드를 가졌으면 UI 는 액션을 받지 않는다. 텍스트 입력 위젯이 가졌으면 Back 과 Tab 만 받는다.
        const InputKeyboardFocus keyboardFocus = _pInput->getKeyboardFocus();
        if ( keyboardFocus == InputKeyboardFocus::DevConsole )
            return;
        const bool      bTextFocus = keyboardFocus == InputKeyboardFocus::UI;
        const InputMap& uiMap      = *_uiInputMap;

        for ( const UIInputRouterInternal::NavigationAction& entry : UIInputRouterInternal::kArrNavigationAction )
        {
            const hashed_string action( entry._pAction );
            const bool          bTab = entry._direction == UINavigationDirection::Next || entry._direction == UINavigationDirection::Previous;
            if ( ( bTextFocus && bTab == false ) || uiMap.wasActionTriggered( action ) == false )
                continue;
            if ( handleNavigation( *pActive, action, entry._direction ) )
                consumeAction( uiMap, action );
        }
        if ( bTextFocus == false )
        {
            processStickNavigation( *pActive, deltaSeconds );
            processScroll( *pActive );
        }

        // 일시정지(Esc)가 메뉴를 열면 UI 층이 켜지는데, 그 Esc 를 아직 누르고 있어도 입력 맵이 눌린 채 켜진 입력을 Held 로 시작시키므로(`InputMap::update`) Back 은 발화하지 않는다.
        const hashed_string arrRoutedAction[] = { hashed_string( UIActionName::kAccept ), hashed_string( UIActionName::kTabNext ),
                                                  hashed_string( UIActionName::kTabPrevious ) };
        for ( const hashed_string& action : arrRoutedAction )
        {
            if ( bTextFocus || uiMap.wasActionTriggered( action ) == false )
                continue;
            // 위젯이 먼저 처리하고, 아무도 소비하지 않은 탭 액션은 화면이 처리한다(옵션 메뉴의 탭 줄).
            const bool bScreenAction = action != hashed_string( UIActionName::kAccept );
            if ( routeAction( *pActive, action, float2{} ) || ( bScreenAction && pActive->onUnhandledAction( action ) ) )
                consumeAction( uiMap, action );
        }

        // 텍스트 입력 위젯: 지우기와 커서 이동 · 선택 · 복사 붙여넣기는 위젯이 키보드 포커스를 가진 동안만 처리한다(그 밖에서 이 키들은 UI 의 것이 아니다).
        if ( bTextFocus )
        {
            for ( const UIInputRouterInternal::TextAction& entry : UIInputRouterInternal::kArrTextAction )
            {
                const hashed_string action( entry._pAction );
                if ( uiMap.wasActionTriggered( action ) == false )
                    continue;
                if ( entry._pShadowedBy != nullptr && uiMap.wasActionTriggered( hashed_string( entry._pShadowedBy ) ) )
                    continue;
                if ( routeAction( *pActive, action, float2{} ) )
                    consumeAction( uiMap, action );
            }
        }

        const hashed_string backAction( UIActionName::kBack );
        if ( uiMap.wasActionTriggered( backAction ) )
        {
            // 위젯이 먼저 처리하고(펼친 목록을 접는 콤보 상자), 아무도 소비하지 않으면 화면이 처리한다(기본은 닫기).
            const bool bHandled = routeAction( *pActive, backAction, float2{} ) || pActive->onBack();
            if ( bHandled )
                consumeAction( uiMap, backAction );
        }
    }

    bool UIInputRouter::handleNavigation( UIScreen& screen, const hashed_string& action, UINavigationDirection direction )
    {
        WidgetTree& tree = screen.getTree();
        // 포커스가 없으면 첫 탐색 입력은 기본 포커스를 잡는 데 쓴다(어디로 갈지 모르는 채 옮기지 않는다).
        if ( _focus.getFocusedTree() != &tree || _focus.getFocusedWidget() == kInvalidWidgetID )
        {
            restoreFocus( screen );
            return _focus.getFocusedTree() == &tree;
        }
        if ( routeAction( screen, action, float2{} ) )
            return true;
        return _focus.navigate( tree, direction );
    }

    void UIInputRouter::processStickNavigation( UIScreen& screen, float32 deltaSeconds )
    {
        const hashed_string stickAction( UIActionName::kNavigateStick );
        const float2        value = _uiInputMap->getVector2D( stickAction );
        const float32       absX  = MathUtil::abs( value._x );
        const float32       absY  = MathUtil::abs( value._y );
        if ( MathUtil::max( absX, absY ) < UIInputRouterInternal::kStickNavigateMagnitude )
        {
            _bStickHeld = SW_FALSE;
            return;
        }
        // 큰 축 방향으로 한 번 이동한다. 스틱 위(+y)는 화면 위다.
        const UINavigationDirection direction = absX >= absY ? ( value._x > 0.0f ? UINavigationDirection::Right : UINavigationDirection::Left )
                                                             : ( value._y > 0.0f ? UINavigationDirection::Up : UINavigationDirection::Down );
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
        consumeAction( *_uiInputMap, stickAction ); // 기울인 동안(반복을 기다리는 동안에도) 스틱은 UI 의 것이다
    }

    void UIInputRouter::processScroll( UIScreen& screen )
    {
        const hashed_string scrollAction( UIActionName::kScroll );
        const float2        value = _uiInputMap->getVector2D( scrollAction );
        if ( value._x == 0.0f && value._y == 0.0f )
            return;
        // 포커스가 이 화면에 있으면 포커스 경로(목록 안의 버튼, 그 목록)로, 없으면 포인터가 올라간 경로로 보낸다.
        UIWidgetPath path{};
        _focus.makeFocusPath( screen.getTree(), path );
        if ( path.isEmpty() && _pointer.getHoverTree() == &screen.getTree() )
            path = _pointer.getHoverPath();
        if ( routeActionAlong( screen, path, scrollAction, value ) )
            consumeAction( *_uiInputMap, scrollAction );
    }

    bool UIInputRouter::routeAction( UIScreen& screen, const hashed_string& action, const float2& value )
    {
        UIWidgetPath path{};
        _focus.makeFocusPath( screen.getTree(), path );
        return routeActionAlong( screen, path, action, value );
    }

    bool UIInputRouter::routeActionAlong( UIScreen& screen, const UIWidgetPath& path, const hashed_string& action, const float2& value )
    {
        if ( path.isEmpty() )
            return false;
        UIActionEvent event{};
        event._action       = action;
        event._value        = value;
        event._deltaSeconds = _inputDeltaSeconds;
        WidgetID      handler{ kInvalidWidgetID };
        const UIReply reply = UIEventRouter::routeActionEvent( screen.getTree(), path, event, handler );
        if ( reply.isHandled() && reply._focusRequest != kInvalidWidgetID )
            (void)_focus.setFocus( screen.getTree(), reply._focusRequest );
        return reply.isHandled();
    }

    void UIInputRouter::updateKeyboardFocus()
    {
        const Widget* pFocused = nullptr;
        if ( _focus.getFocusedTree() != nullptr )
            pFocused = _focus.getFocusedTree()->findWidgetByID( _focus.getFocusedWidget() );
        const bool               bWantsText = pFocused != nullptr && pFocused->supportsTextInput();
        const InputKeyboardFocus current    = _pInput->getKeyboardFocus();
        if ( bWantsText && current == InputKeyboardFocus::Game )
            _pInput->setKeyboardFocus( InputKeyboardFocus::UI );
        else if ( bWantsText == false && current == InputKeyboardFocus::UI )
            _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
    }

    void UIInputRouter::onTextInput( string_view text )
    {
        dispatchTextEvent( text, false );
    }

    void UIInputRouter::onTextComposition( string_view text )
    {
        dispatchTextEvent( text, true );
    }

    void UIInputRouter::dispatchTextEvent( string_view text, bool bComposition )
    {
        WidgetTree* pTree   = _focus.getFocusedTree();
        Widget*     pWidget = pTree != nullptr ? pTree->findWidgetByID( _focus.getFocusedWidget() ) : nullptr;
        if ( pWidget == nullptr || pWidget->supportsTextInput() == false )
            return;
        UITextEvent event{};
        event._text         = string{ text };
        event._bComposition = bComposition ? SW_TRUE : SW_FALSE;
        (void)pWidget->onTextEvent( event );
    }

    void UIInputRouter::restoreFocus( UIScreen& screen )
    {
        WidgetTree& tree = screen.getTree();
        if ( screen._lastFocused != kInvalidWidgetID && _focus.setFocus( tree, screen._lastFocused ) )
            return;
        if ( _inputMode != UIInputMode::Navigation || tree.getRoot() == nullptr )
            return;
        const Widget*  pDefault = screen.getDesc()._defaultFocus.empty() ? nullptr : tree.findWidgetByName( screen.getDesc()._defaultFocus );
        const WidgetID target   = pDefault != nullptr ? pDefault->getID() : UINavigationSolver::findFirstFocusable( *tree.getRoot() );
        if ( target != kInvalidWidgetID )
            (void)_focus.setFocus( tree, target );
    }

    void UIInputRouter::syncInputLayers( const UIScreen* pActive, bool bHasPauseMenu )
    {
        if ( _uiInputMap == nullptr )
            return;
        _uiInputMap->setLayerEnabled( UIInputRouterInternal::kUILayerName, pActive != nullptr );
        _uiInputMap->setLayerEnabled( UIInputRouterInternal::kUIGlobalLayerName, pActive == nullptr && bHasPauseMenu );
    }

    void UIInputRouter::forgetScreen( UIScreen& screen )
    {
        _pointer.forgetTree( screen.getTree() );
        if ( _focus.getFocusedTree() == &screen.getTree() )
            _focus.clearFocus();
    }

    bool UIInputRouter::isActionConsumed( const InputMap& inputMap, const hashed_string& action ) const
    {
        return _pInput != nullptr && _consumption.isActionConsumed( inputMap, *_pInput, action );
    }

    void UIInputRouter::consumeAction( const InputMap& inputMap, const hashed_string& action )
    {
        if ( _pInput != nullptr )
            _consumption.consumeAction( inputMap, *_pInput, action );
    }
} // namespace sw
