#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/KeyCodes.h"
#include "Engine/Utility/Xml/XmlDocument.h"

/**
 * @file ActionMapSerialization.cpp
 * @brief InputMap XML 로드(디자인 타임 기본 바인딩)와 유저 바인딩 저장/로드(런타임 리매핑 영속화)를 담당합니다.
 *
 * 초심자 가이드: 두 XML 포맷은 서로 다른 용도입니다.
 *  - loadFromResource() : Resource/의 InputMap XML(레이어-액션-기본 바인딩 정의)을 읽어 ActionMap을 처음 구성합니다.
 *  - saveUserBindings()/loadUserBindings() : 플레이어가 키를 리매핑한 결과를 저장/복원하는 별도의 유저 바인딩 XML입니다.
 *  - actionTriggerFromName()/actionTriggerToName() : 두 포맷 모두에서 쓰는 ActionTrigger 이름-enum 변환 표입니다.
 */

namespace sw
{
    namespace
    {
        struct ActionMapSerializationInternal
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ActionMap::loadFromResource( string_view relativePath )
    {
        XmlDocument doc;
        string      absPath;
        if ( doc.loadResource( relativePath, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to load InputMap %#", relativePath );
            return false;
        }

        XmlNode root = doc.root( ActionMapSerializationInternal::InputMapXml::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <InputMap> in %#", absPath );
            return false;
        }

        const float32 dblClick  = root.attributeFloat( ActionMapSerializationInternal::InputMapXml::kAttrDoubleClick, ActionMapDefaults::kDoubleClickTime );
        const float32 dblDist   = root.attributeFloat( ActionMapSerializationInternal::InputMapXml::kAttrDoubleClickDist, ActionMapDefaults::kDoubleClickMaxDistance );
        const float32 holdThr   = root.attributeFloat( ActionMapSerializationInternal::InputMapXml::kAttrHoldThreshold, ActionMapDefaults::kHoldThreshold );
        const utf8*   pDefLayer = root.attribute( ActionMapSerializationInternal::InputMapXml::kAttrDefaultLayer );

        clear();
        setDoubleClickTime( dblClick );
        setDoubleClickMaxDistance( dblDist );
        setHoldThreshold( holdThr );
        if ( StringUtil::isNullOrEmpty( pDefLayer ) == false )
            _defaultLayerName = hashed_string( pDefLayer );

        XmlNode layersNode = root.child( ActionMapSerializationInternal::InputMapXml::kLayers );
        if ( layersNode.isValid() )
        {
            for ( XmlNode layerNode = layersNode.child( ActionMapSerializationInternal::InputMapXml::kLayer ); layerNode.isValid();
                  layerNode         = layerNode.next( ActionMapSerializationInternal::InputMapXml::kLayer ) )
            {
                const utf8* pLayerName = layerNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrName );
                if ( StringUtil::isNullOrEmpty( pLayerName ) )
                    continue;
                const int32 priority   = layerNode.attributeInt( ActionMapSerializationInternal::InputMapXml::kAttrPriority, 0 );
                const bool  enabled    = layerNode.attributeBool( ActionMapSerializationInternal::InputMapXml::kAttrEnabled, true );
                const bool  blockLower = layerNode.attributeBool( ActionMapSerializationInternal::InputMapXml::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.attributeBool( ActionMapSerializationInternal::InputMapXml::kAttrAlwaysOn, false );
                registerLayer( pLayerName, priority, enabled, blockLower, alwaysOn );
            }
        }

        ensureLayer( _defaultLayerName, 0, true, false );

        auto loadAction = [this]( XmlNode actionNode, string_view inheritedLayer )
        {
            const utf8* pActionName = actionNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrName );
            if ( StringUtil::isNullOrEmpty( pActionName ) )
                return;

            hashed_string layer      = inheritedLayer.empty() ? _defaultLayerName : hashed_string( inheritedLayer );
            const utf8*   pLayerAttr = actionNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrLayer );
            if ( StringUtil::isNullOrEmpty( pLayerAttr ) == false )
                layer = hashed_string( pLayerAttr );
            ensureLayer( layer );

            auto        defaultTrigger = ActionTrigger::Pressed;
            const utf8* pTriggerAttr   = actionNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrTrigger );
            if ( pTriggerAttr != nullptr )
            {
                const ActionTrigger parsed = actionTriggerFromName( pTriggerAttr );
                if ( parsed != ActionTrigger::Count )
                    defaultTrigger = parsed;
            }

            // 1) <bind> 태그 파싱
            for ( XmlNode bindNode = actionNode.child( ActionMapSerializationInternal::InputMapXml::kBind ); bindNode.isValid();
                  bindNode         = bindNode.next( ActionMapSerializationInternal::InputMapXml::kBind ) )
            {
                const utf8* pSource = bindNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrSource );
                const utf8* pCode   = bindNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrCode );
                if ( pSource == nullptr || StringUtil::isNullOrEmpty( pCode ) )
                    continue;

                ActionTrigger trigger          = defaultTrigger;
                const utf8*   pBindTriggerAttr = bindNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrTrigger );
                if ( pBindTriggerAttr != nullptr )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pBindTriggerAttr );
                    if ( parsed != ActionTrigger::Count )
                        trigger = parsed;
                }

                hashed_string bindLayer      = layer;
                const utf8*   pBindLayerAttr = bindNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrLayer );
                if ( StringUtil::isNullOrEmpty( pBindLayerAttr ) == false )
                {
                    bindLayer = hashed_string( pBindLayerAttr );
                    ensureLayer( bindLayer );
                }

                const utf8* pModifierAttr = bindNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrModifier );
                if ( StringUtil::isNullOrEmpty( pModifierAttr ) == false )
                {
                    Key modKey = KeyCodes::fromName( pModifierAttr );
                    if ( modKey == Key::Unknown )
                    {
                        if ( StringUtil::equals( pModifierAttr, "Ctrl", true ) || StringUtil::equals( pModifierAttr, "Control", true ) )
                            modKey = Key::LeftControl;
                        else if ( StringUtil::equals( pModifierAttr, "Shift", true ) )
                            modKey = Key::LeftShift;
                        else if ( StringUtil::equals( pModifierAttr, "Alt", true ) )
                            modKey = Key::LeftAlt;
                    }
                    const Key triggerKey = KeyCodes::fromName( pCode );
                    if ( modKey != Key::Unknown && triggerKey != Key::Unknown )
                    {
                        bindChord( pActionName, modKey, triggerKey, trigger, bindLayer.view() );
                        continue;
                    }
                }

                if ( StringUtil::equals( pSource, ActionMapSerializationInternal::InputMapXml::kSourceKey, true ) )
                {
                    const Key key = KeyCodes::fromName( pCode );
                    if ( key != Key::Unknown )
                        bind( pActionName, key, trigger, bindLayer.view() );
                }
                else if ( StringUtil::equals( pSource, ActionMapSerializationInternal::InputMapXml::kSourceGamepad, true ) )
                {
                    if ( StringUtil::equals( pCode, "LeftStick", true ) )
                    {
                        const float32 deadzone = bindNode.attributeFloat( ActionMapSerializationInternal::InputMapXml::kAttrDeadzone, 0.15f );
                        bindGamepadStick2D( pActionName, GamepadStick::Left, deadzone, bindLayer.view() );
                    }
                    else if ( StringUtil::equals( pCode, "RightStick", true ) )
                    {
                        const float32 deadzone = bindNode.attributeFloat( ActionMapSerializationInternal::InputMapXml::kAttrDeadzone, 0.15f );
                        bindGamepadStick2D( pActionName, GamepadStick::Right, deadzone, bindLayer.view() );
                    }
                    else
                    {
                        const GamepadButton button = GamepadButtons::fromName( pCode );
                        if ( button != GamepadButton::Count )
                            bind( pActionName, button, trigger, bindLayer.view() );
                    }
                }
                else if ( StringUtil::equals( pSource, ActionMapSerializationInternal::InputMapXml::kSourceMouse, true ) )
                {
                    const MouseButton mouse = MouseButtons::fromName( pCode );
                    if ( mouse != MouseButton::Count )
                        bind( pActionName, mouse, trigger, bindLayer.view() );
                }
            }

            // 2) <vector2d> 태그 파싱
            for ( XmlNode compNode = actionNode.child( "vector2d" ); compNode.isValid(); compNode = compNode.next( "vector2d" ) )
            {
                const Key     upKey          = KeyCodes::fromName( compNode.attribute( "up" ) );
                const Key     downKey        = KeyCodes::fromName( compNode.attribute( "down" ) );
                const Key     leftKey        = KeyCodes::fromName( compNode.attribute( "left" ) );
                const Key     rightKey       = KeyCodes::fromName( compNode.attribute( "right" ) );
                const float32 deadzone       = compNode.attributeFloat( "deadzone", 0.0f );
                hashed_string compLayer      = layer;
                const utf8*   pCompLayerAttr = compNode.attribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pCompLayerAttr ) == false )
                {
                    compLayer = hashed_string( pCompLayerAttr );
                    ensureLayer( compLayer );
                }
                if ( upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown )
                    bindVector2D( pActionName, upKey, downKey, leftKey, rightKey, deadzone, compLayer.view() );
            }

            // 3) <axis1d> 태그 파싱
            for ( XmlNode axisNode = actionNode.child( "axis1d" ); axisNode.isValid(); axisNode = axisNode.next( "axis1d" ) )
            {
                const Key     posKey         = KeyCodes::fromName( axisNode.attribute( "positive" ) );
                const Key     negKey         = KeyCodes::fromName( axisNode.attribute( "negative" ) );
                hashed_string axisLayer      = layer;
                const utf8*   pAxisLayerAttr = axisNode.attribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pAxisLayerAttr ) == false )
                {
                    axisLayer = hashed_string( pAxisLayerAttr );
                    ensureLayer( axisLayer );
                }
                if ( posKey != Key::Unknown && negKey != Key::Unknown )
                    bindAxis1DComposite( pActionName, negKey, posKey, axisLayer.view() );
            }

            // 4) <stick> 태그 파싱
            for ( XmlNode stickNode = actionNode.child( "stick" ); stickNode.isValid(); stickNode = stickNode.next( "stick" ) )
            {
                const utf8*        pStickName      = stickNode.attribute( "stick" );
                const GamepadStick stick           = ( pStickName != nullptr && StringUtil::equals( pStickName, "Right", true ) ) ? GamepadStick::Right : GamepadStick::Left;
                const float32      deadzone        = stickNode.attributeFloat( "deadzone", 0.15f );
                hashed_string      stickLayer      = layer;
                const utf8*        pStickLayerAttr = stickNode.attribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pStickLayerAttr ) == false )
                {
                    stickLayer = hashed_string( pStickLayerAttr );
                    ensureLayer( stickLayer );
                }
                const uint8   padIndex         = static_cast<uint8>( stickNode.attributeInt( "pad", 0 ) );
                const float32 outerDeadzone    = stickNode.attributeFloat( "outerDeadzone", 1.0f );
                const float32 responseExponent = stickNode.attributeFloat( "responseExponent", 1.0f );
                bindGamepadStick2D( pActionName, stick, deadzone, stickLayer.view(), padIndex, outerDeadzone, responseExponent );
            }

            // 5) <chord> 태그 파싱
            for ( XmlNode chordNode = actionNode.child( "chord" ); chordNode.isValid(); chordNode = chordNode.next( "chord" ) )
            {
                const Key     modKey    = KeyCodes::fromName( chordNode.attribute( "modifier" ) );
                const Key     trigKey   = KeyCodes::fromName( chordNode.attribute( "trigger" ) );
                ActionTrigger trig      = defaultTrigger;
                const utf8*   pTrigAttr = chordNode.attribute( "triggerMode" );
                if ( StringUtil::isNullOrEmpty( pTrigAttr ) == false )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pTrigAttr );
                    if ( parsed != ActionTrigger::Count )
                        trig = parsed;
                }
                hashed_string chordLayer      = layer;
                const utf8*   pChordLayerAttr = chordNode.attribute( "layer" );
                if ( StringUtil::isNullOrEmpty( pChordLayerAttr ) == false )
                {
                    chordLayer = hashed_string( pChordLayerAttr );
                    ensureLayer( chordLayer );
                }
                if ( modKey != Key::Unknown && trigKey != Key::Unknown )
                    bindChord( pActionName, modKey, trigKey, trig, chordLayer.view() );
            }
        };

        for ( XmlNode layerNode = root.child( ActionMapSerializationInternal::InputMapXml::kLayer ); layerNode.isValid(); layerNode = layerNode.next( ActionMapSerializationInternal::InputMapXml::kLayer ) )
        {
            const utf8* pLayerName = layerNode.attribute( ActionMapSerializationInternal::InputMapXml::kAttrName );
            if ( StringUtil::isNullOrEmpty( pLayerName ) )
                continue;
            if ( hasLayer( pLayerName ) == false )
            {
                const int32 priority   = layerNode.attributeInt( ActionMapSerializationInternal::InputMapXml::kAttrPriority, 0 );
                const bool  enabled    = layerNode.attributeBool( ActionMapSerializationInternal::InputMapXml::kAttrEnabled, true );
                const bool  blockLower = layerNode.attributeBool( ActionMapSerializationInternal::InputMapXml::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.attributeBool( ActionMapSerializationInternal::InputMapXml::kAttrAlwaysOn, false );
                registerLayer( pLayerName, priority, enabled, blockLower, alwaysOn );
            }
            for ( XmlNode actionNode = layerNode.child( ActionMapSerializationInternal::InputMapXml::kAction ); actionNode.isValid();
                  actionNode         = actionNode.next( ActionMapSerializationInternal::InputMapXml::kAction ) )
            {
                loadAction( actionNode, pLayerName );
            }
        }

        for ( XmlNode actionNode = root.child( ActionMapSerializationInternal::InputMapXml::kAction ); actionNode.isValid();
              actionNode         = actionNode.next( ActionMapSerializationInternal::InputMapXml::kAction ) )
        {
            loadAction( actionNode, _defaultLayerName.view() );
        }

        return true;
    }

    bool ActionMap::saveUserBindings( string_view filePath ) const
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
                // 이름은 표에서 온다 — 예전에는 종류마다 리터럴을 적었고 읽는 쪽에 같은 리터럴이 따로
                // 있어서, 한쪽만 고치면 파일이 조용히 왕복하지 않게 됐다.
                bindNode.appendAttribute( "kind", BindingKinds::toName( b._kind ) );

                // 예전에는 `default: break` 라, 종류를 늘리고 여기를 빠뜨리면 그 바인딩이 특성 하나
                // 없이 저장돼 **조용히 사라졌다.** 이제 그 자리가 소리를 낸다(아래 default 참고).
                switch ( b._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
                        {
                            const Key key = static_cast<Key>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttribute( "source", "key" );
                            bindNode.appendAttribute( "key", KeyCodes::toName( key ) );
                        }
                        else if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Mouse )
                        {
                            const MouseButton btn = static_cast<MouseButton>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttribute( "source", "mouse" );
                            bindNode.appendAttribute( "button", MouseButtons::toName( btn ) );
                        }
                        else if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
                        {
                            const GamepadButton btn = static_cast<GamepadButton>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttribute( "source", "gamepad" );
                            bindNode.appendAttribute( "code", GamepadButtons::toName( btn ) );
                            bindNode.appendAttribute( "pad", static_cast<int32>( b._arrSlot[0]._deviceIndex ) );
                        }
                        break;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        const Key negKey = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key posKey = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        bindNode.appendAttribute( "negKey", KeyCodes::toName( negKey ) );
                        bindNode.appendAttribute( "posKey", KeyCodes::toName( posKey ) );
                        break;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        const Key upKey    = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key downKey  = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        const Key leftKey  = static_cast<Key>( b._arrSlot[2]._controlIndex );
                        const Key rightKey = static_cast<Key>( b._arrSlot[3]._controlIndex );
                        bindNode.appendAttribute( "up", KeyCodes::toName( upKey ) );
                        bindNode.appendAttribute( "down", KeyCodes::toName( downKey ) );
                        bindNode.appendAttribute( "left", KeyCodes::toName( leftKey ) );
                        bindNode.appendAttribute( "right", KeyCodes::toName( rightKey ) );
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
                        bindNode.appendAttribute( "button", MouseButtons::toName( activationButton ) );
                        bindNode.appendAttribute( "radius", b._scale );
                        bindNode.appendAttribute( "deadzone", b._deadzone );
                        bindNode.appendAttribute( "outerDeadzone", b._outerDeadzone );
                        break;
                    }
                    case BindingKind::Chord:
                    {
                        const Key modKey  = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key trigKey = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        bindNode.appendAttribute( "modKey", KeyCodes::toName( modKey ) );
                        bindNode.appendAttribute( "trigKey", KeyCodes::toName( trigKey ) );
                        break;
                    }
                    case BindingKind::Shortcut:
                    {
                        const Key key = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        bindNode.appendAttribute( "key", KeyCodes::toName( key ) );
                        bindNode.appendAttribute( "modifierMask", static_cast<int32>( b._modifierMask ) );
                        break;
                    }
                    case BindingKind::AnyKey:
                    {
                        break; // 이름 말고 적을 것이 없다.
                    }
                    case BindingKind::Count:
                    default:
                    {
                        // 종류를 늘리고 이 switch 를 빠뜨렸다. 이름은 표에서 왔으므로 `kind` 는
                        // 제대로 적혔지만 **딸린 특성이 하나도 없어** 다시 읽을 수 없는 줄이 된다.
                        // 예전에는 `default: break` 라 그 사실조차 남지 않았다.
                        SW_LOG_ERROR( "저장하지 못한 바인딩 종류입니다 (kind=%#) — BindingKind 를 늘리고 saveUserBindings 를 빠뜨렸습니다.",
                                      BindingKinds::toName( b._kind ) );
                        break;
                    }
                }
            }
        }
        return doc.saveFile( filePath );
    }

    bool ActionMap::loadUserBindings( string_view filePath )
    {
        if ( filePath.empty() )
            return false;

        XmlDocument doc;
        if ( doc.loadPath( filePath ) == false )
            return false;

        XmlNode root = doc.root( "UserBindings" );
        if ( root.isValid() == false )
            return false;

        for ( XmlNode bindNode = root.child( "bind" ); bindNode.isValid(); bindNode = bindNode.next( "bind" ) )
        {
            const utf8*       pAction   = bindNode.attribute( "action" );
            const utf8*       pKindStr  = bindNode.attribute( "kind" );
            const utf8*       pLayerStr = bindNode.attribute( "layer" );
            const string_view layer     = ( StringUtil::isNullOrEmpty( pLayerStr ) == false ) ? string_view( pLayerStr ) : string_view{};

            if ( StringUtil::isNullOrEmpty( pAction ) )
                continue;

            // 이름 → 종류는 표가 답한다. 예전에는 여기가 문자열 if/else 사슬이라 저장 쪽 리터럴과
            // 짝이 맞는지 아무도 지켜 주지 않았고, 모르는 이름은 조용히 아래 레거시 경로로 떨어졌다.
            const BindingKind parsedKind = ( pKindStr != nullptr ) ? BindingKinds::fromName( pKindStr ) : BindingKind::Count;
            if ( parsedKind != BindingKind::Count )
            {
                bool bHandled = true;

                switch ( parsedKind )
                {
                    case BindingKind::Axis1DComposite:
                    {
                        const Key negKey = KeyCodes::fromName( bindNode.attribute( "negKey" ) );
                        const Key posKey = KeyCodes::fromName( bindNode.attribute( "posKey" ) );
                        if ( negKey != Key::Unknown && posKey != Key::Unknown )
                            bindAxis1DComposite( pAction, negKey, posKey, layer );
                        break;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        const Key     upKey    = KeyCodes::fromName( bindNode.attribute( "up" ) );
                        const Key     downKey  = KeyCodes::fromName( bindNode.attribute( "down" ) );
                        const Key     leftKey  = KeyCodes::fromName( bindNode.attribute( "left" ) );
                        const Key     rightKey = KeyCodes::fromName( bindNode.attribute( "right" ) );
                        const float32 deadzone = bindNode.attributeFloat( "deadzone", 0.0f );
                        if ( upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown )
                            bindVector2D( pAction, upKey, downKey, leftKey, rightKey, deadzone, layer );
                        break;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        const utf8*        pStickStr     = bindNode.attribute( "stick" );
                        const GamepadStick stick         = StringUtil::equals( pStickStr, "Right", true ) ? GamepadStick::Right : GamepadStick::Left;
                        const uint8        pad           = static_cast<uint8>( bindNode.attributeInt( "pad", 0 ) );
                        const float32      deadzone      = bindNode.attributeFloat( "deadzone", 0.15f );
                        const float32      outerDeadzone = bindNode.attributeFloat( "outerDeadzone", 1.0f );
                        const float32      exp           = bindNode.attributeFloat( "exponent", 1.0f );
                        bindGamepadStick2D( pAction, stick, deadzone, layer, pad, outerDeadzone, exp );
                        break;
                    }
                    case BindingKind::MouseDelta2D:
                    {
                        const float32 scale = bindNode.attributeFloat( "scale", 1.0f );
                        bindMouseDelta( pAction, scale, layer );
                        break;
                    }
                    case BindingKind::VirtualJoystick2D:
                    {
                        const MouseButton activationButton = MouseButtons::fromName( bindNode.attribute( "button" ) );
                        const float32     radius           = bindNode.attributeFloat( "radius", 64.0f );
                        const float32     deadzone         = bindNode.attributeFloat( "deadzone", 0.1f );
                        const float32     outerDeadzone    = bindNode.attributeFloat( "outerDeadzone", 1.0f );
                        if ( activationButton != MouseButton::Count )
                            bindVirtualJoystick2D( pAction, activationButton, radius, deadzone, layer, outerDeadzone );
                        break;
                    }
                    case BindingKind::Chord:
                    {
                        const Key modKey  = KeyCodes::fromName( bindNode.attribute( "modKey" ) );
                        const Key trigKey = KeyCodes::fromName( bindNode.attribute( "trigKey" ) );
                        if ( modKey != Key::Unknown && trigKey != Key::Unknown )
                            bindChord( pAction, modKey, trigKey, ActionTrigger::Pressed, layer );
                        break;
                    }
                    case BindingKind::Shortcut:
                    {
                        const Key   key     = KeyCodes::fromName( bindNode.attribute( "key" ) );
                        const uint8 modMask = static_cast<uint8>( bindNode.attributeInt( "modifierMask", 0 ) );
                        if ( key != Key::Unknown )
                            bindShortcut( pAction, key, modMask, ActionTrigger::Pressed, layer );
                        break;
                    }
                    case BindingKind::AnyKey:
                    {
                        bindAnyKey( pAction, layer );
                        break;
                    }
                    case BindingKind::SingleSlot:
                    {
                        // 아래 레거시 경로가 읽는다 — `kind="single"` 은 특성 이름(source/key/button)이
                        // 그대로라 예전 파일과 같은 코드로 읽힌다.
                        bHandled = false;
                        break;
                    }
                    case BindingKind::Count:
                    default:
                    {
                        // 표는 이름을 알았는데 여기가 모른다 — 종류를 늘리고 이 switch 를 빠뜨렸다.
                        SW_LOG_ERROR( "읽지 못한 바인딩 종류입니다 (kind=%#) — BindingKind 를 늘리고 loadUserBindings 를 빠뜨렸습니다.",
                                      pKindStr );
                        bHandled = false;
                        break;
                    }
                }

                if ( bHandled )
                    continue;
            }

            // Single slot fallback / legacy format
            const utf8* pKeyStr    = bindNode.attribute( "key" );
            const utf8* pCodeStr   = bindNode.attribute( "code" );
            const utf8* pButtonStr = bindNode.attribute( "button" );
            const utf8* pSourceStr = bindNode.attribute( "source" );
            const uint8 padIndex   = static_cast<uint8>( bindNode.attributeInt( "pad", 0 ) );

            if ( pKeyStr != nullptr )
            {
                const Key key = KeyCodes::fromName( pKeyStr );
                if ( key != Key::Unknown )
                    bind( pAction, key, ActionTrigger::Pressed, layer );
            }
            else if ( pButtonStr != nullptr )
            {
                const MouseButton btn = MouseButtons::fromName( pButtonStr );
                if ( btn != MouseButton::Count )
                    bind( pAction, btn, ActionTrigger::Pressed, layer );
            }
            else if ( pCodeStr != nullptr && pSourceStr != nullptr )
            {
                if ( StringUtil::equals( pSourceStr, "key", true ) )
                {
                    const Key key = KeyCodes::fromName( pCodeStr );
                    if ( key != Key::Unknown )
                        bind( pAction, key, ActionTrigger::Pressed, layer );
                }
                else if ( StringUtil::equals( pSourceStr, "mouse", true ) )
                {
                    const MouseButton btn = MouseButtons::fromName( pCodeStr );
                    if ( btn != MouseButton::Count )
                        bind( pAction, btn, ActionTrigger::Pressed, layer );
                }
                else if ( StringUtil::equals( pSourceStr, "gamepad", true ) )
                {
                    const GamepadButton btn = GamepadButtons::fromName( pCodeStr );
                    if ( btn != GamepadButton::Count )
                    {
                        InputSlot slot    = InputSlot::fromGamepadButton( btn );
                        slot._deviceIndex = padIndex;
                        bind( pAction, slot, ActionTrigger::Pressed, layer );
                    }
                }
            }
        }
        return true;
    }

    ActionTrigger ActionMap::actionTriggerFromName( string_view name )
    {
        if ( name.empty() )
            return ActionTrigger::Count;
        for ( const ActionMapSerializationInternal::TriggerNameEntry& entry : ActionMapSerializationInternal::kArrTriggerNames )
        {
            if ( StringUtil::equals( name, entry._pName, true ) )
                return entry._trigger;
        }
        return ActionTrigger::Count;
    }

    const utf8* ActionMap::actionTriggerToName( ActionTrigger trigger )
    {
        for ( const ActionMapSerializationInternal::TriggerNameEntry& entry : ActionMapSerializationInternal::kArrTriggerNames )
        {
            if ( entry._trigger == trigger )
                return entry._pName;
        }
        return nullptr;
    }
} // namespace sw
