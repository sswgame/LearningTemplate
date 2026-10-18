#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualCursor.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

/**
 * @file InputMapSerialization.cpp
 * @brief InputMap XML 로드(디자인 타임 기본 바인딩)와 유저 바인딩 저장, 로드(런타임 리매핑 영속화)를 구현합니다.
 *
 * 두 XML 포맷은 용도가 다릅니다.
 *  - `loadFromResource()`, `saveToResource()`: Resource/ 의 InputMap XML(레이어, 액션, 기본 바인딩 정의)을 읽어 InputMap 을 처음 구성합니다.
 *    에디터 InputMap 패널에서 편집한 정의는 같은 형식으로 같은 위치에 다시 씁니다.
 *  - `saveUserBindings()`, `loadUserBindings()`: 플레이어가 키를 리매핑한 결과를 저장하고 복원하는 별도의 유저 바인딩 XML 입니다.
 *  - `actionTriggerFromName()`, `actionTriggerToName()`: 두 포맷이 함께 쓰는 ActionTrigger 이름과 enum 의 변환 테이블입니다.
 */

namespace sw
{
    namespace
    {
        struct InputMapSerializationInternal
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

            /** @brief `virtualCursor` 특성을 읽습니다. 모르는 이름이면 false 입니다(대소문자는 무시한다). */
            [[nodiscard]] static bool tryParseVirtualCursor( const utf8* pText, InputMapVirtualCursor& outStick )
            {
                for ( const VirtualCursorNameEntry& entry : kArrVirtualCursorNames )
                {
                    if ( StringUtil::equals( pText, entry._pName, true ) )
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

            /**
             * @brief 유저 바인딩 줄의 `trigger` 를 읽습니다. 없거나 모르는 이름이면 그 종류의 기본값(`fallback`)을 반환합니다.
             * @details 저장할 때는 항상 적는다. 빼고 저장하면 다시 읽을 때 기본값으로 돌아가서, 누르는 동안 매 프레임 발화하는(Down) 축 합성 같은 결함이 생긴다.
             */
            static ActionTrigger readUserBindingTrigger( XMLNode bindNode, ActionTrigger fallback )
            {
                const utf8* pTriggerAttr = bindNode.findAttribute( InputMapXML::kAttrTrigger );
                if ( pTriggerAttr == nullptr )
                    return fallback;
                const ActionTrigger parsed = InputMap::actionTriggerFromName( pTriggerAttr );
                return ( parsed != ActionTrigger::Count ) ? parsed : fallback;
            }

            /** @brief 값 종류 이름을 읽습니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool tryParseValueType( string_view name, InputActionValueType& outValueType )
            {
                for ( const ValueTypeNameEntry& entry : kArrValueTypeNames )
                {
                    if ( StringUtil::equals( name, entry._pName, true ) )
                    {
                        outValueType = entry._valueType;
                        return true;
                    }
                }
                return false;
            }

            /**
             * @brief 바인딩 하나를 `<InputMap>` 의 액션 노드 아래에, `loadFromResource` 가 읽는 형태로 씁니다.
             * @return `<InputMap>` 형식으로 표현할 수 없는 종류(가상 조이스틱, 단축키, 아무 키)면 오류를 남기고 false 를 반환합니다.
             *         빼고 쓰면 다시 읽을 때 그 바인딩이 조용히 사라지기 때문입니다.
             */
            [[nodiscard]] static bool writeDefinitionBinding( XMLNode actionNode, const utf8* pActionName, const ActionBinding& binding )
            {
                const utf8* pTriggerName = InputMap::actionTriggerToName( binding._trigger );
                switch ( binding._kind )
                {
                    case BindingKind::SingleSlot:
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
                        InputMapSerializationInternal::writeBindingModifier( bindNode, binding );
                        bindNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::Chord:
                    {
                        XMLNode chordNode = actionNode.appendChild( "chord" );
                        chordNode.appendAttribute( "modifier", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[0]._controlIndex ) ) );
                        chordNode.appendAttribute( "trigger", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[1]._controlIndex ) ) );
                        if ( pTriggerName != nullptr )
                            chordNode.appendAttribute( "triggerMode", pTriggerName );
                        InputMapSerializationInternal::writeBindingModifier( chordNode, binding );
                        chordNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::Axis1DComposite:
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
                        InputMapSerializationInternal::writeBindingModifier( axisNode, binding );
                        axisNode.appendAttribute( InputMapXML::kAttrLayer, binding._layer.c_str() );
                        return true;
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
                        InputMapSerializationInternal::writeStickModifier( stickNode, binding );
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
                    case BindingKind::VirtualJoystick2D:
                    case BindingKind::Shortcut:
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

            /**
             * @brief `pad` 속성을 슬롯 범위(0 ~ `kMaxGamepadSlot` - 1)에서 읽습니다. 없으면 0 번 패드입니다.
             * @return 정수가 아니거나 범위 밖이면 경고하고 false 를 반환하며, 호출한 쪽은 그 바인딩을 버립니다.
             * @details 값을 잘라서 담으면 "256" 이 0 번, "-1" 이 255 번 패드가 되고, 4 번 이상은 없는 패드에 말없이 묶인다.
             *          패드 번호를 읽는 세 곳(리소스 스틱, 유저 스틱, 유저 단일 버튼)이 모두 이 함수를 거친다.
             */
            [[nodiscard]] static bool tryGetPadIndex( XMLNode node, uint8& outPadIndex )
            {
                int32 padIndex{ 0 };
                if ( node.tryGetAttributeIntInRange( "pad", 0, 0, static_cast<int32>( kMaxGamepadSlot ) - 1, padIndex ) == false )
                    return false;
                outPadIndex = static_cast<uint8>( padIndex );
                return true;
            }

            /** @brief 스틱 배율 · 프레임 시간 곱(`scaleX` · `scaleY` · `scaleByDelta`)을 적습니다. 기본값(1, 1, false)이면 적지 않아 기존 파일이 그대로 나온다. */
            static void writeStickModifier( XMLNode node, const ActionBinding& binding )
            {
                if ( binding._axisScale._x != 1.0f )
                    node.appendAttribute( "scaleX", binding._axisScale._x );
                if ( binding._axisScale._y != 1.0f )
                    node.appendAttribute( "scaleY", binding._axisScale._y );
                if ( binding._bScaleByDelta )
                    node.appendAttribute( "scaleByDelta", true );
            }

            /** @brief 스틱 배율 · 프레임 시간 곱을 읽습니다. 없는 속성은 기본값(1, 1, false)입니다. */
            static void readStickModifier( XMLNode node, float2& outAxisScale, bool& outScaleByDelta )
            {
                outAxisScale    = float2{ node.getAttributeFloat( "scaleX", 1.0f ), node.getAttributeFloat( "scaleY", 1.0f ) };
                outScaleByDelta = node.getAttributeBool( "scaleByDelta", false );
            }

            /** @brief 바인딩마다 발화 시간 · 부호 뒤집기(`time` · `negate`)를 적습니다. 기본값(0, false)이면 적지 않아 기존 파일이 그대로 나온다. */
            static void writeBindingModifier( XMLNode node, const ActionBinding& binding )
            {
                if ( binding._triggerTime > 0.0f )
                    node.appendAttribute( "time", binding._triggerTime );
                if ( binding._bNegate )
                    node.appendAttribute( "negate", true );
                if ( binding._trigger == ActionTrigger::MultiTap && binding._tapCount != 2 )
                    node.appendAttribute( "taps", static_cast<int32>( binding._tapCount ) );
            }

            /**
             * @brief @p node 를 읽어 방금 더한 바인딩(인덱스 @p previousCount)에 발화 시간 · 부호 뒤집기를 겁니다.
             * @details 바인딩을 더하지 못했으면(모르는 키 등) 아무것도 하지 않는다. 없는 속성은 기본값(0 = 맵 값, false)이라 옛 파일은 그대로 읽힌다.
             */
            static void readBindingModifier( InputMap& inputMap, const hashed_string& action, uint32 previousCount, XMLNode node )
            {
                if ( inputMap.getBindingCount( action ) <= previousCount )
                    return;
                const float32 triggerTime = node.getAttributeFloat( "time", 0.0f );
                if ( triggerTime > 0.0f )
                    (void)inputMap.setBindingTriggerTime( action, previousCount, triggerTime ); // 바로 위에서 바인딩이 있음을 확인했다
                if ( node.getAttributeBool( "negate", false ) )
                    (void)inputMap.setBindingNegate( action, previousCount, true ); // 바로 위에서 바인딩이 있음을 확인했다
                // MultiTap 횟수. 없으면 2 이고, 2~255 밖이면 잘라 넣는다(옛 파일에는 이 속성이 없다).
                const int32 tapCount = node.getAttributeInt( "taps", 2 );
                if ( tapCount != 2 )
                {
                    const int32 clampedCount = ( tapCount < 2 ) ? 2 : ( ( tapCount > 255 ) ? 255 : tapCount );
                    (void)inputMap.setBindingTapCount( action, previousCount, static_cast<uint8>( clampedCount ) ); // 바로 위에서 바인딩이 있음을 확인했다
                }
            }

            /**
             * @brief `modifierMask` 속성을 아는 수정 키 비트(`ModifierKey::All`) 범위에서 읽습니다. 없으면 0(수정 키 없음)입니다.
             * @return 정수가 아니거나 모르는 비트가 있으면 경고하고 false 를 반환하며, 호출한 쪽은 그 바인딩을 버립니다. 범위를 검사하지 않으면 "257" 이 Ctrl 로 읽힙니다.
             */
            [[nodiscard]] static bool tryGetModifierMask( XMLNode node, uint8& outModifierMask )
            {
                int32 modifierMask{ 0 };
                if ( node.tryGetAttributeIntInRange( "modifierMask", 0, 0, ModifierKey::All, modifierMask ) == false )
                    return false;
                outModifierMask = static_cast<uint8>( modifierMask );
                return true;
            }

            /**
             * @brief 1D 축 합성의 두 슬롯을 읽습니다. `source="gamepad"`(와 `pad`)면 게임패드 버튼 이름, 없거나 `key` 면 키 이름을 읽습니다.
             * @return 이름을 모르거나 패드 번호가 틀리면 경고하고 false 를 반환하며, 호출한 쪽은 그 바인딩을 버립니다.
             */
            [[nodiscard]] static bool tryReadAxisSlots( XMLNode node, const utf8* pNegativeName, const utf8* pPositiveName, InputSlot& outNegative, InputSlot& outPositive )
            {
                const utf8* pSource = node.findAttribute( InputMapXML::kAttrSource );
                if ( pSource != nullptr && StringUtil::equals( pSource, InputMapXML::kSourceGamepad, true ) )
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
                if ( pSource != nullptr && StringUtil::equals( pSource, InputMapXML::kSourceKey, true ) == false )
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool InputMap::loadFromResource( string_view relativePath )
    {
        XMLDocument doc;
        string      absPath;
        if ( doc.loadResource( relativePath, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to load InputMap %#", relativePath );
            return false;
        }

        XMLNode root = doc.getRoot( InputMapSerializationInternal::InputMapXML::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <InputMap> in %#", absPath );
            return false;
        }

        const float32         dblClick       = root.getAttributeFloat( InputMapSerializationInternal::InputMapXML::kAttrDoubleClick, InputMapDefaults::kDoubleClickTime );
        const float32         dblDist        = root.getAttributeFloat( InputMapSerializationInternal::InputMapXML::kAttrDoubleClickDist, InputMapDefaults::kDoubleClickMaxDistance );
        const float32         holdThreshold  = root.getAttributeFloat( InputMapSerializationInternal::InputMapXML::kAttrHoldThreshold, InputMapDefaults::kHoldThreshold );
        const utf8*           pDefaultLayer  = root.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrDefaultLayer );
        const utf8*           pVirtualCursor = root.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrVirtualCursor );
        InputMapVirtualCursor virtualCursor  = InputMapVirtualCursor::Off;
        if ( pVirtualCursor != nullptr && InputMapSerializationInternal::tryParseVirtualCursor( pVirtualCursor, virtualCursor ) == false )
        {
            SW_LOG_ERROR( "InputMap %# has an unknown virtualCursor '%#' (Off, LeftStick, RightStick)", absPath, pVirtualCursor );
            return false;
        }

        clear();
        setDoubleClickTime( dblClick );
        setDoubleClickMaxDistance( dblDist );
        setHoldThreshold( holdThreshold );
        _virtualCursorStick = virtualCursor;
        if ( StringUtil::isNullOrEmpty( pDefaultLayer ) == false )
            _defaultLayerName = hashed_string( pDefaultLayer );

        XMLNode layersNode = root.findChild( InputMapSerializationInternal::InputMapXML::kLayers );
        if ( layersNode.isValid() )
        {
            for ( XMLNode layerNode = layersNode.findChild( InputMapSerializationInternal::InputMapXML::kLayer ); layerNode.isValid();
                  layerNode         = layerNode.findNextSibling( InputMapSerializationInternal::InputMapXML::kLayer ) )
            {
                const utf8* pLayerName = layerNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrName );
                if ( StringUtil::isNullOrEmpty( pLayerName ) )
                    continue;
                const int32 priority   = layerNode.getAttributeInt( InputMapSerializationInternal::InputMapXML::kAttrPriority, 0 );
                const bool  enabled    = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXML::kAttrEnabled, true );
                const bool  blockLower = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXML::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXML::kAttrAlwaysOn, false );
                registerLayer( hashed_string( pLayerName ), priority, enabled, blockLower, alwaysOn );
            }
        }

        ensureLayer( _defaultLayerName, 0, true, false );

        auto loadAction = [this]( XMLNode actionNode, string_view inheritedLayer )
        {
            const utf8* pActionName = actionNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrName );
            if ( StringUtil::isNullOrEmpty( pActionName ) )
                return;

            // 값 종류를 적은 액션은 바인딩보다 먼저 만든다. 바인딩 없이 이름만 있는 액션(에디터의 "Add Action")도 다시 읽혀야 하기 때문이다.
            const utf8* pValueTypeAttr = actionNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrValueType );
            if ( pValueTypeAttr != nullptr )
            {
                InputActionValueType valueType{ InputActionValueType::Boolean };
                if ( InputMapSerializationInternal::tryParseValueType( pValueTypeAttr, valueType ) )
                    createAction( hashed_string( pActionName ), valueType );
                else
                    SW_LOG_WARNING( "Action '%#' has an unknown valueType '%#' - the bindings decide it", pActionName, pValueTypeAttr );
            }

            hashed_string layer      = inheritedLayer.empty() ? _defaultLayerName : hashed_string( inheritedLayer );
            const utf8*   pLayerAttr = actionNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrLayer );
            if ( StringUtil::isNullOrEmpty( pLayerAttr ) == false )
                layer = hashed_string( pLayerAttr );
            ensureLayer( layer );

            // trigger 를 적지 않으면 단일 키와 조합은 Pressed, 축 합성은 Down 이 된다. 적은 trigger 는 셋 모두에 그대로 적용한다.
            auto        defaultTrigger        = ActionTrigger::Pressed;
            bool        bActionTriggerWritten = false;
            const utf8* pTriggerAttr          = actionNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrTrigger );
            if ( pTriggerAttr != nullptr )
            {
                const ActionTrigger parsed = actionTriggerFromName( pTriggerAttr );
                if ( parsed != ActionTrigger::Count )
                {
                    defaultTrigger        = parsed;
                    bActionTriggerWritten = true;
                }
            }

            // 연속 값 바인딩(2D 합성, 스틱, 마우스 이동량)은 매 프레임 읽는 값이라 Down 으로 고정한다. 다른 trigger 는 조용히 버리지 않고 경고한다.
            const bool bHasContinuousBinding = actionNode.findChild( "vector2d" ).isValid() || actionNode.findChild( "stick" ).isValid() ||
                                               actionNode.findChild( "mouseDelta" ).isValid();
            const bool bIgnoredTrigger = bActionTriggerWritten && defaultTrigger != ActionTrigger::Down && bHasContinuousBinding;
            if ( bIgnoredTrigger )
                SW_LOG_WARNING( "Action '%#' sets trigger '%#', but its vector2d/stick/mouseDelta bindings are continuous values read every frame - "
                                "the trigger is ignored for them",
                                pActionName, pTriggerAttr );

            // 1) <bind> 태그 파싱
            for ( XMLNode bindNode = actionNode.findChild( InputMapSerializationInternal::InputMapXML::kBind ); bindNode.isValid();
                  bindNode         = bindNode.findNextSibling( InputMapSerializationInternal::InputMapXML::kBind ) )
            {
                const utf8* pSource = bindNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrSource );
                const utf8* pCode   = bindNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrCode );
                if ( pSource == nullptr || StringUtil::isNullOrEmpty( pCode ) )
                    continue;
                const uint32 bindCountBefore = getBindingCount( hashed_string( pActionName ) );

                ActionTrigger trigger          = defaultTrigger;
                const utf8*   pBindTriggerAttr = bindNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrTrigger );
                if ( pBindTriggerAttr != nullptr )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pBindTriggerAttr );
                    if ( parsed != ActionTrigger::Count )
                        trigger = parsed;
                }

                hashed_string bindLayer      = layer;
                const utf8*   pBindLayerAttr = bindNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrLayer );
                if ( StringUtil::isNullOrEmpty( pBindLayerAttr ) == false )
                {
                    bindLayer = hashed_string( pBindLayerAttr );
                    ensureLayer( bindLayer );
                }

                const utf8* pModifierAttr = bindNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrModifier );
                if ( StringUtil::isNullOrEmpty( pModifierAttr ) == false )
                {
                    Key modifierKey = KeyCodeUtil::fromName( pModifierAttr );
                    if ( modifierKey == Key::Unknown )
                    {
                        if ( StringUtil::equals( pModifierAttr, "Ctrl", true ) || StringUtil::equals( pModifierAttr, "Control", true ) )
                            modifierKey = Key::LeftControl;
                        else if ( StringUtil::equals( pModifierAttr, "Shift", true ) )
                            modifierKey = Key::LeftShift;
                        else if ( StringUtil::equals( pModifierAttr, "Alt", true ) )
                            modifierKey = Key::LeftAlt;
                    }
                    const Key triggerKey = KeyCodeUtil::fromName( pCode );
                    if ( modifierKey != Key::Unknown && triggerKey != Key::Unknown )
                    {
                        bindChord( hashed_string( pActionName ), modifierKey, triggerKey, trigger, hashed_string( bindLayer.view() ) );
                        InputMapSerializationInternal::readBindingModifier( *this, hashed_string( pActionName ), bindCountBefore, bindNode );
                        continue;
                    }
                }

                if ( StringUtil::equals( pSource, InputMapSerializationInternal::InputMapXML::kSourceKey, true ) )
                {
                    const Key key = KeyCodeUtil::fromName( pCode );
                    if ( key != Key::Unknown )
                        bind( hashed_string( pActionName ), key, trigger, hashed_string( bindLayer.view() ) );
                }
                else if ( StringUtil::equals( pSource, InputMapSerializationInternal::InputMapXML::kSourceGamepad, true ) )
                {
                    if ( StringUtil::equals( pCode, "LeftStick", true ) )
                    {
                        const float32 deadzone = bindNode.getAttributeFloat( InputMapSerializationInternal::InputMapXML::kAttrDeadzone, 0.15f );
                        bindGamepadStick2D( hashed_string( pActionName ), GamepadStick::Left, deadzone, hashed_string( bindLayer.view() ) );
                    }
                    else if ( StringUtil::equals( pCode, "RightStick", true ) )
                    {
                        const float32 deadzone = bindNode.getAttributeFloat( InputMapSerializationInternal::InputMapXML::kAttrDeadzone, 0.15f );
                        bindGamepadStick2D( hashed_string( pActionName ), GamepadStick::Right, deadzone, hashed_string( bindLayer.view() ) );
                    }
                    else
                    {
                        const GamepadButton button = GamepadButtonUtil::fromName( pCode );
                        uint8               padIndex{ 0 };
                        if ( button != GamepadButton::Count && InputMapSerializationInternal::tryGetPadIndex( bindNode, padIndex ) )
                            bind( hashed_string( pActionName ), InputSlot::fromGamepadButton( button, padIndex ), trigger, hashed_string( bindLayer.view() ) );
                    }
                }
                else if ( StringUtil::equals( pSource, InputMapSerializationInternal::InputMapXML::kSourceMouse, true ) )
                {
                    const MouseButton mouse = MouseButtonUtil::fromName( pCode );
                    if ( mouse != MouseButton::Count )
                        bind( hashed_string( pActionName ), mouse, trigger, hashed_string( bindLayer.view() ) );
                }
                InputMapSerializationInternal::readBindingModifier( *this, hashed_string( pActionName ), bindCountBefore, bindNode );
            }

            // 2) <vector2d> 태그 파싱
            for ( XMLNode compNode = actionNode.findChild( "vector2d" ); compNode.isValid(); compNode = compNode.findNextSibling( "vector2d" ) )
            {
                const Key     upKey          = KeyCodeUtil::fromName( compNode.getAttributeText( "up" ) );
                const Key     downKey        = KeyCodeUtil::fromName( compNode.getAttributeText( "down" ) );
                const Key     leftKey        = KeyCodeUtil::fromName( compNode.getAttributeText( "left" ) );
                const Key     rightKey       = KeyCodeUtil::fromName( compNode.getAttributeText( "right" ) );
                const float32 deadzone       = compNode.getAttributeFloat( "deadzone", 0.0f );
                hashed_string compLayer      = layer;
                const utf8*   pCompLayerAttr = compNode.findAttribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pCompLayerAttr ) == false )
                {
                    compLayer = hashed_string( pCompLayerAttr );
                    ensureLayer( compLayer );
                }
                const bool bCompleteDirectionKeys = upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown;
                if ( bCompleteDirectionKeys )
                    bindVector2D( hashed_string( pActionName ), upKey, downKey, leftKey, rightKey, deadzone, hashed_string( compLayer.view() ) );
            }

            // 3) <axis1d> 태그 파싱
            for ( XMLNode axisNode = actionNode.findChild( "axis1d" ); axisNode.isValid(); axisNode = axisNode.findNextSibling( "axis1d" ) )
            {
                InputSlot negativeSlot{};
                InputSlot positiveSlot{};
                if ( InputMapSerializationInternal::tryReadAxisSlots( axisNode, "negative", "positive", negativeSlot, positiveSlot ) == false )
                    continue; // 모르는 이름이나 패드 번호는 이미 경고했으므로 이 바인딩을 버린다
                hashed_string axisLayer      = layer;
                const utf8*   pAxisLayerAttr = axisNode.findAttribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pAxisLayerAttr ) == false )
                {
                    axisLayer = hashed_string( pAxisLayerAttr );
                    ensureLayer( axisLayer );
                }
                // 축 값은 trigger 와 관계없이 누르는 동안 읽힌다. trigger 는 액션 발화(누를 때마다 한 칸 등)만 정한다.
                ActionTrigger axisTrigger      = bActionTriggerWritten ? defaultTrigger : ActionTrigger::Down;
                const utf8*   pAxisTriggerAttr = axisNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrTrigger );
                if ( pAxisTriggerAttr != nullptr )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pAxisTriggerAttr );
                    if ( parsed != ActionTrigger::Count )
                        axisTrigger = parsed;
                }
                const uint32 axisCountBefore = getBindingCount( hashed_string( pActionName ) );
                bindAxis1DComposite( hashed_string( pActionName ), negativeSlot, positiveSlot, hashed_string( axisLayer.view() ), axisTrigger );
                InputMapSerializationInternal::readBindingModifier( *this, hashed_string( pActionName ), axisCountBefore, axisNode );
            }

            // 4) <stick> 태그 파싱
            for ( XMLNode stickNode = actionNode.findChild( "stick" ); stickNode.isValid(); stickNode = stickNode.findNextSibling( "stick" ) )
            {
                uint8 padIndex{ 0 };
                if ( InputMapSerializationInternal::tryGetPadIndex( stickNode, padIndex ) == false )
                    continue; // 이미 경고했으므로 엉뚱한 패드에 묶지 않고 이 바인딩을 버린다
                const utf8*        pStickName      = stickNode.findAttribute( "stick" );
                const GamepadStick stick           = ( pStickName != nullptr && StringUtil::equals( pStickName, "Right", true ) ) ? GamepadStick::Right : GamepadStick::Left;
                const float32      deadzone        = stickNode.getAttributeFloat( "deadzone", 0.15f );
                hashed_string      stickLayer      = layer;
                const utf8*        pStickLayerAttr = stickNode.findAttribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pStickLayerAttr ) == false )
                {
                    stickLayer = hashed_string( pStickLayerAttr );
                    ensureLayer( stickLayer );
                }
                const float32 outerDeadzone    = stickNode.getAttributeFloat( "outerDeadzone", 1.0f );
                const float32 responseExponent = stickNode.getAttributeFloat( "responseExponent", 1.0f );
                float2        axisScale{ 1.0f, 1.0f };
                bool          bScaleByDelta{ false };
                InputMapSerializationInternal::readStickModifier( stickNode, axisScale, bScaleByDelta );
                bindGamepadStick2D( hashed_string( pActionName ), stick, deadzone, hashed_string( stickLayer.view() ), padIndex, outerDeadzone, responseExponent, axisScale, bScaleByDelta );
            }

            // 5) <chord> 태그 파싱
            for ( XMLNode chordNode = actionNode.findChild( "chord" ); chordNode.isValid(); chordNode = chordNode.findNextSibling( "chord" ) )
            {
                const Key     modifierKey       = KeyCodeUtil::fromName( chordNode.getAttributeText( "modifier" ) );
                const Key     triggerKey        = KeyCodeUtil::fromName( chordNode.getAttributeText( "trigger" ) );
                ActionTrigger trigger           = defaultTrigger;
                const utf8*   pChordTriggerAttr = chordNode.findAttribute( "triggerMode" );
                if ( StringUtil::isNullOrEmpty( pChordTriggerAttr ) == false )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pChordTriggerAttr );
                    if ( parsed != ActionTrigger::Count )
                        trigger = parsed;
                }
                hashed_string chordLayer      = layer;
                const utf8*   pChordLayerAttr = chordNode.findAttribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pChordLayerAttr ) == false )
                {
                    chordLayer = hashed_string( pChordLayerAttr );
                    ensureLayer( chordLayer );
                }
                const uint32 chordCountBefore = getBindingCount( hashed_string( pActionName ) );
                if ( modifierKey != Key::Unknown && triggerKey != Key::Unknown )
                    bindChord( hashed_string( pActionName ), modifierKey, triggerKey, trigger, hashed_string( chordLayer.view() ) );
                InputMapSerializationInternal::readBindingModifier( *this, hashed_string( pActionName ), chordCountBefore, chordNode );
            }

            // 6) <mouseDelta> 태그 파싱. 마우스 이동량(시점)이며 scale 은 감도 배율이다(1 = 픽셀 그대로).
            for ( XMLNode deltaNode = actionNode.findChild( "mouseDelta" ); deltaNode.isValid(); deltaNode = deltaNode.findNextSibling( "mouseDelta" ) )
            {
                hashed_string deltaLayer      = layer;
                const utf8*   pDeltaLayerAttr = deltaNode.findAttribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pDeltaLayerAttr ) == false )
                {
                    deltaLayer = hashed_string( pDeltaLayerAttr );
                    ensureLayer( deltaLayer );
                }
                bindMouseDelta( hashed_string( pActionName ), deltaNode.getAttributeFloat( "scale", 1.0f ), hashed_string( deltaLayer.view() ) );
            }

            // 7) <mouseWheel> 태그 파싱. 마우스 휠(1D 축)이며 scale 은 배율이다(1 = 한 칸에 1).
            for ( XMLNode wheelNode = actionNode.findChild( "mouseWheel" ); wheelNode.isValid(); wheelNode = wheelNode.findNextSibling( "mouseWheel" ) )
            {
                hashed_string wheelLayer      = layer;
                const utf8*   pWheelLayerAttr = wheelNode.findAttribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pWheelLayerAttr ) == false )
                {
                    wheelLayer = hashed_string( pWheelLayerAttr );
                    ensureLayer( wheelLayer );
                }
                bindMouseWheel( hashed_string( pActionName ), wheelNode.getAttributeFloat( "scale", 1.0f ), hashed_string( wheelLayer.view() ) );
            }
        };

        for ( XMLNode layerNode = root.findChild( InputMapSerializationInternal::InputMapXML::kLayer ); layerNode.isValid(); layerNode = layerNode.findNextSibling( InputMapSerializationInternal::InputMapXML::kLayer ) )
        {
            const utf8* pLayerName = layerNode.findAttribute( InputMapSerializationInternal::InputMapXML::kAttrName );
            if ( StringUtil::isNullOrEmpty( pLayerName ) )
                continue;
            if ( hasLayer( hashed_string( pLayerName ) ) == false )
            {
                const int32 priority   = layerNode.getAttributeInt( InputMapSerializationInternal::InputMapXML::kAttrPriority, 0 );
                const bool  enabled    = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXML::kAttrEnabled, true );
                const bool  blockLower = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXML::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXML::kAttrAlwaysOn, false );
                registerLayer( hashed_string( pLayerName ), priority, enabled, blockLower, alwaysOn );
            }
            for ( XMLNode actionNode = layerNode.findChild( InputMapSerializationInternal::InputMapXML::kAction ); actionNode.isValid();
                  actionNode         = actionNode.findNextSibling( InputMapSerializationInternal::InputMapXML::kAction ) )
            {
                loadAction( actionNode, pLayerName );
            }
        }

        for ( XMLNode actionNode = root.findChild( InputMapSerializationInternal::InputMapXML::kAction ); actionNode.isValid();
              actionNode         = actionNode.findNextSibling( InputMapSerializationInternal::InputMapXML::kAction ) )
        {
            loadAction( actionNode, _defaultLayerName.view() );
        }

        // 가상 커서가 마우스로 바꾸는 스틱 · 버튼을 액션에 따로 묶으면 한 입력이 두 입력이 된다 — 읽기를 실패로 돌려 데이터를 고치게 한다.
        if ( countVirtualCursorConflicts( absPath ) > 0 )
            return false;
        return true;
    }

    uint32 InputMap::countVirtualCursorConflicts( string_view sourceName ) const
    {
        if ( _virtualCursorStick == InputMapVirtualCursor::Off )
            return 0;
        const VirtualCursorSettings cursor{};
        const GamepadStick          cursorStick       = _virtualCursorStick == InputMapVirtualCursor::RightStick ? GamepadStick::Right : GamepadStick::Left;
        const InputSlot             arrCursorButton[] = { InputSlot::fromGamepadButton( cursor._primaryButton, cursor._padIndex ),
                                                          InputSlot::fromGamepadButton( cursor._secondaryButton, cursor._padIndex ) };
        uint32                      conflictCount     = 0;
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry == nullptr )
                continue;
            for ( const ActionBinding& binding : pEntry->_listBinding )
            {
                const bool bCursorStick  = binding._kind == BindingKind::GamepadStick2D && binding._stick == cursorStick && binding._deviceIndex == cursor._padIndex;
                bool       bCursorButton = false;
                for ( const InputSlot& slot : binding._arrSlot )
                {
                    for ( const InputSlot& button : arrCursorButton )
                    {
                        bCursorButton = bCursorButton || slot == button;
                    }
                }
                if ( bCursorStick == false && bCursorButton == false )
                    continue;
                SW_LOG_ERROR( "InputMap %#: action '%#' binds the %# the virtual cursor (%#) turns into the mouse - one press would fire twice; bind the mouse only",
                              sourceName, actionName.c_str(), bCursorStick ? "stick" : "gamepad button",
                              InputMapSerializationInternal::toVirtualCursorName( _virtualCursorStick ) );
                ++conflictCount;
            }
        }
        return conflictCount;
    }

    bool InputMap::saveToResource( string_view relativePath ) const
    {
        if ( relativePath.empty() )
            return false;

        using InputMapXML = InputMapSerializationInternal::InputMapXML;
        XMLDocument doc;
        XMLNode     root = doc.appendRoot( InputMapXML::kRoot );
        root.appendAttribute( InputMapXML::kAttrDefaultLayer, _defaultLayerName.c_str() );
        root.appendAttribute( InputMapXML::kAttrDoubleClick, _doubleClickTime );
        root.appendAttribute( InputMapXML::kAttrDoubleClickDist, _doubleClickMaxDistance );
        root.appendAttribute( InputMapXML::kAttrHoldThreshold, _holdThreshold );
        if ( _virtualCursorStick != InputMapVirtualCursor::Off )
            root.appendAttribute( InputMapXML::kAttrVirtualCursor, InputMapSerializationInternal::toVirtualCursorName( _virtualCursorStick ) );

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
            actionNode.appendAttribute( InputMapXML::kAttrValueType, InputMapSerializationInternal::toValueTypeName( pEntry->_valueType ) );
            for ( const ActionBinding& binding : pEntry->_listBinding )
            {
                bAllWritten = InputMapSerializationInternal::writeDefinitionBinding( actionNode, actionName.c_str(), binding ) && bAllWritten;
            }
        }
        if ( bAllWritten == false )
            return false;

        const string absPath = ResourcePaths::getWritePath( relativePath );
        if ( doc.saveFile( absPath ) == false )
        {
            SW_LOG_ERROR( "Could not write InputMap %#", absPath );
            return false;
        }
        return true;
    }

    bool InputMap::saveUserBindings( string_view filePath ) const
    {
        if ( filePath.empty() )
            return false;

        XMLDocument doc;
        XMLNode     root = doc.appendRoot( "UserBindings" );
        for ( const auto& [actionName, actIndex] : _mapAction )
        {
            const ActionEntry& entry = _listActionEntry[actIndex];
            for ( const ActionBinding& b : entry._listBinding )
            {
                XMLNode bindNode = root.appendChild( "bind" );
                bindNode.appendAttribute( "action", actionName.c_str() );
                bindNode.appendAttribute( "layer", b._layer.c_str() );
                // 이름은 테이블에서 가져온다. 쓰는 쪽과 읽는 쪽이 리터럴을 따로 가지면 한쪽만 고쳤을 때 파일이 조용히 왕복되지 않는다.
                bindNode.appendAttribute( "kind", BindingKinds::toName( b._kind ) );
                // 발화 규칙도 바인딩의 일부다. 빼면 다시 읽을 때 종류의 기본값으로 돌아간다(`readUserBindingTrigger`).
                const utf8* pTriggerName = actionTriggerToName( b._trigger );
                if ( pTriggerName != nullptr )
                    bindNode.appendAttribute( "trigger", pTriggerName );
                InputMapSerializationInternal::writeBindingModifier( bindNode, b );

                // 종류를 추가하고 여기를 빠뜨리면 그 바인딩이 특성 하나 없이 저장되어 조용히 사라진다. 그래서 default 에서 오류를 남긴다.
                switch ( b._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
                        {
                            const Key key = static_cast<Key>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttribute( "source", "key" );
                            bindNode.appendAttribute( "key", KeyCodeUtil::toName( key ) );
                        }
                        else if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Mouse )
                        {
                            const MouseButton btn = static_cast<MouseButton>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttribute( "source", "mouse" );
                            bindNode.appendAttribute( "button", MouseButtonUtil::toName( btn ) );
                        }
                        else if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
                        {
                            const GamepadButton btn = static_cast<GamepadButton>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttribute( "source", "gamepad" );
                            bindNode.appendAttribute( "code", GamepadButtonUtil::toName( btn ) );
                            bindNode.appendAttribute( "pad", static_cast<int32>( b._arrSlot[0]._deviceIndex ) );
                        }
                        break;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
                        {
                            bindNode.appendAttribute( "source", "gamepad" );
                            bindNode.appendAttribute( "negKey", GamepadButtonUtil::toName( static_cast<GamepadButton>( b._arrSlot[0]._controlIndex ) ) );
                            bindNode.appendAttribute( "posKey", GamepadButtonUtil::toName( static_cast<GamepadButton>( b._arrSlot[1]._controlIndex ) ) );
                            bindNode.appendAttribute( "pad", static_cast<int32>( b._arrSlot[0]._deviceIndex ) );
                            break;
                        }
                        bindNode.appendAttribute( "negKey", KeyCodeUtil::toName( static_cast<Key>( b._arrSlot[0]._controlIndex ) ) );
                        bindNode.appendAttribute( "posKey", KeyCodeUtil::toName( static_cast<Key>( b._arrSlot[1]._controlIndex ) ) );
                        break;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        const Key upKey    = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key downKey  = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        const Key leftKey  = static_cast<Key>( b._arrSlot[2]._controlIndex );
                        const Key rightKey = static_cast<Key>( b._arrSlot[3]._controlIndex );
                        bindNode.appendAttribute( "up", KeyCodeUtil::toName( upKey ) );
                        bindNode.appendAttribute( "down", KeyCodeUtil::toName( downKey ) );
                        bindNode.appendAttribute( "left", KeyCodeUtil::toName( leftKey ) );
                        bindNode.appendAttribute( "right", KeyCodeUtil::toName( rightKey ) );
                        bindNode.appendAttribute( "deadzone", b._deadzone );
                        break;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        bindNode.appendAttribute( "stick", b._stick == GamepadStick::Left ? "Left" : "Right" );
                        bindNode.appendAttribute( "pad", static_cast<int32>( b._deviceIndex ) );
                        bindNode.appendAttribute( "deadzone", b._deadzone );
                        bindNode.appendAttribute( "outerDeadzone", b._outerDeadzone );
                        bindNode.appendAttribute( "exponent", b._responseExponent );
                        InputMapSerializationInternal::writeStickModifier( bindNode, b );
                        break;
                    }
                    case BindingKind::MouseDelta2D:
                    case BindingKind::MouseWheel1D:
                    {
                        bindNode.appendAttribute( "scale", b._scale );
                        break;
                    }
                    case BindingKind::VirtualJoystick2D:
                    {
                        const MouseButton activationButton = static_cast<MouseButton>( b._arrSlot[0]._controlIndex );
                        bindNode.appendAttribute( "button", MouseButtonUtil::toName( activationButton ) );
                        bindNode.appendAttribute( "radius", b._scale );
                        bindNode.appendAttribute( "deadzone", b._deadzone );
                        bindNode.appendAttribute( "outerDeadzone", b._outerDeadzone );
                        break;
                    }
                    case BindingKind::Chord:
                    {
                        const Key modifierKey = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key triggerKey  = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        bindNode.appendAttribute( "modKey", KeyCodeUtil::toName( modifierKey ) );
                        bindNode.appendAttribute( "trigKey", KeyCodeUtil::toName( triggerKey ) );
                        break;
                    }
                    case BindingKind::Shortcut:
                    {
                        const Key key = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        bindNode.appendAttribute( "key", KeyCodeUtil::toName( key ) );
                        bindNode.appendAttribute( "modifierMask", static_cast<int32>( b._modifierMask ) );
                        break;
                    }
                    case BindingKind::AnyKey:
                    {
                        break; // 이름 말고는 적을 것이 없다.
                    }
                    case BindingKind::Count:
                    {
                        // 종류를 추가하고 이 switch 를 빠뜨린 경우다. 이름은 테이블에서 왔으므로 `kind` 는
                        // 제대로 적히지만 딸린 특성이 하나도 없어서 다시 읽을 수 없는 줄이 된다.
                        SW_LOG_ERROR( "저장하지 못한 바인딩 종류입니다 (kind=%#) — BindingKind 를 늘리고 saveUserBindings 를 빠뜨렸습니다.",
                                      BindingKinds::toName( b._kind ) );
                        break;
                    }
                }
            }
        }
        return doc.saveFile( filePath );
    }

    bool InputMap::loadUserBindings( string_view filePath )
    {
        if ( filePath.empty() )
            return false;

        XMLDocument doc;
        if ( doc.loadResource( filePath ) == false )
            return false;

        XMLNode root = doc.getRoot( "UserBindings" );
        if ( root.isValid() == false )
            return false;

        for ( XMLNode bindNode = root.findChild( "bind" ); bindNode.isValid(); bindNode = bindNode.findNextSibling( "bind" ) )
        {
            const utf8*       pAction   = bindNode.findAttribute( "action" );
            const utf8*       pKindStr  = bindNode.findAttribute( "kind" );
            const utf8*       pLayerStr = bindNode.findAttribute( "layer" );
            const string_view layer     = ( StringUtil::isNullOrEmpty( pLayerStr ) == false ) ? string_view( pLayerStr ) : string_view{};

            if ( StringUtil::isNullOrEmpty( pAction ) )
                continue;

            // 이름에서 종류는 테이블이 찾는다. 종류가 없거나 모르는 이름이면 그 바인딩은 읽지 않는다. 저장할 때는 항상 `kind` 를 쓰기 때문이다.
            const BindingKind parsedKind = ( pKindStr != nullptr ) ? BindingKinds::fromName( pKindStr ) : BindingKind::Count;
            if ( parsedKind == BindingKind::Count )
            {
                SW_LOG_ERROR( "바인딩 종류가 없거나 모르는 이름입니다 (action=%#, kind=%#) — 이 바인딩은 건너뜁니다.", pAction,
                              ( pKindStr != nullptr ) ? pKindStr : "" );
                continue;
            }

            const hashed_string actionName( pAction );
            const uint32        previousCount = getBindingCount( actionName );
            switch ( parsedKind )
            {
                case BindingKind::Axis1DComposite:
                {
                    InputSlot negativeSlot{};
                    InputSlot positiveSlot{};
                    if ( InputMapSerializationInternal::tryReadAxisSlots( bindNode, "negKey", "posKey", negativeSlot, positiveSlot ) )
                        bindAxis1DComposite( hashed_string( pAction ), negativeSlot, positiveSlot, hashed_string( layer ),
                                             InputMapSerializationInternal::readUserBindingTrigger( bindNode, ActionTrigger::Down ) );
                    break;
                }
                case BindingKind::Vector2DComposite:
                {
                    const Key     upKey                  = KeyCodeUtil::fromName( bindNode.getAttributeText( "up" ) );
                    const Key     downKey                = KeyCodeUtil::fromName( bindNode.getAttributeText( "down" ) );
                    const Key     leftKey                = KeyCodeUtil::fromName( bindNode.getAttributeText( "left" ) );
                    const Key     rightKey               = KeyCodeUtil::fromName( bindNode.getAttributeText( "right" ) );
                    const float32 deadzone               = bindNode.getAttributeFloat( "deadzone", 0.0f );
                    const bool    bAllDirectionKeysBound = upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown;
                    if ( bAllDirectionKeysBound )
                        bindVector2D( hashed_string( pAction ), upKey, downKey, leftKey, rightKey, deadzone, hashed_string( layer ) );
                    break;
                }
                case BindingKind::GamepadStick2D:
                {
                    uint8 pad{ 0 };
                    if ( InputMapSerializationInternal::tryGetPadIndex( bindNode, pad ) == false )
                        break; // 이미 경고했으므로 이 바인딩을 버린다
                    const utf8*        pStickStr     = bindNode.findAttribute( "stick" );
                    const GamepadStick stick         = StringUtil::equals( pStickStr, "Right", true ) ? GamepadStick::Right : GamepadStick::Left;
                    const float32      deadzone      = bindNode.getAttributeFloat( "deadzone", 0.15f );
                    const float32      outerDeadzone = bindNode.getAttributeFloat( "outerDeadzone", 1.0f );
                    const float32      exp           = bindNode.getAttributeFloat( "exponent", 1.0f );
                    float2             axisScale{ 1.0f, 1.0f };
                    bool               bScaleByDelta{ false };
                    InputMapSerializationInternal::readStickModifier( bindNode, axisScale, bScaleByDelta );
                    bindGamepadStick2D( hashed_string( pAction ), stick, deadzone, hashed_string( layer ), pad, outerDeadzone, exp, axisScale, bScaleByDelta );
                    break;
                }
                case BindingKind::MouseDelta2D:
                {
                    const float32 scale = bindNode.getAttributeFloat( "scale", 1.0f );
                    bindMouseDelta( hashed_string( pAction ), scale, hashed_string( layer ) );
                    break;
                }
                case BindingKind::MouseWheel1D:
                {
                    const float32 scale = bindNode.getAttributeFloat( "scale", 1.0f );
                    bindMouseWheel( hashed_string( pAction ), scale, hashed_string( layer ) );
                    break;
                }
                case BindingKind::VirtualJoystick2D:
                {
                    const MouseButton activationButton = MouseButtonUtil::fromName( bindNode.getAttributeText( "button" ) );
                    const float32     radius           = bindNode.getAttributeFloat( "radius", 64.0f );
                    const float32     deadzone         = bindNode.getAttributeFloat( "deadzone", 0.1f );
                    const float32     outerDeadzone    = bindNode.getAttributeFloat( "outerDeadzone", 1.0f );
                    if ( activationButton != MouseButton::Count )
                        bindVirtualJoystick2D( hashed_string( pAction ), activationButton, radius, deadzone, hashed_string( layer ), outerDeadzone );
                    break;
                }
                case BindingKind::Chord:
                {
                    const Key modifierKey = KeyCodeUtil::fromName( bindNode.getAttributeText( "modKey" ) );
                    const Key triggerKey  = KeyCodeUtil::fromName( bindNode.getAttributeText( "trigKey" ) );
                    if ( modifierKey != Key::Unknown && triggerKey != Key::Unknown )
                        bindChord( hashed_string( pAction ), modifierKey, triggerKey, InputMapSerializationInternal::readUserBindingTrigger( bindNode, ActionTrigger::Pressed ),
                                   hashed_string( layer ) );
                    break;
                }
                case BindingKind::Shortcut:
                {
                    uint8 modifierMask{ 0 };
                    if ( InputMapSerializationInternal::tryGetModifierMask( bindNode, modifierMask ) == false )
                        break; // 이미 경고했으므로 다른 수정 키 조합으로 묶지 않고 이 바인딩을 버린다
                    const Key key = KeyCodeUtil::fromName( bindNode.getAttributeText( "key" ) );
                    if ( key != Key::Unknown )
                        bindShortcut( hashed_string( pAction ), key, modifierMask, InputMapSerializationInternal::readUserBindingTrigger( bindNode, ActionTrigger::Pressed ),
                                      hashed_string( layer ) );
                    break;
                }
                case BindingKind::AnyKey:
                {
                    bindAnyKey( hashed_string( pAction ), hashed_string( layer ) );
                    break;
                }
                case BindingKind::SingleSlot:
                {
                    // 저장할 때와 같은 형태만 읽는다: source="key" key=…, source="mouse" button=…, source="gamepad" code=… pad=…
                    const utf8*         pSourceStr    = bindNode.findAttribute( "source" );
                    const ActionTrigger singleTrigger = InputMapSerializationInternal::readUserBindingTrigger( bindNode, ActionTrigger::Pressed );
                    if ( StringUtil::equals( pSourceStr, "key", true ) )
                    {
                        const Key key = KeyCodeUtil::fromName( bindNode.getAttributeText( "key" ) );
                        if ( key != Key::Unknown )
                            bind( hashed_string( pAction ), key, singleTrigger, hashed_string( layer ) );
                    }
                    else if ( StringUtil::equals( pSourceStr, "mouse", true ) )
                    {
                        const MouseButton btn = MouseButtonUtil::fromName( bindNode.getAttributeText( "button" ) );
                        if ( btn != MouseButton::Count )
                            bind( hashed_string( pAction ), btn, singleTrigger, hashed_string( layer ) );
                    }
                    else if ( StringUtil::equals( pSourceStr, "gamepad", true ) )
                    {
                        const GamepadButton btn = GamepadButtonUtil::fromName( bindNode.getAttributeText( "code" ) );
                        uint8               padIndex{ 0 };
                        if ( btn != GamepadButton::Count && InputMapSerializationInternal::tryGetPadIndex( bindNode, padIndex ) )
                        {
                            InputSlot slot    = InputSlot::fromGamepadButton( btn );
                            slot._deviceIndex = padIndex;
                            bind( hashed_string( pAction ), slot, singleTrigger, hashed_string( layer ) );
                        }
                    }
                    else
                    {
                        SW_LOG_ERROR( "단일 슬롯 바인딩의 source 가 없거나 모르는 값입니다 (action=%#, source=%#) — 이 바인딩은 건너뜁니다.", pAction,
                                      ( pSourceStr != nullptr ) ? pSourceStr : "" );
                    }
                    break;
                }
                case BindingKind::Count:
                {
                    // 테이블은 이 이름을 아는데 여기서는 모른다. 종류를 추가하고 이 switch 를 빠뜨린 경우다.
                    SW_LOG_ERROR( "읽지 못한 바인딩 종류입니다 (kind=%#) — BindingKind 를 늘리고 loadUserBindings 를 빠뜨렸습니다.", pKindStr );
                    break;
                }
            }
            InputMapSerializationInternal::readBindingModifier( *this, actionName, previousCount, bindNode );
        }
        return true;
    }

    ActionTrigger InputMap::actionTriggerFromName( string_view name )
    {
        if ( name.empty() )
            return ActionTrigger::Count;
        for ( const InputMapSerializationInternal::TriggerNameEntry& entry : InputMapSerializationInternal::kArrTriggerNames )
        {
            if ( StringUtil::equals( name, entry._pName, true ) )
                return entry._trigger;
        }
        return ActionTrigger::Count;
    }

    const utf8* InputMap::actionTriggerToName( ActionTrigger trigger )
    {
        for ( const InputMapSerializationInternal::TriggerNameEntry& entry : InputMapSerializationInternal::kArrTriggerNames )
        {
            if ( entry._trigger == trigger )
                return entry._pName;
        }
        return nullptr;
    }
} // namespace sw
