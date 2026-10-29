#include "pch.h"

#include "Engine/Input/Map/InputRemapSerializer.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapXMLUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Serialization/XML/XMLDocument.h"

/**
 * @file InputRemapSerializer.cpp
 * @brief 플레이어 키 리매핑 파일(`<UserBindings>`)의 쓰기 · 읽기와 수정 키 이름 표를 구현합니다.
 * @details 한 줄이 바인딩 하나(`<bind action layer kind trigger …>`)이고, 종류마다 딸린 칸은 저장할 때 모두 적는다 — 빠진 칸은 기본값이 아니라 깨진 파일이다.
 *          패드 번호 · 수정 키 마스크 · 스틱 배율 · 발화 시간 칸은 맵 정의와 같은 규칙(`InputMapXMLUtil`)으로 읽는다.
 */

namespace sw
{
    namespace
    {
        struct InputRemapSerializerInternal
        {
            struct ModifierNameEntry
            {
                const utf8* _pName;
                uint8       _bit;
            };
            /** @brief `<bind modifier="Ctrl+Shift">` 의 이름과 수정 키 비트입니다. 읽기와 쓰기가 이 표 하나를 쓴다. */
            static constexpr ModifierNameEntry kArrModifierNames[] = {
                { "Ctrl",  ModifierKey::Ctrl},
                {"Shift", ModifierKey::Shift},
                {  "Alt",   ModifierKey::Alt},
                {"Super", ModifierKey::Super},
            };

            /**
             * @brief 유저 바인딩 줄의 `trigger` 를 읽습니다. 없거나 모르는 이름이면 그 종류의 기본값(`fallback`)을 반환합니다.
             * @details 저장할 때는 항상 적는다. 빼고 저장하면 다시 읽을 때 기본값으로 돌아가서, 누르는 동안 매 프레임 발화하는(Down) 축 합성 같은 결함이 생긴다.
             */
            static ActionTrigger readUserBindingTrigger( XMLNode bindNode, ActionTrigger fallback )
            {
                const utf8* pTriggerAttr = bindNode.findAttribute( "trigger" );
                if ( pTriggerAttr == nullptr )
                    return fallback;
                const ActionTrigger parsed = InputMap::actionTriggerFromName( pTriggerAttr );
                return ( parsed != ActionTrigger::Count ) ? parsed : fallback;
            }

            // ------------------------------------------------------------------------------
            // 사용자 바인딩 파일 — `InputRemapSerializer::save` · `load` 의 바인딩 하나 쓰기 · 읽기
            // ------------------------------------------------------------------------------

            /** @brief `SingleSlot` 바인딩의 입력 장치별 속성(`source` · 키 · 버튼 · 패드)을 적습니다. */
            static void writeSingleSlotFields( XMLNode bindNode, const ActionBinding& binding )
            {
                if ( binding._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
                {
                    const Key key = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                    bindNode.appendAttribute( "source", "key" );
                    bindNode.appendAttribute( "key", KeyCodeUtil::toName( key ) );
                }
                else if ( binding._arrSlot[0]._deviceKind == InputDeviceKind::Mouse )
                {
                    const MouseButton button = static_cast<MouseButton>( binding._arrSlot[0]._controlIndex );
                    bindNode.appendAttribute( "source", "mouse" );
                    bindNode.appendAttribute( "button", MouseButtonUtil::toName( button ) );
                }
                else if ( binding._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
                {
                    const GamepadButton button = static_cast<GamepadButton>( binding._arrSlot[0]._controlIndex );
                    bindNode.appendAttribute( "source", "gamepad" );
                    bindNode.appendAttribute( "code", GamepadButtonUtil::toName( button ) );
                    bindNode.appendAttribute( "pad", static_cast<int32>( binding._arrSlot[0]._deviceIndex ) );
                }
            }

            /** @brief `Axis1DComposite` 바인딩의 음 · 양 키(게임패드면 버튼과 패드 번호)를 적습니다. */
            static void writeAxis1DFields( XMLNode bindNode, const ActionBinding& binding )
            {
                if ( binding._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
                {
                    bindNode.appendAttribute( "source", "gamepad" );
                    bindNode.appendAttribute( "negKey", GamepadButtonUtil::toName( static_cast<GamepadButton>( binding._arrSlot[0]._controlIndex ) ) );
                    bindNode.appendAttribute( "posKey", GamepadButtonUtil::toName( static_cast<GamepadButton>( binding._arrSlot[1]._controlIndex ) ) );
                    bindNode.appendAttribute( "pad", static_cast<int32>( binding._arrSlot[0]._deviceIndex ) );
                    return;
                }
                bindNode.appendAttribute( "negKey", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[0]._controlIndex ) ) );
                bindNode.appendAttribute( "posKey", KeyCodeUtil::toName( static_cast<Key>( binding._arrSlot[1]._controlIndex ) ) );
            }

            /**
             * @brief 바인딩 종류별 딸린 특성을 적습니다.
             * @details 종류를 추가하고 여기를 빠뜨리면 그 바인딩이 특성 하나 없이 저장되어 조용히 사라진다. 그래서 `Count` 에서 오류를 남긴다.
             */
            static void writeUserBindingFields( XMLNode bindNode, const ActionBinding& binding )
            {
                switch ( binding._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        writeSingleSlotFields( bindNode, binding );
                        break;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        writeAxis1DFields( bindNode, binding );
                        break;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        const Key upKey    = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                        const Key downKey  = static_cast<Key>( binding._arrSlot[1]._controlIndex );
                        const Key leftKey  = static_cast<Key>( binding._arrSlot[2]._controlIndex );
                        const Key rightKey = static_cast<Key>( binding._arrSlot[3]._controlIndex );
                        bindNode.appendAttribute( "up", KeyCodeUtil::toName( upKey ) );
                        bindNode.appendAttribute( "down", KeyCodeUtil::toName( downKey ) );
                        bindNode.appendAttribute( "left", KeyCodeUtil::toName( leftKey ) );
                        bindNode.appendAttribute( "right", KeyCodeUtil::toName( rightKey ) );
                        bindNode.appendAttribute( "deadzone", binding._deadzone );
                        break;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        bindNode.appendAttribute( "stick", binding._stick == GamepadStick::Left ? "Left" : "Right" );
                        bindNode.appendAttribute( "pad", static_cast<int32>( binding._deviceIndex ) );
                        bindNode.appendAttribute( "deadzone", binding._deadzone );
                        bindNode.appendAttribute( "outerDeadzone", binding._outerDeadzone );
                        bindNode.appendAttribute( "exponent", binding._responseExponent );
                        InputMapXMLUtil::writeStickModifier( bindNode, binding );
                        break;
                    }
                    case BindingKind::MouseDelta2D:
                    case BindingKind::MouseWheel1D:
                    {
                        bindNode.appendAttribute( "scale", binding._scale );
                        break;
                    }
                    case BindingKind::VirtualJoystick2D:
                    {
                        const MouseButton activationButton = static_cast<MouseButton>( binding._arrSlot[0]._controlIndex );
                        bindNode.appendAttribute( "button", MouseButtonUtil::toName( activationButton ) );
                        bindNode.appendAttribute( "radius", binding._scale );
                        bindNode.appendAttribute( "deadzone", binding._deadzone );
                        bindNode.appendAttribute( "outerDeadzone", binding._outerDeadzone );
                        break;
                    }
                    case BindingKind::Chord:
                    {
                        const Key modifierKey = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                        const Key triggerKey  = static_cast<Key>( binding._arrSlot[1]._controlIndex );
                        bindNode.appendAttribute( "modKey", KeyCodeUtil::toName( modifierKey ) );
                        bindNode.appendAttribute( "trigKey", KeyCodeUtil::toName( triggerKey ) );
                        break;
                    }
                    case BindingKind::Shortcut:
                    {
                        const Key key = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                        bindNode.appendAttribute( "key", KeyCodeUtil::toName( key ) );
                        bindNode.appendAttribute( "modifierMask", static_cast<int32>( binding._modifierMask ) );
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
                        SW_LOG_ERROR( "Unsaved binding kind (kind=%#) - BindingKind was extended but InputRemapSerializer::save was not updated.", BindingKinds::toName( binding._kind ) );
                        break;
                    }
                }
            }

            /** @brief 사용자 바인딩 하나를 `<bind>` 로 적습니다. */
            static void writeUserBinding( XMLNode root, const hashed_string& actionName, const ActionBinding& binding )
            {
                XMLNode bindNode = root.appendChild( "bind" );
                bindNode.appendAttribute( "action", actionName.c_str() );
                bindNode.appendAttribute( "layer", binding._layer.c_str() );
                // 이름은 테이블에서 가져온다. 쓰는 쪽과 읽는 쪽이 리터럴을 따로 가지면 한쪽만 고쳤을 때 파일이 조용히 왕복되지 않는다.
                bindNode.appendAttribute( "kind", BindingKinds::toName( binding._kind ) );
                // 발화 규칙도 바인딩의 일부다. 빼면 다시 읽을 때 종류의 기본값으로 돌아간다(`readUserBindingTrigger`).
                const utf8* pTriggerName = InputMap::actionTriggerToName( binding._trigger );
                if ( pTriggerName != nullptr )
                    bindNode.appendAttribute( "trigger", pTriggerName );
                InputMapXMLUtil::writeBindingModifier( bindNode, binding );
                writeUserBindingFields( bindNode, binding );
            }

            /** @brief 사용자 바인딩 `GamepadStick2D` 를 읽어 맵에 묶습니다. 패드 번호나 축이 틀리면 그 바인딩을 버립니다. 빠진 칸은 깨진 파일이라 @p inoutbValid 를 내립니다. */
            static void readUserStick( InputMap& map, XMLNode bindNode, const utf8* pAction, string_view layer, bool& inoutbValid )
            {
                uint8 pad{ 0 };
                if ( InputMapXMLUtil::tryGetPadIndex( bindNode, pad ) == false )
                    return; // 이미 경고했으므로 이 바인딩을 버린다
                const utf8*        pStickStr = bindNode.findAttribute( "stick" );
                const GamepadStick stick     = StringUtil::equals( pStickStr, "Right" ) ? GamepadStick::Right : GamepadStick::Left;
                float32            deadzone{};
                float32            outerDeadzone{};
                float32            exp{};
                bool               bFields = true;
                InputMapXMLUtil::readRequired( bindNode, "deadzone", deadzone, bFields );
                InputMapXMLUtil::readRequired( bindNode, "outerDeadzone", outerDeadzone, bFields );
                InputMapXMLUtil::readRequired( bindNode, "exponent", exp, bFields );
                float2           axisScale{};
                bool             bScaleByDelta{ false };
                GamepadStickAxis axis{ GamepadStickAxis::Both };
                const bool       bAxisRead = InputMapXMLUtil::readStickModifier( bindNode, axisScale, bScaleByDelta, axis, bFields );
                inoutbValid                = bFields && inoutbValid;
                if ( bFields && bAxisRead )
                    map.bindGamepadStick2D( hashed_string( pAction ), stick, deadzone, hashed_string( layer ), pad, outerDeadzone, exp, axisScale, bScaleByDelta, axis );
            }

            /** @brief 사용자 바인딩 `Vector2DComposite` 를 읽어 맵에 묶습니다. 사용자 바인딩 파일은 저장할 때 모든 칸을 적는다 — 빠진 칸은 기본값이 아니라 깨진 파일이다. */
            static void readUserVector2D( InputMap& map, XMLNode bindNode, const utf8* pAction, string_view layer, bool& inoutbValid )
            {
                const Key upKey    = KeyCodeUtil::fromName( bindNode.getAttributeText( "up" ) );
                const Key downKey  = KeyCodeUtil::fromName( bindNode.getAttributeText( "down" ) );
                const Key leftKey  = KeyCodeUtil::fromName( bindNode.getAttributeText( "left" ) );
                const Key rightKey = KeyCodeUtil::fromName( bindNode.getAttributeText( "right" ) );
                float32   deadzone{};
                bool      bFields = true;
                InputMapXMLUtil::readRequired( bindNode, "deadzone", deadzone, bFields );
                inoutbValid                       = bFields && inoutbValid;
                const bool bAllDirectionKeysBound = bFields && upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown;
                if ( bAllDirectionKeysBound )
                    map.bindVector2D( hashed_string( pAction ), upKey, downKey, leftKey, rightKey, deadzone, hashed_string( layer ) );
            }

            /** @brief 사용자 바인딩 `VirtualJoystick2D` 를 읽어 맵에 묶습니다. */
            static void readUserVirtualJoystick( InputMap& map, XMLNode bindNode, const utf8* pAction, string_view layer, bool& inoutbValid )
            {
                const MouseButton activationButton = MouseButtonUtil::fromName( bindNode.getAttributeText( "button" ) );
                float32           radius{};
                float32           deadzone{};
                float32           outerDeadzone{};
                bool              bFields = true;
                InputMapXMLUtil::readRequired( bindNode, "radius", radius, bFields );
                InputMapXMLUtil::readRequired( bindNode, "deadzone", deadzone, bFields );
                InputMapXMLUtil::readRequired( bindNode, "outerDeadzone", outerDeadzone, bFields );
                inoutbValid = bFields && inoutbValid;
                if ( bFields && activationButton != MouseButton::Count )
                    map.bindVirtualJoystick2D( hashed_string( pAction ), activationButton, radius, deadzone, hashed_string( layer ), outerDeadzone );
            }

            /** @brief 사용자 바인딩 `SingleSlot` 을 읽어 맵에 묶습니다. 저장할 때와 같은 형태만 읽는다: source="key" key=…, source="mouse" button=…, source="gamepad" code=… pad=… */
            static void readUserSingleSlot( InputMap& map, XMLNode bindNode, const utf8* pAction, string_view layer )
            {
                const utf8*         pSourceStr    = bindNode.findAttribute( "source" );
                const ActionTrigger singleTrigger = readUserBindingTrigger( bindNode, ActionTrigger::Pressed );
                if ( StringUtil::equals( pSourceStr, "key" ) )
                {
                    const Key key = KeyCodeUtil::fromName( bindNode.getAttributeText( "key" ) );
                    if ( key != Key::Unknown )
                        map.bind( hashed_string( pAction ), key, singleTrigger, hashed_string( layer ) );
                }
                else if ( StringUtil::equals( pSourceStr, "mouse" ) )
                {
                    const MouseButton button = MouseButtonUtil::fromName( bindNode.getAttributeText( "button" ) );
                    if ( button != MouseButton::Count )
                        map.bind( hashed_string( pAction ), button, singleTrigger, hashed_string( layer ) );
                }
                else if ( StringUtil::equals( pSourceStr, "gamepad" ) )
                {
                    const GamepadButton button = GamepadButtonUtil::fromName( bindNode.getAttributeText( "code" ) );
                    uint8               padIndex{ 0 };
                    if ( button != GamepadButton::Count && InputMapXMLUtil::tryGetPadIndex( bindNode, padIndex ) )
                    {
                        InputSlot slot    = InputSlot::fromGamepadButton( button );
                        slot._deviceIndex = padIndex;
                        map.bind( hashed_string( pAction ), slot, singleTrigger, hashed_string( layer ) );
                    }
                }
                else
                {
                    if ( pSourceStr == nullptr )
                        SW_LOG_ERROR_ID( "Input.BindingSourceMissing", "The source of a single-slot binding is missing (action=%#) - skipping this binding.", pAction );
                    else
                        SW_LOG_ERROR_ID( "Input.BindingSourceUnknown", "The source of a single-slot binding is unknown (action=%#, source=%#) - skipping this binding.", pAction, pSourceStr );
                }
            }

            /**
             * @brief 사용자 바인딩 `<bind>` 하나를 종류에 맞게 읽어 맵에 묶습니다. 깨진 칸이 있는 바인딩은 묶지 않고 @p inoutbValid 를 내립니다.
             * @details 종류를 추가하고 여기를 빠뜨리면 `Count` 에서 오류를 남긴다.
             */
            static void readUserBinding( InputMap& map, XMLNode bindNode, const utf8* pAction, const utf8* pKindStr, BindingKind kind, string_view layer, bool& inoutbValid )
            {
                switch ( kind )
                {
                    case BindingKind::Axis1DComposite:
                    {
                        InputSlot negativeSlot{};
                        InputSlot positiveSlot{};
                        if ( InputMapXMLUtil::tryReadAxisSlots( bindNode, "negKey", "posKey", negativeSlot, positiveSlot ) )
                            map.bindAxis1DComposite( hashed_string( pAction ), negativeSlot, positiveSlot, hashed_string( layer ), readUserBindingTrigger( bindNode, ActionTrigger::Down ) );
                        break;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        readUserVector2D( map, bindNode, pAction, layer, inoutbValid );
                        break;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        readUserStick( map, bindNode, pAction, layer, inoutbValid );
                        break;
                    }
                    case BindingKind::MouseDelta2D:
                    case BindingKind::MouseWheel1D:
                    {
                        float32 scale{};
                        bool    bFields = true;
                        InputMapXMLUtil::readRequired( bindNode, "scale", scale, bFields );
                        inoutbValid = bFields && inoutbValid;
                        if ( bFields && kind == BindingKind::MouseDelta2D )
                            map.bindMouseDelta( hashed_string( pAction ), scale, hashed_string( layer ) );
                        else if ( bFields )
                            map.bindMouseWheel( hashed_string( pAction ), scale, hashed_string( layer ) );
                        break;
                    }
                    case BindingKind::VirtualJoystick2D:
                    {
                        readUserVirtualJoystick( map, bindNode, pAction, layer, inoutbValid );
                        break;
                    }
                    case BindingKind::Chord:
                    {
                        const Key modifierKey = KeyCodeUtil::fromName( bindNode.getAttributeText( "modKey" ) );
                        const Key triggerKey  = KeyCodeUtil::fromName( bindNode.getAttributeText( "trigKey" ) );
                        if ( modifierKey != Key::Unknown && triggerKey != Key::Unknown )
                            map.bindChord( hashed_string( pAction ), modifierKey, triggerKey, readUserBindingTrigger( bindNode, ActionTrigger::Pressed ), hashed_string( layer ) );
                        break;
                    }
                    case BindingKind::Shortcut:
                    {
                        uint8 modifierMask{ 0 };
                        if ( InputMapXMLUtil::tryGetModifierMask( bindNode, modifierMask ) == false )
                            break; // 이미 경고했으므로 다른 수정 키 조합으로 묶지 않고 이 바인딩을 버린다
                        const Key key = KeyCodeUtil::fromName( bindNode.getAttributeText( "key" ) );
                        if ( key != Key::Unknown )
                            map.bindShortcut( hashed_string( pAction ), key, modifierMask, readUserBindingTrigger( bindNode, ActionTrigger::Pressed ), hashed_string( layer ) );
                        break;
                    }
                    case BindingKind::AnyKey:
                    {
                        map.bindAnyKey( hashed_string( pAction ), hashed_string( layer ) );
                        break;
                    }
                    case BindingKind::SingleSlot:
                    {
                        readUserSingleSlot( map, bindNode, pAction, layer );
                        break;
                    }
                    case BindingKind::Count:
                    {
                        // 테이블은 이 이름을 아는데 여기서는 모른다. 종류를 추가하고 이 switch 를 빠뜨린 경우다.
                        SW_LOG_ERROR( "Unread binding kind (kind=%#) - BindingKind was extended but InputRemapSerializer::load was not updated.", pKindStr );
                        break;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool InputRemapSerializer::save( const InputMap& map, string_view filePath )
    {
        if ( filePath.empty() )
            return false;

        XMLDocument doc;
        XMLNode     root = doc.appendRoot( "UserBindings" );
        for ( const hashed_string& actionName : map.getActionNames() )
        {
            const uint32 bindCount = map.getBindingCount( actionName );
            for ( uint32 bindIndex = 0; bindIndex < bindCount; ++bindIndex )
            {
                InputRemapSerializerInternal::writeUserBinding( root, actionName, *map.getBinding( actionName, bindIndex ) ); // 개수 안의 인덱스라 늘 있다
            }
        }
        return doc.saveFile( filePath );
    }

    bool InputRemapSerializer::load( InputMap& map, string_view filePath )
    {
        if ( filePath.empty() )
            return false;

        // 사용자 바인딩은 `save` 가 디스크에 쓴 플레이어 파일이다 — 리소스 id 가 아니므로 디스크 입구로 읽는다.
        XMLDocument doc;
        if ( doc.loadFile( filePath ) == false )
            return false;

        XMLNode root = doc.getRoot( "UserBindings" );
        if ( root.isValid() == false )
            return false;

        bool bCursorConflict = false;
        bool bValid          = true; // 깨진 칸이 있는 바인딩은 넣지 않고 읽기를 실패로 돌린다
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
                if ( pKindStr == nullptr )
                    SW_LOG_ERROR_ID( "Input.BindingKindMissing", "The binding kind is missing (action=%#) - skipping this binding.", pAction );
                else
                    SW_LOG_ERROR_ID( "Input.BindingKindUnknown", "The binding kind is unknown (action=%#, kind=%#) - skipping this binding.", pAction, pKindStr );
                continue;
            }

            const hashed_string actionName( pAction );
            const uint32        previousCount = map.getBindingCount( actionName );
            InputRemapSerializerInternal::readUserBinding( map, bindNode, pAction, pKindStr, parsedKind, layer, bValid );
            InputMapXMLUtil::readBindingModifier( map, actionName, previousCount, bindNode, bValid );

            // 맵 읽기와 같은 검사 — 가상 커서가 마우스로 바꾸는 스틱 · 버튼을 다시 묶은 바인딩은 넣지 않고 읽기를 실패로 돌린다.
            bCursorConflict = map.removeVirtualCursorConflicts( filePath, actionName, previousCount ) > 0 || bCursorConflict;
        }
        if ( bValid == false )
            SW_LOG_ERROR( "user bindings %# have invalid attribute values (listed above)", filePath );
        return bCursorConflict == false && bValid;
    }

    bool InputRemapSerializer::tryParseModifierNames( string_view text, uint8& outModifierMask )
    {
        outModifierMask = ModifierKey::None;
        while ( true )
        {
            const size_t      plusIndex = text.find( '+' );
            const string_view token     = text.substr( 0, plusIndex );
            uint8             bit       = ModifierKey::None;
            for ( const InputRemapSerializerInternal::ModifierNameEntry& entry : InputRemapSerializerInternal::kArrModifierNames )
            {
                if ( StringUtil::equals( token, entry._pName, true ) )
                    bit = entry._bit;
            }
            if ( bit == ModifierKey::None || ( outModifierMask & bit ) != 0 )
                return false;
            outModifierMask = static_cast<uint8>( outModifierMask | bit );
            if ( plusIndex == string_view::npos )
                return true;
            text = text.substr( plusIndex + 1 );
        }
    }

    string InputRemapSerializer::formatModifierNames( uint8 modifierMask )
    {
        string text;
        for ( const InputRemapSerializerInternal::ModifierNameEntry& entry : InputRemapSerializerInternal::kArrModifierNames )
        {
            if ( ( modifierMask & entry._bit ) == 0 )
                continue;
            if ( text.empty() == false )
                text += "+";
            text += entry._pName;
        }
        return text;
    }
} // namespace sw
