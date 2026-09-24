#include "pch.h"

#include "Engine/Input/ActionMap.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"

/**
 * @file ActionMap.cpp
 * @brief ActionMap 의 핵심(수명주기 · 바인딩 등록 · 레이어 스택 · 리바인드 · 조회 API)입니다.
 *
 * ActionMap 의 실제 로직은 여러 파일에 나뉘어 있습니다. 찾는 것에 따라 아래 파일을 보십시오.
 *  - ActionMap.cpp (이 파일)     : 생성자, bind*() 등록 함수들, 레이어 스택(pushLayer/popLayer), 리바인드, is/wasActionXxx() 조회.
 *  - ActionMapEvaluate.cpp      : update() 가 매 프레임 부르는 상태 머신(액션이 지금 눌렸는지 · 트리거됐는지 판정).
 *  - ActionMapSerialization.cpp : InputMap XML 로드와 유저 키 리매핑 저장 · 로드.
 *  - ActionMapCombo.cpp         : 선입력 버퍼링, 격투 게임식 커맨드 시퀀스 · 패턴 판정.
 *  - ActionMapGlyph.cpp         : 액션을 UI 프롬프트 문자열("[ E ]" 등)로 바꾸는 글리프 조회.
 */

namespace sw
{
    namespace
    {
        /**
         * @brief `BindingKind` 한 종류에 딸린 값들입니다.
         * @details 여기 없는 값은 종류마다 **정말로 다른 것**(어떤 특성을 읽고 쓰는가, 어떻게 평가하는가)
         *          뿐이고, 그것들은 각자의 switch 에 남습니다. 이 저장소는 모든 switch 에 `default:` 를
         *          요구하므로(-Wswitch-default) 컴파일러가 그 switch 에서 빠진 종류를 짚어 주지는 못합니다.
         *          대신 그 `default:` 가 소리를 냅니다(저장 · 로드는 오류 로그, 평가는 단언).
         */
        struct BindingKindTraits
        {
            BindingKind _kind;              ///< 표의 자리와 열거자가 어긋나지 않게 들고 있는 자기 값.
            const utf8* _pXmlName;          ///< XML `kind` 특성에 적히는 이름.
            uint32      _conflictSlotCount; ///< 키 충돌 검사가 훑을 슬롯 수 (0 = 특정 키를 점유하지 않음).
        };

        // 종류를 더하면 **여기 한 줄**이다. 빠뜨리면 아래 static_assert 가 컴파일을 세운다.
        constexpr BindingKindTraits kArrBindingKindTraits[] = {
            {       BindingKind::SingleSlot,          "single", 1},
            {  BindingKind::Axis1DComposite,          "axis1d", 2},
            {BindingKind::Vector2DComposite,        "vector2d", 4},
            {   BindingKind::GamepadStick2D,           "stick", 0},
            {            BindingKind::Chord,           "chord", 2},
            {     BindingKind::MouseDelta2D,      "mouseDelta", 0},
            {         BindingKind::Shortcut,        "shortcut", 1},
            {           BindingKind::AnyKey,          "anyKey", 0},
            {BindingKind::VirtualJoystick2D, "virtualJoystick", 1},
        };

        static_assert( sizeof( kArrBindingKindTraits ) / sizeof( kArrBindingKindTraits[0] ) == static_cast<size_t>( BindingKind::Count ),
                       "BindingKind 를 늘렸으면 kArrBindingKindTraits 에도 줄을 더할 것 — 이름과 충돌 슬롯 수가 여기서 온다." );

        /** @brief 표에서 종류의 줄을 찾습니다. 범위 밖이면 nullptr 입니다. */
        const BindingKindTraits* findBindingKindTraits( BindingKind kind )
        {
            for ( const BindingKindTraits& traits : kArrBindingKindTraits )
            {
                if ( traits._kind == kind )
                    return &traits;
            }
            return nullptr;
        }
    } // namespace

    SW_LOG_CALLER( "ActionMap" );

    const utf8* BindingKinds::toName( BindingKind kind )
    {
        const BindingKindTraits* pTraits = findBindingKindTraits( kind );
        // 이름이 없으면 저장이 조용히 망가지므로, 모르는 종류는 빈 문자열로 **눈에 띄게** 둔다.
        return ( pTraits != nullptr ) ? pTraits->_pXmlName : "";
    }

    BindingKind BindingKinds::fromName( string_view name )
    {
        for ( const BindingKindTraits& traits : kArrBindingKindTraits )
        {
            // string_view 오버로드를 쓴다. `name.data()` 는 널 종료가 보장되지 않는다.
            if ( StringUtil::equals( name, string_view{ traits._pXmlName }, true ) )
                return traits._kind;
        }
        return BindingKind::Count;
    }

    uint32 BindingKinds::getConflictSlotCount( BindingKind kind )
    {
        const BindingKindTraits* pTraits = findBindingKindTraits( kind );
        return ( pTraits != nullptr ) ? pTraits->_conflictSlotCount : 0;
    }

    ActionMap::ActionMap()
        : _pInput{ nullptr }
        , _mapAction{}
        , _mapLayer{}
        , _listActionEntry{}
        , _listLayerEntry{}
        , _listActionName{}
        , _listLayerName{}
        , _listLayerStack{}
        , _arrBufferedAction{}
        , _bufferedActionCount{ 0 }
        , _bufferedActionHead{ 0 }
        , _arrCommandHistory{}
        , _commandHistoryCount{ 0 }
        , _commandHistoryHead{ 0 }
        , _nextGeneration{ 1 }
        , _defaultLayerName{ ActionMapDefaults::kDefaultLayerName }
        , _mouseSensitivity{ 1.0f, 1.0f }
        , _gamepadSensitivity{ 1.0f, 1.0f }
        , _doubleClickTime{ ActionMapDefaults::kDoubleClickTime }
        , _doubleClickMaxDistance{ ActionMapDefaults::kDoubleClickMaxDistance }
        , _doubleTapTime{ ActionMapDefaults::kDoubleTapTime }
        , _holdThreshold{ ActionMapDefaults::kHoldThreshold }
        , _navRepeatDelay{ 0.4f }
        , _navRepeatRate{ 0.08f }
        , _totalElapsedTime{ 0.0f }
        , _deadzoneShape{ DeadzoneShape::Radial }
        , _digitalNormalization{ DigitalNormalization::Circular }
        , _bInvertX{ SW_FALSE }
        , _bInvertY{ SW_FALSE }
        , _bSuppressBaseActionOnChord{ SW_TRUE }
        , _reservedFlags{ 0 }
    {
    }

    void ActionMap::clear()
    {
        _mapAction.clear();
        _mapLayer.clear();
        _listActionEntry.clear();
        _listLayerEntry.clear();
        _listActionName.clear();
        _listLayerName.clear();
        _listLayerStack.clear();
        _bufferedActionCount    = 0;
        _bufferedActionHead     = 0;
        _commandHistoryCount    = 0;
        _commandHistoryHead     = 0;
        _defaultLayerName       = hashed_string( ActionMapDefaults::kDefaultLayerName );
        _doubleClickTime        = ActionMapDefaults::kDoubleClickTime;
        _doubleClickMaxDistance = ActionMapDefaults::kDoubleClickMaxDistance;
        _holdThreshold          = ActionMapDefaults::kHoldThreshold;
        _totalElapsedTime       = 0.0f;
    }

    void ActionMap::bindDefaultFallback()
    {
        clear();
        registerLayer( ActionMapDefaults::kDebugLayerName, 1000, true, false, true );
        registerLayer( "UI", 100, true, false, false );
        registerLayer( ActionMapDefaults::kDefaultLayerName, 0, true, false, false );

        // 디버그 핫키 격리: Ctrl + F6, Ctrl + F7, Ctrl + F8 (게임플레이의 F1~F12 스킬과 겹치지 않게 한다)
#if !defined( SW_SHIPPING )
        bindChord( ActionMapDefaults::kReloadEditorAction, Key::LeftControl, Key::F6, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );
        bindChord( ActionMapDefaults::kReloadGameAction, Key::LeftControl, Key::F7, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );
        bindChord( ActionMapDefaults::kReloadShadersAction, Key::LeftControl, Key::F8, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );
#endif

        bind( ActionMapDefaults::kQuickSaveAction, Key::F5, ActionTrigger::Pressed, ActionMapDefaults::kDefaultLayerName );
        bind( ActionMapDefaults::kQuickLoadAction, Key::F9, ActionTrigger::Pressed, ActionMapDefaults::kDefaultLayerName );

        bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D, 0.0f, ActionMapDefaults::kDefaultLayerName );
        bindGamepadStick2D( "Move", GamepadStick::Left, 0.15f, ActionMapDefaults::kDefaultLayerName );

        bind( "Jump", Key::Space, ActionTrigger::Pressed, ActionMapDefaults::kDefaultLayerName );
        bind( "Jump", GamepadButton::A, ActionTrigger::Pressed, ActionMapDefaults::kDefaultLayerName );

        bind( "Interact", Key::E, ActionTrigger::Pressed, ActionMapDefaults::kDefaultLayerName );
        bind( "Interact", GamepadButton::X, ActionTrigger::Pressed, ActionMapDefaults::kDefaultLayerName );

        bind( "Pause", Key::Escape, ActionTrigger::Pressed, ActionMapDefaults::kDefaultLayerName );
        bind( "Pause", GamepadButton::Start, ActionTrigger::Pressed, ActionMapDefaults::kDefaultLayerName );
    }

    void ActionMap::createAction( const hashed_string& action, InputActionValueType valueType )
    {
        if ( action.empty() )
            return;
        getOrCreateAction( hashed_string( action ), valueType );
    }

    ActionBinding ActionMap::beginBinding( const hashed_string& action, BindingKind kind, ActionTrigger trigger, const hashed_string& layer )
    {
        const hashed_string layerName = layer.empty() ? _defaultLayerName : layer;
        ensureLayer( layerName );
        ensureActionListed( action );

        ActionBinding binding{};
        binding._layer            = layerName;
        binding._kind             = kind;
        binding._trigger          = trigger;
        binding._cachedLayerIndex = _mapLayer.find( layerName )->second;
        return binding;
    }

    void ActionMap::commitBinding( const hashed_string& action, InputActionValueType valueType, const ActionBinding& binding )
    {
        ActionEntry& entry = getOrCreateAction( action, valueType );
        entry._listBinding.push_back( binding );
        entry._listDefaultBinding.push_back( binding );
        entry._listBindingState.push_back( ActionBindingState{} );
    }

    void ActionMap::bind( const hashed_string& action, InputSlot slot, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::SingleSlot, trigger, layer );
        binding._arrSlot[0]   = slot;
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void ActionMap::bind( const hashed_string& action, Key key, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( key == Key::Unknown )
            return;
        bind( action, InputSlot::fromKey( key ), trigger, layer );
    }

    void ActionMap::bind( const hashed_string& action, GamepadButton button, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( button == GamepadButton::Count )
            return;
        bind( action, InputSlot::fromGamepadButton( button ), trigger, layer );
    }

    void ActionMap::bind( const hashed_string& action, MouseButton mouse, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( mouse == MouseButton::Count )
            return;
        bind( action, InputSlot::fromMouseButton( mouse ), trigger, layer );
    }

    void ActionMap::bindAxis1DComposite( const hashed_string& action, Key negativeKey, Key positiveKey, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Axis1DComposite, ActionTrigger::Down, layer );
        binding._arrSlot[0]   = InputSlot::fromKey( negativeKey );
        binding._arrSlot[1]   = InputSlot::fromKey( positiveKey );
        commitBinding( action, InputActionValueType::Axis1D, binding );
    }

    float32 ActionMap::getAxis1D( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0.0f;
        float32 val = 0.0f;
        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            float2 bVal{ 0.0f, 0.0f };
            if ( isBindingLayerActive( binding ) && evaluateBindingDown( binding, bVal ) )
                val += bVal._x;
        }
        if ( _bInvertX == SW_TRUE )
            val = -val;
        return val < -1.0f ? -1.0f : ( val > 1.0f ? 1.0f : val );
    }

    void ActionMap::bindVector2D( const hashed_string& action, Key up, Key down, Key left, Key right, float32 deadzone, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Vector2DComposite, ActionTrigger::Down, layer );
        binding._arrSlot[0]   = InputSlot::fromKey( up );
        binding._arrSlot[1]   = InputSlot::fromKey( down );
        binding._arrSlot[2]   = InputSlot::fromKey( left );
        binding._arrSlot[3]   = InputSlot::fromKey( right );
        binding._deadzone     = deadzone;
        commitBinding( action, InputActionValueType::Axis2D, binding );
    }

    void ActionMap::bindGamepadStick2D( const hashed_string& action, GamepadStick stick, float32 deadzone, const hashed_string& layer, uint8 padIndex, float32 outerDeadzone, float32 responseExponent )
    {
        if ( action.empty() )
            return;

        ActionBinding binding     = beginBinding( action, BindingKind::GamepadStick2D, ActionTrigger::Down, layer );
        binding._deviceIndex      = padIndex;
        binding._stick            = stick;
        binding._deadzone         = deadzone;
        binding._outerDeadzone    = outerDeadzone;
        binding._responseExponent = responseExponent;
        commitBinding( action, InputActionValueType::Axis2D, binding );
    }

    float2 ActionMap::getVector2D( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return float2{ 0.0f, 0.0f };

        float2 val{ 0.0f, 0.0f };
        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            float2 bVal{ 0.0f, 0.0f };
            if ( isBindingLayerActive( binding ) && evaluateBindingDown( binding, bVal ) )
            {
                val._x += bVal._x;
                val._y += bVal._y;
            }
        }
        if ( _bInvertX == SW_TRUE )
            val._x = -val._x;
        if ( _bInvertY == SW_TRUE )
            val._y = -val._y;
        val._x = val._x < -1.0f ? -1.0f : ( val._x > 1.0f ? 1.0f : val._x );
        val._y = val._y < -1.0f ? -1.0f : ( val._y > 1.0f ? 1.0f : val._y );
        return val;
    }

    void ActionMap::bindMouseDelta( const hashed_string& action, float32 sensitivity, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::MouseDelta2D, ActionTrigger::Down, layer );
        binding._scale        = sensitivity;
        commitBinding( action, InputActionValueType::Axis2D, binding );
    }

    void ActionMap::bindVirtualJoystick2D( const hashed_string& action, MouseButton activationButton, float32 radius, float32 deadzone, const hashed_string& layer, float32 outerDeadzone )
    {
        if ( action.empty() || activationButton == MouseButton::Count )
            return;

        ActionBinding binding  = beginBinding( action, BindingKind::VirtualJoystick2D, ActionTrigger::Down, layer );
        binding._arrSlot[0]    = InputSlot::fromMouseButton( activationButton );
        binding._deadzone      = deadzone;
        binding._outerDeadzone = outerDeadzone;
        binding._scale         = radius;
        commitBinding( action, InputActionValueType::Axis2D, binding );
    }

    void ActionMap::bindShortcut( const hashed_string& action, Key key, uint8 modifierMask, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( action.empty() || key == Key::Unknown )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Shortcut, trigger, layer );
        binding._modifierMask = modifierMask;
        binding._arrSlot[0]   = InputSlot::fromKey( key );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void ActionMap::bindAnyKey( const hashed_string& action, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::AnyKey, ActionTrigger::Pressed, layer );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void ActionMap::bindChord( const hashed_string& action, Key modifierKey, Key triggerKey, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( action.empty() || modifierKey == Key::Unknown || triggerKey == Key::Unknown )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Chord, trigger, layer );
        binding._arrSlot[0]   = InputSlot::fromKey( modifierKey );
        binding._arrSlot[1]   = InputSlot::fromKey( triggerKey );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    bool ActionMap::isChordDown( const hashed_string& action ) const
    {
        return hasActiveChord( action, false );
    }

    bool ActionMap::wasChordTriggered( const hashed_string& action ) const
    {
        return hasActiveChord( action, true );
    }

    bool ActionMap::hasActiveChord( const hashed_string& action, bool bTriggerJustPressed ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || _pInput == nullptr )
            return false;

        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            if ( binding._kind == BindingKind::Chord && isBindingLayerActive( binding ) )
            {
                const Key  modKey     = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                const Key  trigKey    = static_cast<Key>( binding._arrSlot[1]._controlIndex );
                const bool bTriggerOk = bTriggerJustPressed ? _pInput->wasKeyPressed( trigKey ) : _pInput->isKeyDown( trigKey );
                if ( _pInput->isKeyDown( modKey ) && bTriggerOk )
                    return true;
            }
        }
        return false;
    }

    void ActionMap::bindActionCallback( const hashed_string& action, ActionTrigger trigger, ActionCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return;
        ActionEntry&        entry = getOrCreateAction( action );
        ActionCallbackEntry cbEntry{};
        cbEntry._trigger  = trigger;
        cbEntry._callback = std::move( callback );
        entry._listActionCallback.push_back( std::move( cbEntry ) );
    }

    void ActionMap::bindPhaseCallback( const hashed_string& action, ActionPhase phase, ActionCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return;
        ActionEntry&       entry = getOrCreateAction( action );
        PhaseCallbackEntry cbEntry{};
        cbEntry._phase    = phase;
        cbEntry._callback = std::move( callback );
        entry._listPhaseCallback.push_back( std::move( cbEntry ) );
    }

    void ActionMap::bindVector2DCallback( const hashed_string& action, Vector2DCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return;
        ActionEntry& entry = getOrCreateAction( action, InputActionValueType::Axis2D );
        entry._listVector2DCallback.push_back( std::move( callback ) );
    }

    void ActionMap::clearCallbacks()
    {
        for ( ActionEntry& entry : _listActionEntry )
        {
            entry._listActionCallback.clear();
            entry._listPhaseCallback.clear();
            entry._listVector2DCallback.clear();
        }
    }

    void ActionMap::registerLayer( const hashed_string& name, int32 priority, bool enabled, bool blockLower, bool alwaysOn )
    {
        ensureLayer( name, priority, enabled, blockLower, alwaysOn );
    }

    void ActionMap::setLayerEnabled( const hashed_string& layer, bool enabled )
    {
        LayerDef* pDef = findLayer( layer );
        if ( pDef != nullptr )
            pDef->_bEnabled = enabled ? SW_TRUE : SW_FALSE;
    }

    void ActionMap::pushLayer( const hashed_string& layer, bool blockLower, bool showCursor )
    {
        const hashed_string hLayer( layer );
        ensureLayer( hLayer, 0, true, blockLower );
        LayerDef* pDef = findLayer( hLayer );
        if ( pDef != nullptr )
        {
            pDef->_bEnabled    = SW_TRUE;
            pDef->_bBlockLower = blockLower ? SW_TRUE : SW_FALSE;
        }

        for ( auto it = _listLayerStack.begin(); it != _listLayerStack.end(); ++it )
        {
            if ( *it == hLayer )
            {
                _listLayerStack.erase( it );
                break;
            }
        }
        _listLayerStack.push_back( hLayer );
        if ( _pInput != nullptr && showCursor )
            _pInput->setCursorVisible( true );
    }

    void ActionMap::popLayer()
    {
        if ( _listLayerStack.empty() == false )
            _listLayerStack.pop_back();
    }

    void ActionMap::popLayer( const hashed_string& layer )
    {
        const hashed_string hLayer( layer );
        for ( auto it = _listLayerStack.rbegin(); it != _listLayerStack.rend(); ++it )
        {
            if ( *it == hLayer )
            {
                _listLayerStack.erase( std::next( it ).base() );
                break;
            }
        }
    }

    string_view ActionMap::getCurrentTopLayer() const
    {
        if ( _listLayerStack.empty() == false )
            return _listLayerStack.back().view();
        return _defaultLayerName.view();
    }

    void ActionMap::enableOnlyLayer( const hashed_string& layer )
    {
        const hashed_string hLayer( layer );
        for ( auto& [name, index] : _mapLayer )
        {
            LayerDef& def = _listLayerEntry[index];
            if ( def._bAlwaysOn == SW_TRUE )
                continue;
            def._bEnabled = ( name == hLayer ) ? SW_TRUE : SW_FALSE;
        }
        _listLayerStack.clear();
        _listLayerStack.push_back( hLayer );
    }

    void ActionMap::setToggleMode( const hashed_string& action, bool bToggle )
    {
        ActionEntry& entry = getOrCreateAction( hashed_string( action ) );
        entry._bToggleMode = bToggle ? SW_TRUE : SW_FALSE;
        if ( bToggle == false )
            entry._bToggleState = SW_FALSE;
    }

    bool ActionMap::isActionToggled( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bToggleState == SW_TRUE;
    }

    bool ActionMap::rebindKey( const hashed_string& action, Key newKey, uint32 bindIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;

        pEntry->_listBinding[bindIndex]._kind       = BindingKind::SingleSlot;
        pEntry->_listBinding[bindIndex]._arrSlot[0] = InputSlot::fromKey( newKey );
        return true;
    }

    bool ActionMap::rebindSlot( const hashed_string& action, InputSlot slot, uint32 bindIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;

        pEntry->_listBinding[bindIndex]._kind       = BindingKind::SingleSlot;
        pEntry->_listBinding[bindIndex]._arrSlot[0] = slot;
        return true;
    }

    bool ActionMap::rebindWithResolution( const hashed_string& action, InputSlot newSlot, ConflictResolution strategy, uint32 bindIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;

        const hashed_string targetLayer = pEntry->_listBinding[bindIndex]._layer;

        hashed_string conflictingAction{};
        uint32        conflictingBindIndex = 0;
        for ( auto& [actName, actIndex] : _mapAction )
        {
            if ( actName == action )
                continue;
            const ActionEntry& entry = _listActionEntry[actIndex];
            for ( uint32 bIdx = 0; bIdx < entry._listBinding.size(); ++bIdx )
            {
                if ( entry._listBinding[bIdx]._layer == targetLayer && entry._listBinding[bIdx]._arrSlot[0] == newSlot )
                {
                    conflictingAction    = actName;
                    conflictingBindIndex = bIdx;
                    break;
                }
            }
            if ( conflictingAction.empty() == false )
                break;
        }

        if ( conflictingAction.empty() == false )
        {
            ActionEntry* pConflictEntry = findAction( conflictingAction );
            if ( strategy == ConflictResolution::Swap && pConflictEntry != nullptr )
            {
                const InputSlot oldSlot                                        = pEntry->_listBinding[bindIndex]._arrSlot[0];
                pConflictEntry->_listBinding[conflictingBindIndex]._arrSlot[0] = oldSlot;
                pEntry->_listBinding[bindIndex]._arrSlot[0]                    = newSlot;
                return true;
            }
            else if ( strategy == ConflictResolution::Override && pConflictEntry != nullptr )
            {
                pConflictEntry->_listBinding[conflictingBindIndex]._arrSlot[0] = InputSlot{};
                pEntry->_listBinding[bindIndex]._arrSlot[0]                    = newSlot;
                return true;
            }
            else if ( strategy == ConflictResolution::AddSecondary )
            {
                ActionBinding newBinding = pEntry->_listBinding[bindIndex];
                newBinding._arrSlot[0]   = newSlot;
                pEntry->_listBinding.push_back( newBinding );
                pEntry->_listBindingState.push_back( ActionBindingState{} );
                return true;
            }
        }

        pEntry->_listBinding[bindIndex]._arrSlot[0] = newSlot;
        return true;
    }

    bool ActionMap::hasBindingConflict( const InputSlot& slot, const hashed_string& layer, string& outConflictingAction ) const
    {
        const hashed_string targetLayer = layer.empty() ? _defaultLayerName : hashed_string( layer );

        for ( const auto& [actionName, actIndex] : _mapAction )
        {
            const ActionEntry& entry = _listActionEntry[actIndex];
            for ( const ActionBinding& binding : entry._listBinding )
            {
                if ( binding._layer != targetLayer )
                    continue;

                // 예전에는 여기 switch 가 있었고 `default:` 가 0 을 줬다. 새 종류를 더하면 충돌
                // 검사가 그 바인딩을 **못 본 채** 지나가고, 같은 키를 두 번 걸어도 조용했다.
                const uint32 slotCount = BindingKinds::getConflictSlotCount( binding._kind );

                for ( uint32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
                {
                    if ( binding._arrSlot[slotIndex] == slot )
                    {
                        outConflictingAction = actionName.c_str();
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool ActionMap::resetActionToDefault( const hashed_string& action )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || pEntry->_listDefaultBinding.empty() )
            return false;

        pEntry->_listBinding = pEntry->_listDefaultBinding;
        pEntry->_listBindingState.clear();
        pEntry->_listBindingState.resize( pEntry->_listBinding.size() );
        return true;
    }

    void ActionMap::resetAllBindingsToDefault()
    {
        for ( ActionEntry& entry : _listActionEntry )
        {
            if ( entry._listDefaultBinding.empty() == false )
            {
                entry._listBinding = entry._listDefaultBinding;
                entry._listBindingState.clear();
                entry._listBindingState.resize( entry._listBinding.size() );
            }
        }
    }

    void ActionMap::setDoubleClickTime( float32 seconds )
    {
        _doubleClickTime = MathUtil::clamp( seconds, ActionMapDefaults::kDoubleClickTimeMin, ActionMapDefaults::kDoubleClickTimeMax );
    }

    void ActionMap::setDoubleClickMaxDistance( float32 pixels )
    {
        _doubleClickMaxDistance = MathUtil::clamp( pixels, 0.0f, ActionMapDefaults::kDoubleClickDistanceMax );
    }

    void ActionMap::setHoldThreshold( float32 seconds )
    {
        _holdThreshold = MathUtil::clamp( seconds, ActionMapDefaults::kHoldThresholdMin, ActionMapDefaults::kHoldThresholdMax );
    }

    bool ActionMap::hasLayer( const hashed_string& layer ) const
    {
        return _mapLayer.find( layer ) != _mapLayer.end();
    }

    bool ActionMap::isLayerEnabled( const hashed_string& layer ) const
    {
        return isLayerActiveInternal( layer );
    }

    int32 ActionMap::getLayerPriority( const hashed_string& layer ) const
    {
        const LayerDef* pDef = findLayer( layer );
        return pDef != nullptr ? pDef->_priority : 0;
    }

    bool ActionMap::hasAction( const hashed_string& action ) const
    {
        return _mapAction.find( action ) != _mapAction.end();
    }

    ActionTrigger ActionMap::getBindingTrigger( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return ActionTrigger::Pressed;
        return pEntry->_listBinding[bindIndex]._trigger;
    }

    uint32 ActionMap::getBindingCount( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? static_cast<uint32>( pEntry->_listBinding.size() ) : 0;
    }

    const ActionBinding* ActionMap::getBinding( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return nullptr;
        return &pEntry->_listBinding[bindIndex];
    }

    bool ActionMap::wasActionTriggered( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
    }

    bool ActionMap::isActionDown( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
    }

    ActionHandle ActionMap::getActionHandle( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry != nullptr && pEntry->_handleIndex != ActionHandle::kInvalidIndex )
            return ActionHandle{ pEntry->_handleIndex, pEntry->_generation };
        return ActionHandle{};
    }

    const ActionMap::ActionEntry* ActionMap::getActionFromHandle( ActionHandle handle ) const
    {
        if ( handle._index < _listActionEntry.size() )
        {
            const ActionEntry& entry = _listActionEntry[handle._index];
            if ( entry._generation == handle._generation )
                return &entry;
        }
        return nullptr;
    }

    bool ActionMap::wasActionTriggered( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
    }

    bool ActionMap::isActionDown( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
    }

    bool ActionMap::wasActionPressed( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bPressed == SW_TRUE;
    }

    bool ActionMap::wasActionPressed( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bPressed == SW_TRUE;
    }

    bool ActionMap::wasActionReleased( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bReleased == SW_TRUE;
    }

    bool ActionMap::wasActionReleased( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bReleased == SW_TRUE;
    }

    bool ActionMap::wasActionDoubleClicked( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bDoubleClicked == SW_TRUE;
    }

    bool ActionMap::wasActionDoubleClicked( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bDoubleClicked == SW_TRUE;
    }

    bool ActionMap::wasActionHoldThreshold( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bHoldThreshold == SW_TRUE;
    }

    bool ActionMap::wasActionHoldThreshold( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bHoldThreshold == SW_TRUE;
    }

    float32 ActionMap::getActionHoldDuration( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? pEntry->_holdDuration : 0.0f;
    }

    float32 ActionMap::getActionHoldDuration( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_holdDuration : 0.0f;
    }

    float2 ActionMap::getVector2D( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_currentValue : float2{ 0.0f, 0.0f };
    }

    ActionPhase ActionMap::getActionPhase( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? pEntry->_currentPhase : ActionPhase::None;
    }

    ActionPhase ActionMap::getActionPhase( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_currentPhase : ActionPhase::None;
    }

    void ActionMap::ensureActionListed( const hashed_string& action )
    {
        for ( const hashed_string& name : _listActionName )
        {
            if ( name == action )
                return;
        }
        _listActionName.push_back( action );
    }

    LayerDef& ActionMap::ensureLayer( const hashed_string& name, int32 priority, bool enabled, bool blockLower, bool alwaysOn )
    {
        auto it = _mapLayer.find( name );
        if ( it != _mapLayer.end() )
            return _listLayerEntry[it->second];

        LayerDef def{};
        def._name        = name;
        def._priority    = priority;
        def._bEnabled    = enabled ? SW_TRUE : SW_FALSE;
        def._bBlockLower = blockLower ? SW_TRUE : SW_FALSE;
        def._bAlwaysOn   = alwaysOn ? SW_TRUE : SW_FALSE;

        _listLayerName.push_back( def._name );
        const uint32 index = static_cast<uint32>( _listLayerEntry.size() );
        _listLayerEntry.push_back( def );
        _mapLayer.emplace( name, index );
        return _listLayerEntry[index];
    }

    ActionMap::ActionEntry& ActionMap::getOrCreateAction( const hashed_string& action, InputActionValueType valueType )
    {
        ensureActionListed( action );
        auto it = _mapAction.find( action );
        if ( it == _mapAction.end() )
        {
            const uint32 index = static_cast<uint32>( _listActionEntry.size() );

            ActionEntry entry{};
            entry._valueType   = valueType;
            entry._handleIndex = index;
            entry._generation  = ++_nextGeneration;
            _listActionEntry.push_back( std::move( entry ) );
            _mapAction.emplace( action, index );
            return _listActionEntry[index];
        }

        ActionEntry& entry = _listActionEntry[it->second];
        if ( entry._valueType == InputActionValueType::Boolean && valueType != InputActionValueType::Boolean )
            entry._valueType = valueType;
        return entry;
    }

    LayerDef* ActionMap::findLayer( const hashed_string& name )
    {
        auto it = _mapLayer.find( name );
        return it != _mapLayer.end() ? &_listLayerEntry[it->second] : nullptr;
    }

    const LayerDef* ActionMap::findLayer( const hashed_string& name ) const
    {
        auto it = _mapLayer.find( name );
        return it != _mapLayer.end() ? &_listLayerEntry[it->second] : nullptr;
    }

    ActionMap::ActionEntry* ActionMap::findAction( const hashed_string& action )
    {
        auto it = _mapAction.find( action );
        return it != _mapAction.end() ? &_listActionEntry[it->second] : nullptr;
    }

    const ActionMap::ActionEntry* ActionMap::findAction( const hashed_string& action ) const
    {
        auto it = _mapAction.find( action );
        return it != _mapAction.end() ? &_listActionEntry[it->second] : nullptr;
    }

    void ActionMap::getDebugActionStates( vector<DebugActionState>& outListState ) const
    {
        outListState.clear();
        outListState.reserve( _mapAction.size() );
        for ( const auto& [actName, actIndex] : _mapAction )
        {
            const ActionEntry& entry = _listActionEntry[actIndex];
            DebugActionState   state{};
            state._action       = actName;
            state._layer        = entry._listBinding.empty() ? _defaultLayerName : entry._listBinding[0]._layer;
            state._valueType    = entry._valueType;
            state._phase        = entry._currentPhase;
            state._value        = entry._currentValue;
            state._holdDuration = entry._holdDuration;
            state._bTriggered   = entry._bTriggered;
            state._bDown        = entry._bDown;
            outListState.push_back( state );
        }
    }
} // namespace sw
