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

        const float32 dblClick  = root.attrFloat( ActionMapSerializationInternal::InputMapXml::kAttrDoubleClick, ActionMapDefaults::kDoubleClickTime );
        const float32 dblDist   = root.attrFloat( ActionMapSerializationInternal::InputMapXml::kAttrDoubleClickDist, ActionMapDefaults::kDoubleClickMaxDistance );
        const float32 holdThr   = root.attrFloat( ActionMapSerializationInternal::InputMapXml::kAttrHoldThreshold, ActionMapDefaults::kHoldThreshold );
        const utf8*   pDefLayer = root.attr( ActionMapSerializationInternal::InputMapXml::kAttrDefaultLayer );

        clear();
        setDoubleClickTime( dblClick );
        setDoubleClickMaxDistance( dblDist );
        setHoldThreshold( holdThr );
        if ( pDefLayer != nullptr && pDefLayer[0] != '\0' )
            _defaultLayerName = hashed_string( pDefLayer );

        XmlNode layersNode = root.child( ActionMapSerializationInternal::InputMapXml::kLayers );
        if ( layersNode.isValid() )
        {
            for ( XmlNode layerNode = layersNode.child( ActionMapSerializationInternal::InputMapXml::kLayer ); layerNode.isValid();
                  layerNode         = layerNode.next( ActionMapSerializationInternal::InputMapXml::kLayer ) )
            {
                const utf8* pLayerName = layerNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrName );
                if ( pLayerName == nullptr || pLayerName[0] == '\0' )
                    continue;
                const int32 priority   = layerNode.attrInt( ActionMapSerializationInternal::InputMapXml::kAttrPriority, 0 );
                const bool  enabled    = layerNode.attrBool( ActionMapSerializationInternal::InputMapXml::kAttrEnabled, true );
                const bool  blockLower = layerNode.attrBool( ActionMapSerializationInternal::InputMapXml::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.attrBool( ActionMapSerializationInternal::InputMapXml::kAttrAlwaysOn, false );
                registerLayer( pLayerName, priority, enabled, blockLower, alwaysOn );
            }
        }

        ensureLayer( _defaultLayerName, 0, true, false );

        auto loadAction = [this]( XmlNode actionNode, string_view inheritedLayer )
        {
            const utf8* pActionName = actionNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrName );
            if ( pActionName == nullptr || pActionName[0] == '\0' )
                return;

            hashed_string layer      = inheritedLayer.empty() ? _defaultLayerName : hashed_string( inheritedLayer );
            const utf8*   pLayerAttr = actionNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrLayer );
            if ( pLayerAttr != nullptr && pLayerAttr[0] != '\0' )
                layer = hashed_string( pLayerAttr );
            ensureLayer( layer );

            auto        defaultTrigger = ActionTrigger::Pressed;
            const utf8* pTriggerAttr   = actionNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrTrigger );
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
                const utf8* pSource = bindNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrSource );
                const utf8* pCode   = bindNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrCode );
                if ( pSource == nullptr || pCode == nullptr || pCode[0] == '\0' )
                    continue;

                ActionTrigger trigger          = defaultTrigger;
                const utf8*   pBindTriggerAttr = bindNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrTrigger );
                if ( pBindTriggerAttr != nullptr )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pBindTriggerAttr );
                    if ( parsed != ActionTrigger::Count )
                        trigger = parsed;
                }

                hashed_string bindLayer      = layer;
                const utf8*   pBindLayerAttr = bindNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrLayer );
                if ( pBindLayerAttr != nullptr && pBindLayerAttr[0] != '\0' )
                {
                    bindLayer = hashed_string( pBindLayerAttr );
                    ensureLayer( bindLayer );
                }

                const utf8* pModifierAttr = bindNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrModifier );
                if ( pModifierAttr != nullptr && pModifierAttr[0] != '\0' )
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
                        const float32 deadzone = bindNode.attrFloat( ActionMapSerializationInternal::InputMapXml::kAttrDeadzone, 0.15f );
                        bindGamepadStick2D( pActionName, GamepadStick::Left, deadzone, bindLayer.view() );
                    }
                    else if ( StringUtil::equals( pCode, "RightStick", true ) )
                    {
                        const float32 deadzone = bindNode.attrFloat( ActionMapSerializationInternal::InputMapXml::kAttrDeadzone, 0.15f );
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
                const Key     upKey          = KeyCodes::fromName( compNode.attr( "up" ) );
                const Key     downKey        = KeyCodes::fromName( compNode.attr( "down" ) );
                const Key     leftKey        = KeyCodes::fromName( compNode.attr( "left" ) );
                const Key     rightKey       = KeyCodes::fromName( compNode.attr( "right" ) );
                const float32 deadzone       = compNode.attrFloat( "deadzone", 0.0f );
                hashed_string compLayer      = layer;
                const utf8*   pCompLayerAttr = compNode.attr( "layer" );
                if ( pCompLayerAttr != nullptr && pCompLayerAttr[0] != '\0' )
                {
                    compLayer = hashed_string( pCompLayerAttr );
                    ensureLayer( compLayer );
                }
                if ( upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown )
                {
                    bindVector2D( pActionName, upKey, downKey, leftKey, rightKey, deadzone, compLayer.view() );
                }
            }

            // 3) <axis1d> 태그 파싱
            for ( XmlNode axisNode = actionNode.child( "axis1d" ); axisNode.isValid(); axisNode = axisNode.next( "axis1d" ) )
            {
                const Key     posKey         = KeyCodes::fromName( axisNode.attr( "positive" ) );
                const Key     negKey         = KeyCodes::fromName( axisNode.attr( "negative" ) );
                hashed_string axisLayer      = layer;
                const utf8*   pAxisLayerAttr = axisNode.attr( "layer" );
                if ( pAxisLayerAttr != nullptr && pAxisLayerAttr[0] != '\0' )
                {
                    axisLayer = hashed_string( pAxisLayerAttr );
                    ensureLayer( axisLayer );
                }
                if ( posKey != Key::Unknown && negKey != Key::Unknown )
                {
                    bindAxis1DComposite( pActionName, negKey, posKey, axisLayer.view() );
                }
            }

            // 4) <stick> 태그 파싱
            for ( XmlNode stickNode = actionNode.child( "stick" ); stickNode.isValid(); stickNode = stickNode.next( "stick" ) )
            {
                const utf8*        pStickName      = stickNode.attr( "stick" );
                const GamepadStick stick           = ( pStickName != nullptr && StringUtil::equals( pStickName, "Right", true ) ) ? GamepadStick::Right : GamepadStick::Left;
                const float32      deadzone        = stickNode.attrFloat( "deadzone", 0.15f );
                hashed_string      stickLayer      = layer;
                const utf8*        pStickLayerAttr = stickNode.attr( "layer" );
                if ( pStickLayerAttr != nullptr && pStickLayerAttr[0] != '\0' )
                {
                    stickLayer = hashed_string( pStickLayerAttr );
                    ensureLayer( stickLayer );
                }
                const uint8   padIndex         = static_cast<uint8>( stickNode.attrInt( "pad", 0 ) );
                const float32 outerDeadzone    = stickNode.attrFloat( "outerDeadzone", 1.0f );
                const float32 responseExponent = stickNode.attrFloat( "responseExponent", 1.0f );
                bindGamepadStick2D( pActionName, stick, deadzone, stickLayer.view(), padIndex, outerDeadzone, responseExponent );
            }

            // 5) <chord> 태그 파싱
            for ( XmlNode chordNode = actionNode.child( "chord" ); chordNode.isValid(); chordNode = chordNode.next( "chord" ) )
            {
                const Key     modKey    = KeyCodes::fromName( chordNode.attr( "modifier" ) );
                const Key     trigKey   = KeyCodes::fromName( chordNode.attr( "trigger" ) );
                ActionTrigger trig      = defaultTrigger;
                const utf8*   pTrigAttr = chordNode.attr( "triggerMode" );
                if ( pTrigAttr != nullptr && pTrigAttr[0] != '\0' )
                {
                    const ActionTrigger parsed = actionTriggerFromName( pTrigAttr );
                    if ( parsed != ActionTrigger::Count )
                        trig = parsed;
                }
                hashed_string chordLayer      = layer;
                const utf8*   pChordLayerAttr = chordNode.attr( "layer" );
                if ( pChordLayerAttr != nullptr && pChordLayerAttr[0] != '\0' )
                {
                    chordLayer = hashed_string( pChordLayerAttr );
                    ensureLayer( chordLayer );
                }
                if ( modKey != Key::Unknown && trigKey != Key::Unknown )
                {
                    bindChord( pActionName, modKey, trigKey, trig, chordLayer.view() );
                }
            }
        };

        for ( XmlNode layerNode = root.child( ActionMapSerializationInternal::InputMapXml::kLayer ); layerNode.isValid(); layerNode = layerNode.next( ActionMapSerializationInternal::InputMapXml::kLayer ) )
        {
            const utf8* pLayerName = layerNode.attr( ActionMapSerializationInternal::InputMapXml::kAttrName );
            if ( pLayerName == nullptr || pLayerName[0] == '\0' )
                continue;
            if ( hasLayer( pLayerName ) == false )
            {
                const int32 priority   = layerNode.attrInt( ActionMapSerializationInternal::InputMapXml::kAttrPriority, 0 );
                const bool  enabled    = layerNode.attrBool( ActionMapSerializationInternal::InputMapXml::kAttrEnabled, true );
                const bool  blockLower = layerNode.attrBool( ActionMapSerializationInternal::InputMapXml::kAttrBlockLower, false );
                const bool  alwaysOn   = layerNode.attrBool( ActionMapSerializationInternal::InputMapXml::kAttrAlwaysOn, false );
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
                bindNode.appendAttr( "action", actionName.c_str() );
                bindNode.appendAttr( "layer", b._layer.c_str() );

                switch ( b._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        bindNode.appendAttr( "kind", "single" );
                        if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
                        {
                            const Key key = static_cast<Key>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttr( "source", "key" );
                            bindNode.appendAttr( "key", KeyCodes::toName( key ) );
                        }
                        else if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Mouse )
                        {
                            const MouseButton btn = static_cast<MouseButton>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttr( "source", "mouse" );
                            bindNode.appendAttr( "button", MouseButtons::toName( btn ) );
                        }
                        else if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
                        {
                            const GamepadButton btn = static_cast<GamepadButton>( b._arrSlot[0]._controlIndex );
                            bindNode.appendAttr( "source", "gamepad" );
                            bindNode.appendAttr( "code", GamepadButtons::toName( btn ) );
                            bindNode.appendAttr( "pad", static_cast<int32>( b._arrSlot[0]._deviceIndex ) );
                        }
                        break;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        bindNode.appendAttr( "kind", "axis1d" );
                        const Key negKey = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key posKey = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        bindNode.appendAttr( "negKey", KeyCodes::toName( negKey ) );
                        bindNode.appendAttr( "posKey", KeyCodes::toName( posKey ) );
                        break;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        bindNode.appendAttr( "kind", "vector2d" );
                        const Key upKey    = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key downKey  = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        const Key leftKey  = static_cast<Key>( b._arrSlot[2]._controlIndex );
                        const Key rightKey = static_cast<Key>( b._arrSlot[3]._controlIndex );
                        bindNode.appendAttr( "up", KeyCodes::toName( upKey ) );
                        bindNode.appendAttr( "down", KeyCodes::toName( downKey ) );
                        bindNode.appendAttr( "left", KeyCodes::toName( leftKey ) );
                        bindNode.appendAttr( "right", KeyCodes::toName( rightKey ) );
                        bindNode.appendAttr( "deadzone", b._deadzone );
                        break;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        bindNode.appendAttr( "kind", "stick" );
                        bindNode.appendAttr( "stick", b._stick == GamepadStick::Left ? "Left" : "Right" );
                        bindNode.appendAttr( "pad", static_cast<int32>( b._deviceIndex ) );
                        bindNode.appendAttr( "deadzone", b._deadzone );
                        bindNode.appendAttr( "outerDeadzone", b._outerDeadzone );
                        bindNode.appendAttr( "exponent", b._responseExponent );
                        break;
                    }
                    case BindingKind::MouseDelta2D:
                    {
                        bindNode.appendAttr( "kind", "mouseDelta" );
                        bindNode.appendAttr( "scale", b._scale );
                        break;
                    }
                    case BindingKind::VirtualJoystick2D:
                    {
                        bindNode.appendAttr( "kind", "virtualJoystick" );
                        const MouseButton activationButton = static_cast<MouseButton>( b._arrSlot[0]._controlIndex );
                        bindNode.appendAttr( "button", MouseButtons::toName( activationButton ) );
                        bindNode.appendAttr( "radius", b._scale );
                        bindNode.appendAttr( "deadzone", b._deadzone );
                        bindNode.appendAttr( "outerDeadzone", b._outerDeadzone );
                        break;
                    }
                    case BindingKind::Chord:
                    {
                        bindNode.appendAttr( "kind", "chord" );
                        const Key modKey  = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        const Key trigKey = static_cast<Key>( b._arrSlot[1]._controlIndex );
                        bindNode.appendAttr( "modKey", KeyCodes::toName( modKey ) );
                        bindNode.appendAttr( "trigKey", KeyCodes::toName( trigKey ) );
                        break;
                    }
                    case BindingKind::Shortcut:
                    {
                        bindNode.appendAttr( "kind", "shortcut" );
                        const Key key = static_cast<Key>( b._arrSlot[0]._controlIndex );
                        bindNode.appendAttr( "key", KeyCodes::toName( key ) );
                        bindNode.appendAttr( "modifierMask", static_cast<int32>( b._modifierMask ) );
                        break;
                    }
                    case BindingKind::AnyKey:
                    {
                        bindNode.appendAttr( "kind", "anyKey" );
                        break;
                    }
                    default:
                        break;
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
            const utf8*       pAction   = bindNode.attr( "action" );
            const utf8*       pKindStr  = bindNode.attr( "kind" );
            const utf8*       pLayerStr = bindNode.attr( "layer" );
            const string_view layer     = ( pLayerStr != nullptr && pLayerStr[0] != '\0' ) ? string_view( pLayerStr ) : string_view{};

            if ( pAction == nullptr || pAction[0] == '\0' )
                continue;

            if ( pKindStr != nullptr )
            {
                if ( StringUtil::equals( pKindStr, "axis1d", true ) )
                {
                    const Key negKey = KeyCodes::fromName( bindNode.attr( "negKey" ) );
                    const Key posKey = KeyCodes::fromName( bindNode.attr( "posKey" ) );
                    if ( negKey != Key::Unknown && posKey != Key::Unknown )
                        bindAxis1DComposite( pAction, negKey, posKey, layer );
                    continue;
                }
                else if ( StringUtil::equals( pKindStr, "vector2d", true ) )
                {
                    const Key     upKey    = KeyCodes::fromName( bindNode.attr( "up" ) );
                    const Key     downKey  = KeyCodes::fromName( bindNode.attr( "down" ) );
                    const Key     leftKey  = KeyCodes::fromName( bindNode.attr( "left" ) );
                    const Key     rightKey = KeyCodes::fromName( bindNode.attr( "right" ) );
                    const float32 deadzone = bindNode.attrFloat( "deadzone", 0.0f );
                    if ( upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown )
                        bindVector2D( pAction, upKey, downKey, leftKey, rightKey, deadzone, layer );
                    continue;
                }
                else if ( StringUtil::equals( pKindStr, "stick", true ) )
                {
                    const utf8*        pStickStr     = bindNode.attr( "stick" );
                    const GamepadStick stick         = StringUtil::equals( pStickStr, "Right", true ) ? GamepadStick::Right : GamepadStick::Left;
                    const uint8        pad           = static_cast<uint8>( bindNode.attrInt( "pad", 0 ) );
                    const float32      deadzone      = bindNode.attrFloat( "deadzone", 0.15f );
                    const float32      outerDeadzone = bindNode.attrFloat( "outerDeadzone", 1.0f );
                    const float32      exp           = bindNode.attrFloat( "exponent", 1.0f );
                    bindGamepadStick2D( pAction, stick, deadzone, layer, pad, outerDeadzone, exp );
                    continue;
                }
                else if ( StringUtil::equals( pKindStr, "mouseDelta", true ) )
                {
                    const float32 scale = bindNode.attrFloat( "scale", 1.0f );
                    bindMouseDelta( pAction, scale, layer );
                    continue;
                }
                else if ( StringUtil::equals( pKindStr, "virtualJoystick", true ) )
                {
                    const MouseButton activationButton = MouseButtons::fromName( bindNode.attr( "button" ) );
                    const float32     radius           = bindNode.attrFloat( "radius", 64.0f );
                    const float32     deadzone         = bindNode.attrFloat( "deadzone", 0.1f );
                    const float32     outerDeadzone    = bindNode.attrFloat( "outerDeadzone", 1.0f );
                    if ( activationButton != MouseButton::Count )
                        bindVirtualJoystick2D( pAction, activationButton, radius, deadzone, layer, outerDeadzone );
                    continue;
                }
                else if ( StringUtil::equals( pKindStr, "chord", true ) )
                {
                    const Key modKey  = KeyCodes::fromName( bindNode.attr( "modKey" ) );
                    const Key trigKey = KeyCodes::fromName( bindNode.attr( "trigKey" ) );
                    if ( modKey != Key::Unknown && trigKey != Key::Unknown )
                        bindChord( pAction, modKey, trigKey, ActionTrigger::Pressed, layer );
                    continue;
                }
                else if ( StringUtil::equals( pKindStr, "shortcut", true ) )
                {
                    const Key   key     = KeyCodes::fromName( bindNode.attr( "key" ) );
                    const uint8 modMask = static_cast<uint8>( bindNode.attrInt( "modifierMask", 0 ) );
                    if ( key != Key::Unknown )
                        bindShortcut( pAction, key, modMask, ActionTrigger::Pressed, layer );
                    continue;
                }
                else if ( StringUtil::equals( pKindStr, "anyKey", true ) )
                {
                    bindAnyKey( pAction, layer );
                    continue;
                }
            }

            // Single slot fallback / legacy format
            const utf8* pKeyStr    = bindNode.attr( "key" );
            const utf8* pCodeStr   = bindNode.attr( "code" );
            const utf8* pButtonStr = bindNode.attr( "button" );
            const utf8* pSourceStr = bindNode.attr( "source" );
            const uint8 padIndex   = static_cast<uint8>( bindNode.attrInt( "pad", 0 ) );

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
