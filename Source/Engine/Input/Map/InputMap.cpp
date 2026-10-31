#include "pch.h"

#include "Engine/Input/Map/InputMap.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMapXMLUtil.h"
#include "Engine/Input/Map/InputRemapSerializer.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualCursor.h"
#include "Engine/Input/Virtual/VirtualJoystick.h"
#include "Engine/Serialization/XML/XMLDocument.h"

/**
 * @file InputMap.cpp
 * @brief InputMap 의 핵심 로직(수명 주기, 바인딩 등록, 레이어 스택, 리바인드, 조회 API)을 구현합니다.
 *
 * InputMap 의 로직은 여러 파일에 나뉘어 있습니다. 찾는 내용에 따라 아래 파일을 보십시오.
 *  - 생성자, bind*() 등록 함수, 레이어 스택(pushLayer/popLayer), 리바인드, is/wasActionXxx() 조회.
 *  - update() 가 매 프레임 부르는 상태 머신(액션이 눌렸는지, 트리거됐는지 판정).
 *  - InputMap XML 정의의 로드와 저장.
 *  맵이 들고 있거나 맵을 읽는 협력 클래스는 따로 있다(언리얼 Enhanced Input 이 매핑 컨텍스트와 플레이어 설정 · 아이콘을 가르는 나눔).
 *  - InputActionBuffer: 선입력 버퍼링과 격투 게임식 커맨드 시퀀스, 패턴 판정(맵이 들고 프레임마다 진행).
 *  - InputGlyphResolver: 액션을 UI 프롬프트 문자열("[ E ]" 등)로 바꾸는 글리프 조회(상태 없음).
 *  - InputAccessibilitySettings: 감도 · 축 반전 · 데드존 · 토글 액션(맵이 들고, 맵을 다시 읽어도 남는다).
 *  - InputRemapSerializer: 플레이어 리매핑 파일(`<UserBindings>`)의 쓰기 · 읽기. 두 XML 형식이 함께 쓰는 칸 읽기는 InputMapXMLUtil.
 */

namespace sw
{
    namespace
    {
        /**
         * @brief `BindingKind` 한 종류에 딸린 값입니다.
         * @details 이 테이블에 없는 값은 종류마다 실제로 다른 것(어떤 특성을 읽고 쓰는가, 어떻게 평가하는가)이고,
         *          그것들은 각자의 switch 에 남는다. 이 저장소는 모든 switch 에 `default:` 를
         *          요구하므로(-Wswitch-default) 컴파일러가 그 switch 에서 빠진 종류를 알려 주지 못한다.
         *          대신 `default:` 에서 알린다(저장과 로드는 오류 로그, 평가는 단언).
         */
        struct BindingKindInfo
        {
            BindingKind _kind;              ///< 테이블 위치와 열거자가 어긋나지 않는지 확인하려고 가진 자기 값입니다.
            const utf8* _pXMLName;          ///< XML `kind` 특성에 적는 이름입니다.
            uint32      _conflictSlotCount; ///< 키 충돌 검사가 훑을 슬롯 수입니다(0 이면 특정 키를 점유하지 않음).
            uint32      _rebindSlotIndex;   ///< 키 하나로 다시 잡을 때 바뀌는 슬롯입니다(`BindingKinds::kNoRebindSlot` 이면 키 하나로는 못 바꿈).
            bool        _bRelative;         ///< 이번 프레임의 이동량인지 나타냅니다. 묶지 않고, 액션 값 단계에서 반전하지 않습니다(`BindingKinds::isRelative`).
        };

        constexpr uint32 kNoRebind = BindingKinds::kNoRebindSlot;

        // 종류를 추가하면 이 테이블에 한 줄을 더한다. 빠뜨리면 아래 static_assert 가 컴파일을 막는다.
        constexpr BindingKindInfo kArrBindingKindInfo[] = {
            {       BindingKind::SingleSlot,          "single", 1,         0, false},
            {  BindingKind::Axis1DComposite,          "axis1d", 2, kNoRebind, false},
            {BindingKind::Vector2DComposite,        "vector2d", 4, kNoRebind, false},
            {   BindingKind::GamepadStick2D,           "stick", 0, kNoRebind, false},
            {            BindingKind::Chord,           "chord", 2,         1, false},
            {     BindingKind::MouseDelta2D,      "mouseDelta", 0, kNoRebind,  true},
            {         BindingKind::Shortcut,        "shortcut", 1,         0, false},
            {           BindingKind::AnyKey,          "anyKey", 0, kNoRebind, false},
            {BindingKind::VirtualJoystick2D, "virtualJoystick", 1, kNoRebind, false},
            {     BindingKind::MouseWheel1D,      "mouseWheel", 0, kNoRebind,  true},
        };

        static_assert( sizeof( kArrBindingKindInfo ) / sizeof( kArrBindingKindInfo[0] ) == static_cast<size_t>( BindingKind::Count ),
                       "BindingKind 를 늘렸으면 kArrBindingKindInfo 에도 줄을 더할 것 — 이름과 충돌 슬롯 수가 여기서 온다." );

        /** @brief 테이블에서 종류에 해당하는 줄을 찾습니다. 범위 밖이면 nullptr 입니다. */
        const BindingKindInfo* findBindingKindInfo( BindingKind kind )
        {
            for ( const BindingKindInfo& info : kArrBindingKindInfo )
            {
                if ( info._kind == kind )
                    return &info;
            }
            return nullptr;
        }

        /**
         * @brief 다시 잡으려는 슬롯과 겹치는 이 바인딩의 슬롯 번호입니다. 겹치지 않으면 `BindingKinds::kNoRebindSlot` 입니다.
         * @details 키 하나로 다시 잡는 슬롯이 있는 종류(단일 슬롯, Chord 의 방아쇠, Shortcut)는 그 슬롯만 본다. Chord 의 수식 키(Ctrl)는
         *          다른 액션이 단독으로 써도 겹침이 아니다. 그런 슬롯이 없는 합성 축과 2D 합성은 키를 점유하는 모든 슬롯을 본다(그 부분이 맞바뀌거나 비워진다).
         */
        uint32 findConflictingSlot( const ActionBinding& binding, const InputSlot& slot )
        {
            const uint32 rebindSlot = BindingKinds::getRebindSlotIndex( binding._kind );
            if ( rebindSlot != BindingKinds::kNoRebindSlot )
                return binding._arrSlot[rebindSlot] == slot ? rebindSlot : BindingKinds::kNoRebindSlot;
            const uint32 slotCount = BindingKinds::getConflictSlotCount( binding._kind );
            for ( uint32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
            {
                if ( binding._arrSlot[slotIndex] == slot )
                    return slotIndex;
            }
            return BindingKinds::kNoRebindSlot;
        }

        /** @brief 데드존 밖 크기(0 ~ 1)를 [안쪽 데드존, 바깥 데드존] 구간으로 펴고 응답 지수를 적용합니다. */
        float32 shapeStickMagnitude( float32 magnitude, float32 inDeadzone, float32 deadRange, float32 responseExponent )
        {
            float32 norm = deadRange > 0.0001f ? MathUtil::clamp( ( magnitude - inDeadzone ) / deadRange, 0.0f, 1.0f ) : 1.0f;
            if ( responseExponent != 1.0f && responseExponent > 0.0f )
                norm = MathUtil::pow( norm, responseExponent );
            return norm;
        }

        /** @brief 축마다 따로 데드존을 적용합니다(Axial). */
        float32 applyAxialDeadzone( float32 val, float32 inDeadzone, float32 deadRange, float32 responseExponent )
        {
            const float32 absVal = MathUtil::abs( val );
            if ( absVal <= inDeadzone )
                return 0.0f;
            return ( val > 0.0f ? 1.0f : -1.0f ) * shapeStickMagnitude( absVal, inDeadzone, deadRange, responseExponent );
        }

        struct InputMapInternal
        {
            struct InputMapXML
            {
                static constexpr const utf8* kRoot                = "InputMap";
                static constexpr const utf8* kLayers              = "layers";
                static constexpr const utf8* kLayer               = "layer";
                static constexpr const utf8* kAction              = "action";
                static constexpr const utf8* kBind                = "bind";
                static constexpr const utf8* kAttrDefaultLayer    = "defaultLayer";
                static constexpr const utf8* kAttrDoubleClick     = "doubleClickTime";
                static constexpr const utf8* kAttrDoubleClickDist = "doubleClickMaxDistance";
                static constexpr const utf8* kAttrHoldThreshold   = "holdThreshold";
                static constexpr const utf8* kAttrDragThreshold   = "dragThreshold";
                static constexpr const utf8* kAttrVirtualCursor   = "virtualCursor";
                static constexpr const utf8* kAttrName            = "name";
                static constexpr const utf8* kAttrPriority        = "priority";
                static constexpr const utf8* kAttrEnabled         = "enabled";
                static constexpr const utf8* kAttrBlockLower      = "blockLower";
                static constexpr const utf8* kAttrAlwaysOn        = "alwaysOn";
                static constexpr const utf8* kAttrLayer           = "layer";
                static constexpr const utf8* kAttrTrigger         = "trigger";
                static constexpr const utf8* kAttrSource          = "source";
                static constexpr const utf8* kAttrCode            = "code";
                static constexpr const utf8* kAttrModifier        = "modifier";
                static constexpr const utf8* kAttrDeadzone        = "deadzone";
                static constexpr const utf8* kAttrValueType       = "valueType";
                static constexpr const utf8* kAttrPad             = "pad";
                static constexpr const utf8* kSourceKey           = "key";
                static constexpr const utf8* kSourceGamepad       = "gamepad";
                static constexpr const utf8* kSourceMouse         = "mouse";
            };

            struct VirtualCursorNameEntry
            {
                const utf8*           _pName;
                InputMapVirtualCursor _stick;
            };

            /** @brief `virtualCursor` 특성의 이름입니다. 읽기와 쓰기가 이 표 하나를 함께 씁니다. */
            static constexpr VirtualCursorNameEntry kArrVirtualCursorNames[] = {
                {       "Off",        InputMapVirtualCursor::Off},
                { "LeftStick",  InputMapVirtualCursor::LeftStick},
                {"RightStick", InputMapVirtualCursor::RightStick},
            };

            /** @brief `virtualCursor` 특성을 읽습니다. 모르는 이름이면 false 입니다(철자 그대로 견준다). */
            [[nodiscard]] static bool tryParseVirtualCursor( const utf8* pText, InputMapVirtualCursor& outStick )
            {
                for ( const VirtualCursorNameEntry& entry : kArrVirtualCursorNames )
                {
                    if ( StringUtil::equals( pText, entry._pName ) )
                    {
                        outStick = entry._stick;
                        return true;
                    }
                }
                return false;
            }

            static const utf8* toVirtualCursorName( InputMapVirtualCursor stick )
            {
                for ( const VirtualCursorNameEntry& entry : kArrVirtualCursorNames )
                {
                    if ( entry._stick == stick )
                        return entry._pName;
                }
                return kArrVirtualCursorNames[0]._pName;
            }

            struct TriggerNameEntry
            {
                const utf8*   _pName;
                ActionTrigger _trigger;
            };

            static constexpr TriggerNameEntry kArrTriggerNames[] = {
                {       "Pressed",        ActionTrigger::Pressed},
                {         "Press",        ActionTrigger::Pressed},
                {       "Started",        ActionTrigger::Pressed},
                {          "Down",           ActionTrigger::Down},
                {          "Held",           ActionTrigger::Down},
                {     "Performed",           ActionTrigger::Down},
                {      "Released",       ActionTrigger::Released},
                {       "Release",       ActionTrigger::Released},
                {      "Canceled",       ActionTrigger::Released},
                { "DoubleClicked",  ActionTrigger::DoubleClicked},
                {   "DoubleClick",  ActionTrigger::DoubleClicked},
                { "HoldThreshold",  ActionTrigger::HoldThreshold},
                {          "Hold",  ActionTrigger::HoldThreshold},
                {"HoldAndRelease", ActionTrigger::HoldAndRelease},
                {           "Tap",            ActionTrigger::Tap},
                {         "Pulse",          ActionTrigger::Pulse},
                {     "DoubleTap",      ActionTrigger::DoubleTap},
                {        "Repeat",         ActionTrigger::Repeat},
                {      "MultiTap",       ActionTrigger::MultiTap},
                {     "NavRepeat",         ActionTrigger::Repeat},
                {         "Click",          ActionTrigger::Click},
                {          "Drag",           ActionTrigger::Drag},
            };

            struct ValueTypeNameEntry
            {
                const utf8*          _pName;
                InputActionValueType _valueType;
            };

            static constexpr ValueTypeNameEntry kArrValueTypeNames[] = {
                {"Boolean", InputActionValueType::Boolean},
                { "Axis1D",  InputActionValueType::Axis1D},
                { "Axis2D",  InputActionValueType::Axis2D},
            };

            static const utf8* toValueTypeName( InputActionValueType valueType )
            {
                for ( const ValueTypeNameEntry& entry : kArrValueTypeNames )
                {
                    if ( entry._valueType == valueType )
                        return entry._pName;
                }
                return kArrValueTypeNames[0]._pName;
            }

            /** @brief 값 종류 이름을 읽습니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool tryParseValueType( string_view name, InputActionValueType& outValueType )
            {
                for ( const ValueTypeNameEntry& entry : kArrValueTypeNames )
                {
                    if ( StringUtil::equals( name, entry._pName ) )
                    {
                        outValueType = entry._valueType;
                        return true;
                    }
                }
                return false;
            }

            /** @brief `SingleSlot` 바인딩을 `<bind>` 로 씁니다. `<InputMap>` 형식이 이름 붙일 수 없는 장치면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool writeDefinitionSingleSlot( XMLNode actionNode, const utf8* pActionName, const ActionBinding& binding, const utf8* pTriggerName )
            {
                const InputSlot& slot     = binding._arrSlot[0];
                XMLNode          bindNode = actionNode.appendChild( InputMapXML::kBind );
                if ( slot._deviceKind == InputDeviceKind::Keyboard )
                {
                    bindNode.appendAttribute( InputMapXML::kAttrSource, InputMapXML::kSourceKey );
                    bindNode.appendAttribute( InputMapXML::kAttrCode, KeyCodeUtil::toName( static_cast<Key>( slot._controlIndex ) ) );
                }
                else if ( slot._deviceKind == InputDeviceKind::Mouse )
                {
                    bindNode.appendAttribute( InputMapXML::kAttrSource, InputMapXML::kSourceMouse );
                    bindNode.appendAttribute( InputMapXML::kAttrCode, MouseButtonUtil::toName( static_cast<MouseButton>( slot._controlIndex ) ) );
                }
                else if ( slot._deviceKind == InputDeviceKind::Gamepad )
                {
                    bindNode.appendAttribute( InputMapXML::kAttrSource, InputMapXML::kSourceGamepad );
                    bindNode.appendAttribute( InputMapXML::kAttrCode, GamepadButtonUtil::toName( static_cast<GamepadButton>( slot._controlIndex ) ) );
                    bindNode.appendAttribute( InputMapXML::kAttrPad, static_cast<int32>( slot._deviceIndex ) );
                }
                else
                {
                    SW_LOG_ERROR( "Action '%#' has a binding on a device the <InputMap> format cannot name - not saved", pActionName );
                    return false;
                }
                if ( pTriggerName != nullptr )
                    bindNode.appendAttribute( InputMapXML::kAttrTrigger, pTriggerName );
                InputMapXMLUtil::writeBindingModifier( bindNode, binding );
                bindNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                return true;
            }

            /** @brief `Shortcut` 바인딩(수정 키 마스크 + 키)을 `<bind source="key" modifier="Ctrl+Shift">` 로 씁니다. 키가 아닌 슬롯이면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool writeDefinitionShortcut( XMLNode actionNode, const utf8* pActionName, const ActionBinding& binding, const utf8* pTriggerName )
            {
                const InputSlot& slot = binding._arrSlot[0];
                if ( slot._deviceKind != InputDeviceKind::Keyboard || binding._modifierMask == ModifierKey::None )
                {
                    SW_LOG_ERROR( "Action '%#' has a shortcut binding without a key or a modifier - not saved", pActionName );
                    return false;
                }
                XMLNode bindNode = actionNode.appendChild( InputMapXML::kBind );
                bindNode.appendAttribute( InputMapXML::kAttrSource, InputMapXML::kSourceKey );
                bindNode.appendAttribute( InputMapXML::kAttrModifier, InputRemapSerializer::formatModifierNames( binding._modifierMask ).c_str() );
                bindNode.appendAttribute( InputMapXML::kAttrCode, KeyCodeUtil::toName( static_cast<Key>( slot._controlIndex ) ) );
                if ( pTriggerName != nullptr )
                    bindNode.appendAttribute( InputMapXML::kAttrTrigger, pTriggerName );
                InputMapXMLUtil::writeBindingModifier( bindNode, binding );
                bindNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                return true;
            }

            /** @brief `Axis1DComposite` 바인딩을 `<axis1d>` 로 씁니다. 두 장치에 걸치면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool writeDefinitionAxis1D( XMLNode actionNode, const utf8* pActionName, const ActionBinding& binding, const utf8* pTriggerName )
            {
                const InputSlot& negativeSlot = binding._arrSlot[0];
                const InputSlot& positiveSlot = binding._arrSlot[1];
                const bool       bGamepad     = negativeSlot._deviceKind == InputDeviceKind::Gamepad && positiveSlot._deviceKind == InputDeviceKind::Gamepad &&
                                      negativeSlot._deviceIndex == positiveSlot._deviceIndex;
                const bool bKeyboard = negativeSlot._deviceKind == InputDeviceKind::Keyboard && positiveSlot._deviceKind == InputDeviceKind::Keyboard;
                if ( bGamepad == false && bKeyboard == false )
                {
                    SW_LOG_ERROR( "Action '%#' has an axis1d binding over two devices, which the <InputMap> format cannot name - not saved", pActionName );
                    return false;
                }
                XMLNode axisNode = actionNode.appendChild( "axis1d" );
                if ( bGamepad )
                {
                    axisNode.appendAttribute( InputMapXML::kAttrSource, InputMapXML::kSourceGamepad );
                    axisNode.appendAttribute( "negative", GamepadButtonUtil::toName( static_cast<GamepadButton>( negativeSlot._controlIndex ) ) );
                    axisNode.appendAttribute( "positive", GamepadButtonUtil::toName( static_cast<GamepadButton>( positiveSlot._controlIndex ) ) );
                    axisNode.appendAttribute( InputMapXML::kAttrPad, static_cast<int32>( negativeSlot._deviceIndex ) );
                }
                else
                {
                    axisNode.appendAttribute( "negative", KeyCodeUtil::toName( static_cast<Key>( negativeSlot._controlIndex ) ) );
                    axisNode.appendAttribute( "positive", KeyCodeUtil::toName( static_cast<Key>( positiveSlot._controlIndex ) ) );
                }
                // Down 은 적지 않아도 Down 으로 읽히므로, 그 밖의 trigger 만 적는다.
                if ( binding._trigger != ActionTrigger::Down && pTriggerName != nullptr )
                    axisNode.appendAttribute( InputMapXML::kAttrTrigger, pTriggerName );
                InputMapXMLUtil::writeBindingModifier( axisNode, binding );
                axisNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                return true;
            }

            /**
             * @brief 바인딩 하나를 `<InputMap>` 의 액션 노드 아래에, `loadFromAsset` 가 읽는 형태로 씁니다.
             * @return `<InputMap>` 형식으로 표현할 수 없는 종류(가상 조이스틱, 아무 키)면 오류를 남기고 false 를 반환합니다.
             *         빼고 쓰면 다시 읽을 때 그 바인딩이 조용히 사라지기 때문입니다.
             */
            [[nodiscard]] static bool writeDefinitionBinding( XMLNode actionNode, const utf8* pActionName, const ActionBinding& binding )
            {
                const utf8* pTriggerName = InputMap::actionTriggerToName( binding._trigger );
                switch ( binding._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        return writeDefinitionSingleSlot( actionNode, pActionName, binding, pTriggerName );
                    }
                    case BindingKind::Chord:
                    {
                        XMLNode chordNode = actionNode.appendChild( "chord" );
                        chordNode.appendAttribute( "modifier", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[0]._controlIndex ) ) );
                        chordNode.appendAttribute( "trigger", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[1]._controlIndex ) ) );
                        if ( pTriggerName != nullptr )
                            chordNode.appendAttribute( "triggerMode", pTriggerName );
                        InputMapXMLUtil::writeBindingModifier( chordNode, binding );
                        chordNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        return writeDefinitionAxis1D( actionNode, pActionName, binding, pTriggerName );
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        XMLNode compNode = actionNode.appendChild( "vector2d" );
                        compNode.appendAttribute( "up", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[0]._controlIndex ) ) );
                        compNode.appendAttribute( "down", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[1]._controlIndex ) ) );
                        compNode.appendAttribute( "left", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[2]._controlIndex ) ) );
                        compNode.appendAttribute( "right", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[3]._controlIndex ) ) );
                        compNode.appendAttribute( InputMapXML::kAttrDeadzone, binding._deadzone );
                        compNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        XMLNode stickNode = actionNode.appendChild( "stick" );
                        stickNode.appendAttribute( "stick", binding._stick == GamepadStick::Right ? "Right" : "Left" );
                        stickNode.appendAttribute( InputMapXML::kAttrPad, static_cast<int32>( binding._deviceIndex ) );
                        stickNode.appendAttribute( InputMapXML::kAttrDeadzone, binding._deadzone );
                        stickNode.appendAttribute( "outerDeadzone", binding._outerDeadzone );
                        stickNode.appendAttribute( "responseExponent", binding._responseExponent );
                        InputMapXMLUtil::writeStickModifier( stickNode, binding );
                        stickNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::MouseDelta2D:
                    {
                        XMLNode deltaNode = actionNode.appendChild( "mouseDelta" );
                        deltaNode.appendAttribute( "scale", binding._scale );
                        deltaNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::MouseWheel1D:
                    {
                        XMLNode wheelNode = actionNode.appendChild( "mouseWheel" );
                        wheelNode.appendAttribute( "scale", binding._scale );
                        wheelNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::Shortcut:
                    {
                        return writeDefinitionShortcut( actionNode, pActionName, binding, pTriggerName );
                    }
                    case BindingKind::VirtualJoystick2D:
                    case BindingKind::AnyKey:
                    case BindingKind::Count:
                    {
                        SW_LOG_ERROR( "Action '%#' has a %# binding, which the <InputMap> format cannot hold - not saved", pActionName,
                                      BindingKinds::toName( binding._kind ) );
                        return false;
                    }
                }
                return false;
            }

            /** @brief `<layer>` 의 설정입니다. 적지 않은 칸은 이 초기값(우선순위 0, 켜짐, 아래를 막지 않음, 늘 켜지 않음)입니다. */
            struct LayerSettings
            {
                int32 _priority{ 0 };
                bool  _bEnabled{ true };
                bool  _bBlockLower{ false };
                bool  _bAlwaysOn{ false };
            };

            /** @brief `<layer>` 원소의 설정을 읽습니다. 깨진 값은 오류로 @p inoutbValid 를 내립니다. */
            static void readLayerSettings( XMLNode node, LayerSettings& inoutSettings, bool& inoutbValid )
            {
                InputMapXMLUtil::readOptional( node, InputMapXML::kAttrPriority, inoutSettings._priority, inoutbValid );
                InputMapXMLUtil::readOptional( node, InputMapXML::kAttrEnabled, inoutSettings._bEnabled, inoutbValid );
                InputMapXMLUtil::readOptional( node, InputMapXML::kAttrBlockLower, inoutSettings._bBlockLower, inoutbValid );
                InputMapXMLUtil::readOptional( node, InputMapXML::kAttrAlwaysOn, inoutSettings._bAlwaysOn, inoutbValid );
            }

            /** @brief 더하는 파일이 맵 전체 값을 정했으면 오류를 남기고 true 입니다. 두 파일이 각자 정하면 어느 값이 이기는지가 읽는 순서에 달리기 때문이다. */
            [[nodiscard]] static bool hasMapWideAttribute( const XMLNode& root, string_view absPath )
            {
                static constexpr const utf8* kArrMapWideAttr[] = { InputMapXML::kAttrDefaultLayer, InputMapXML::kAttrDoubleClick, InputMapXML::kAttrDoubleClickDist,
                                                                   InputMapXML::kAttrHoldThreshold, InputMapXML::kAttrDragThreshold, InputMapXML::kAttrVirtualCursor };
                bool                         bMapWide          = false;
                for ( const utf8* pAttr : kArrMapWideAttr )
                {
                    if ( root.findAttribute( pAttr ) == nullptr )
                        continue;
                    SW_LOG_ERROR( "InputMap %# is appended to another map but sets the map-wide attribute '%#' - only the base map sets it", absPath, pAttr );
                    bMapWide = true;
                }
                return bMapWide;
            }
        };
    } // namespace

    SW_LOG_CALLER( "InputMap" );

    const utf8* BindingKinds::toName( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        // 이름이 없으면 저장이 조용히 망가지므로, 모르는 종류는 빈 문자열로 눈에 띄게 둔다.
        return ( pInfo != nullptr ) ? pInfo->_pXMLName : "";
    }

    BindingKind BindingKinds::fromName( string_view name )
    {
        for ( const BindingKindInfo& info : kArrBindingKindInfo )
        {
            // string_view 오버로드를 쓴다. `name.data()` 는 널 종료가 보장되지 않는다.
            if ( StringUtil::equals( name, string_view{ info._pXMLName } ) )
                return info._kind;
        }
        return BindingKind::Count;
    }

    uint32 BindingKinds::getRebindSlotIndex( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return ( pInfo != nullptr ) ? pInfo->_rebindSlotIndex : kNoRebindSlot;
    }

    uint32 BindingKinds::getConflictSlotCount( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return ( pInfo != nullptr ) ? pInfo->_conflictSlotCount : 0;
    }

    bool BindingKinds::isRelative( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return pInfo != nullptr && pInfo->_bRelative;
    }

    bool BindingKinds::isRelative( const ActionBinding& binding )
    {
        if ( isRelative( binding._kind ) )
            return true;
        return binding._kind == BindingKind::GamepadStick2D && ( binding._bScaleByDelta || binding._axisScale._x != 1.0f || binding._axisScale._y != 1.0f );
    }

    InputMap::InputMap()
        : _pInput{ nullptr }
        , _mapAction{}
        , _mapLayer{}
        , _listActionEntry{}
        , _listLayerEntry{}
        , _listActionName{}
        , _listLayerName{}
        , _listLayerStack{}
        , _actionBuffer{}
        , _nextGeneration{ 1 }
        , _defaultLayerName{ InputMapDefaults::kDefaultLayerName }
        , _accessibility{}
        , _doubleClickTime{ InputMapDefaults::kDoubleClickTime }
        , _doubleClickMaxDistance{ InputMapDefaults::kDoubleClickMaxDistance }
        , _dragThreshold{ InputMapDefaults::kDragThreshold }
        , _doubleTapTime{ InputMapDefaults::kDoubleTapTime }
        , _holdThreshold{ InputMapDefaults::kHoldThreshold }
        , _navRepeatDelay{ 0.4f }
        , _navRepeatRate{ 0.08f }
        , _totalElapsedTime{ 0.0f }
        , _lastDeltaSeconds{ 0.0f }
        , _virtualCursorStick{ InputMapVirtualCursor::Off }
        , _bSuppressBaseActionOnChord{ SW_TRUE }
        , _bKeyboardFocusIgnored{ SW_FALSE }
        , _bReadsToolKeyboard{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    void InputMap::clear()
    {
        _mapAction.clear();
        _mapLayer.clear();
        _listActionEntry.clear();
        _listLayerEntry.clear();
        _listActionName.clear();
        _listLayerName.clear();
        _listLayerStack.clear();
        _actionBuffer.clear();
        _defaultLayerName       = hashed_string( InputMapDefaults::kDefaultLayerName );
        _doubleClickTime        = InputMapDefaults::kDoubleClickTime;
        _doubleClickMaxDistance = InputMapDefaults::kDoubleClickMaxDistance;
        _dragThreshold          = InputMapDefaults::kDragThreshold;
        _holdThreshold          = InputMapDefaults::kHoldThreshold;
        _totalElapsedTime       = 0.0f;
        _lastDeltaSeconds       = 0.0f;
        _virtualCursorStick     = InputMapVirtualCursor::Off;
    }

    void InputMap::createAction( const hashed_string& action, InputActionValueType valueType )
    {
        if ( action.empty() )
            return;
        getOrCreateAction( hashed_string( action ), valueType );
    }

    ActionBinding InputMap::beginBinding( const hashed_string& action, BindingKind kind, ActionTrigger trigger, const hashed_string& layer )
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

    void InputMap::commitBinding( const hashed_string& action, InputActionValueType valueType, const ActionBinding& binding )
    {
        ActionEntry& entry = getOrCreateAction( action, valueType );
        entry._listBinding.push_back( binding );
        entry._listDefaultBinding.push_back( binding );
        entry._listBindingState.push_back( makeBindingState( binding ) );
    }

    void InputMap::bind( const hashed_string& action, InputSlot slot, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::SingleSlot, trigger, layer );
        binding._arrSlot[0]   = slot;
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void InputMap::bind( const hashed_string& action, Key key, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( key == Key::Unknown )
            return;
        bind( action, InputSlot::fromKey( key ), trigger, layer );
    }

    void InputMap::bind( const hashed_string& action, GamepadButton button, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( button == GamepadButton::Count )
            return;
        bind( action, InputSlot::fromGamepadButton( button ), trigger, layer );
    }

    void InputMap::bind( const hashed_string& action, MouseButton mouse, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( mouse == MouseButton::Count )
            return;
        bind( action, InputSlot::fromMouseButton( mouse ), trigger, layer );
    }

    void InputMap::bindAxis1DComposite( const hashed_string& action, Key negativeKey, Key positiveKey, const hashed_string& layer, ActionTrigger trigger )
    {
        bindAxis1DComposite( action, InputSlot::fromKey( negativeKey ), InputSlot::fromKey( positiveKey ), layer, trigger );
    }

    void InputMap::bindAxis1DComposite( const hashed_string& action, InputSlot negativeSlot, InputSlot positiveSlot, const hashed_string& layer, ActionTrigger trigger )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Axis1DComposite, trigger, layer );
        binding._arrSlot[0]   = negativeSlot;
        binding._arrSlot[1]   = positiveSlot;
        commitBinding( action, InputActionValueType::Axis1D, binding );
    }

    float32 InputMap::getAxis1D( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0.0f;
        float32 bounded  = 0.0f;
        float32 relative = 0.0f;
        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            float2 bVal{ 0.0f, 0.0f };
            if ( isBindingLayerActive( binding ) == false || evaluateBindingDown( binding, bVal ) == false )
                continue;
            float32& accumulator = BindingKinds::isRelative( binding ) ? relative : bounded;
            accumulator += binding._bNegate ? -bVal._x : bVal._x; // 프레임 평가(InputMapEvaluate)와 같은 부호 뒤집기
        }
        if ( _accessibility._bInvertX )
            bounded = -bounded;
        return MathUtil::clamp( bounded, -1.0f, 1.0f ) + relative;
    }

    void InputMap::bindVector2D( const hashed_string& action, Key up, Key down, Key left, Key right, float32 deadzone, const hashed_string& layer )
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

    void InputMap::bindGamepadStick2D( const hashed_string& action, GamepadStick stick, float32 deadzone, const hashed_string& layer, uint8 padIndex, float32 outerDeadzone, float32 responseExponent,
                                       float2 axisScale, bool bScaleByDelta, GamepadStickAxis axis )
    {
        if ( action.empty() )
            return;

        ActionBinding binding     = beginBinding( action, BindingKind::GamepadStick2D, ActionTrigger::Down, layer );
        binding._deviceIndex      = padIndex;
        binding._stick            = stick;
        binding._stickAxis        = axis;
        binding._deadzone         = deadzone;
        binding._outerDeadzone    = outerDeadzone;
        binding._responseExponent = responseExponent;
        binding._axisScale        = axisScale;
        binding._bScaleByDelta    = bScaleByDelta;
        commitBinding( action, axis == GamepadStickAxis::Both ? InputActionValueType::Axis2D : InputActionValueType::Axis1D, binding );
    }

    float2 InputMap::getVector2D( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return float2{ 0.0f, 0.0f };

        float2 bounded{ 0.0f, 0.0f };
        float2 relative{ 0.0f, 0.0f };
        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            float2 bindingValue{ 0.0f, 0.0f };
            if ( isBindingLayerActive( binding ) && evaluateBindingDown( binding, bindingValue ) )
            {
                float2& accumulator = BindingKinds::isRelative( binding ) ? relative : bounded;
                accumulator._x += binding._bNegate ? -bindingValue._x : bindingValue._x; // 프레임 평가(InputMapEvaluate)와 같은 부호 뒤집기
                accumulator._y += binding._bNegate ? -bindingValue._y : bindingValue._y;
            }
        }
        return composeActionValue( bounded, relative );
    }

    void InputMap::bindMouseDelta( const hashed_string& action, float32 sensitivity, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::MouseDelta2D, ActionTrigger::Down, layer );
        binding._scale        = sensitivity;
        commitBinding( action, InputActionValueType::Axis2D, binding );
    }

    void InputMap::bindMouseWheel( const hashed_string& action, float32 scale, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::MouseWheel1D, ActionTrigger::Down, layer );
        binding._scale        = scale;
        commitBinding( action, InputActionValueType::Axis1D, binding );
    }

    void InputMap::bindVirtualJoystick2D( const hashed_string& action, MouseButton activationButton, float32 radius, float32 deadzone, const hashed_string& layer, float32 outerDeadzone )
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

    void InputMap::bindShortcut( const hashed_string& action, Key key, uint8 modifierMask, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( action.empty() || key == Key::Unknown )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Shortcut, trigger, layer );
        binding._modifierMask = modifierMask;
        binding._arrSlot[0]   = InputSlot::fromKey( key );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void InputMap::bindAnyKey( const hashed_string& action, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::AnyKey, ActionTrigger::Pressed, layer );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void InputMap::bindChord( const hashed_string& action, Key modifierKey, Key triggerKey, ActionTrigger trigger, const hashed_string& layer )
    {
        const bool bInvalidChordArguments = action.empty() || modifierKey == Key::Unknown || triggerKey == Key::Unknown;
        if ( bInvalidChordArguments )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Chord, trigger, layer );
        binding._arrSlot[0]   = InputSlot::fromKey( modifierKey );
        binding._arrSlot[1]   = InputSlot::fromKey( triggerKey );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    bool InputMap::isChordDown( const hashed_string& action ) const
    {
        return hasActiveChord( action, false );
    }

    bool InputMap::wasChordTriggered( const hashed_string& action ) const
    {
        return hasActiveChord( action, true );
    }

    bool InputMap::hasActiveChord( const hashed_string& action, bool bTriggerJustPressed ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || _pInput == nullptr )
            return false;

        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            if ( binding._kind == BindingKind::Chord && isBindingLayerActive( binding ) )
            {
                const Key  modifierKey = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                const Key  triggerKey  = static_cast<Key>( binding._arrSlot[1]._controlIndex );
                const bool bTriggerOk  = bTriggerJustPressed ? wasKeyboardKeyPressed( triggerKey ) : isKeyboardKeyDown( triggerKey );
                if ( isKeyboardKeyDown( modifierKey ) && bTriggerOk )
                    return true;
            }
        }
        return false;
    }

    Subscription InputMap::bindActionCallback( const hashed_string& action, ActionCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return {};
        return getOrCreateAction( action )._onTriggered.add( callback );
    }

    Subscription InputMap::bindPhaseCallback( const hashed_string& action, ActionPhase phase, ActionCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return {};
        return getOrCreateAction( action )._onPhase.add( PhaseCallbackDelegate::create( [phase, callback]( ActionPhase currentPhase )
        {
            if ( currentPhase == phase )
                callback();
        } ) );
    }

    Subscription InputMap::bindVector2DCallback( const hashed_string& action, Vector2DCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return {};
        return getOrCreateAction( action, InputActionValueType::Axis2D )._onVector2D.add( callback );
    }

    void InputMap::registerLayer( const hashed_string& name, int32 priority, bool enabled, bool blockLower, bool alwaysOn )
    {
        ensureLayer( name, priority, enabled, blockLower, alwaysOn );
    }

    void InputMap::setLayerEnabled( const hashed_string& layer, bool enabled )
    {
        LayerDefinition* pLayer = findLayer( layer );
        if ( pLayer != nullptr )
            pLayer->_bEnabled = enabled ? SW_TRUE : SW_FALSE;
    }

    void InputMap::pushLayer( const hashed_string& layer, bool blockLower, bool showCursor )
    {
        const hashed_string hLayer( layer );
        ensureLayer( hLayer, 0, true, blockLower );
        LayerDefinition* pLayer = findLayer( hLayer );
        if ( pLayer != nullptr )
        {
            pLayer->_bEnabled    = SW_TRUE;
            pLayer->_bBlockLower = blockLower ? SW_TRUE : SW_FALSE;
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

    void InputMap::popLayer()
    {
        if ( _listLayerStack.empty() == false )
            _listLayerStack.pop_back();
    }

    void InputMap::popLayer( const hashed_string& layer )
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

    string_view InputMap::getCurrentTopLayer() const
    {
        if ( _listLayerStack.empty() == false )
            return _listLayerStack.back().view();
        return _defaultLayerName.view();
    }

    void InputMap::enableOnlyLayer( const hashed_string& layer )
    {
        const hashed_string hLayer( layer );
        for ( auto& [name, index] : _mapLayer )
        {
            LayerDefinition& def = _listLayerEntry[index];
            if ( def._bAlwaysOn == SW_TRUE )
                continue;
            def._bEnabled = ( name == hLayer ) ? SW_TRUE : SW_FALSE;
        }
        _listLayerStack.clear();
        _listLayerStack.push_back( hLayer );
    }

    bool InputMap::isActionToggled( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bToggleState == SW_TRUE;
    }

    bool InputMap::rebindKey( const hashed_string& action, Key newKey, uint32 bindIndex )
    {
        return rebindSlot( action, InputSlot::fromKey( newKey ), bindIndex );
    }

    bool InputMap::rebindSlot( const hashed_string& action, InputSlot slot, uint32 bindIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;

        // 종류는 그대로 두고 그 종류에서 키가 들어가는 슬롯만 바꾼다(`rebindKey` 설명 참고). 단일 키로 바꾸면 수식 키와 축이 사라진다.
        ActionBinding& binding   = pEntry->_listBinding[bindIndex];
        const uint32   slotIndex = BindingKinds::getRebindSlotIndex( binding._kind );
        if ( slotIndex == BindingKinds::kNoRebindSlot )
        {
            SW_LOG_WARNING( "rebind: binding %# of '%#' is a '%#' binding - one key cannot replace it (bind it again, or edit its parts)", bindIndex,
                            action.c_str(), BindingKinds::toName( binding._kind ) );
            return false;
        }
        // 맵 읽기와 같은 검사 — 가상 커서가 마우스로 바꾸는 스틱 · 버튼으로는 다시 묶지 않는다(사용자 설정의 키 바인딩도 이 길이다).
        ActionBinding candidate       = binding;
        candidate._arrSlot[slotIndex] = slot;
        if ( reportVirtualCursorConflict( "rebind", action, candidate ) )
            return false;
        binding = candidate;
        reseedBindingState( *pEntry, bindIndex );
        return true;
    }

    bool InputMap::rebindKeyShortcut( const hashed_string& action, uint32 bindIndex, Key key, uint8 modifierMask )
    {
        ActionEntry* pEntry      = findAction( action );
        const bool   bValidChord = key != Key::Unknown && ( modifierMask & ~ModifierKey::All ) == 0;
        const bool   bNoBinding  = pEntry == nullptr || bindIndex >= pEntry->_listBinding.size();
        if ( bNoBinding || bValidChord == false )
            return false;
        ActionBinding& binding        = pEntry->_listBinding[bindIndex];
        const bool     bKeyboardSlot  = binding._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard;
        const bool     bKeyOrShortcut = binding._kind == BindingKind::SingleSlot || binding._kind == BindingKind::Shortcut;
        if ( bKeyboardSlot == false || bKeyOrShortcut == false )
        {
            SW_LOG_WARNING( "rebind: binding %# of '%#' is not a keyboard key or shortcut - a key with modifiers cannot replace it", bindIndex, action.c_str() );
            return false;
        }
        // 수정 키가 없으면 단일 키, 있으면 단축키다 — 읽기(`<bind modifier>`)와 같은 규칙이라 저장 · 다시 읽기에서 종류가 바뀌지 않는다.
        binding._kind         = modifierMask == ModifierKey::None ? BindingKind::SingleSlot : BindingKind::Shortcut;
        binding._modifierMask = modifierMask;
        binding._arrSlot[0]   = InputSlot::fromKey( key );
        reseedBindingState( *pEntry, bindIndex );
        return true;
    }

    bool InputMap::findSameLayerInputConflict( const hashed_string& action, uint32 bindIndex, hashed_string& outOtherAction ) const
    {
        const ActionBinding* pBinding = getBinding( action, bindIndex );
        if ( pBinding == nullptr )
            return false;
        const uint32 slotCount = BindingKinds::getConflictSlotCount( pBinding->_kind );
        if ( slotCount == 0 )
            return false;
        for ( const hashed_string& otherName : _listActionName )
        {
            const ActionEntry* pOther = otherName == action ? nullptr : findAction( otherName );
            if ( pOther == nullptr )
                continue;
            for ( const ActionBinding& other : pOther->_listBinding )
            {
                bool bSameInput = other._kind == pBinding->_kind && other._layer == pBinding->_layer && other._trigger == pBinding->_trigger &&
                                  other._modifierMask == pBinding->_modifierMask;
                for ( uint32 slotIndex = 0; bSameInput && slotIndex < slotCount; ++slotIndex )
                {
                    bSameInput = other._arrSlot[slotIndex] == pBinding->_arrSlot[slotIndex];
                }
                if ( bSameInput )
                {
                    outOtherAction = otherName;
                    return true;
                }
            }
        }
        return false;
    }

    const ActionBinding* InputMap::getDefaultBinding( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listDefaultBinding.size() )
            return nullptr;
        return &pEntry->_listDefaultBinding[bindIndex];
    }

    bool InputMap::rebindWithResolution( const hashed_string& action, InputSlot newSlot, ConflictResolution strategy, uint32 bindIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;

        // 대상의 어느 슬롯을 바꾸는지는 `rebindSlot` 과 같은 규칙이다(Chord 는 방아쇠, 키 하나로 못 바꾸는 종류는 거절). 늘 0 번에 쓰면
        // Chord 의 수식 키를 덮어쓰고, 겹침도 다른 액션의 0 번 슬롯만 보면 Chord 의 방아쇠나 축의 양의 키와 겹쳐도 알 수 없다.
        const uint32 targetSlot = BindingKinds::getRebindSlotIndex( pEntry->_listBinding[bindIndex]._kind );
        if ( targetSlot == BindingKinds::kNoRebindSlot )
        {
            SW_LOG_WARNING( "rebind: binding %# of '%#' is a '%#' binding - one key cannot replace it (bind it again, or edit its parts)", bindIndex,
                            action.c_str(), BindingKinds::toName( pEntry->_listBinding[bindIndex]._kind ) );
            return false;
        }
        ActionBinding candidate        = pEntry->_listBinding[bindIndex];
        candidate._arrSlot[targetSlot] = newSlot;
        if ( reportVirtualCursorConflict( "rebind", action, candidate ) )
            return false;
        const hashed_string targetLayer = pEntry->_listBinding[bindIndex]._layer;

        hashed_string conflictingAction{};
        uint32        conflictingBindIndex = 0;
        uint32        conflictingSlot      = BindingKinds::kNoRebindSlot;
        for ( auto& [actName, actIndex] : _mapAction )
        {
            if ( actName == action )
                continue;
            const ActionEntry& entry = _listActionEntry[actIndex];
            for ( uint32 bIdx = 0; bIdx < entry._listBinding.size(); ++bIdx )
            {
                if ( entry._listBinding[bIdx]._layer != targetLayer )
                    continue;
                conflictingSlot = findConflictingSlot( entry._listBinding[bIdx], newSlot );
                if ( conflictingSlot != BindingKinds::kNoRebindSlot )
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
                const InputSlot oldSlot                                                      = pEntry->_listBinding[bindIndex]._arrSlot[targetSlot];
                pConflictEntry->_listBinding[conflictingBindIndex]._arrSlot[conflictingSlot] = oldSlot;
                pEntry->_listBinding[bindIndex]._arrSlot[targetSlot]                         = newSlot;
                reseedBindingState( *pEntry, bindIndex );
                reseedBindingState( *pConflictEntry, conflictingBindIndex );
                return true;
            }
            else if ( strategy == ConflictResolution::Override && pConflictEntry != nullptr )
            {
                pConflictEntry->_listBinding[conflictingBindIndex]._arrSlot[conflictingSlot] = InputSlot{};
                pEntry->_listBinding[bindIndex]._arrSlot[targetSlot]                         = newSlot;
                reseedBindingState( *pEntry, bindIndex );
                reseedBindingState( *pConflictEntry, conflictingBindIndex );
                return true;
            }
            else if ( strategy == ConflictResolution::AddSecondary )
            {
                ActionBinding newBinding        = pEntry->_listBinding[bindIndex];
                newBinding._arrSlot[targetSlot] = newSlot;
                pEntry->_listBinding.push_back( newBinding );
                pEntry->_listBindingState.push_back( makeBindingState( newBinding ) );
                return true;
            }
        }

        pEntry->_listBinding[bindIndex]._arrSlot[targetSlot] = newSlot;
        reseedBindingState( *pEntry, bindIndex );
        return true;
    }

    bool InputMap::findRebindSlot( const hashed_string& action, uint32 bindIndex, InputSlot& outSlot ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        const ActionBinding& binding   = pEntry->_listBinding[bindIndex];
        const uint32         slotIndex = BindingKinds::getRebindSlotIndex( binding._kind );
        if ( slotIndex == BindingKinds::kNoRebindSlot )
            return false;
        outSlot = binding._arrSlot[slotIndex];
        return true;
    }

    bool InputMap::findDefaultRebindSlot( const hashed_string& action, uint32 bindIndex, InputSlot& outSlot ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listDefaultBinding.size() )
            return false;
        const ActionBinding& binding   = pEntry->_listDefaultBinding[bindIndex];
        const uint32         slotIndex = BindingKinds::getRebindSlotIndex( binding._kind );
        if ( slotIndex == BindingKinds::kNoRebindSlot )
            return false;
        outSlot = binding._arrSlot[slotIndex];
        return true;
    }

    hashed_string InputMap::findBindingLayer( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return {};
        return pEntry->_listBinding[bindIndex]._layer;
    }

    bool InputMap::hasBindingConflict( const InputSlot& slot, const hashed_string& layer, string& outConflictingAction ) const
    {
        const hashed_string targetLayer = layer.empty() ? _defaultLayerName : hashed_string( layer );

        for ( const auto& [actionName, actIndex] : _mapAction )
        {
            const ActionEntry& entry = _listActionEntry[actIndex];
            for ( const ActionBinding& binding : entry._listBinding )
            {
                if ( binding._layer != targetLayer )
                    continue;

                // 슬롯 수는 테이블에서 가져온다. switch 의 `default:` 로 두면 새 종류를 추가했을 때 충돌 검사가 그 바인딩을
                // 보지 못한 채 지나가고, 같은 키를 두 번 바인딩해도 조용하다.
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

    bool InputMap::resetActionToDefault( const hashed_string& action )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || pEntry->_listDefaultBinding.empty() )
            return false;

        pEntry->_listBinding = pEntry->_listDefaultBinding;
        rebuildBindingStates( *pEntry );
        return true;
    }

    void InputMap::resetAllBindingsToDefault()
    {
        for ( ActionEntry& entry : _listActionEntry )
        {
            if ( entry._listDefaultBinding.empty() == false )
            {
                entry._listBinding = entry._listDefaultBinding;
                rebuildBindingStates( entry );
            }
        }
    }

    bool InputMap::setDoubleClickTime( float32 seconds )
    {
        const bool bValid = seconds >= InputMapDefaults::kDoubleClickTimeMin && seconds <= InputMapDefaults::kDoubleClickTimeMax;
        if ( bValid == false )
        {
            SW_LOG_ERROR( "InputMap: refused double click time %# - it must be within [%#, %#]", seconds, InputMapDefaults::kDoubleClickTimeMin, InputMapDefaults::kDoubleClickTimeMax );
            return false;
        }
        _doubleClickTime = seconds;
        return true;
    }

    bool InputMap::setDoubleClickMaxDistance( float32 pixels )
    {
        const bool bValid = pixels >= 0.0f && pixels <= InputMapDefaults::kDoubleClickDistanceMax;
        if ( bValid == false )
        {
            SW_LOG_ERROR( "InputMap: refused double click distance %# - it must be within [0, %#]", pixels, InputMapDefaults::kDoubleClickDistanceMax );
            return false;
        }
        _doubleClickMaxDistance = pixels;
        return true;
    }

    bool InputMap::setDragThreshold( float32 pixels )
    {
        const bool bValid = pixels >= 0.0f && pixels <= InputMapDefaults::kDragThresholdMax;
        if ( bValid == false )
        {
            SW_LOG_ERROR( "InputMap: refused drag threshold %# - it must be within [0, %#]", pixels, InputMapDefaults::kDragThresholdMax );
            return false;
        }
        _dragThreshold = pixels;
        return true;
    }

    bool InputMap::setHoldThreshold( float32 seconds )
    {
        const bool bValid = seconds >= InputMapDefaults::kHoldThresholdMin && seconds <= InputMapDefaults::kHoldThresholdMax;
        if ( bValid == false )
        {
            SW_LOG_ERROR( "InputMap: refused hold threshold %# - it must be within [%#, %#]", seconds, InputMapDefaults::kHoldThresholdMin, InputMapDefaults::kHoldThresholdMax );
            return false;
        }
        _holdThreshold = seconds;
        return true;
    }

    bool InputMap::hasLayer( const hashed_string& layer ) const
    {
        return _mapLayer.find( layer ) != _mapLayer.end();
    }

    bool InputMap::isLayerEnabled( const hashed_string& layer ) const
    {
        return isLayerActiveInternal( layer );
    }

    int32 InputMap::getLayerPriority( const hashed_string& layer ) const
    {
        const LayerDefinition* pLayer = findLayer( layer );
        return pLayer != nullptr ? pLayer->_priority : 0;
    }

    bool InputMap::hasAction( const hashed_string& action ) const
    {
        return _mapAction.find( action ) != _mapAction.end();
    }

    ActionTrigger InputMap::getBindingTrigger( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return ActionTrigger::Pressed;
        return pEntry->_listBinding[bindIndex]._trigger;
    }

    bool InputMap::setBindingTrigger( const hashed_string& action, uint32 bindIndex, ActionTrigger trigger )
    {
        ActionEntry* pEntry                = findAction( action );
        const bool   bInvalidTriggerRebind = pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() || trigger >= ActionTrigger::Count;
        if ( bInvalidTriggerRebind )
            return false;
        pEntry->_listBinding[bindIndex]._trigger = trigger;
        return true;
    }

    bool InputMap::setBindingTriggerTime( const hashed_string& action, uint32 bindIndex, float32 seconds )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        const float32 triggerTime                    = ( seconds > 0.0f ) ? seconds : 0.0f;
        pEntry->_listBinding[bindIndex]._triggerTime = triggerTime;
        if ( bindIndex < pEntry->_listDefaultBinding.size() )
            pEntry->_listDefaultBinding[bindIndex]._triggerTime = triggerTime;
        return true;
    }

    bool InputMap::setBindingNegate( const hashed_string& action, uint32 bindIndex, bool bNegate )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        pEntry->_listBinding[bindIndex]._bNegate = bNegate;
        if ( bindIndex < pEntry->_listDefaultBinding.size() )
            pEntry->_listDefaultBinding[bindIndex]._bNegate = bNegate;
        return true;
    }

    bool InputMap::setBindingTapCount( const hashed_string& action, uint32 bindIndex, uint8 tapCount )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        const uint8 clampedCount                  = ( tapCount < 2 ) ? static_cast<uint8>( 2 ) : tapCount;
        pEntry->_listBinding[bindIndex]._tapCount = clampedCount;
        if ( bindIndex < pEntry->_listDefaultBinding.size() )
            pEntry->_listDefaultBinding[bindIndex]._tapCount = clampedCount;
        return true;
    }

    uint32 InputMap::getRequiredTapCount( const ActionBinding& binding )
    {
        if ( binding._trigger == ActionTrigger::MultiTap )
            return ( binding._tapCount < 2 ) ? 2u : static_cast<uint32>( binding._tapCount );
        if ( binding._trigger == ActionTrigger::DoubleTap || binding._trigger == ActionTrigger::DoubleClicked )
            return 2u;
        return 0u;
    }

    float32 InputMap::getEffectiveTriggerTime( const ActionBinding& binding ) const
    {
        float32 mapTime{ 0.0f };
        switch ( binding._trigger )
        {
            case ActionTrigger::HoldThreshold:
            case ActionTrigger::HoldAndRelease:
            {
                mapTime = _holdThreshold;
                break;
            }
            case ActionTrigger::Tap:
            {
                mapTime = InputMapDefaults::kTapMaxTime;
                break;
            }
            case ActionTrigger::Pulse:
            {
                mapTime = InputMapDefaults::kPulseInterval;
                break;
            }
            case ActionTrigger::DoubleClicked:
            case ActionTrigger::DoubleTap:
            case ActionTrigger::MultiTap:
            {
                mapTime = _doubleClickTime;
                break;
            }
            case ActionTrigger::Pressed:
            case ActionTrigger::Released:
            case ActionTrigger::Down:
            case ActionTrigger::Repeat: // 지연 · 간격 두 값이라 맵 값(`setNavRepeatTiming`)만 쓴다
            case ActionTrigger::Click:  // 시간이 아니라 거리(`_dragThreshold`)를 본다
            case ActionTrigger::Drag:
            case ActionTrigger::Count:
            {
                return 0.0f;
            }
        }
        return ( binding._triggerTime > 0.0f ) ? binding._triggerTime : mapTime;
    }

    uint32 InputMap::getBindingCount( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? static_cast<uint32>( pEntry->_listBinding.size() ) : 0;
    }

    const ActionBinding* InputMap::getBinding( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return nullptr;
        return &pEntry->_listBinding[bindIndex];
    }

    bool InputMap::wasActionTriggered( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
    }

    bool InputMap::isActionDown( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
    }

    ActionHandle InputMap::getActionHandle( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry != nullptr && pEntry->_handleIndex != ActionHandle::kInvalidIndex )
            return ActionHandle{ pEntry->_handleIndex, pEntry->_generation };
        return ActionHandle{};
    }

    const InputMap::ActionEntry* InputMap::getActionFromHandle( ActionHandle handle ) const
    {
        if ( handle._index < _listActionEntry.size() )
        {
            const ActionEntry& entry = _listActionEntry[handle._index];
            if ( entry._generation == handle._generation )
                return &entry;
        }
        return nullptr;
    }

    bool InputMap::wasActionTriggered( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
    }

    bool InputMap::isActionDown( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
    }

    bool InputMap::wasActionPressed( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bPressed == SW_TRUE;
    }

    bool InputMap::wasActionPressed( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bPressed == SW_TRUE;
    }

    bool InputMap::tryGetActionPressInstant( ActionHandle handle, InputPressInstant& outInstant ) const
    {
        const ActionEntry* pEntry      = getActionFromHandle( handle );
        const bool         bNotPressed = pEntry == nullptr || pEntry->_bPressed == SW_FALSE || _pInput == nullptr;
        if ( bNotPressed )
            return false;
        bool bFound = false;
        for ( size_t bindingIndex = 0; bindingIndex < pEntry->_listBinding.size(); ++bindingIndex )
        {
            if ( pEntry->_listBindingState[bindingIndex]._bPressed == SW_FALSE )
                continue;
            const ActionBinding& binding = pEntry->_listBinding[bindingIndex];
            // 바인딩이 완성된 순간 — 단일 슬롯은 그 슬롯, 조합 키(수정자 + 트리거)·단축키는 이번 프레임에 눌린 슬롯 중 늦은 것이다.
            int32 slotCount = 0;
            if ( binding._kind == BindingKind::SingleSlot || binding._kind == BindingKind::Shortcut )
                slotCount = 1;
            else if ( binding._kind == BindingKind::Chord )
                slotCount = 2;
            bool              bBindingFound = false;
            InputPressInstant bindingInstant{};
            for ( int32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
            {
                InputPressInstant slotInstant{};
                if ( _pInput->tryGetPressInstant( binding._arrSlot[slotIndex], slotInstant ) == false )
                    continue;
                if ( bBindingFound == false || slotInstant._frameFraction > bindingInstant._frameFraction )
                    bindingInstant = slotInstant;
                bBindingFound = true;
            }
            if ( bBindingFound == false )
                continue;
            if ( bFound == false || bindingInstant._frameFraction < outInstant._frameFraction )
                outInstant = bindingInstant;
            bFound = true;
        }
        return bFound;
    }

    bool InputMap::wasActionReleased( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bReleased == SW_TRUE;
    }

    bool InputMap::wasActionReleased( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bReleased == SW_TRUE;
    }

    float32 InputMap::getActionHoldDuration( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? pEntry->_holdDuration : 0.0f;
    }

    float32 InputMap::getActionHoldDuration( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_holdDuration : 0.0f;
    }

    float2 InputMap::getVector2D( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_currentValue : float2{ 0.0f, 0.0f };
    }

    ActionPhase InputMap::getActionPhase( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? pEntry->_currentPhase : ActionPhase::None;
    }

    ActionPhase InputMap::getActionPhase( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_currentPhase : ActionPhase::None;
    }

    void InputMap::ensureActionListed( const hashed_string& action )
    {
        for ( const hashed_string& name : _listActionName )
        {
            if ( name == action )
                return;
        }
        _listActionName.push_back( action );
    }

    LayerDefinition& InputMap::ensureLayer( const hashed_string& name, int32 priority, bool enabled, bool blockLower, bool alwaysOn )
    {
        auto it = _mapLayer.find( name );
        if ( it != _mapLayer.end() )
            return _listLayerEntry[it->second];

        LayerDefinition def{};
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

    InputMap::ActionEntry& InputMap::getOrCreateAction( const hashed_string& action, InputActionValueType valueType )
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

    LayerDefinition* InputMap::findLayer( const hashed_string& name )
    {
        auto it = _mapLayer.find( name );
        return it != _mapLayer.end() ? &_listLayerEntry[it->second] : nullptr;
    }

    const LayerDefinition* InputMap::findLayer( const hashed_string& name ) const
    {
        auto it = _mapLayer.find( name );
        return it != _mapLayer.end() ? &_listLayerEntry[it->second] : nullptr;
    }

    InputMap::ActionEntry* InputMap::findAction( const hashed_string& action )
    {
        auto it = _mapAction.find( action );
        return it != _mapAction.end() ? &_listActionEntry[it->second] : nullptr;
    }

    const InputMap::ActionEntry* InputMap::findAction( const hashed_string& action ) const
    {
        auto it = _mapAction.find( action );
        return it != _mapAction.end() ? &_listActionEntry[it->second] : nullptr;
    }

    void InputMap::getDebugActionStates( vector<DebugActionState>& outListState ) const
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

    void InputMap::update( float32 deltaSeconds )
    {
        _totalElapsedTime += deltaSeconds;
        _lastDeltaSeconds = deltaSeconds;

        _actionBuffer.update( deltaSeconds ); // 선입력 만료 · 오래된 커맨드 이력 정리

        if ( _pInput == nullptr )
            return;

        const int2 curMousePos = _pInput->getMousePosition();

        // 액션마다 런타임 평가와 ActionPhase 상태 머신을 돌린다
        for ( auto& [actionName, actIndex] : _mapAction )
        {
            ActionEntry& actionEntry = _listActionEntry[actIndex];

            const size_t bindCount = actionEntry._listBinding.size();
            if ( actionEntry._listBindingState.size() != bindCount )
                rebuildBindingStates( actionEntry );

            ActionFrameAccum accum{};
            for ( size_t bindIndex = 0; bindIndex < bindCount; ++bindIndex )
            {
                updateBinding( actionEntry._listBinding[bindIndex], actionEntry._listBindingState[bindIndex], deltaSeconds, curMousePos, accum );
            }

            const bool bPrevDown      = actionEntry._bDown == SW_TRUE;
            actionEntry._bDown        = accum._bAnyDown ? SW_TRUE : SW_FALSE;
            actionEntry._bPressed     = accum._bAnyPressed ? SW_TRUE : SW_FALSE;
            actionEntry._bReleased    = accum._bAnyReleased ? SW_TRUE : SW_FALSE;
            actionEntry._bTriggered   = accum._bAnyTriggered ? SW_TRUE : SW_FALSE;
            actionEntry._holdDuration = accum._maxHold;

            // 모디파이어를 적용한다(축 반전, 클램핑 또는 원형 정규화 — 마우스 이동량은 빼고).
            actionEntry._currentValue = composeActionValue( accum._totalValue, accum._relativeValue );

            advanceActionPhase( actionEntry, bPrevDown, accum._bAnyHoldReached );

            // 접근성 토글 — 누른 프레임이나 켜진 동안만 목록을 본다. 토글에서 빠진 액션은 꺼진다.
            const bool bToggleCandidate = actionEntry._bPressed == SW_TRUE || actionEntry._bToggleState == SW_TRUE;
            if ( bToggleCandidate && _accessibility.isToggleAction( actionName ) == false )
                actionEntry._bToggleState = SW_FALSE;
            else if ( actionEntry._bPressed == SW_TRUE )
                actionEntry._bToggleState = ( actionEntry._bToggleState == SW_TRUE ) ? SW_FALSE : SW_TRUE;

            // 커맨드 이력 기록
            if ( actionEntry._bTriggered == SW_TRUE )
                _actionBuffer.recordTriggered( actionName );

            dispatchActionCallbacks( actionEntry, bPrevDown );
        }
    }

    void InputMap::updateBinding( const ActionBinding& binding, ActionBindingState& state, float32 deltaSeconds, const int2& mousePos, ActionFrameAccum& accum ) const
    {
        // 꺼진 레이어의 바인딩이나 콘솔이 가린 키도 장치를 읽어 눌림을 추적한다. 다시 보이는 프레임에 이미 눌려 있던 입력을 새 누름으로 읽지 않기 위해서다(아래 `_bWasDown`).
        const bool bLayerActive = isBindingLayerActive( binding );
        float2     deviceValue{ 0.0f, 0.0f };
        float2     bindingValue{ 0.0f, 0.0f };
        const bool bPhysicalDown = evaluateBindingDown( binding, deviceValue, SlotRead::Device );
        const bool bRawDown      = bLayerActive && evaluateBindingDown( binding, bindingValue, SlotRead::Game );
        // 장치는 눌려 있는데 레이어가 켜진 게임이 안 눌림으로 보면 콘솔 · 포커스가 눌린 키를 가린 것이다.
        const bool bLive = bLayerActive && ( bPhysicalDown == false || bRawDown );
        if ( binding._bNegate )
        {
            bindingValue._x = -bindingValue._x;
            bindingValue._y = -bindingValue._y;
        }
        // 바인딩마다 시간(`_triggerTime`)이 있으면 홀드 판정과 더블 판정 창도 그 시간을 쓴다. 다른 트리거는 맵 값 그대로다.
        const bool    bHoldTrigger   = binding._trigger == ActionTrigger::HoldThreshold || binding._trigger == ActionTrigger::HoldAndRelease;
        const uint32  requiredTaps   = getRequiredTapCount( binding );
        const bool    bDoubleTrigger = requiredTaps > 0;
        const float32 holdTime       = bHoldTrigger ? getEffectiveTriggerTime( binding ) : _holdThreshold;
        const float32 doubleWindow   = bDoubleTrigger ? getEffectiveTriggerTime( binding ) : _doubleClickTime;

        state._bWasDown = state._bDown;
        state._bDown    = bRawDown ? SW_TRUE : SW_FALSE;
        // 바인딩이 막 보이게 됐고(레이어가 켜짐, 콘솔이 닫혀 키가 다시 보임, 리바인딩 · 복원 · 새 바인딩으로 상태가 새로 만들어짐) 그 전부터 눌려 있었으면
        // Pressed 가 아니라 Held 로 시작한다(눌린 채 켜진 입력을 새 누름으로 읽으면 일시정지를 연 Esc 가 층 안 Back 이 된다).
        // 눌린 채 보이게 된 프레임에 뗐으면 아무 일도 아니다. 보인 뒤 뗐다 다시 누른 입력은 새 누름이다.
        const bool bJustBecameLive = bLive && state._bLive == SW_FALSE;
        if ( bJustBecameLive && state._bPhysicalDown == SW_TRUE )
            state._bWasDown = state._bDown;
        state._bPhysicalDown = bPhysicalDown ? SW_TRUE : SW_FALSE;
        state._bLive         = bLive ? SW_TRUE : SW_FALSE;

        state._bPressed       = ( state._bDown == SW_TRUE && state._bWasDown == SW_FALSE ) ? SW_TRUE : SW_FALSE;
        state._bReleased      = ( state._bDown == SW_FALSE && state._bWasDown == SW_TRUE ) ? SW_TRUE : SW_FALSE;
        state._bDoubleClicked = SW_FALSE;
        state._bHoldThreshold = SW_FALSE;

        if ( state._timeSinceLastPress < InputMapDefaults::kNeverPressedSentinel * 0.5f )
            state._timeSinceLastPress += deltaSeconds;

        if ( state._bDown == SW_TRUE )
        {
            state._holdDuration += deltaSeconds;
            if ( state._holdDuration >= holdTime )
                state._bHoldThreshold = SW_TRUE;
            state._pulseTimer += deltaSeconds;
        }

        if ( state._bPressed == SW_TRUE )
            advanceTapStreak( binding, state, requiredTaps, doubleWindow, mousePos );
        advanceDrag( binding, state, mousePos );

        const bool bTriggerFired = evaluateTrigger( binding, state, deltaSeconds );
        state._bTriggered        = bTriggerFired ? SW_TRUE : SW_FALSE;
        // 뗀 프레임까지는 `Click` 이 끌렸는지 봐야 하므로 끌기 표시는 판정 뒤에 지운다.
        if ( state._bDown == SW_FALSE )
            state._bDragged = SW_FALSE;

        if ( state._bDown == SW_TRUE )
            accum._bAnyDown = true;
        if ( state._bPressed == SW_TRUE )
            accum._bAnyPressed = true;
        if ( state._bReleased == SW_TRUE )
            accum._bAnyReleased = true;
        if ( state._bTriggered == SW_TRUE )
            accum._bAnyTriggered = true;
        if ( state._holdDuration > accum._maxHold )
            accum._maxHold = state._holdDuration;
        // 뗀 순간 Completed/Canceled 판정은 바인딩마다 시간(`getEffectiveTriggerTime`)으로 본다. 맵 값과 비교하면 짧은 차지가 Canceled 로 나온다.
        if ( state._holdDuration >= holdTime )
            accum._bAnyHoldReached = true;

        if ( state._bDown == SW_FALSE )
        {
            state._holdDuration = 0.0f;
            state._pulseTimer   = 0.0f;
        }

        if ( bRawDown )
        {
            float2& accumulator = BindingKinds::isRelative( binding ) ? accum._relativeValue : accum._totalValue;
            accumulator._x += bindingValue._x;
            accumulator._y += bindingValue._y;
        }
    }

    bool InputMap::isPointerBinding( const ActionBinding& binding )
    {
        return binding._arrSlot[0]._deviceKind == InputDeviceKind::Mouse;
    }

    void InputMap::advanceDrag( const ActionBinding& binding, ActionBindingState& state, const int2& mousePos ) const
    {
        state._bDragStarted = SW_FALSE;
        // 포인터가 없는 슬롯(키 · 패드)은 끌 수 없다 — 누름 위치는 그 누름의 마우스 위치가 아니다.
        const bool bUndraggedPointerHeld = state._bDown == SW_TRUE && state._bDragged == SW_FALSE && isPointerBinding( binding );
        if ( bUndraggedPointerHeld == false )
            return;
        const int2    moved  = mousePos - state._lastPress;
        const float32 distSq = static_cast<float32>( moved._x * moved._x + moved._y * moved._y );
        if ( distSq <= _dragThreshold * _dragThreshold )
            return;
        state._bDragged     = SW_TRUE;
        state._bDragStarted = SW_TRUE;
    }

    void InputMap::advanceTapStreak( const ActionBinding& binding, ActionBindingState& state, uint32 requiredTaps, float32 doubleWindow, const int2& mousePos ) const
    {
        const bool bIsMouseBinding  = isPointerBinding( binding );
        bool       bContinuesStreak = false;

        if ( state._timeSinceLastPress <= doubleWindow )
        {
            if ( bIsMouseBinding )
            {
                const int2    pressDelta = mousePos - state._lastPress;
                const float32 distSq     = static_cast<float32>( pressDelta._x * pressDelta._x + pressDelta._y * pressDelta._y );
                if ( distSq <= ( _doubleClickMaxDistance * _doubleClickMaxDistance ) )
                    bContinuesStreak = true;
            }
            else
            {
                bContinuesStreak = true;
            }
        }

        // 간격 안에 이어진 누름을 센다. 필요한 수(DoubleTap · DoubleClicked 는 2, MultiTap 은 `_tapCount`)에 닿으면
        // 발화하고 처음부터 다시 센다. 2 일 때는 옛 더블 판정과 같다. 다른 트리거도 `_bDoubleClicked` 는 2 연타로 둔다.
        const uint32 streakGoal = ( requiredTaps > 0 ) ? requiredTaps : 2u;
        state._tapStreak        = bContinuesStreak ? static_cast<uint8>( state._tapStreak + 1 ) : static_cast<uint8>( 1 );
        if ( state._tapStreak >= streakGoal )
        {
            state._bDoubleClicked     = SW_TRUE;
            state._timeSinceLastPress = InputMapDefaults::kNeverPressedSentinel;
            state._tapStreak          = 0;
        }
        else
        {
            state._timeSinceLastPress = 0.0f;
        }
        state._lastPress = mousePos;
    }

    void InputMap::advanceActionPhase( ActionEntry& actionEntry, bool bPrevDown, bool bAnyHoldReached )
    {
        if ( actionEntry._bTriggered == SW_TRUE )
        {
            actionEntry._currentPhase = ActionPhase::Triggered;
        }
        else if ( actionEntry._bPressed == SW_TRUE || ( bPrevDown == false && actionEntry._bDown == SW_TRUE ) )
        {
            actionEntry._currentPhase = ActionPhase::Started;
        }
        else if ( actionEntry._bDown == SW_TRUE )
        {
            actionEntry._currentPhase = ActionPhase::Ongoing;
        }
        else if ( actionEntry._bReleased == SW_TRUE )
        {
            actionEntry._currentPhase = ( actionEntry._currentPhase == ActionPhase::Triggered || actionEntry._currentPhase == ActionPhase::Ongoing || bAnyHoldReached )
                                          ? ActionPhase::Completed
                                          : ActionPhase::Canceled;
        }
        else
        {
            actionEntry._currentPhase = ActionPhase::None;
        }
    }

    void InputMap::dispatchActionCallbacks( ActionEntry& actionEntry, bool bPrevDown )
    {
        // 델리게이트 이벤트 디스패치(Triggered)
        if ( actionEntry._bTriggered == SW_TRUE )
            actionEntry._onTriggered.broadcast();

        // 페이즈 델리게이트 디스패치(Phase) — 항목이 자기 단계만 거른다
        if ( actionEntry._currentPhase != ActionPhase::None )
            actionEntry._onPhase.broadcast( actionEntry._currentPhase );

        // 2D 벡터 델리게이트 디스패치
        if ( ( actionEntry._currentValue._x != 0.0f || actionEntry._currentValue._y != 0.0f ) || ( bPrevDown && actionEntry._bDown == SW_FALSE ) )
            actionEntry._onVector2D.broadcast( actionEntry._currentValue );
    }

    float2 InputMap::composeActionValue( const float2& bounded, const float2& relative ) const
    {
        float2 value = bounded;
        if ( _accessibility._bInvertX )
            value._x = -value._x;
        if ( _accessibility._bInvertY )
            value._y = -value._y;
        if ( _accessibility._digitalNormalization == DigitalNormalization::Circular )
        {
            const float32 lenSq = value._x * value._x + value._y * value._y;
            if ( lenSq > 1.0f )
            {
                const float32 invLen = 1.0f / MathUtil::sqrt( lenSq );
                value._x *= invLen;
                value._y *= invLen;
            }
        }
        else
        {
            value._x = MathUtil::clamp( value._x, -1.0f, 1.0f );
            value._y = MathUtil::clamp( value._y, -1.0f, 1.0f );
        }
        return float2{ value._x + relative._x, value._y + relative._y };
    }

    InputMap::ActionBindingState InputMap::makeBindingState( const ActionBinding& binding ) const
    {
        float2             unused{ 0.0f, 0.0f };
        ActionBindingState state{};
        state._bPhysicalDown = evaluateBindingDown( binding, unused, SlotRead::Device ) ? SW_TRUE : SW_FALSE;
        return state;
    }

    void InputMap::reseedBindingState( ActionEntry& entry, uint32 bindIndex ) const
    {
        entry._listBindingState[bindIndex] = makeBindingState( entry._listBinding[bindIndex] );
    }

    void InputMap::rebuildBindingStates( ActionEntry& entry ) const
    {
        entry._listBindingState.clear();
        entry._listBindingState.reserve( entry._listBinding.size() );
        for ( const ActionBinding& binding : entry._listBinding )
        {
            entry._listBindingState.push_back( makeBindingState( binding ) );
        }
    }

    bool InputMap::isKeyboardKeyDown( Key key, SlotRead read ) const
    {
        if ( _pInput == nullptr )
            return false;
        if ( read == SlotRead::Device )
            return _pInput->getKeyboard() != nullptr && _pInput->getKeyboard()->isKeyDown( key );
        if ( _bKeyboardFocusIgnored == SW_TRUE )
            return isToolKeyboardHidden() == false && _pInput->getKeyboard() != nullptr && _pInput->getKeyboard()->isKeyDown( key );
        return _pInput->isKeyDown( key );
    }

    bool InputMap::wasKeyboardKeyPressed( Key key ) const
    {
        if ( _pInput == nullptr )
            return false;
        if ( _bKeyboardFocusIgnored == SW_TRUE )
            return isToolKeyboardHidden() == false && _pInput->getKeyboard() != nullptr && _pInput->getKeyboard()->wasKeyPressed( key );
        return _pInput->wasKeyPressed( key );
    }

    bool InputMap::isToolKeyboardHidden() const
    {
        return _bReadsToolKeyboard == SW_FALSE && _pInput->getKeyboardFocus() == InputKeyboardFocus::Tool;
    }

    bool InputMap::isSlotDown( const InputSlot& slot, SlotRead read ) const
    {
        const IInputDevice* pDevice = _pInput != nullptr ? _pInput->getDevice( slot._deviceKind, slot._deviceIndex ) : nullptr;
        if ( pDevice == nullptr )
            return false;
        if ( read == SlotRead::Game && isSlotHiddenByConsole( slot ) )
            return false;
        return pDevice->isControlDown( slot._controlIndex );
    }

    bool InputMap::wasSlotPressed( const InputSlot& slot, SlotRead read ) const
    {
        const IInputDevice* pDevice = _pInput != nullptr ? _pInput->getDevice( slot._deviceKind, slot._deviceIndex ) : nullptr;
        if ( pDevice == nullptr )
            return false;
        if ( read == SlotRead::Game && isSlotHiddenByConsole( slot ) )
            return false;
        return pDevice->wasControlPressed( slot._controlIndex );
    }

    bool InputMap::isSlotHeldThisFrame( const InputSlot& slot, SlotRead read ) const
    {
        return isSlotDown( slot, read ) || wasSlotPressed( slot, read );
    }

    bool InputMap::isSlotHiddenByConsole( const InputSlot& slot ) const
    {
        // 포커스를 무시하는 맵(셸 · UI · 에디터)도 도구 포커스 동안의 키는 도구 자신의 맵만 본다.
        if ( _bKeyboardFocusIgnored == SW_TRUE )
            return slot._deviceKind == InputDeviceKind::Keyboard && isToolKeyboardHidden();
        // 개발 콘솔이 입력을 쥔 동안 게임 맵은 키보드 바인딩과 패드 바인딩을 모두 "안 눌림" 으로 본다(패드는 디버그 키 맵이 콘솔 조작에 쓴다).
        if ( slot._deviceKind == InputDeviceKind::Keyboard )
            return _pInput->isKeyVisibleToGame( static_cast<Key>( slot._controlIndex ) ) == false;
        if ( slot._deviceKind == InputDeviceKind::Gamepad )
            return _pInput->isGamepadControlVisibleToGame( slot._deviceIndex, slot._controlIndex ) == false;
        return false;
    }

    bool InputMap::isBaseKeySuppressedByModifier( const ActionBinding& binding, SlotRead read ) const
    {
        if ( _bSuppressBaseActionOnChord == SW_FALSE || binding._arrSlot[0]._deviceKind != InputDeviceKind::Keyboard )
            return false;
        const Key  key            = static_cast<Key>( binding._arrSlot[0]._controlIndex );
        const bool bIsModifierKey = ( key == Key::LeftControl || key == Key::RightControl || key == Key::LeftShift || key == Key::RightShift || key == Key::LeftAlt || key == Key::RightAlt || key == Key::LeftSuper || key == Key::RightSuper );
        if ( bIsModifierKey )
            return false;
        const bool bCtrlHeld = isKeyboardKeyDown( Key::LeftControl, read ) || isKeyboardKeyDown( Key::RightControl, read );
        const bool bAltHeld  = isKeyboardKeyDown( Key::LeftAlt, read ) || isKeyboardKeyDown( Key::RightAlt, read );
        return bCtrlHeld || bAltHeld;
    }

    bool InputMap::evaluateGamepadStick( const ActionBinding& binding, float2& outValue ) const
    {
        GamepadDevice* pPad = _pInput->getGamepad( binding._deviceIndex );
        if ( pPad == nullptr || pPad->isConnected() == false )
            return false;
        const float2 stick = ( binding._stick == GamepadStick::Left ) ? pPad->getLeftStick() : pPad->getRightStick();
        // 한 성분만 내는 바인딩은 다른 성분을 읽지 않는다 — 원형 데드존이 두 성분을 함께 정규화하면 다른 성분이 1D 값에 섞인다.
        float32 stickX = binding._stickAxis == GamepadStickAxis::Y ? 0.0f : stick._x;
        float32 stickY = binding._stickAxis == GamepadStickAxis::X ? 0.0f : stick._y;

        const float32 inDeadzone  = _accessibility._stickDeadzoneOverride >= 0.0f ? _accessibility._stickDeadzoneOverride : binding._deadzone;
        const float32 outDeadzone = binding._outerDeadzone > inDeadzone ? binding._outerDeadzone : 1.0f;
        const float32 deadRange   = outDeadzone - inDeadzone;

        if ( _accessibility._deadzoneShape == DeadzoneShape::Radial )
        {
            const float32 mag = MathUtil::sqrt( stickX * stickX + stickY * stickY );
            if ( mag <= inDeadzone )
            {
                stickX = 0.0f;
                stickY = 0.0f;
            }
            else
            {
                const float32 norm = shapeStickMagnitude( mag, inDeadzone, deadRange, binding._responseExponent );
                stickX             = ( stickX / mag ) * norm;
                stickY             = ( stickY / mag ) * norm;
            }
        }
        else // Axial
        {
            stickX = applyAxialDeadzone( stickX, inDeadzone, deadRange, binding._responseExponent );
            stickY = applyAxialDeadzone( stickY, inDeadzone, deadRange, binding._responseExponent );
        }

        stickX *= _accessibility._gamepadSensitivity._x;
        stickY *= _accessibility._gamepadSensitivity._y;
        if ( BindingKinds::isRelative( binding ) )
        {
            // 이동량으로 쓰는 스틱: 배율과 프레임 시간을 곱하고, 반전은 마우스 이동처럼 여기서 한 번만 적용한다(액션 값 단계는 이동량을 반전하지 않는다).
            // 한 성분만 내는 스틱(확대 같은 1D 액션)은 시점 반전을 받지 않는다 — 반전 설정은 2D 시점 이동의 것이다.
            const float32 frameScale = binding._bScaleByDelta ? _lastDeltaSeconds : 1.0f;
            const bool    bLook      = binding._stickAxis == GamepadStickAxis::Both;
            stickX *= binding._axisScale._x * frameScale * ( bLook && _accessibility._bInvertX ? -1.0f : 1.0f );
            stickY *= binding._axisScale._y * frameScale * ( bLook && _accessibility._bInvertY ? -1.0f : 1.0f );
        }
        // 한 성분만 내는 바인딩은 그 성분이 1D 값(x)이다 — getAxis1D 가 x 를 읽는다.
        if ( binding._stickAxis == GamepadStickAxis::X )
        {
            stickY = 0.0f;
        }
        else if ( binding._stickAxis == GamepadStickAxis::Y )
        {
            stickX = stickY;
            stickY = 0.0f;
        }
        outValue = float2{ stickX, stickY };
        return ( stickX != 0.0f || stickY != 0.0f );
    }

    bool InputMap::evaluateVirtualJoystick( const ActionBinding& binding, float2& outValue, SlotRead read ) const
    {
        const bool bActivationDown = isSlotDown( binding._arrSlot[0], read );

        if ( bActivationDown == false )
        {
            binding._bJoystickAnchored = false;
            return false;
        }

        const int2   curTilePos = _pInput->getMousePosition();
        const float2 curPos     = curTilePos.toFloat2();

        // 앵커는 고정 좌표가 아니라 활성화 버튼을 처음 누른 지점에 놓는다(모바일 온스크린 스틱에서 흔한 방식).
        if ( binding._bJoystickAnchored == false )
        {
            binding._joystickAnchor    = curPos;
            binding._bJoystickAnchored = true;
        }

        outValue = VirtualJoystick::computeVector( binding._joystickAnchor, curPos, binding._scale, binding._deadzone, binding._outerDeadzone );
        return ( outValue._x != 0.0f || outValue._y != 0.0f );
    }

    bool InputMap::areShortcutModifiersExact( const ActionBinding& binding, SlotRead read ) const
    {
        struct ModifierFamily
        {
            uint8 _bit;
            Key   _left;
            Key   _right;
        };
        static constexpr ModifierFamily kArrFamily[] = {
            { ModifierKey::Ctrl, Key::LeftControl, Key::RightControl},
            {ModifierKey::Shift,   Key::LeftShift,   Key::RightShift},
            {  ModifierKey::Alt,     Key::LeftAlt,     Key::RightAlt},
            {ModifierKey::Super,   Key::LeftSuper,   Key::RightSuper},
        };
        // 요구한 키만 보면 Ctrl+Shift+Z 가 Ctrl+Z 도 함께 발화시킨다. 눌린 수정 키 집합이 마스크와 같아야 한다.
        // 방아쇠 키 자신이 수정 키(Ctrl+LeftShift)면 그 키는 늘 눌려 있으므로 비교에서 뺀다.
        const bool bKeyboardTrigger = binding._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard;
        const Key  triggerKey       = static_cast<Key>( binding._arrSlot[0]._controlIndex );
        for ( const ModifierFamily& family : kArrFamily )
        {
            if ( bKeyboardTrigger && ( triggerKey == family._left || triggerKey == family._right ) )
                continue;
            const bool bRequired = ( binding._modifierMask & family._bit ) != 0;
            const bool bHeld     = isKeyboardKeyDown( family._left, read ) || isKeyboardKeyDown( family._right, read );
            if ( bRequired != bHeld )
                return false;
        }
        return true;
    }

    // 긴 케이스(스틱 · 가상 조이스틱 · 단축키 수정 키)는 함수로 뺐고, 남은 것은 종류마다 짧은 switch 디스패처라 나누지 않는다.
    bool InputMap::evaluateBindingDown( const ActionBinding& binding, float2& outValue, SlotRead read ) const
    {
        if ( _pInput == nullptr )
            return false;
        // 이동량과 "아무 키" 신호는 눌려 있는 상태가 없어 이어 갈 장치 눌림이 없다. 있는 척하면 다음 신호가 새 누름이 아닌 것으로 읽힌다.
        if ( read == SlotRead::Device && ( binding._kind == BindingKind::MouseDelta2D || binding._kind == BindingKind::MouseWheel1D || binding._kind == BindingKind::AnyKey ) )
            return false;

        switch ( binding._kind )
        {
            case BindingKind::SingleSlot:
            {
                if ( isSlotHeldThisFrame( binding._arrSlot[0], read ) && isBaseKeySuppressedByModifier( binding, read ) == false )
                {
                    outValue = float2{ 1.0f, 0.0f };
                    return true;
                }
                return false;
            }
            case BindingKind::Axis1DComposite:
            {
                float32 axisValue = 0.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[0], read ) )
                    axisValue -= 1.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[1], read ) )
                    axisValue += 1.0f;
                outValue = float2{ axisValue, 0.0f };
                return axisValue != 0.0f;
            }
            case BindingKind::Vector2DComposite:
            {
                float2 kbdVec{ 0.0f, 0.0f };
                if ( isSlotHeldThisFrame( binding._arrSlot[0], read ) )
                    kbdVec._y += 1.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[1], read ) )
                    kbdVec._y -= 1.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[2], read ) )
                    kbdVec._x -= 1.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[3], read ) )
                    kbdVec._x += 1.0f;

                const float32 lenSq = kbdVec._x * kbdVec._x + kbdVec._y * kbdVec._y;
                if ( _accessibility._digitalNormalization == DigitalNormalization::Circular && lenSq > 1.0f )
                {
                    const float32 invLen = 1.0f / MathUtil::sqrt( lenSq );
                    kbdVec._x *= invLen;
                    kbdVec._y *= invLen;
                }
                outValue = kbdVec;
                return ( kbdVec._x != 0.0f || kbdVec._y != 0.0f );
            }
            case BindingKind::GamepadStick2D:
            {
                return evaluateGamepadStick( binding, outValue );
            }
            case BindingKind::Chord:
            {
                if ( isSlotDown( binding._arrSlot[0], read ) && isSlotDown( binding._arrSlot[1], read ) )
                {
                    outValue = float2{ 1.0f, 0.0f };
                    return true;
                }
                return false;
            }
            case BindingKind::MouseDelta2D:
            {
                const float2  movement = _pInput->getMouse() != nullptr ? _pInput->getMouse()->getMovementDelta() : float2{};
                const float32 rdx      = movement._x;
                const float32 rdy      = movement._y;
                outValue._x            = rdx * binding._scale * _accessibility._mouseSensitivity._x * ( _accessibility._bInvertX ? -1.0f : 1.0f );
                outValue._y            = rdy * binding._scale * _accessibility._mouseSensitivity._y * ( _accessibility._bInvertY ? -1.0f : 1.0f );
                return ( rdx != 0.0f || rdy != 0.0f );
            }
            case BindingKind::MouseWheel1D:
            {
                const float32 wheel = _pInput->getMouseWheel();
                outValue            = float2{ wheel * binding._scale, 0.0f };
                return wheel != 0.0f;
            }
            case BindingKind::VirtualJoystick2D:
            {
                return evaluateVirtualJoystick( binding, outValue, read );
            }
            case BindingKind::Shortcut:
            {
                if ( areShortcutModifiersExact( binding, read ) == false )
                    return false;

                if ( isSlotDown( binding._arrSlot[0], read ) )
                {
                    outValue = float2{ 1.0f, 0.0f };
                    return true;
                }
                return false;
            }
            case BindingKind::AnyKey:
            {
                const bool bAny = _pInput->wasAnyInputPressed();
                if ( bAny )
                    outValue = float2{ 1.0f, 0.0f };
                return bAny;
            }
            case BindingKind::Count:
            {
                // 이 저장소는 모든 switch 에 `default:` 를 요구한다(-Wswitch-default). 그래서 컴파일러는
                // 이 switch 에서 빠진 종류를 알려 주지 못한다. 컴파일 시점에 잡히는 것은 테이블에서 빠진 줄뿐이다
                // (`kArrBindingKindInfo` 의 static_assert). 여기까지 왔다면 종류를 추가하고
                // 이 switch 를 빠뜨린 것이고, 그 액션은 영원히 발동하지 않는다.
                // 매 프레임 도는 경로라 로그는 남기지 않고, 개발 빌드에서 단언으로 멈춘다.
                SW_ASSERT( false );
                return false;
            }
        }
    }

    bool InputMap::evaluateTrigger( const ActionBinding& binding, const ActionBindingState& state, float32 deltaSeconds ) const
    {
        switch ( binding._trigger )
        {
            case ActionTrigger::Pressed:
            {
                return state._bPressed == SW_TRUE;
            }
            case ActionTrigger::Released:
            {
                return state._bReleased == SW_TRUE;
            }
            case ActionTrigger::Down:
            {
                return state._bDown == SW_TRUE;
            }
            case ActionTrigger::DoubleClicked:
            case ActionTrigger::DoubleTap:
            case ActionTrigger::MultiTap:
            {
                return state._bDoubleClicked == SW_TRUE;
            }
            case ActionTrigger::HoldThreshold:
            {
                return state._bHoldThreshold == SW_TRUE;
            }
            case ActionTrigger::HoldAndRelease:
            {
                return state._bReleased == SW_TRUE && state._holdDuration >= getEffectiveTriggerTime( binding );
            }
            case ActionTrigger::Tap:
            {
                return state._bReleased == SW_TRUE && state._holdDuration < getEffectiveTriggerTime( binding );
            }
            case ActionTrigger::Pulse:
            {
                // 누른 뒤 간격마다 한 번 발화한다. `타이머 >= 간격` 만 보면 첫 간격이 지난 뒤 매 프레임 발화해서 연사 속도가 프레임률을
                // 따라간다. 타이머는 뗄 때만 0 으로 돌아가므로, 이번 프레임에 간격 경계를 넘었는지로 판정한다(Repeat 과 같은 방식).
                // 한 프레임이 간격보다 길어도 한 번만 발화한다.
                if ( state._bDown == SW_FALSE )
                    return false;
                const float32 pulseInterval = getEffectiveTriggerTime( binding );
                const int32   stepNow       = static_cast<int32>( state._pulseTimer / pulseInterval );
                const int32   stepPrev      = static_cast<int32>( MathUtil::max( state._pulseTimer - deltaSeconds, 0.0f ) / pulseInterval );
                return stepNow > stepPrev;
            }
            case ActionTrigger::Repeat:
            {
                if ( state._bPressed == SW_TRUE )
                    return true;
                if ( state._bDown == SW_TRUE && state._holdDuration >= _navRepeatDelay )
                {
                    const float32 repeatTime     = state._holdDuration - _navRepeatDelay;
                    const float32 prevRepeatTime = ( state._holdDuration - deltaSeconds ) - _navRepeatDelay;
                    if ( prevRepeatTime < 0.0f )
                        return true;
                    const int32 stepNow  = static_cast<int32>( repeatTime / _navRepeatRate );
                    const int32 stepPrev = static_cast<int32>( prevRepeatTime / _navRepeatRate );
                    return stepNow > stepPrev;
                }
                return false;
            }
            case ActionTrigger::Click:
            {
                return state._bReleased == SW_TRUE && state._bDragged == SW_FALSE;
            }
            case ActionTrigger::Drag:
            {
                return state._bDragStarted == SW_TRUE;
            }
            case ActionTrigger::Count:
            {
                return false;
            }
        }
    }

    bool InputMap::isBindingLayerActive( const ActionBinding& binding ) const
    {
        if ( binding._cachedLayerIndex == ActionBinding::kInvalidLayerIndex )
        {
            auto it = _mapLayer.find( binding._layer );
            if ( it != _mapLayer.end() )
                binding._cachedLayerIndex = it->second;
        }

        if ( binding._cachedLayerIndex != ActionBinding::kInvalidLayerIndex && _listLayerStack.empty() )
        {
            if ( _listLayerEntry[binding._cachedLayerIndex]._bEnabled == SW_FALSE )
                return false;
            return true;
        }

        return isLayerActiveInternal( binding._layer );
    }

    bool InputMap::isLayerActiveInternal( const hashed_string& layer ) const
    {
        const LayerDefinition* pLayer = findLayer( layer );
        if ( pLayer == nullptr || pLayer->_bEnabled == SW_FALSE )
            return false;
        if ( pLayer->_bAlwaysOn == SW_TRUE )
            return true;

        if ( _listLayerStack.empty() == false )
        {
            for ( auto it = _listLayerStack.rbegin(); it != _listLayerStack.rend(); ++it )
            {
                if ( *it == layer )
                    return true;
                const LayerDefinition* pTopLayer = findLayer( *it );
                if ( pTopLayer != nullptr && pTopLayer->_bBlockLower == SW_TRUE )
                    return false;
            }
            return false;
        }

        return true;
    }

} // namespace sw

namespace sw
{
    bool InputMapXMLUtil::tryGetPadIndex( XMLNode node, uint8& outPadIndex )
    {
        int32 padIndex{ 0 }; // 적지 않으면 0 번 패드다
        bool  bValid = true;
        readOptional( node, "pad", padIndex, bValid );
        if ( bValid == false )
            return false;
        const bool bInRange = 0 <= padIndex && padIndex < static_cast<int32>( kMaxGamepadSlot );
        if ( bInRange == false )
        {
            SW_LOG_ERROR_ID( "Input.PadIndexOutOfRange", "<%# pad=\"%#\"> is outside 0..%# - binding dropped", node.getName(), padIndex, static_cast<int32>( kMaxGamepadSlot ) - 1 );
            return false;
        }
        outPadIndex = static_cast<uint8>( padIndex );
        return true;
    }

    void InputMapXMLUtil::writeStickModifier( XMLNode node, const ActionBinding& binding )
    {
        if ( binding._axisScale._x != 1.0f )
            node.appendAttribute( "scaleX", binding._axisScale._x );
        if ( binding._axisScale._y != 1.0f )
            node.appendAttribute( "scaleY", binding._axisScale._y );
        if ( binding._bScaleByDelta )
            node.appendAttribute( "scaleByDelta", true );
        if ( binding._stickAxis != GamepadStickAxis::Both )
            node.appendAttribute( "axis", binding._stickAxis == GamepadStickAxis::X ? "X" : "Y" );
    }

    const ActionBinding& InputMapXMLUtil::getDefaultBinding()
    {
        static const ActionBinding s_defaultBinding{};
        return s_defaultBinding;
    }

    bool InputMapXMLUtil::readStickModifier( XMLNode node, float2& outAxisScale, bool& outScaleByDelta, GamepadStickAxis& outAxis, bool& inoutbValid )
    {
        outAxisScale    = getDefaultBinding()._axisScale;
        outScaleByDelta = getDefaultBinding()._bScaleByDelta;
        outAxis         = getDefaultBinding()._stickAxis;
        readOptional( node, "scaleX", outAxisScale._x, inoutbValid );
        readOptional( node, "scaleY", outAxisScale._y, inoutbValid );
        readOptional( node, "scaleByDelta", outScaleByDelta, inoutbValid );

        const utf8* pAxisStr = node.findAttribute( "axis" );
        if ( StringUtil::isNullOrEmpty( pAxisStr ) )
            return true;
        if ( StringUtil::equals( pAxisStr, "X" ) )
        {
            outAxis = GamepadStickAxis::X;
            return true;
        }
        if ( StringUtil::equals( pAxisStr, "Y" ) )
        {
            outAxis = GamepadStickAxis::Y;
            return true;
        }
        SW_LOG_ERROR( "stick axis '%#' is neither 'X' nor 'Y' - binding dropped", pAxisStr );
        inoutbValid = false;
        return false;
    }

    void InputMapXMLUtil::writeBindingModifier( XMLNode node, const ActionBinding& binding )
    {
        if ( binding._triggerTime > 0.0f )
            node.appendAttribute( "time", binding._triggerTime );
        if ( binding._bNegate )
            node.appendAttribute( "negate", true );
        if ( binding._trigger == ActionTrigger::MultiTap && binding._tapCount != 2 )
            node.appendAttribute( "taps", static_cast<int32>( binding._tapCount ) );
    }

    void InputMapXMLUtil::readBindingModifier( InputMap& inputMap, const hashed_string& action, uint32 previousCount, XMLNode node, bool& inoutbValid )
    {
        if ( inputMap.getBindingCount( action ) <= previousCount )
            return;
        float32 triggerTime = getDefaultBinding()._triggerTime;
        bool    bNegate     = getDefaultBinding()._bNegate;
        int32   tapCount    = getDefaultBinding()._tapCount;
        readOptional( node, "time", triggerTime, inoutbValid );
        readOptional( node, "negate", bNegate, inoutbValid );
        readOptional( node, "taps", tapCount, inoutbValid );
        if ( triggerTime > 0.0f )
            (void)inputMap.setBindingTriggerTime( action, previousCount, triggerTime ); // 바로 위에서 바인딩이 있음을 확인했다
        if ( bNegate )
            (void)inputMap.setBindingNegate( action, previousCount, true ); // 바로 위에서 바인딩이 있음을 확인했다
        const bool bTapCountInRange = kMinTapCount <= tapCount && tapCount <= kMaxTapCount;
        if ( bTapCountInRange == false )
        {
            SW_LOG_ERROR( "Action '%#' has taps=\"%#\" outside %#..%#", action.c_str(), tapCount, kMinTapCount, kMaxTapCount );
            inoutbValid = false;
            return;
        }
        if ( tapCount != getDefaultBinding()._tapCount )
            (void)inputMap.setBindingTapCount( action, previousCount, static_cast<uint8>( tapCount ) ); // 바로 위에서 바인딩이 있음을 확인했다
    }

    bool InputMapXMLUtil::tryGetModifierMask( XMLNode node, uint8& outModifierMask )
    {
        int32 modifierMask{ 0 }; // 적지 않으면 수정 키 없음이다
        bool  bValid = true;
        readOptional( node, "modifierMask", modifierMask, bValid );
        if ( bValid == false )
            return false;
        const bool bKnownBits = 0 <= modifierMask && modifierMask <= static_cast<int32>( ModifierKey::All );
        if ( bKnownBits == false )
        {
            SW_LOG_ERROR_ID( "Input.ModifierMaskOutOfRange", "<%# modifierMask=\"%#\"> is outside 0..%# - binding dropped", node.getName(), modifierMask, static_cast<int32>( ModifierKey::All ) );
            return false;
        }
        outModifierMask = static_cast<uint8>( modifierMask );
        return true;
    }

    bool InputMapXMLUtil::tryReadAxisSlots( XMLNode node, const utf8* pNegativeName, const utf8* pPositiveName, InputSlot& outNegative, InputSlot& outPositive )
    {
        using InputMapXML   = InputMapInternal::InputMapXML;
        const utf8* pSource = node.findAttribute( InputMapXML::kAttrSource );
        if ( pSource != nullptr && StringUtil::equals( pSource, InputMapXML::kSourceGamepad ) )
        {
            uint8 padIndex{ 0 };
            if ( tryGetPadIndex( node, padIndex ) == false )
                return false;
            const GamepadButton negativeButton = GamepadButtonUtil::fromName( node.getAttributeText( pNegativeName ) );
            const GamepadButton positiveButton = GamepadButtonUtil::fromName( node.getAttributeText( pPositiveName ) );
            if ( negativeButton == GamepadButton::Count || positiveButton == GamepadButton::Count )
            {
                SW_LOG_WARNING( "axis1d names an unknown gamepad button ('%#' / '%#') - binding dropped", node.getAttributeText( pNegativeName ),
                                node.getAttributeText( pPositiveName ) );
                return false;
            }
            outNegative = InputSlot::fromGamepadButton( negativeButton, padIndex );
            outPositive = InputSlot::fromGamepadButton( positiveButton, padIndex );
            return true;
        }
        if ( pSource != nullptr && StringUtil::equals( pSource, InputMapXML::kSourceKey ) == false )
        {
            SW_LOG_WARNING( "axis1d source '%#' is neither 'key' nor 'gamepad' - binding dropped", pSource );
            return false;
        }
        const Key negativeKey = KeyCodeUtil::fromName( node.getAttributeText( pNegativeName ) );
        const Key positiveKey = KeyCodeUtil::fromName( node.getAttributeText( pPositiveName ) );
        if ( negativeKey == Key::Unknown || positiveKey == Key::Unknown )
        {
            SW_LOG_WARNING( "axis1d names an unknown key ('%#' / '%#') - binding dropped", node.getAttributeText( pNegativeName ),
                            node.getAttributeText( pPositiveName ) );
            return false;
        }
        outNegative = InputSlot::fromKey( negativeKey );
        outPositive = InputSlot::fromKey( positiveKey );
        return true;
    }
} // namespace sw

namespace sw
{
    /**
     * @brief `<action>` 노드 하나를 읽어 InputMap 에 바인딩을 넣습니다. `loadFromAsset` 가 레이어마다, 그리고 최상위에서 부릅니다.
     * @details 바인딩 태그 종류마다 함수 하나다. 읽기 실패는 `_bValid` 에 모아 `loadFromAsset` 가 한 번에 오류로 돌린다.
     */
    struct InputMapActionLoader
    {
        using Internal = InputMapInternal;

        InputMap&     _map;
        XMLNode       _actionNode;
        hashed_string _action;
        hashed_string _layer;          ///< 액션의 레이어. 바인딩이 자기 레이어를 적지 않았을 때 쓴다.
        ActionTrigger _defaultTrigger; ///< 액션의 trigger. 적지 않았으면 `Pressed` 다.
        bool          _bTriggerWritten;
        bool&         _bValid;

        InputMapActionLoader( InputMap& map, XMLNode actionNode, const hashed_string& action, const hashed_string& layer, ActionTrigger defaultTrigger, bool bTriggerWritten, bool& bValid )
            : _map{ map }
            , _actionNode{ actionNode }
            , _action{ action }
            , _layer{ layer }
            , _defaultTrigger{ defaultTrigger }
            , _bTriggerWritten{ bTriggerWritten }
            , _bValid{ bValid }
        {
        }

        /** @brief 노드의 `layer` 속성이 있으면 그 레이어를 만들어 돌려주고, 없으면 액션의 레이어를 돌려줍니다. */
        hashed_string resolveLayer( XMLNode node, const utf8* pAttrName )
        {
            hashed_string layer      = _layer;
            const utf8*   pLayerAttr = node.findAttribute( pAttrName );
            if ( StringUtil::isNullOrEmpty( pLayerAttr ) == false )
            {
                layer = hashed_string( pLayerAttr );
                _map.ensureLayer( layer );
            }
            return layer;
        }

        /** @brief 노드에 알려진 trigger 이름이 있으면 그 값을, 아니면 @p fallback 을 돌려줍니다. */
        static ActionTrigger readTrigger( XMLNode node, const utf8* pAttrName, ActionTrigger fallback )
        {
            const utf8* pTriggerAttr = node.findAttribute( pAttrName );
            if ( pTriggerAttr == nullptr )
                return fallback;
            const ActionTrigger parsed = InputMap::actionTriggerFromName( pTriggerAttr );
            return parsed != ActionTrigger::Count ? parsed : fallback;
        }

        static void load( InputMap& map, XMLNode actionNode, string_view inheritedLayer, bool& bValid )
        {
            const utf8* pActionName = actionNode.findAttribute( Internal::InputMapXML::kAttrName );
            if ( StringUtil::isNullOrEmpty( pActionName ) )
                return;

            // 값 종류를 적은 액션은 바인딩보다 먼저 만든다. 바인딩 없이 이름만 있는 액션(에디터의 "Add Action")도 다시 읽혀야 하기 때문이다.
            const utf8* pValueTypeAttr = actionNode.findAttribute( Internal::InputMapXML::kAttrValueType );
            if ( pValueTypeAttr != nullptr )
            {
                InputActionValueType valueType{ InputActionValueType::Boolean };
                if ( Internal::tryParseValueType( pValueTypeAttr, valueType ) )
                    map.createAction( hashed_string( pActionName ), valueType );
                else
                    SW_LOG_WARNING( "Action '%#' has an unknown valueType '%#' - the bindings decide it", pActionName, pValueTypeAttr );
            }

            hashed_string layer      = inheritedLayer.empty() ? map._defaultLayerName : hashed_string( inheritedLayer );
            const utf8*   pLayerAttr = actionNode.findAttribute( Internal::InputMapXML::kAttrLayer );
            if ( StringUtil::isNullOrEmpty( pLayerAttr ) == false )
                layer = hashed_string( pLayerAttr );
            map.ensureLayer( layer );

            // trigger 를 적지 않으면 단일 키와 조합은 Pressed, 축 합성은 Down 이 된다. 적은 trigger 는 셋 모두에 그대로 적용한다.
            auto        defaultTrigger        = ActionTrigger::Pressed;
            bool        bActionTriggerWritten = false;
            const utf8* pTriggerAttr          = actionNode.findAttribute( Internal::InputMapXML::kAttrTrigger );
            if ( pTriggerAttr != nullptr )
            {
                const ActionTrigger parsed = InputMap::actionTriggerFromName( pTriggerAttr );
                if ( parsed != ActionTrigger::Count )
                {
                    defaultTrigger        = parsed;
                    bActionTriggerWritten = true;
                }
            }

            // 연속 값 바인딩(2D 합성, 스틱, 마우스 이동량)은 매 프레임 읽는 값이라 Down 으로 고정한다. 다른 trigger 는 조용히 버리지 않고 경고한다.
            const bool bHasContinuousBinding = actionNode.findChild( "vector2d" ).isValid() || actionNode.findChild( "stick" ).isValid() || actionNode.findChild( "mouseDelta" ).isValid();
            const bool bIgnoredTrigger       = bActionTriggerWritten && defaultTrigger != ActionTrigger::Down && bHasContinuousBinding;
            if ( bIgnoredTrigger )
                SW_LOG_WARNING_ID( "Input.ContinuousTriggerIgnored", "Action '%#' sets trigger '%#', but its vector2d/stick/mouseDelta bindings are continuous values read every frame - "
                                                                     "the trigger is ignored for them",
                                   pActionName, pTriggerAttr );

            InputMapActionLoader loader{ map, actionNode, hashed_string( pActionName ), layer, defaultTrigger, bActionTriggerWritten, bValid };
            loader.loadBinds();
            loader.loadVector2Ds();
            loader.loadAxes();
            loader.loadSticks();
            loader.loadChords();
            loader.loadMouseDeltas();
            loader.loadMouseWheels();
        }

        /** @brief `<bind>` 태그: 키 · 패드 · 마우스 단일 바인딩과 modifier 가 붙은 조합입니다. */
        void loadBinds()
        {
            for ( XMLNode bindNode = _actionNode.findChild( Internal::InputMapXML::kBind ); bindNode.isValid(); bindNode = bindNode.findNextSibling( Internal::InputMapXML::kBind ) )
            {
                const utf8* pSource = bindNode.findAttribute( Internal::InputMapXML::kAttrSource );
                const utf8* pCode   = bindNode.findAttribute( Internal::InputMapXML::kAttrCode );
                if ( pSource == nullptr || StringUtil::isNullOrEmpty( pCode ) )
                    continue;
                const uint32 bindCountBefore = _map.getBindingCount( _action );

                const ActionTrigger trigger   = readTrigger( bindNode, Internal::InputMapXML::kAttrTrigger, _defaultTrigger );
                const hashed_string bindLayer = resolveLayer( bindNode, Internal::InputMapXML::kAttrLayer );

                // `modifier` 가 있으면 단축키다(수정 키 마스크 + 키, 정확 일치 — `InputMap::bindShortcut`). 두 키 조합은 `<chord>` 태그로 적는다.
                const utf8* pModifierAttr = bindNode.findAttribute( Internal::InputMapXML::kAttrModifier );
                if ( pModifierAttr != nullptr )
                {
                    uint8      modifierMask = ModifierKey::None;
                    const Key  triggerKey   = KeyCodeUtil::fromName( pCode );
                    const bool bKeyShortcut = StringUtil::equals( pSource, Internal::InputMapXML::kSourceKey, true ) && triggerKey != Key::Unknown;
                    if ( bKeyShortcut == false || InputRemapSerializer::tryParseModifierNames( pModifierAttr, modifierMask ) == false )
                    {
                        SW_LOG_ERROR( "Action '%#': <bind modifier=\"%#\" code=\"%#\"> is not a key shortcut (modifier is Ctrl, Shift, Alt, Super joined by '+')", _action.c_str(),
                                      pModifierAttr, pCode );
                        _bValid = false;
                        continue;
                    }
                    _map.bindShortcut( _action, triggerKey, modifierMask, trigger, bindLayer );
                    InputMapXMLUtil::readBindingModifier( _map, _action, bindCountBefore, bindNode, _bValid );
                    continue;
                }

                if ( StringUtil::equals( pSource, Internal::InputMapXML::kSourceKey ) )
                {
                    const Key key = KeyCodeUtil::fromName( pCode );
                    if ( key != Key::Unknown )
                        _map.bind( _action, key, trigger, bindLayer );
                }
                else if ( StringUtil::equals( pSource, Internal::InputMapXML::kSourceGamepad ) )
                {
                    const bool bLeftStick  = StringUtil::equals( pCode, "LeftStick" );
                    const bool bRightStick = StringUtil::equals( pCode, "RightStick" );
                    if ( bLeftStick || bRightStick )
                    {
                        float32 deadzone = InputMapDefaults::kStickDeadzone;
                        InputMapXMLUtil::readOptional( bindNode, Internal::InputMapXML::kAttrDeadzone, deadzone, _bValid );
                        _map.bindGamepadStick2D( _action, bLeftStick ? GamepadStick::Left : GamepadStick::Right, deadzone, bindLayer );
                    }
                    else
                    {
                        const GamepadButton button = GamepadButtonUtil::fromName( pCode );
                        uint8               padIndex{ 0 };
                        if ( button != GamepadButton::Count && InputMapXMLUtil::tryGetPadIndex( bindNode, padIndex ) )
                            _map.bind( _action, InputSlot::fromGamepadButton( button, padIndex ), trigger, bindLayer );
                    }
                }
                else if ( StringUtil::equals( pSource, Internal::InputMapXML::kSourceMouse ) )
                {
                    const MouseButton mouse = MouseButtonUtil::fromName( pCode );
                    if ( mouse != MouseButton::Count )
                        _map.bind( _action, mouse, trigger, bindLayer );
                }
                InputMapXMLUtil::readBindingModifier( _map, _action, bindCountBefore, bindNode, _bValid );
            }
        }

        /** @brief `<vector2d>` 태그: 네 방향 키로 2D 값을 합성합니다. */
        void loadVector2Ds()
        {
            for ( XMLNode compNode = _actionNode.findChild( "vector2d" ); compNode.isValid(); compNode = compNode.findNextSibling( "vector2d" ) )
            {
                const Key upKey    = KeyCodeUtil::fromName( compNode.getAttributeText( "up" ) );
                const Key downKey  = KeyCodeUtil::fromName( compNode.getAttributeText( "down" ) );
                const Key leftKey  = KeyCodeUtil::fromName( compNode.getAttributeText( "left" ) );
                const Key rightKey = KeyCodeUtil::fromName( compNode.getAttributeText( "right" ) );
                float32   deadzone = InputMapXMLUtil::getDefaultBinding()._deadzone;
                InputMapXMLUtil::readOptional( compNode, "deadzone", deadzone, _bValid );
                const hashed_string compLayer              = resolveLayer( compNode, "layer" );
                const bool          bCompleteDirectionKeys = upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown;
                if ( bCompleteDirectionKeys )
                    _map.bindVector2D( _action, upKey, downKey, leftKey, rightKey, deadzone, compLayer );
            }
        }

        /** @brief `<axis1d>` 태그: 음 · 양 슬롯 두 개로 1D 축을 합성합니다. */
        void loadAxes()
        {
            for ( XMLNode axisNode = _actionNode.findChild( "axis1d" ); axisNode.isValid(); axisNode = axisNode.findNextSibling( "axis1d" ) )
            {
                InputSlot negativeSlot{};
                InputSlot positiveSlot{};
                if ( InputMapXMLUtil::tryReadAxisSlots( axisNode, "negative", "positive", negativeSlot, positiveSlot ) == false )
                    continue; // 모르는 이름이나 패드 번호는 이미 경고했으므로 이 바인딩을 버린다
                const hashed_string axisLayer = resolveLayer( axisNode, "layer" );
                // 축 값은 trigger 와 관계없이 누르는 동안 읽힌다. trigger 는 액션 발화(누를 때마다 한 칸 등)만 정한다.
                const ActionTrigger axisTrigger     = readTrigger( axisNode, Internal::InputMapXML::kAttrTrigger, _bTriggerWritten ? _defaultTrigger : ActionTrigger::Down );
                const uint32        axisCountBefore = _map.getBindingCount( _action );
                _map.bindAxis1DComposite( _action, negativeSlot, positiveSlot, axisLayer, axisTrigger );
                InputMapXMLUtil::readBindingModifier( _map, _action, axisCountBefore, axisNode, _bValid );
            }
        }

        /** @brief `<stick>` 태그: 패드 스틱 2D 값입니다. */
        void loadSticks()
        {
            for ( XMLNode stickNode = _actionNode.findChild( "stick" ); stickNode.isValid(); stickNode = stickNode.findNextSibling( "stick" ) )
            {
                uint8 padIndex{ 0 };
                if ( InputMapXMLUtil::tryGetPadIndex( stickNode, padIndex ) == false )
                    continue; // 이미 경고했으므로 엉뚱한 패드에 묶지 않고 이 바인딩을 버린다
                const utf8*        pStickName = stickNode.findAttribute( "stick" );
                const GamepadStick stick      = ( pStickName != nullptr && StringUtil::equals( pStickName, "Right" ) ) ? GamepadStick::Right : GamepadStick::Left;
                float32            deadzone   = InputMapDefaults::kStickDeadzone;
                InputMapXMLUtil::readOptional( stickNode, "deadzone", deadzone, _bValid );
                const hashed_string stickLayer       = resolveLayer( stickNode, "layer" );
                float32             outerDeadzone    = InputMapXMLUtil::getDefaultBinding()._outerDeadzone;
                float32             responseExponent = InputMapXMLUtil::getDefaultBinding()._responseExponent;
                InputMapXMLUtil::readOptional( stickNode, "outerDeadzone", outerDeadzone, _bValid );
                InputMapXMLUtil::readOptional( stickNode, "responseExponent", responseExponent, _bValid );
                float2           axisScale{};
                bool             bScaleByDelta{ false };
                GamepadStickAxis axis{ GamepadStickAxis::Both };
                if ( InputMapXMLUtil::readStickModifier( stickNode, axisScale, bScaleByDelta, axis, _bValid ) == false )
                    continue; // 이미 오류를 남겼으므로 엉뚱한 성분으로 묶지 않고 이 바인딩을 버린다
                _map.bindGamepadStick2D( _action, stick, deadzone, stickLayer, padIndex, outerDeadzone, responseExponent, axisScale, bScaleByDelta, axis );
            }
        }

        /** @brief `<chord>` 태그: modifier 키와 trigger 키 조합입니다. */
        void loadChords()
        {
            for ( XMLNode chordNode = _actionNode.findChild( "chord" ); chordNode.isValid(); chordNode = chordNode.findNextSibling( "chord" ) )
            {
                const Key           modifierKey      = KeyCodeUtil::fromName( chordNode.getAttributeText( "modifier" ) );
                const Key           triggerKey       = KeyCodeUtil::fromName( chordNode.getAttributeText( "trigger" ) );
                const ActionTrigger trigger          = readTrigger( chordNode, "triggerMode", _defaultTrigger );
                const hashed_string chordLayer       = resolveLayer( chordNode, "layer" );
                const uint32        chordCountBefore = _map.getBindingCount( _action );
                if ( modifierKey != Key::Unknown && triggerKey != Key::Unknown )
                    _map.bindChord( _action, modifierKey, triggerKey, trigger, chordLayer );
                InputMapXMLUtil::readBindingModifier( _map, _action, chordCountBefore, chordNode, _bValid );
            }
        }

        /** @brief `<mouseDelta>` 태그: 마우스 이동량(시점)이며 scale 은 감도 배율이다(1 = 픽셀 그대로). */
        void loadMouseDeltas()
        {
            for ( XMLNode deltaNode = _actionNode.findChild( "mouseDelta" ); deltaNode.isValid(); deltaNode = deltaNode.findNextSibling( "mouseDelta" ) )
            {
                const hashed_string deltaLayer = resolveLayer( deltaNode, "layer" );
                float32             scale      = InputMapXMLUtil::getDefaultBinding()._scale;
                InputMapXMLUtil::readOptional( deltaNode, "scale", scale, _bValid );
                _map.bindMouseDelta( _action, scale, deltaLayer );
            }
        }

        /** @brief `<mouseWheel>` 태그: 마우스 휠(1D 축)이며 scale 은 배율이다(1 = 한 칸에 1). */
        void loadMouseWheels()
        {
            for ( XMLNode wheelNode = _actionNode.findChild( "mouseWheel" ); wheelNode.isValid(); wheelNode = wheelNode.findNextSibling( "mouseWheel" ) )
            {
                const hashed_string wheelLayer = resolveLayer( wheelNode, "layer" );
                float32             scale      = InputMapXMLUtil::getDefaultBinding()._scale;
                InputMapXMLUtil::readOptional( wheelNode, "scale", scale, _bValid );
                _map.bindMouseWheel( _action, scale, wheelLayer );
            }
        }
    };
} // namespace sw

namespace sw
{
    bool InputMap::loadFromAsset( const AssetID& assetID )
    {
        const string_view relativePath = assetID.view();
        return loadDefinition( AssetID{ relativePath }, false );
    }

    bool InputMap::appendFromAsset( const AssetID& assetID )
    {
        return loadDefinition( assetID, true );
    }

    bool InputMap::loadDefinition( const AssetID& assetID, bool bAppend )
    {
        const string_view relativePath = assetID.view();
        using InputMapXML              = InputMapInternal::InputMapXML;
        XMLDocument doc;
        string      absPath;
        if ( doc.loadResource( assetID, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to load InputMap %#", relativePath );
            return false;
        }

        XMLNode root = doc.getRoot( InputMapXML::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <InputMap> in %#", absPath );
            return false;
        }

        // 읽기 단계: 맵 전체 값 → 레이어 → 액션 → 충돌 검사. 단계가 실패하면 거기서 끝낸다.
        if ( bAppend && InputMapInternal::hasMapWideAttribute( root, absPath ) )
            return false;
        if ( readMapSettings( root, absPath, bAppend ) == false )
            return false;

        bool bValid = true;
        readLayerDefinitions( root, absPath, bAppend, bValid );
        vector<hashed_string> listLoadedAction;
        readActionDefinitions( root, absPath, bAppend, listLoadedAction, bValid );

        if ( bValid == false )
        {
            SW_LOG_ERROR( "InputMap %# has invalid attribute values (listed above)", absPath );
            return false;
        }
        // 가상 커서가 마우스로 바꾸는 스틱 · 버튼을 액션에 따로 묶으면 한 입력이 두 입력이 된다 — 읽기를 실패로 돌려 데이터를 고치게 한다.
        if ( countVirtualCursorConflicts( absPath ) > 0 )
            return false;
        uint32 sameLayerConflictCount = 0;
        for ( const hashed_string& actionName : listLoadedAction )
        {
            sameLayerConflictCount += countSameLayerInputConflicts( absPath, actionName );
        }
        return sameLayerConflictCount == 0;
    }

    bool InputMap::readMapSettings( const XMLNode& root, string_view absPath, bool bAppend )
    {
        using InputMapXML = InputMapInternal::InputMapXML;
        // 적지 않은 값은 `InputMapDefaults` 의 기본값이다. 깨진 값은 읽기 실패다.
        bool    bValid        = true;
        float32 dblClick      = InputMapDefaults::kDoubleClickTime;
        float32 dblDist       = InputMapDefaults::kDoubleClickMaxDistance;
        float32 holdThreshold = InputMapDefaults::kHoldThreshold;
        float32 dragThreshold = InputMapDefaults::kDragThreshold;
        InputMapXMLUtil::readOptional( root, InputMapXML::kAttrDoubleClick, dblClick, bValid );
        InputMapXMLUtil::readOptional( root, InputMapXML::kAttrDoubleClickDist, dblDist, bValid );
        InputMapXMLUtil::readOptional( root, InputMapXML::kAttrHoldThreshold, holdThreshold, bValid );
        InputMapXMLUtil::readOptional( root, InputMapXML::kAttrDragThreshold, dragThreshold, bValid );
        if ( bValid == false )
        {
            SW_LOG_ERROR( "InputMap %# has invalid timing attributes", absPath );
            return false;
        }
        const utf8*           pDefaultLayer  = root.findAttribute( InputMapXML::kAttrDefaultLayer );
        const utf8*           pVirtualCursor = root.findAttribute( InputMapXML::kAttrVirtualCursor );
        InputMapVirtualCursor virtualCursor  = InputMapVirtualCursor::Off;
        if ( pVirtualCursor != nullptr && InputMapInternal::tryParseVirtualCursor( pVirtualCursor, virtualCursor ) == false )
        {
            SW_LOG_ERROR( "InputMap %# has an unknown virtualCursor '%#' (Off, LeftStick, RightStick)", absPath, pVirtualCursor );
            return false;
        }

        if ( bAppend == false )
        {
            clear();
            const bool bTimingRefused = setDoubleClickTime( dblClick ) == false || setDoubleClickMaxDistance( dblDist ) == false || setHoldThreshold( holdThreshold ) == false ||
                                        setDragThreshold( dragThreshold ) == false;
            if ( bTimingRefused )
                return false;
            _virtualCursorStick = virtualCursor;
            if ( StringUtil::isNullOrEmpty( pDefaultLayer ) == false )
                _defaultLayerName = hashed_string( pDefaultLayer );
        }
        return true;
    }

    void InputMap::readLayerDefinitions( const XMLNode& root, string_view absPath, bool bAppend, bool& inoutbValid )
    {
        using InputMapXML  = InputMapInternal::InputMapXML;
        XMLNode layersNode = root.findChild( InputMapXML::kLayers );
        if ( layersNode.isValid() )
        {
            for ( XMLNode layerNode = layersNode.findChild( InputMapXML::kLayer ); layerNode.isValid(); layerNode = layerNode.findNextSibling( InputMapXML::kLayer ) )
            {
                const utf8* pLayerName = layerNode.findAttribute( InputMapXML::kAttrName );
                if ( StringUtil::isNullOrEmpty( pLayerName ) )
                    continue;
                if ( bAppend && hasLayer( hashed_string( pLayerName ) ) )
                {
                    SW_LOG_ERROR( "InputMap %# redefines layer '%#' that the map already has - put actions in it with <layer name=...> instead", absPath, pLayerName );
                    inoutbValid = false;
                    continue;
                }
                InputMapInternal::LayerSettings settings;
                InputMapInternal::readLayerSettings( layerNode, settings, inoutbValid );
                registerLayer( hashed_string( pLayerName ), settings._priority, settings._bEnabled, settings._bBlockLower, settings._bAlwaysOn );
            }
        }

        ensureLayer( _defaultLayerName, 0, true, false );
    }

    void InputMap::readActionDefinitions( const XMLNode& root, string_view absPath, bool bAppend, vector<hashed_string>& outListLoadedAction, bool& inoutbValid )
    {
        using InputMapXML = InputMapInternal::InputMapXML;
        // 더할 때는 이미 있는 액션을 다시 정의하지 못한다(두 파일이 같은 이름을 쓰면 바인딩이 섞인다). 더한 액션은 끝에서 같은 레이어 충돌을 본다.
        const auto loadAction = [&]( XMLNode actionNode, string_view inheritedLayer )
        {
            const utf8* pActionName = actionNode.findAttribute( InputMapXML::kAttrName );
            if ( bAppend && StringUtil::isNullOrEmpty( pActionName ) == false )
            {
                const hashed_string actionName( pActionName );
                if ( hasAction( actionName ) )
                {
                    SW_LOG_ERROR( "InputMap %# defines action '%#' that the map already has", absPath, pActionName );
                    inoutbValid = false;
                    return;
                }
                outListLoadedAction.push_back( actionName );
            }
            InputMapActionLoader::load( *this, actionNode, inheritedLayer, inoutbValid );
        };

        for ( XMLNode layerNode = root.findChild( InputMapXML::kLayer ); layerNode.isValid(); layerNode = layerNode.findNextSibling( InputMapXML::kLayer ) )
        {
            const utf8* pLayerName = layerNode.findAttribute( InputMapXML::kAttrName );
            if ( StringUtil::isNullOrEmpty( pLayerName ) )
                continue;
            if ( hasLayer( hashed_string( pLayerName ) ) == false )
            {
                InputMapInternal::LayerSettings settings;
                InputMapInternal::readLayerSettings( layerNode, settings, inoutbValid );
                registerLayer( hashed_string( pLayerName ), settings._priority, settings._bEnabled, settings._bBlockLower, settings._bAlwaysOn );
            }
            for ( XMLNode actionNode = layerNode.findChild( InputMapXML::kAction ); actionNode.isValid(); actionNode = actionNode.findNextSibling( InputMapXML::kAction ) )
            {
                loadAction( actionNode, pLayerName );
            }
        }

        for ( XMLNode actionNode = root.findChild( InputMapXML::kAction ); actionNode.isValid(); actionNode = actionNode.findNextSibling( InputMapXML::kAction ) )
        {
            loadAction( actionNode, _defaultLayerName.view() );
        }
    }

    uint32 InputMap::countSameLayerInputConflicts( string_view sourceName, const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0;
        uint32 conflictCount = 0;
        for ( uint32 bindIndex = 0; bindIndex < static_cast<uint32>( pEntry->_listBinding.size() ); ++bindIndex )
        {
            hashed_string otherName{};
            if ( findSameLayerInputConflict( action, bindIndex, otherName ) == false )
                continue;
            SW_LOG_ERROR( "InputMap %#: action '%#' uses the same input as '%#' in layer '%#' - one press would fire both", sourceName, action.c_str(), otherName.c_str(),
                          pEntry->_listBinding[bindIndex]._layer.c_str() );
            ++conflictCount;
        }
        return conflictCount;
    }

    uint32 InputMap::countVirtualCursorConflicts( string_view sourceName ) const
    {
        uint32 conflictCount = 0;
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry == nullptr )
                continue;
            for ( const ActionBinding& binding : pEntry->_listBinding )
            {
                if ( reportVirtualCursorConflict( sourceName, actionName, binding ) )
                    ++conflictCount;
            }
        }
        return conflictCount;
    }

    uint32 InputMap::removeVirtualCursorConflicts( string_view sourceName, const hashed_string& action, uint32 firstIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0;
        uint32 removedCount = 0;
        for ( uint32 bindIndex = static_cast<uint32>( pEntry->_listBinding.size() ); bindIndex > firstIndex; --bindIndex )
        {
            const uint32 removeIndex = bindIndex - 1;
            if ( reportVirtualCursorConflict( sourceName, action, pEntry->_listBinding[removeIndex] ) == false )
                continue;
            pEntry->_listBinding.erase( pEntry->_listBinding.begin() + removeIndex );
            pEntry->_listDefaultBinding.erase( pEntry->_listDefaultBinding.begin() + removeIndex );
            pEntry->_listBindingState.erase( pEntry->_listBindingState.begin() + removeIndex );
            ++removedCount;
        }
        return removedCount;
    }

    bool InputMap::reportVirtualCursorConflict( string_view sourceName, const hashed_string& action, const ActionBinding& binding ) const
    {
        if ( _virtualCursorStick == InputMapVirtualCursor::Off )
            return false;
        const VirtualCursorSettings cursor{};
        const GamepadStick          cursorStick       = _virtualCursorStick == InputMapVirtualCursor::RightStick ? GamepadStick::Right : GamepadStick::Left;
        const InputSlot             arrCursorButton[] = { InputSlot::fromGamepadButton( cursor._primaryButton, cursor._padIndex ),
                                                          InputSlot::fromGamepadButton( cursor._secondaryButton, cursor._padIndex ) };
        const bool                  bCursorStick      = binding._kind == BindingKind::GamepadStick2D && binding._stick == cursorStick && binding._deviceIndex == cursor._padIndex;
        bool                        bCursorButton     = false;
        for ( const InputSlot& slot : binding._arrSlot )
        {
            for ( const InputSlot& button : arrCursorButton )
            {
                bCursorButton = bCursorButton || slot == button;
            }
        }
        if ( bCursorStick == false && bCursorButton == false )
            return false;
        SW_LOG_ERROR( "InputMap %#: action '%#' binds the %# the virtual cursor (%#) turns into the mouse - one press would fire twice; bind the mouse only",
                      sourceName, action.c_str(), bCursorStick ? "stick" : "gamepad button", InputMapInternal::toVirtualCursorName( _virtualCursorStick ) );
        return true;
    }

    bool InputMap::saveToAsset( const AbsolutePath& absPath ) const
    {
        if ( absPath.str().empty() )
            return false;

        using InputMapXML = InputMapInternal::InputMapXML;
        XMLDocument doc;
        XMLNode     root = doc.appendRoot( InputMapXML::kRoot );
        root.appendAttribute( InputMapXML::kAttrDefaultLayer, _defaultLayerName.c_str() );
        root.appendAttribute( InputMapXML::kAttrDoubleClick, _doubleClickTime );
        root.appendAttribute( InputMapXML::kAttrDoubleClickDist, _doubleClickMaxDistance );
        root.appendAttribute( InputMapXML::kAttrHoldThreshold, _holdThreshold );
        root.appendAttribute( InputMapXML::kAttrDragThreshold, _dragThreshold );
        if ( _virtualCursorStick != InputMapVirtualCursor::Off )
            root.appendAttribute( InputMapXML::kAttrVirtualCursor, InputMapInternal::toVirtualCursorName( _virtualCursorStick ) );

        XMLNode layersNode = root.appendChild( InputMapXML::kLayers );
        for ( const hashed_string& layerName : _listLayerName )
        {
            const LayerDefinition* pLayer = findLayer( layerName );
            if ( pLayer == nullptr )
                continue;
            XMLNode layerNode = layersNode.appendChild( InputMapXML::kLayer );
            layerNode.appendAttribute( InputMapXML::kAttrName, layerName.c_str() );
            layerNode.appendAttribute( InputMapXML::kAttrPriority, pLayer->_priority );
            layerNode.appendAttribute( InputMapXML::kAttrEnabled, pLayer->_bEnabled != SW_FALSE );
            layerNode.appendAttribute( InputMapXML::kAttrBlockLower, pLayer->_bBlockLower != SW_FALSE );
            layerNode.appendAttribute( InputMapXML::kAttrAlwaysOn, pLayer->_bAlwaysOn != SW_FALSE );
        }

        // 액션은 등록 순서대로 쓴다. 그래야 다시 읽은 맵의 액션 순서가 같다. 바인딩마다 레이어와 트리거를 적어서 액션 기본값에 기대지 않는다.
        bool bAllWritten = true;
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry == nullptr )
                continue;
            XMLNode actionNode = root.appendChild( InputMapXML::kAction );
            actionNode.appendAttribute( InputMapXML::kAttrName, actionName.c_str() );
            actionNode.appendAttribute( InputMapXML::kAttrValueType, InputMapInternal::toValueTypeName( pEntry->_valueType ) );
            for ( const ActionBinding& binding : pEntry->_listBinding )
            {
                bAllWritten = InputMapInternal::writeDefinitionBinding( actionNode, actionName.c_str(), binding ) && bAllWritten;
            }
        }
        if ( bAllWritten == false )
            return false;
        if ( doc.saveFile( absPath.str() ) == false )
        {
            SW_LOG_ERROR( "Could not write InputMap %#", absPath.str() );
            return false;
        }
        return true;
    }

    ActionTrigger InputMap::actionTriggerFromName( string_view name )
    {
        if ( name.empty() )
            return ActionTrigger::Count;
        for ( const InputMapInternal::TriggerNameEntry& entry : InputMapInternal::kArrTriggerNames )
        {
            if ( StringUtil::equals( name, entry._pName ) )
                return entry._trigger;
        }
        return ActionTrigger::Count;
    }

    const utf8* InputMap::actionTriggerToName( ActionTrigger trigger )
    {
        for ( const InputMapInternal::TriggerNameEntry& entry : InputMapInternal::kArrTriggerNames )
        {
            if ( entry._trigger == trigger )
                return entry._pName;
        }
        return nullptr;
    }
} // namespace sw
