#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapXMLUtil.h"
#include "Engine/Input/Map/InputRemapSerializer.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualCursor.h"
#include "Engine/Serialization/XML/XMLDocument.h"

/**
 * @file InputMapSerialization.cpp
 * @brief InputMap XML 정의(디자인 타임 기본 바인딩)의 로드와 저장, 두 XML 형식이 함께 쓰는 칸 읽기(`InputMapXMLUtil`)를 구현합니다.
 *
 * 플레이어 리매핑 파일(`<UserBindings>`)은 `InputRemapSerializer.cpp` 가 맡는다.
 *  - `loadFromAsset()`, `saveToAsset()`: Content/ 의 InputMap XML(레이어, 액션, 기본 바인딩 정의)을 읽어 InputMap 을 처음 구성합니다.
 *    에디터 InputMap 패널에서 편집한 정의는 같은 형식으로 같은 위치에 다시 씁니다.
 *  - `appendFromAsset()`: 같은 형식의 다른 파일을 이미 읽은 맵에 더합니다(확장 모듈 · 게임 모드). 맵 전체 속성은 적지 않고, 있는 액션 · 레이어를 다시 정의하지 않는다.
 *
 * 정의 형식(`<InputMap>`) 요약 — 읽는 곳은 이 파일 하나다.
 *  - 루트 속성(맵 전체, 기본 맵만): `defaultLayer`, `doubleClickTime`(초), `doubleClickMaxDistance`(픽셀), `holdThreshold`(초),
 *    `dragThreshold`(픽셀, 클릭과 끌기를 가르는 거리 — `Click` · `Drag` 트리거), `virtualCursor`(Off · LeftStick · RightStick).
 *  - `<layers><layer name priority enabled blockLower alwaysOn/></layers>` 와 `<layer name=…>` 묶음 안의 `<action>`.
 *  - `<action name layer trigger valueType>` 아래 `<bind source="key|mouse|gamepad" code modifier=…>` 등. `modifier` 가 있는 키 바인딩은 단축키이고
 *    수정 키가 정확히 그 조합이어야 발화한다(더 눌린 수정 키가 있으면 발화하지 않는다).
 *  - `trigger`: Pressed · Released · Down · DoubleClicked · HoldThreshold · HoldAndRelease · Tap · Pulse · DoubleTap · Repeat · MultiTap · Click · Drag.
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
        using InputMapXML   = InputMapSerializationInternal::InputMapXML;
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
        using Internal = InputMapSerializationInternal;

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
        using InputMapXML              = InputMapSerializationInternal::InputMapXML;
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
        if ( bAppend && InputMapSerializationInternal::hasMapWideAttribute( root, absPath ) )
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
        using InputMapXML = InputMapSerializationInternal::InputMapXML;
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
        if ( pVirtualCursor != nullptr && InputMapSerializationInternal::tryParseVirtualCursor( pVirtualCursor, virtualCursor ) == false )
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
        using InputMapXML  = InputMapSerializationInternal::InputMapXML;
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
                InputMapSerializationInternal::LayerSettings settings;
                InputMapSerializationInternal::readLayerSettings( layerNode, settings, inoutbValid );
                registerLayer( hashed_string( pLayerName ), settings._priority, settings._bEnabled, settings._bBlockLower, settings._bAlwaysOn );
            }
        }

        ensureLayer( _defaultLayerName, 0, true, false );
    }

    void InputMap::readActionDefinitions( const XMLNode& root, string_view absPath, bool bAppend, vector<hashed_string>& outListLoadedAction, bool& inoutbValid )
    {
        using InputMapXML = InputMapSerializationInternal::InputMapXML;
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
                InputMapSerializationInternal::LayerSettings settings;
                InputMapSerializationInternal::readLayerSettings( layerNode, settings, inoutbValid );
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
                      sourceName, action.c_str(), bCursorStick ? "stick" : "gamepad button", InputMapSerializationInternal::toVirtualCursorName( _virtualCursorStick ) );
        return true;
    }

    bool InputMap::saveToAsset( const AbsolutePath& absPath ) const
    {
        if ( absPath.str().empty() )
            return false;

        using InputMapXML = InputMapSerializationInternal::InputMapXML;
        XMLDocument doc;
        XMLNode     root = doc.appendRoot( InputMapXML::kRoot );
        root.appendAttribute( InputMapXML::kAttrDefaultLayer, _defaultLayerName.c_str() );
        root.appendAttribute( InputMapXML::kAttrDoubleClick, _doubleClickTime );
        root.appendAttribute( InputMapXML::kAttrDoubleClickDist, _doubleClickMaxDistance );
        root.appendAttribute( InputMapXML::kAttrHoldThreshold, _holdThreshold );
        root.appendAttribute( InputMapXML::kAttrDragThreshold, _dragThreshold );
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
        for ( const InputMapSerializationInternal::TriggerNameEntry& entry : InputMapSerializationInternal::kArrTriggerNames )
        {
            if ( StringUtil::equals( name, entry._pName ) )
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
