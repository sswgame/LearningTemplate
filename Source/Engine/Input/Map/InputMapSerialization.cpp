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
                        return writeSingleSlotDefinition( actionNode, pActionName, binding, pTriggerName );
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
                        return writeAxis1DDefinition( actionNode, pActionName, binding, pTriggerName );
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

            /** @brief 슬롯 하나짜리 바인딩을 `<bind>` 로 씁니다. 키보드 · 마우스 · 게임패드 밖의 장치면 오류를 남기고 false 를 반환합니다. */
            [[nodiscard]] static bool writeSingleSlotDefinition( XMLNode actionNode, const utf8* pActionName, const ActionBinding& binding, const utf8* pTriggerName )
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

            /** @brief 1D 축 합성을 `<axis1d>` 로 씁니다. 키보드 둘 또는 같은 패드의 버튼 둘만 표현할 수 있어, 그 밖이면 오류를 남기고 false 를 반환합니다. */
            [[nodiscard]] static bool writeAxis1DDefinition( XMLNode actionNode, const utf8* pActionName, const ActionBinding& binding, const utf8* pTriggerName )
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

            /** @brief 스틱 배율 · 프레임 시간 곱 · 성분(`scaleX` · `scaleY` · `scaleByDelta` · `axis`)을 적습니다. 기본값(1, 1, false, 둘 다)이면 적지 않아 기존 파일이 그대로 나온다. */
            static void writeStickModifier( XMLNode node, const ActionBinding& binding )
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

            /**
             * @brief 스틱 배율 · 프레임 시간 곱 · 성분을 읽습니다. 없는 속성은 기본값(1, 1, false, 둘 다)입니다.
             * @return `axis` 가 `X` · `Y` 가 아닌 글이면 경고하고 false 입니다(그 바인딩은 버린다 — 2D 로 바꿔 묶으면 1D 액션이 엉뚱한 성분을 읽는다).
             */
            [[nodiscard]] static bool readStickModifier( XMLNode node, float2& outAxisScale, bool& outScaleByDelta, GamepadStickAxis& outAxis )
            {
                outAxisScale    = float2{ node.getAttributeFloat( "scaleX", 1.0f ), node.getAttributeFloat( "scaleY", 1.0f ) };
                outScaleByDelta = node.getAttributeBool( "scaleByDelta", false );

                outAxis              = GamepadStickAxis::Both;
                const utf8* pAxisStr = node.findAttribute( "axis" );
                if ( StringUtil::isNullOrEmpty( pAxisStr ) )
                    return true;
                if ( StringUtil::equals( pAxisStr, "X", true ) )
                {
                    outAxis = GamepadStickAxis::X;
                    return true;
                }
                if ( StringUtil::equals( pAxisStr, "Y", true ) )
                {
                    outAxis = GamepadStickAxis::Y;
                    return true;
                }
                SW_LOG_WARNING( "stick axis '%#' is neither 'X' nor 'Y' - binding dropped", pAxisStr );
                return false;
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

            static constexpr float32 kStickDeadzoneDefault           = 0.15f; ///< `deadzone` 를 적지 않은 스틱 바인딩의 안쪽 데드존
            static constexpr float32 kVector2DDeadzoneDefault        = 0.0f;
            static constexpr float32 kOuterDeadzoneDefault           = 1.0f;
            static constexpr float32 kResponseExponentDefault        = 1.0f;
            static constexpr float32 kScaleDefault                   = 1.0f; ///< 마우스 이동량 · 휠 `scale`
            static constexpr float32 kVirtualJoystickRadiusDefault   = 64.0f;
            static constexpr float32 kVirtualJoystickDeadzoneDefault = 0.1f;

            /** @brief 액션 하나를 읽는 동안 모든 태그가 함께 쓰는 값입니다. */
            struct ActionLoadScope
            {
                hashed_string _action;
                hashed_string _layer;          ///< 액션(또는 상위 레이어)의 레이어. 태그가 `layer` 를 적으면 그것이 이긴다.
                ActionTrigger _defaultTrigger; ///< 액션이 적은 trigger. 안 적었으면 Pressed.
                bool          _bTriggerWritten;
            };

            /** @brief `trigger` 계열 특성을 읽습니다. 없거나 모르는 이름이면 false 이고 @p outTrigger 는 그대로입니다. */
            [[nodiscard]] static bool tryReadTrigger( XMLNode node, const utf8* pAttrName, ActionTrigger& outTrigger )
            {
                const utf8* pAttr = node.findAttribute( pAttrName );
                if ( pAttr == nullptr )
                    return false;
                const ActionTrigger parsed = InputMap::actionTriggerFromName( pAttr );
                if ( parsed == ActionTrigger::Count )
                    return false;
                outTrigger = parsed;
                return true;
            }

            /** @brief 태그의 `layer` 특성을 읽습니다. 없으면 @p fallback 이고, 적었으면 그 레이어를 (없으면) 만들어 둡니다. */
            [[nodiscard]] static hashed_string readLayer( InputMap& inputMap, XMLNode node, const hashed_string& fallback )
            {
                const utf8* pLayerAttr = node.findAttribute( InputMapXML::kAttrLayer );
                if ( StringUtil::isNullOrEmpty( pLayerAttr ) )
                    return fallback;
                const hashed_string layer( pLayerAttr );
                inputMap.registerLayer( layer );
                return layer;
            }

            /** @brief `<layer>` 노드의 우선순위 · 켜짐 · 아래 막기 · 항상 켜짐을 읽어 레이어를 등록합니다. */
            static void registerLayerNode( InputMap& inputMap, XMLNode layerNode, const utf8* pLayerName )
            {
                const int32 priority   = layerNode.getAttributeInt( InputMapXML::kAttrPriority, 0 );
                const bool  enabled    = layerNode.getAttributeBool( InputMapXML::kAttrEnabled, true );
                const bool  blockLower = layerNode.getAttributeBool( InputMapXML::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.getAttributeBool( InputMapXML::kAttrAlwaysOn, false );
                inputMap.registerLayer( hashed_string( pLayerName ), priority, enabled, blockLower, alwaysOn );
            }

            /** @brief `modifier` 특성의 키를 읽습니다. 키 이름이 아니면 Ctrl · Shift · Alt 별칭을 봅니다. 모르면 Key::Unknown 입니다. */
            static Key resolveModifierKey( const utf8* pModifierAttr )
            {
                const Key modifierKey = KeyCodeUtil::fromName( pModifierAttr );
                if ( modifierKey != Key::Unknown )
                    return modifierKey;
                if ( StringUtil::equals( pModifierAttr, "Ctrl", true ) || StringUtil::equals( pModifierAttr, "Control", true ) )
                    return Key::LeftControl;
                if ( StringUtil::equals( pModifierAttr, "Shift", true ) )
                    return Key::LeftShift;
                if ( StringUtil::equals( pModifierAttr, "Alt", true ) )
                    return Key::LeftAlt;
                return Key::Unknown;
            }

            /** @brief `<bind modifier=...>` 이 키 조합이면 묶고 true 를 반환합니다. 수정 키나 방아쇠 키를 모르면 false 이며 단일 바인딩으로 이어집니다. */
            [[nodiscard]] static bool tryBindChord( InputMap& inputMap, XMLNode bindNode, const utf8* pCode, const ActionLoadScope& scope, ActionTrigger trigger, const hashed_string& layer )
            {
                const utf8* pModifierAttr = bindNode.findAttribute( InputMapXML::kAttrModifier );
                if ( StringUtil::isNullOrEmpty( pModifierAttr ) )
                    return false;
                const Key modifierKey = resolveModifierKey( pModifierAttr );
                const Key triggerKey  = KeyCodeUtil::fromName( pCode );
                if ( modifierKey == Key::Unknown || triggerKey == Key::Unknown )
                    return false;
                inputMap.bindChord( scope._action, modifierKey, triggerKey, trigger, layer );
                return true;
            }

            /** @brief `source="gamepad"` 바인딩을 묶습니다. 코드가 스틱 이름이면 스틱, 아니면 버튼입니다. */
            static void bindGamepadSource( InputMap& inputMap, XMLNode bindNode, const utf8* pCode, const ActionLoadScope& scope, ActionTrigger trigger, const hashed_string& layer )
            {
                const bool bLeftStick  = StringUtil::equals( pCode, "LeftStick", true );
                const bool bRightStick = StringUtil::equals( pCode, "RightStick", true );
                if ( bLeftStick || bRightStick )
                {
                    const float32 deadzone = bindNode.getAttributeFloat( InputMapXML::kAttrDeadzone, kStickDeadzoneDefault );
                    inputMap.bindGamepadStick2D( scope._action, bLeftStick ? GamepadStick::Left : GamepadStick::Right, deadzone, layer );
                    return;
                }
                const GamepadButton button = GamepadButtonUtil::fromName( pCode );
                uint8               padIndex{ 0 };
                if ( button != GamepadButton::Count && tryGetPadIndex( bindNode, padIndex ) )
                    inputMap.bind( scope._action, InputSlot::fromGamepadButton( button, padIndex ), trigger, layer );
            }

            /** @brief `<bind source=... code=...>` 하나를 읽습니다. */
            static void loadBindNode( InputMap& inputMap, XMLNode bindNode, const ActionLoadScope& scope )
            {
                const utf8* pSource = bindNode.findAttribute( InputMapXML::kAttrSource );
                const utf8* pCode   = bindNode.findAttribute( InputMapXML::kAttrCode );
                if ( pSource == nullptr || StringUtil::isNullOrEmpty( pCode ) )
                    return;
                const uint32 bindCountBefore = inputMap.getBindingCount( scope._action );

                ActionTrigger trigger = scope._defaultTrigger;
                (void)tryReadTrigger( bindNode, InputMapXML::kAttrTrigger, trigger ); // 없거나 모르는 이름이면 trigger 가 액션 기본값 그대로다
                const hashed_string bindLayer = readLayer( inputMap, bindNode, scope._layer );

                if ( tryBindChord( inputMap, bindNode, pCode, scope, trigger, bindLayer ) == false )
                {
                    if ( StringUtil::equals( pSource, InputMapXML::kSourceKey, true ) )
                    {
                        const Key key = KeyCodeUtil::fromName( pCode );
                        if ( key != Key::Unknown )
                            inputMap.bind( scope._action, key, trigger, bindLayer );
                    }
                    else if ( StringUtil::equals( pSource, InputMapXML::kSourceGamepad, true ) )
                    {
                        bindGamepadSource( inputMap, bindNode, pCode, scope, trigger, bindLayer );
                    }
                    else if ( StringUtil::equals( pSource, InputMapXML::kSourceMouse, true ) )
                    {
                        const MouseButton mouse = MouseButtonUtil::fromName( pCode );
                        if ( mouse != MouseButton::Count )
                            inputMap.bind( scope._action, mouse, trigger, bindLayer );
                    }
                }
                readBindingModifier( inputMap, scope._action, bindCountBefore, bindNode );
            }

            static void loadVector2DNode( InputMap& inputMap, XMLNode compNode, const ActionLoadScope& scope )
            {
                const Key           upKey                  = KeyCodeUtil::fromName( compNode.getAttributeText( "up" ) );
                const Key           downKey                = KeyCodeUtil::fromName( compNode.getAttributeText( "down" ) );
                const Key           leftKey                = KeyCodeUtil::fromName( compNode.getAttributeText( "left" ) );
                const Key           rightKey               = KeyCodeUtil::fromName( compNode.getAttributeText( "right" ) );
                const float32       deadzone               = compNode.getAttributeFloat( "deadzone", kVector2DDeadzoneDefault );
                const hashed_string compLayer              = readLayer( inputMap, compNode, scope._layer );
                const bool          bCompleteDirectionKeys = upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown;
                if ( bCompleteDirectionKeys )
                    inputMap.bindVector2D( scope._action, upKey, downKey, leftKey, rightKey, deadzone, compLayer );
            }

            static void loadAxis1DNode( InputMap& inputMap, XMLNode axisNode, const ActionLoadScope& scope )
            {
                InputSlot negativeSlot{};
                InputSlot positiveSlot{};
                if ( tryReadAxisSlots( axisNode, "negative", "positive", negativeSlot, positiveSlot ) == false )
                    return; // 모르는 이름이나 패드 번호는 이미 경고했으므로 이 바인딩을 버린다
                const hashed_string axisLayer = readLayer( inputMap, axisNode, scope._layer );
                // 축 값은 trigger 와 관계없이 누르는 동안 읽힌다. trigger 는 액션 발화(누를 때마다 한 칸 등)만 정한다.
                ActionTrigger axisTrigger = scope._bTriggerWritten ? scope._defaultTrigger : ActionTrigger::Down;
                (void)tryReadTrigger( axisNode, InputMapXML::kAttrTrigger, axisTrigger ); // 없으면 축 기본 trigger 그대로다
                const uint32 axisCountBefore = inputMap.getBindingCount( scope._action );
                inputMap.bindAxis1DComposite( scope._action, negativeSlot, positiveSlot, axisLayer, axisTrigger );
                readBindingModifier( inputMap, scope._action, axisCountBefore, axisNode );
            }

            static void loadStickNode( InputMap& inputMap, XMLNode stickNode, const ActionLoadScope& scope )
            {
                uint8 padIndex{ 0 };
                if ( tryGetPadIndex( stickNode, padIndex ) == false )
                    return; // 이미 경고했으므로 엉뚱한 패드에 묶지 않고 이 바인딩을 버린다
                const utf8*         pStickName       = stickNode.findAttribute( "stick" );
                const GamepadStick  stick            = ( pStickName != nullptr && StringUtil::equals( pStickName, "Right", true ) ) ? GamepadStick::Right : GamepadStick::Left;
                const float32       deadzone         = stickNode.getAttributeFloat( "deadzone", kStickDeadzoneDefault );
                const hashed_string stickLayer       = readLayer( inputMap, stickNode, scope._layer );
                const float32       outerDeadzone    = stickNode.getAttributeFloat( "outerDeadzone", kOuterDeadzoneDefault );
                const float32       responseExponent = stickNode.getAttributeFloat( "responseExponent", kResponseExponentDefault );
                float2              axisScale{ 1.0f, 1.0f };
                bool                bScaleByDelta{ false };
                GamepadStickAxis    axis{ GamepadStickAxis::Both };
                if ( readStickModifier( stickNode, axisScale, bScaleByDelta, axis ) == false )
                    return; // 이미 경고했으므로 엉뚱한 성분으로 묶지 않고 이 바인딩을 버린다
                inputMap.bindGamepadStick2D( scope._action, stick, deadzone, stickLayer, padIndex, outerDeadzone, responseExponent, axisScale, bScaleByDelta, axis );
            }

            static void loadChordNode( InputMap& inputMap, XMLNode chordNode, const ActionLoadScope& scope )
            {
                const Key     modifierKey = KeyCodeUtil::fromName( chordNode.getAttributeText( "modifier" ) );
                const Key     triggerKey  = KeyCodeUtil::fromName( chordNode.getAttributeText( "trigger" ) );
                ActionTrigger trigger     = scope._defaultTrigger;
                (void)tryReadTrigger( chordNode, "triggerMode", trigger ); // 없으면 액션 기본 trigger 그대로다
                const hashed_string chordLayer       = readLayer( inputMap, chordNode, scope._layer );
                const uint32        chordCountBefore = inputMap.getBindingCount( scope._action );
                if ( modifierKey != Key::Unknown && triggerKey != Key::Unknown )
                    inputMap.bindChord( scope._action, modifierKey, triggerKey, trigger, chordLayer );
                readBindingModifier( inputMap, scope._action, chordCountBefore, chordNode );
            }

            /** @brief 값 종류를 적은 액션은 바인딩보다 먼저 만듭니다. 바인딩 없이 이름만 있는 액션(에디터의 "Add Action")도 다시 읽혀야 하기 때문입니다. */
            static void createActionFromValueType( InputMap& inputMap, XMLNode actionNode, const utf8* pActionName )
            {
                const utf8* pValueTypeAttr = actionNode.findAttribute( InputMapXML::kAttrValueType );
                if ( pValueTypeAttr == nullptr )
                    return;
                InputActionValueType valueType{ InputActionValueType::Boolean };
                if ( tryParseValueType( pValueTypeAttr, valueType ) )
                    inputMap.createAction( hashed_string( pActionName ), valueType );
                else
                    SW_LOG_WARNING( "Action '%#' has an unknown valueType '%#' - the bindings decide it", pActionName, pValueTypeAttr );
            }

            /** @brief 연속 값 바인딩(2D 합성, 스틱, 마우스 이동량)은 매 프레임 읽는 값이라 Down 으로 고정한다. 다른 trigger 는 조용히 버리지 않고 경고한다. */
            static void warnIgnoredContinuousTrigger( XMLNode actionNode, const utf8* pActionName, const ActionLoadScope& scope )
            {
                const bool bHasContinuousBinding = actionNode.findChild( "vector2d" ).isValid() || actionNode.findChild( "stick" ).isValid() ||
                                                   actionNode.findChild( "mouseDelta" ).isValid();
                const bool bIgnoredTrigger = scope._bTriggerWritten && scope._defaultTrigger != ActionTrigger::Down && bHasContinuousBinding;
                if ( bIgnoredTrigger )
                    SW_LOG_WARNING( "Action '%#' sets trigger '%#', but its vector2d/stick/mouseDelta bindings are continuous values read every frame - "
                                    "the trigger is ignored for them",
                                    pActionName, actionNode.findAttribute( InputMapXML::kAttrTrigger ) );
            }

            /** @brief `<action>` 하나를 읽습니다. 태그 종류마다 형제 노드를 모두 읽습니다. */
            static void loadAction( InputMap& inputMap, XMLNode actionNode, string_view inheritedLayer, const hashed_string& defaultLayerName )
            {
                const utf8* pActionName = actionNode.findAttribute( InputMapXML::kAttrName );
                if ( StringUtil::isNullOrEmpty( pActionName ) )
                    return;
                createActionFromValueType( inputMap, actionNode, pActionName );

                ActionLoadScope scope{ hashed_string( pActionName ), inheritedLayer.empty() ? defaultLayerName : hashed_string( inheritedLayer ), ActionTrigger::Pressed, false };
                scope._layer = readLayer( inputMap, actionNode, scope._layer );
                inputMap.registerLayer( scope._layer );

                // trigger 를 적지 않으면 단일 키와 조합은 Pressed, 축 합성은 Down 이 된다. 적은 trigger 는 셋 모두에 그대로 적용한다.
                scope._bTriggerWritten = tryReadTrigger( actionNode, InputMapXML::kAttrTrigger, scope._defaultTrigger );
                warnIgnoredContinuousTrigger( actionNode, pActionName, scope );

                for ( XMLNode node = actionNode.findChild( InputMapXML::kBind ); node.isValid(); node = node.findNextSibling( InputMapXML::kBind ) )
                {
                    loadBindNode( inputMap, node, scope );
                }
                for ( XMLNode node = actionNode.findChild( "vector2d" ); node.isValid(); node = node.findNextSibling( "vector2d" ) )
                {
                    loadVector2DNode( inputMap, node, scope );
                }
                for ( XMLNode node = actionNode.findChild( "axis1d" ); node.isValid(); node = node.findNextSibling( "axis1d" ) )
                {
                    loadAxis1DNode( inputMap, node, scope );
                }
                for ( XMLNode node = actionNode.findChild( "stick" ); node.isValid(); node = node.findNextSibling( "stick" ) )
                {
                    loadStickNode( inputMap, node, scope );
                }
                for ( XMLNode node = actionNode.findChild( "chord" ); node.isValid(); node = node.findNextSibling( "chord" ) )
                {
                    loadChordNode( inputMap, node, scope );
                }
                // 마우스 이동량(시점)이며 scale 은 감도 배율이다(1 = 픽셀 그대로).
                for ( XMLNode node = actionNode.findChild( "mouseDelta" ); node.isValid(); node = node.findNextSibling( "mouseDelta" ) )
                {
                    inputMap.bindMouseDelta( scope._action, node.getAttributeFloat( "scale", kScaleDefault ), readLayer( inputMap, node, scope._layer ) );
                }
                // 마우스 휠(1D 축)이며 scale 은 배율이다(1 = 한 칸에 1).
                for ( XMLNode node = actionNode.findChild( "mouseWheel" ); node.isValid(); node = node.findNextSibling( "mouseWheel" ) )
                {
                    inputMap.bindMouseWheel( scope._action, node.getAttributeFloat( "scale", kScaleDefault ), readLayer( inputMap, node, scope._layer ) );
                }
            }

            /** @brief `<layers>` 안의 레이어 정의를 등록합니다. */
            static void loadLayersTag( InputMap& inputMap, XMLNode root )
            {
                XMLNode layersNode = root.findChild( InputMapXML::kLayers );
                if ( layersNode.isValid() == false )
                    return;
                for ( XMLNode layerNode = layersNode.findChild( InputMapXML::kLayer ); layerNode.isValid(); layerNode = layerNode.findNextSibling( InputMapXML::kLayer ) )
                {
                    const utf8* pLayerName = layerNode.findAttribute( InputMapXML::kAttrName );
                    if ( StringUtil::isNullOrEmpty( pLayerName ) == false )
                        registerLayerNode( inputMap, layerNode, pLayerName );
                }
            }

            /** @brief 루트 아래 `<layer>` 안의 액션과 루트에 바로 놓인 액션을 읽습니다. */
            static void loadActions( InputMap& inputMap, XMLNode root, const hashed_string& defaultLayerName )
            {
                for ( XMLNode layerNode = root.findChild( InputMapXML::kLayer ); layerNode.isValid(); layerNode = layerNode.findNextSibling( InputMapXML::kLayer ) )
                {
                    const utf8* pLayerName = layerNode.findAttribute( InputMapXML::kAttrName );
                    if ( StringUtil::isNullOrEmpty( pLayerName ) )
                        continue;
                    if ( inputMap.hasLayer( hashed_string( pLayerName ) ) == false )
                        registerLayerNode( inputMap, layerNode, pLayerName );
                    for ( XMLNode actionNode = layerNode.findChild( InputMapXML::kAction ); actionNode.isValid(); actionNode = actionNode.findNextSibling( InputMapXML::kAction ) )
                    {
                        loadAction( inputMap, actionNode, pLayerName, defaultLayerName );
                    }
                }
                for ( XMLNode actionNode = root.findChild( InputMapXML::kAction ); actionNode.isValid(); actionNode = actionNode.findNextSibling( InputMapXML::kAction ) )
                {
                    loadAction( inputMap, actionNode, defaultLayerName.view(), defaultLayerName );
                }
            }

            /** @brief 유저 바인딩의 슬롯 하나짜리 줄의 특성(`source` 와 장치별 코드)을 적습니다. */
            static void writeUserSingleSlot( XMLNode bindNode, const InputSlot& slot )
            {
                if ( slot._deviceKind == InputDeviceKind::Keyboard )
                {
                    bindNode.appendAttribute( "source", "key" );
                    bindNode.appendAttribute( "key", KeyCodeUtil::toName( static_cast<Key>( slot._controlIndex ) ) );
                }
                else if ( slot._deviceKind == InputDeviceKind::Mouse )
                {
                    bindNode.appendAttribute( "source", "mouse" );
                    bindNode.appendAttribute( "button", MouseButtonUtil::toName( static_cast<MouseButton>( slot._controlIndex ) ) );
                }
                else if ( slot._deviceKind == InputDeviceKind::Gamepad )
                {
                    bindNode.appendAttribute( "source", "gamepad" );
                    bindNode.appendAttribute( "code", GamepadButtonUtil::toName( static_cast<GamepadButton>( slot._controlIndex ) ) );
                    bindNode.appendAttribute( "pad", static_cast<int32>( slot._deviceIndex ) );
                }
            }

            /** @brief 유저 바인딩 줄의 종류별 특성을 적습니다. */
            static void writeUserBindingKind( XMLNode bindNode, const ActionBinding& b )
            {
                // 종류를 추가하고 여기를 빠뜨리면 그 바인딩이 특성 하나 없이 저장되어 조용히 사라진다. 그래서 default 에서 오류를 남긴다.
                switch ( b._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        writeUserSingleSlot( bindNode, b._arrSlot[0] );
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

            /** @brief 유저 바인딩의 슬롯 하나짜리 줄을 읽습니다. 저장할 때와 같은 형태만 읽는다: source="key" key=…, source="mouse" button=…, source="gamepad" code=… pad=… */
            static void loadUserSingleSlot( InputMap& inputMap, XMLNode bindNode, const utf8* pAction, string_view layer )
            {
                const utf8*         pSourceStr    = bindNode.findAttribute( "source" );
                const ActionTrigger singleTrigger = readUserBindingTrigger( bindNode, ActionTrigger::Pressed );
                if ( StringUtil::equals( pSourceStr, "key", true ) )
                {
                    const Key key = KeyCodeUtil::fromName( bindNode.getAttributeText( "key" ) );
                    if ( key != Key::Unknown )
                        inputMap.bind( hashed_string( pAction ), key, singleTrigger, hashed_string( layer ) );
                }
                else if ( StringUtil::equals( pSourceStr, "mouse", true ) )
                {
                    const MouseButton btn = MouseButtonUtil::fromName( bindNode.getAttributeText( "button" ) );
                    if ( btn != MouseButton::Count )
                        inputMap.bind( hashed_string( pAction ), btn, singleTrigger, hashed_string( layer ) );
                }
                else if ( StringUtil::equals( pSourceStr, "gamepad", true ) )
                {
                    const GamepadButton btn = GamepadButtonUtil::fromName( bindNode.getAttributeText( "code" ) );
                    uint8               padIndex{ 0 };
                    if ( btn != GamepadButton::Count && tryGetPadIndex( bindNode, padIndex ) )
                    {
                        InputSlot slot    = InputSlot::fromGamepadButton( btn );
                        slot._deviceIndex = padIndex;
                        inputMap.bind( hashed_string( pAction ), slot, singleTrigger, hashed_string( layer ) );
                    }
                }
                else
                {
                    SW_LOG_ERROR( "단일 슬롯 바인딩의 source 가 없거나 모르는 값입니다 (action=%#, source=%#) — 이 바인딩은 건너뜁니다.", pAction,
                                  ( pSourceStr != nullptr ) ? pSourceStr : "" );
                }
            }

            /** @brief 유저 바인딩의 2D 방향 합성 줄을 읽습니다. 네 방향 키가 모두 있어야 묶습니다. */
            static void loadUserVector2D( InputMap& inputMap, XMLNode bindNode, const utf8* pAction, string_view layer )
            {
                const Key     upKey                  = KeyCodeUtil::fromName( bindNode.getAttributeText( "up" ) );
                const Key     downKey                = KeyCodeUtil::fromName( bindNode.getAttributeText( "down" ) );
                const Key     leftKey                = KeyCodeUtil::fromName( bindNode.getAttributeText( "left" ) );
                const Key     rightKey               = KeyCodeUtil::fromName( bindNode.getAttributeText( "right" ) );
                const float32 deadzone               = bindNode.getAttributeFloat( "deadzone", kVector2DDeadzoneDefault );
                const bool    bAllDirectionKeysBound = upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown;
                if ( bAllDirectionKeysBound )
                    inputMap.bindVector2D( hashed_string( pAction ), upKey, downKey, leftKey, rightKey, deadzone, hashed_string( layer ) );
            }

            /** @brief 유저 바인딩 줄 하나를 종류에 맞게 읽어 맵에 묶습니다. 저장할 때와 같은 형태만 읽습니다. */
            static void loadUserBindingNode( InputMap& inputMap, XMLNode bindNode, const utf8* pAction, const utf8* pKindStr, BindingKind parsedKind, string_view layer )
            {
                switch ( parsedKind )
                {
                    case BindingKind::Axis1DComposite:
                    {
                        InputSlot negativeSlot{};
                        InputSlot positiveSlot{};
                        if ( InputMapSerializationInternal::tryReadAxisSlots( bindNode, "negKey", "posKey", negativeSlot, positiveSlot ) )
                            inputMap.bindAxis1DComposite( hashed_string( pAction ), negativeSlot, positiveSlot, hashed_string( layer ),
                                                          InputMapSerializationInternal::readUserBindingTrigger( bindNode, ActionTrigger::Down ) );
                        break;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        loadUserVector2D( inputMap, bindNode, pAction, layer );
                        break;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        uint8 pad{ 0 };
                        if ( InputMapSerializationInternal::tryGetPadIndex( bindNode, pad ) == false )
                            break; // 이미 경고했으므로 이 바인딩을 버린다
                        const utf8*        pStickStr     = bindNode.findAttribute( "stick" );
                        const GamepadStick stick         = StringUtil::equals( pStickStr, "Right", true ) ? GamepadStick::Right : GamepadStick::Left;
                        const float32      deadzone      = bindNode.getAttributeFloat( "deadzone", kStickDeadzoneDefault );
                        const float32      outerDeadzone = bindNode.getAttributeFloat( "outerDeadzone", kOuterDeadzoneDefault );
                        const float32      exp           = bindNode.getAttributeFloat( "exponent", kResponseExponentDefault );
                        float2             axisScale{ 1.0f, 1.0f };
                        bool               bScaleByDelta{ false };
                        GamepadStickAxis   axis{ GamepadStickAxis::Both };
                        if ( InputMapSerializationInternal::readStickModifier( bindNode, axisScale, bScaleByDelta, axis ) == false )
                            break; // 이미 경고했으므로 이 바인딩을 버린다
                        inputMap.bindGamepadStick2D( hashed_string( pAction ), stick, deadzone, hashed_string( layer ), pad, outerDeadzone, exp, axisScale, bScaleByDelta, axis );
                        break;
                    }
                    case BindingKind::MouseDelta2D:
                    {
                        const float32 scale = bindNode.getAttributeFloat( "scale", kScaleDefault );
                        inputMap.bindMouseDelta( hashed_string( pAction ), scale, hashed_string( layer ) );
                        break;
                    }
                    case BindingKind::MouseWheel1D:
                    {
                        const float32 scale = bindNode.getAttributeFloat( "scale", kScaleDefault );
                        inputMap.bindMouseWheel( hashed_string( pAction ), scale, hashed_string( layer ) );
                        break;
                    }
                    case BindingKind::VirtualJoystick2D:
                    {
                        const MouseButton activationButton = MouseButtonUtil::fromName( bindNode.getAttributeText( "button" ) );
                        const float32     radius           = bindNode.getAttributeFloat( "radius", kVirtualJoystickRadiusDefault );
                        const float32     deadzone         = bindNode.getAttributeFloat( "deadzone", kVirtualJoystickDeadzoneDefault );
                        const float32     outerDeadzone    = bindNode.getAttributeFloat( "outerDeadzone", kOuterDeadzoneDefault );
                        if ( activationButton != MouseButton::Count )
                            inputMap.bindVirtualJoystick2D( hashed_string( pAction ), activationButton, radius, deadzone, hashed_string( layer ), outerDeadzone );
                        break;
                    }
                    case BindingKind::Chord:
                    {
                        const Key modifierKey = KeyCodeUtil::fromName( bindNode.getAttributeText( "modKey" ) );
                        const Key triggerKey  = KeyCodeUtil::fromName( bindNode.getAttributeText( "trigKey" ) );
                        if ( modifierKey != Key::Unknown && triggerKey != Key::Unknown )
                            inputMap.bindChord( hashed_string( pAction ), modifierKey, triggerKey, InputMapSerializationInternal::readUserBindingTrigger( bindNode, ActionTrigger::Pressed ),
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
                            inputMap.bindShortcut( hashed_string( pAction ), key, modifierMask, InputMapSerializationInternal::readUserBindingTrigger( bindNode, ActionTrigger::Pressed ),
                                                   hashed_string( layer ) );
                        break;
                    }
                    case BindingKind::AnyKey:
                    {
                        inputMap.bindAnyKey( hashed_string( pAction ), hashed_string( layer ) );
                        break;
                    }
                    case BindingKind::SingleSlot:
                    {
                        loadUserSingleSlot( inputMap, bindNode, pAction, layer );
                        break;
                    }
                    case BindingKind::Count:
                    {
                        // 테이블은 이 이름을 아는데 여기서는 모른다. 종류를 추가하고 이 switch 를 빠뜨린 경우다.
                        SW_LOG_ERROR( "읽지 못한 바인딩 종류입니다 (kind=%#) — BindingKind 를 늘리고 loadUserBindings 를 빠뜨렸습니다.", pKindStr );
                        break;
                    }
                }
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

        InputMapSerializationInternal::loadLayersTag( *this, root );
        ensureLayer( _defaultLayerName, 0, true, false );
        InputMapSerializationInternal::loadActions( *this, root, _defaultLayerName );

        // 가상 커서가 마우스로 바꾸는 스틱 · 버튼을 액션에 따로 묶으면 한 입력이 두 입력이 된다 — 읽기를 실패로 돌려 데이터를 고치게 한다.
        if ( countVirtualCursorConflicts( absPath ) > 0 )
            return false;
        return true;
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
                      sourceName, action.c_str(), bCursorStick ? "stick" : "gamepad button", InputMapSerializationInternal::toVirtualCursorName( _virtualCursorStick ) );
        return true;
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

                InputMapSerializationInternal::writeUserBindingKind( bindNode, b );
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

        bool bCursorConflict = false;
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
            InputMapSerializationInternal::loadUserBindingNode( *this, bindNode, pAction, pKindStr, parsedKind, layer );
            InputMapSerializationInternal::readBindingModifier( *this, actionName, previousCount, bindNode );

            // 맵 읽기와 같은 검사 — 가상 커서가 마우스로 바꾸는 스틱 · 버튼을 다시 묶은 바인딩은 넣지 않고 읽기를 실패로 돌린다.
            ActionEntry* pEntry = findAction( actionName );
            if ( pEntry == nullptr )
                continue;
            for ( uint32 bindIndex = static_cast<uint32>( pEntry->_listBinding.size() ); bindIndex > previousCount; --bindIndex )
            {
                const uint32 removeIndex = bindIndex - 1;
                if ( reportVirtualCursorConflict( filePath, actionName, pEntry->_listBinding[removeIndex] ) == false )
                    continue;
                pEntry->_listBinding.erase( pEntry->_listBinding.begin() + removeIndex );
                pEntry->_listDefaultBinding.erase( pEntry->_listDefaultBinding.begin() + removeIndex );
                pEntry->_listBindingState.erase( pEntry->_listBindingState.begin() + removeIndex );
                bCursorConflict = true;
            }
        }
        return bCursorConflict == false;
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
