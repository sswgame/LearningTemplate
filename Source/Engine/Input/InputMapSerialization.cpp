#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

/**
 * @file InputMapSerialization.cpp
 * @brief InputMap XML 로드(디자인 타임 기본 바인딩)와 유저 바인딩 저장 · 로드(런타임 리매핑 영속화)입니다.
 *
 * 두 XML 포맷은 용도가 다릅니다.
 *  - loadFromResource() / saveToResource() : Resource/ 의 InputMap XML(레이어-액션-기본 바인딩 정의)을 읽어 InputMap 을 처음 구성하고,
 *    에디터 InputMap 패널이 편집한 정의를 같은 형식 · 같은 자리에 다시 씁니다.
 *  - saveUserBindings()/loadUserBindings() : 플레이어가 키를 리매핑한 결과를 저장 · 복원하는 별도의 유저 바인딩 XML 입니다.
 *  - actionTriggerFromName()/actionTriggerToName() : 두 포맷이 함께 쓰는 ActionTrigger 이름 ↔ enum 변환 표입니다.
 */

namespace sw
{
    namespace
    {
        struct InputMapSerializationInternal
        {
            struct InputMapXml
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
             * @brief 유저 바인딩 줄의 `trigger` 를 읽습니다. 없거나 모르는 이름이면 그 종류의 기본값(`fallback`)입니다.
             * @details 저장 쪽은 늘 적는다 — 빼고 저장하면 다시 읽을 때 기본값으로 돌아가, 누르는 동안 매 프레임 발화하는(Down) 축 합성 같은 결함이 돌아온다.
             */
            static ActionTrigger readUserBindingTrigger( XmlNode bindNode, ActionTrigger fallback )
            {
                const utf8* pTriggerAttr = bindNode.findAttribute( InputMapXml::kAttrTrigger );
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
             * @brief 바인딩 하나를 `<InputMap>` 의 액션 노드 아래에 `loadFromResource` 가 읽는 모양으로 씁니다.
             * @return `<InputMap>` 형식에 자리가 없는 종류(가상 조이스틱 · 단축키 · 아무 키)면 오류를 남기고 false 입니다 —
             *         빼고 쓰면 다시 읽을 때 그 바인딩이 조용히 사라진다.
             */
            [[nodiscard]] static bool writeDefinitionBinding( XmlNode actionNode, const utf8* pActionName, const ActionBinding& binding )
            {
                const utf8* pTriggerName = InputMap::actionTriggerToName( binding._trigger );
                switch ( binding._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        const InputSlot& slot     = binding._arrSlot[0];
                        XmlNode          bindNode = actionNode.appendChild( InputMapXml::kBind );
                        if ( slot._deviceKind == InputDeviceKind::Keyboard )
                        {
                            bindNode.appendAttribute( InputMapXml::kAttrSource, InputMapXml::kSourceKey );
                            bindNode.appendAttribute( InputMapXml::kAttrCode, KeyCodeUtil::toName( static_cast<Key>( slot._controlIndex ) ) );
                        }
                        else if ( slot._deviceKind == InputDeviceKind::Mouse )
                        {
                            bindNode.appendAttribute( InputMapXml::kAttrSource, InputMapXml::kSourceMouse );
                            bindNode.appendAttribute( InputMapXml::kAttrCode, MouseButtonUtil::toName( static_cast<MouseButton>( slot._controlIndex ) ) );
                        }
                        else if ( slot._deviceKind == InputDeviceKind::Gamepad )
                        {
                            bindNode.appendAttribute( InputMapXml::kAttrSource, InputMapXml::kSourceGamepad );
                            bindNode.appendAttribute( InputMapXml::kAttrCode, GamepadButtonUtil::toName( static_cast<GamepadButton>( slot._controlIndex ) ) );
                            bindNode.appendAttribute( InputMapXml::kAttrPad, static_cast<int32>( slot._deviceIndex ) );
                        }
                        else
                        {
                            SW_LOG_ERROR( "Action '%#' has a binding on a device the <InputMap> format cannot name - not saved", pActionName );
                            return false;
                        }
                        if ( pTriggerName != nullptr )
                            bindNode.appendAttribute( InputMapXml::kAttrTrigger, pTriggerName );
                        bindNode.appendAttribute( InputMapXml::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::Chord:
                    {
                        XmlNode chordNode = actionNode.appendChild( "chord" );
                        chordNode.appendAttribute( "modifier", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[0]._controlIndex ) ) );
                        chordNode.appendAttribute( "trigger", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[1]._controlIndex ) ) );
                        if ( pTriggerName != nullptr )
                            chordNode.appendAttribute( "triggerMode", pTriggerName );
                        chordNode.appendAttribute( InputMapXml::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        XmlNode axisNode = actionNode.appendChild( "axis1d" );
                        axisNode.appendAttribute( "negative", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[0]._controlIndex ) ) );
                        axisNode.appendAttribute( "positive", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[1]._controlIndex ) ) );
                        // 적지 않으면 Down 으로 읽힌다 — 그 밖의 trigger 만 적는다.
                        if ( binding._trigger != ActionTrigger::Down && pTriggerName != nullptr )
                            axisNode.appendAttribute( InputMapXml::kAttrTrigger, pTriggerName );
                        axisNode.appendAttribute( InputMapXml::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        XmlNode compNode = actionNode.appendChild( "vector2d" );
                        compNode.appendAttribute( "up", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[0]._controlIndex ) ) );
                        compNode.appendAttribute( "down", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[1]._controlIndex ) ) );
                        compNode.appendAttribute( "left", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[2]._controlIndex ) ) );
                        compNode.appendAttribute( "right", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[3]._controlIndex ) ) );
                        compNode.appendAttribute( InputMapXml::kAttrDeadzone, binding._deadzone );
                        compNode.appendAttribute( InputMapXml::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        XmlNode stickNode = actionNode.appendChild( "stick" );
                        stickNode.appendAttribute( "stick", binding._stick == GamepadStick::Right ? "Right" : "Left" );
                        stickNode.appendAttribute( InputMapXml::kAttrPad, static_cast<int32>( binding._deviceIndex ) );
                        stickNode.appendAttribute( InputMapXml::kAttrDeadzone, binding._deadzone );
                        stickNode.appendAttribute( "outerDeadzone", binding._outerDeadzone );
                        stickNode.appendAttribute( "responseExponent", binding._responseExponent );
                        stickNode.appendAttribute( InputMapXml::kAttrLayer, binding._layer.c_str() );
                        return true;
                    }
                    case BindingKind::MouseDelta2D:
                    {
                        XmlNode deltaNode = actionNode.appendChild( "mouseDelta" );
                        deltaNode.appendAttribute( "scale", binding._scale );
                        deltaNode.appendAttribute( InputMapXml::kAttrLayer, binding._layer.c_str() );
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
             * @return 정수가 아니거나 범위 밖이면 경고하고 false 입니다 — 부르는 쪽은 그 바인딩을 버립니다.
             * @details 잘라 담으면 "256" 이 0 번, "-1" 이 255 번 패드가 되고 4 번 이상은 없는 패드에 말없이 묶입니다. 패드 번호를 읽는
             *          자리 셋(리소스 스틱 · 유저 스틱 · 유저 단일 버튼)이 이 하나를 지납니다.
             */
            [[nodiscard]] static bool tryGetPadIndex( XmlNode node, uint8& outPadIndex )
            {
                int32 padIndex{ 0 };
                if ( node.tryGetAttributeIntInRange( "pad", 0, 0, static_cast<int32>( kMaxGamepadSlot ) - 1, padIndex ) == false )
                    return false;
                outPadIndex = static_cast<uint8>( padIndex );
                return true;
            }

            /**
             * @brief `modifierMask` 속성을 아는 수정 키 비트(`ModifierKey::All`) 안에서 읽습니다. 없으면 0(수정 키 없음)입니다.
             * @return 정수가 아니거나 모르는 비트를 들면 경고하고 false 입니다 — 부르는 쪽은 그 바인딩을 버립니다("257" 이 Ctrl 로 감기던 자리).
             */
            [[nodiscard]] static bool tryGetModifierMask( XmlNode node, uint8& outModifierMask )
            {
                int32 modifierMask{ 0 };
                if ( node.tryGetAttributeIntInRange( "modifierMask", 0, 0, ModifierKey::All, modifierMask ) == false )
                    return false;
                outModifierMask = static_cast<uint8>( modifierMask );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool InputMap::loadFromResource( string_view relativePath )
    {
        XmlDocument doc;
        string      absPath;
        if ( doc.loadResource( relativePath, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to load InputMap %#", relativePath );
            return false;
        }

        XmlNode root = doc.getRoot( InputMapSerializationInternal::InputMapXml::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <InputMap> in %#", absPath );
            return false;
        }

        const float32 dblClick      = root.getAttributeFloat( InputMapSerializationInternal::InputMapXml::kAttrDoubleClick, InputMapDefaults::kDoubleClickTime );
        const float32 dblDist       = root.getAttributeFloat( InputMapSerializationInternal::InputMapXml::kAttrDoubleClickDist, InputMapDefaults::kDoubleClickMaxDistance );
        const float32 holdThreshold = root.getAttributeFloat( InputMapSerializationInternal::InputMapXml::kAttrHoldThreshold, InputMapDefaults::kHoldThreshold );
        const utf8*   pDefaultLayer = root.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrDefaultLayer );

        clear();
        setDoubleClickTime( dblClick );
        setDoubleClickMaxDistance( dblDist );
        setHoldThreshold( holdThreshold );
        if ( StringUtil::isNullOrEmpty( pDefaultLayer ) == false )
            _defaultLayerName = hashed_string( pDefaultLayer );

        XmlNode layersNode = root.findChild( InputMapSerializationInternal::InputMapXml::kLayers );
        if ( layersNode.isValid() )
        {
            for ( XmlNode layerNode = layersNode.findChild( InputMapSerializationInternal::InputMapXml::kLayer ); layerNode.isValid();
                  layerNode         = layerNode.findNextSibling( InputMapSerializationInternal::InputMapXml::kLayer ) )
            {
                const utf8* pLayerName = layerNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrName );
                if ( StringUtil::isNullOrEmpty( pLayerName ) )
                    continue;
                const int32 priority   = layerNode.getAttributeInt( InputMapSerializationInternal::InputMapXml::kAttrPriority, 0 );
                const bool  enabled    = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXml::kAttrEnabled, true );
                const bool  blockLower = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXml::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXml::kAttrAlwaysOn, false );
                registerLayer( hashed_string( pLayerName ), priority, enabled, blockLower, alwaysOn );
            }
        }

        ensureLayer( _defaultLayerName, 0, true, false );

        auto loadAction = [this]( XmlNode actionNode, string_view inheritedLayer )
        {
            const utf8* pActionName = actionNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrName );
            if ( StringUtil::isNullOrEmpty( pActionName ) )
                return;

            // 값 종류를 적은 액션은 바인딩보다 먼저 만든다 — 바인딩 없이 이름만 있는 액션(에디터의 "Add Action")도 다시 읽힌다.
            const utf8* pValueTypeAttr = actionNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrValueType );
            if ( pValueTypeAttr != nullptr )
            {
                InputActionValueType valueType{ InputActionValueType::Boolean };
                if ( InputMapSerializationInternal::tryParseValueType( pValueTypeAttr, valueType ) )
                    createAction( hashed_string( pActionName ), valueType );
                else
                    SW_LOG_WARNING( "Action '%#' has an unknown valueType '%#' - the bindings decide it", pActionName, pValueTypeAttr );
            }

            hashed_string layer      = inheritedLayer.empty() ? _defaultLayerName : hashed_string( inheritedLayer );
            const utf8*   pLayerAttr = actionNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrLayer );
            if ( StringUtil::isNullOrEmpty( pLayerAttr ) == false )
                layer = hashed_string( pLayerAttr );
            ensureLayer( layer );

            // 적지 않은 trigger 는 단일 키 · 조합이 Pressed, 축 합성이 Down 이다 — 적은 trigger 는 셋 모두에 간다.
            auto        defaultTrigger        = ActionTrigger::Pressed;
            bool        bActionTriggerWritten = false;
            const utf8* pTriggerAttr          = actionNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrTrigger );
            if ( pTriggerAttr != nullptr )
            {
                const ActionTrigger parsed = actionTriggerFromName( pTriggerAttr );
                if ( parsed != ActionTrigger::Count )
                {
                    defaultTrigger        = parsed;
                    bActionTriggerWritten = true;
                }
            }

            // 연속 값 바인딩(2D 합성 · 스틱 · 마우스 이동량)은 매 프레임 읽는 값이라 Down 고정이다 — 다른 trigger 를 조용히 버리지 않고 알린다.
            const bool bHasContinuousBinding = actionNode.findChild( "vector2d" ).isValid() || actionNode.findChild( "stick" ).isValid() ||
                                               actionNode.findChild( "mouseDelta" ).isValid();
            if ( bActionTriggerWritten && defaultTrigger != ActionTrigger::Down && bHasContinuousBinding )
                SW_LOG_WARNING( "Action '%#' sets trigger '%#', but its vector2d/stick/mouseDelta bindings are continuous values read every frame - "
                                "the trigger is ignored for them",
                                pActionName, pTriggerAttr );

            // 1) <bind> 태그 파싱
            for ( XmlNode bindNode = actionNode.findChild( InputMapSerializationInternal::InputMapXml::kBind ); bindNode.isValid();
                  bindNode         = bindNode.findNextSibling( InputMapSerializationInternal::InputMapXml::kBind ) )
            {
                const utf8* pSource = bindNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrSource );
                const utf8* pCode   = bindNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrCode );
                if ( pSource == nullptr || StringUtil::isNullOrEmpty( pCode ) )
                    continue;

                ActionTrigger trigger          = defaultTrigger;
                const utf8*   pBindTriggerAttr = bindNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrTrigger );
                if ( pBindTriggerAttr != nullptr )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pBindTriggerAttr );
                    if ( parsed != ActionTrigger::Count )
                        trigger = parsed;
                }

                hashed_string bindLayer      = layer;
                const utf8*   pBindLayerAttr = bindNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrLayer );
                if ( StringUtil::isNullOrEmpty( pBindLayerAttr ) == false )
                {
                    bindLayer = hashed_string( pBindLayerAttr );
                    ensureLayer( bindLayer );
                }

                const utf8* pModifierAttr = bindNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrModifier );
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
                        continue;
                    }
                }

                if ( StringUtil::equals( pSource, InputMapSerializationInternal::InputMapXml::kSourceKey, true ) )
                {
                    const Key key = KeyCodeUtil::fromName( pCode );
                    if ( key != Key::Unknown )
                        bind( hashed_string( pActionName ), key, trigger, hashed_string( bindLayer.view() ) );
                }
                else if ( StringUtil::equals( pSource, InputMapSerializationInternal::InputMapXml::kSourceGamepad, true ) )
                {
                    if ( StringUtil::equals( pCode, "LeftStick", true ) )
                    {
                        const float32 deadzone = bindNode.getAttributeFloat( InputMapSerializationInternal::InputMapXml::kAttrDeadzone, 0.15f );
                        bindGamepadStick2D( hashed_string( pActionName ), GamepadStick::Left, deadzone, hashed_string( bindLayer.view() ) );
                    }
                    else if ( StringUtil::equals( pCode, "RightStick", true ) )
                    {
                        const float32 deadzone = bindNode.getAttributeFloat( InputMapSerializationInternal::InputMapXml::kAttrDeadzone, 0.15f );
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
                else if ( StringUtil::equals( pSource, InputMapSerializationInternal::InputMapXml::kSourceMouse, true ) )
                {
                    const MouseButton mouse = MouseButtonUtil::fromName( pCode );
                    if ( mouse != MouseButton::Count )
                        bind( hashed_string( pActionName ), mouse, trigger, hashed_string( bindLayer.view() ) );
                }
            }

            // 2) <vector2d> 태그 파싱
            for ( XmlNode compNode = actionNode.findChild( "vector2d" ); compNode.isValid(); compNode = compNode.findNextSibling( "vector2d" ) )
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
                if ( upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown )
                    bindVector2D( hashed_string( pActionName ), upKey, downKey, leftKey, rightKey, deadzone, hashed_string( compLayer.view() ) );
            }

            // 3) <axis1d> 태그 파싱
            for ( XmlNode axisNode = actionNode.findChild( "axis1d" ); axisNode.isValid(); axisNode = axisNode.findNextSibling( "axis1d" ) )
            {
                const Key     posKey         = KeyCodeUtil::fromName( axisNode.getAttributeText( "positive" ) );
                const Key     negativeKey    = KeyCodeUtil::fromName( axisNode.getAttributeText( "negative" ) );
                hashed_string axisLayer      = layer;
                const utf8*   pAxisLayerAttr = axisNode.findAttribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pAxisLayerAttr ) == false )
                {
                    axisLayer = hashed_string( pAxisLayerAttr );
                    ensureLayer( axisLayer );
                }
                // 축 값은 trigger 와 관계없이 누르는 동안 읽힌다 — trigger 는 액션 발화(누를 때마다 한 칸 등)만 정한다.
                ActionTrigger axisTrigger      = bActionTriggerWritten ? defaultTrigger : ActionTrigger::Down;
                const utf8*   pAxisTriggerAttr = axisNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrTrigger );
                if ( pAxisTriggerAttr != nullptr )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pAxisTriggerAttr );
                    if ( parsed != ActionTrigger::Count )
                        axisTrigger = parsed;
                }
                if ( posKey != Key::Unknown && negativeKey != Key::Unknown )
                    bindAxis1DComposite( hashed_string( pActionName ), negativeKey, posKey, hashed_string( axisLayer.view() ), axisTrigger );
            }

            // 4) <stick> 태그 파싱
            for ( XmlNode stickNode = actionNode.findChild( "stick" ); stickNode.isValid(); stickNode = stickNode.findNextSibling( "stick" ) )
            {
                uint8 padIndex{ 0 };
                if ( InputMapSerializationInternal::tryGetPadIndex( stickNode, padIndex ) == false )
                    continue; // 경고했다 — 엉뚱한 패드에 묶지 않고 이 바인딩을 버린다
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
                bindGamepadStick2D( hashed_string( pActionName ), stick, deadzone, hashed_string( stickLayer.view() ), padIndex, outerDeadzone, responseExponent );
            }

            // 5) <chord> 태그 파싱
            for ( XmlNode chordNode = actionNode.findChild( "chord" ); chordNode.isValid(); chordNode = chordNode.findNextSibling( "chord" ) )
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
                if ( modifierKey != Key::Unknown && triggerKey != Key::Unknown )
                    bindChord( hashed_string( pActionName ), modifierKey, triggerKey, trigger, hashed_string( chordLayer.view() ) );
            }

            // 6) <mouseDelta> 태그 파싱 — 마우스 이동량(시점). scale 은 감도 배율이다(1 = 픽셀 그대로).
            for ( XmlNode deltaNode = actionNode.findChild( "mouseDelta" ); deltaNode.isValid(); deltaNode = deltaNode.findNextSibling( "mouseDelta" ) )
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
        };

        for ( XmlNode layerNode = root.findChild( InputMapSerializationInternal::InputMapXml::kLayer ); layerNode.isValid(); layerNode = layerNode.findNextSibling( InputMapSerializationInternal::InputMapXml::kLayer ) )
        {
            const utf8* pLayerName = layerNode.findAttribute( InputMapSerializationInternal::InputMapXml::kAttrName );
            if ( StringUtil::isNullOrEmpty( pLayerName ) )
                continue;
            if ( hasLayer( hashed_string( pLayerName ) ) == false )
            {
                const int32 priority   = layerNode.getAttributeInt( InputMapSerializationInternal::InputMapXml::kAttrPriority, 0 );
                const bool  enabled    = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXml::kAttrEnabled, true );
                const bool  blockLower = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXml::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.getAttributeBool( InputMapSerializationInternal::InputMapXml::kAttrAlwaysOn, false );
                registerLayer( hashed_string( pLayerName ), priority, enabled, blockLower, alwaysOn );
            }
            for ( XmlNode actionNode = layerNode.findChild( InputMapSerializationInternal::InputMapXml::kAction ); actionNode.isValid();
                  actionNode         = actionNode.findNextSibling( InputMapSerializationInternal::InputMapXml::kAction ) )
            {
                loadAction( actionNode, pLayerName );
            }
        }

        for ( XmlNode actionNode = root.findChild( InputMapSerializationInternal::InputMapXml::kAction ); actionNode.isValid();
              actionNode         = actionNode.findNextSibling( InputMapSerializationInternal::InputMapXml::kAction ) )
        {
            loadAction( actionNode, _defaultLayerName.view() );
        }

        return true;
    }

    bool InputMap::saveToResource( string_view relativePath ) const
    {
        if ( relativePath.empty() )
            return false;

        using InputMapXml = InputMapSerializationInternal::InputMapXml;
        XmlDocument doc;
        XmlNode     root = doc.appendRoot( InputMapXml::kRoot );
        root.appendAttribute( InputMapXml::kAttrDefaultLayer, _defaultLayerName.c_str() );
        root.appendAttribute( InputMapXml::kAttrDoubleClick, _doubleClickTime );
        root.appendAttribute( InputMapXml::kAttrDoubleClickDist, _doubleClickMaxDistance );
        root.appendAttribute( InputMapXml::kAttrHoldThreshold, _holdThreshold );

        XmlNode layersNode = root.appendChild( InputMapXml::kLayers );
        for ( const hashed_string& layerName : _listLayerName )
        {
            const LayerDefinition* pLayer = findLayer( layerName );
            if ( pLayer == nullptr )
                continue;
            XmlNode layerNode = layersNode.appendChild( InputMapXml::kLayer );
            layerNode.appendAttribute( InputMapXml::kAttrName, layerName.c_str() );
            layerNode.appendAttribute( InputMapXml::kAttrPriority, pLayer->_priority );
            layerNode.appendAttribute( InputMapXml::kAttrEnabled, pLayer->_bEnabled != SW_FALSE );
            layerNode.appendAttribute( InputMapXml::kAttrBlockLower, pLayer->_bBlockLower != SW_FALSE );
            layerNode.appendAttribute( InputMapXml::kAttrAlwaysOn, pLayer->_bAlwaysOn != SW_FALSE );
        }

        // 액션은 등록 순서로 쓴다 — 다시 읽은 맵의 액션 순서가 같다. 바인딩마다 레이어 · 트리거를 적어 액션 기본값에 기대지 않는다.
        bool bAllWritten = true;
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry == nullptr )
                continue;
            XmlNode actionNode = root.appendChild( InputMapXml::kAction );
            actionNode.appendAttribute( InputMapXml::kAttrName, actionName.c_str() );
            actionNode.appendAttribute( InputMapXml::kAttrValueType, InputMapSerializationInternal::toValueTypeName( pEntry->_valueType ) );
            for ( const ActionBinding& binding : pEntry->_listBinding )
                bAllWritten = InputMapSerializationInternal::writeDefinitionBinding( actionNode, actionName.c_str(), binding ) && bAllWritten;
        }
        if ( bAllWritten == false )
            return false;

        const string absPath = ResourceUtil::getWritePath( relativePath );
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

        XmlDocument doc;
        XmlNode     root = doc.appendRoot( "UserBindings" );
        for ( const auto& [actionName, actIndex] : _mapAction )
        {
            const ActionEntry& entry = _listActionEntry[actIndex];
            for ( const ActionBinding& b : entry._listBinding )
            {
                XmlNode bindNode = root.appendChild( "bind" );
                bindNode.appendAttribute( "action", actionName.c_str() );
                bindNode.appendAttribute( "layer", b._layer.c_str() );
                // 이름은 표에서 온다 — 쓰는 쪽과 읽는 쪽이 리터럴을 따로 들면 한쪽만 고쳐 파일이 조용히 왕복하지 않게 된다.
                bindNode.appendAttribute( "kind", BindingKinds::toName( b._kind ) );
                // 발화 규칙도 바인딩의 일부다 — 빼면 다시 읽을 때 종류의 기본값으로 돌아간다(`readUserBindingTrigger`).
                const utf8* pTriggerName = actionTriggerToName( b._trigger );
                if ( pTriggerName != nullptr )
                    bindNode.appendAttribute( "trigger", pTriggerName );

                // 종류를 늘리고 여기를 빠뜨리면 그 바인딩이 특성 하나 없이 저장돼 **조용히 사라진다** — 그래서 default 가 오류를 남긴다.
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
                        const Key negativeKey = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key posKey      = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        bindNode.appendAttribute( "negKey", KeyCodeUtil::toName( negativeKey ) );
                        bindNode.appendAttribute( "posKey", KeyCodeUtil::toName( posKey ) );
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
                        break;
                    }
                    case BindingKind::MouseDelta2D:
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
                        break; // 이름 말고 적을 것이 없다.
                    }
                    case BindingKind::Count:
                    {
                        // 종류를 늘리고 이 switch 를 빠뜨렸다. 이름은 표에서 왔으므로 `kind` 는
                        // 제대로 적혔지만 **딸린 특성이 하나도 없어** 다시 읽을 수 없는 줄이 된다.
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

        XmlDocument doc;
        if ( doc.loadPath( filePath ) == false )
            return false;

        XmlNode root = doc.getRoot( "UserBindings" );
        if ( root.isValid() == false )
            return false;

        for ( XmlNode bindNode = root.findChild( "bind" ); bindNode.isValid(); bindNode = bindNode.findNextSibling( "bind" ) )
        {
            const utf8*       pAction   = bindNode.findAttribute( "action" );
            const utf8*       pKindStr  = bindNode.findAttribute( "kind" );
            const utf8*       pLayerStr = bindNode.findAttribute( "layer" );
            const string_view layer     = ( StringUtil::isNullOrEmpty( pLayerStr ) == false ) ? string_view( pLayerStr ) : string_view{};

            if ( StringUtil::isNullOrEmpty( pAction ) )
                continue;

            // 이름 → 종류는 표가 답한다. 종류가 없거나 모르는 이름이면 그 바인딩은 읽지 않는다 — 저장 쪽은 늘 `kind` 를 쓴다.
            const BindingKind parsedKind = ( pKindStr != nullptr ) ? BindingKinds::fromName( pKindStr ) : BindingKind::Count;
            if ( parsedKind == BindingKind::Count )
            {
                SW_LOG_ERROR( "바인딩 종류가 없거나 모르는 이름입니다 (action=%#, kind=%#) — 이 바인딩은 건너뜁니다.", pAction,
                              ( pKindStr != nullptr ) ? pKindStr : "" );
                continue;
            }

            switch ( parsedKind )
            {
                case BindingKind::Axis1DComposite:
                {
                    const Key negativeKey = KeyCodeUtil::fromName( bindNode.getAttributeText( "negKey" ) );
                    const Key posKey      = KeyCodeUtil::fromName( bindNode.getAttributeText( "posKey" ) );
                    if ( negativeKey != Key::Unknown && posKey != Key::Unknown )
                        bindAxis1DComposite( hashed_string( pAction ), negativeKey, posKey, hashed_string( layer ),
                                             InputMapSerializationInternal::readUserBindingTrigger( bindNode, ActionTrigger::Down ) );
                    break;
                }
                case BindingKind::Vector2DComposite:
                {
                    const Key     upKey    = KeyCodeUtil::fromName( bindNode.getAttributeText( "up" ) );
                    const Key     downKey  = KeyCodeUtil::fromName( bindNode.getAttributeText( "down" ) );
                    const Key     leftKey  = KeyCodeUtil::fromName( bindNode.getAttributeText( "left" ) );
                    const Key     rightKey = KeyCodeUtil::fromName( bindNode.getAttributeText( "right" ) );
                    const float32 deadzone = bindNode.getAttributeFloat( "deadzone", 0.0f );
                    if ( upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown )
                        bindVector2D( hashed_string( pAction ), upKey, downKey, leftKey, rightKey, deadzone, hashed_string( layer ) );
                    break;
                }
                case BindingKind::GamepadStick2D:
                {
                    uint8 pad{ 0 };
                    if ( InputMapSerializationInternal::tryGetPadIndex( bindNode, pad ) == false )
                        break; // 경고했다 — 이 바인딩을 버린다
                    const utf8*        pStickStr     = bindNode.findAttribute( "stick" );
                    const GamepadStick stick         = StringUtil::equals( pStickStr, "Right", true ) ? GamepadStick::Right : GamepadStick::Left;
                    const float32      deadzone      = bindNode.getAttributeFloat( "deadzone", 0.15f );
                    const float32      outerDeadzone = bindNode.getAttributeFloat( "outerDeadzone", 1.0f );
                    const float32      exp           = bindNode.getAttributeFloat( "exponent", 1.0f );
                    bindGamepadStick2D( hashed_string( pAction ), stick, deadzone, hashed_string( layer ), pad, outerDeadzone, exp );
                    break;
                }
                case BindingKind::MouseDelta2D:
                {
                    const float32 scale = bindNode.getAttributeFloat( "scale", 1.0f );
                    bindMouseDelta( hashed_string( pAction ), scale, hashed_string( layer ) );
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
                        break; // 경고했다 — 다른 수정 키 조합으로 묶지 않고 이 바인딩을 버린다
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
                    // 저장 쪽과 같은 모양만 읽는다: source="key" key=… · source="mouse" button=… · source="gamepad" code=… pad=…
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
                    // 표는 이름을 알았는데 여기가 모른다. 종류를 늘리고 이 switch 를 빠뜨린 것이다.
                    SW_LOG_ERROR( "읽지 못한 바인딩 종류입니다 (kind=%#) — BindingKind 를 늘리고 loadUserBindings 를 빠뜨렸습니다.", pKindStr );
                    break;
                }
            }
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
