#include "pch.h"

#include "Engine/Input/Map/InputMapXML.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapFields.h"
#include "Engine/Input/Map/InputRemapSerializer.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    namespace
    {
        struct InputMapXMLInternal
        {
            static constexpr const utf8* kArrBindingElement[] = { "bind", "axis1d", "vector2d", "stick", "chord", "mouseDelta", "mouseWheel", "virtualJoystick", "anyKey" };

            /** @brief `<action>` 하나를 읽는 동안의 문맥입니다. */
            struct ActionContext
            {
                InputMap&     _map;
                hashed_string _action;
                hashed_string _layer;   ///< 액션의 레이어 — 바인딩이 `layer` 를 적지 않으면 이것
                ActionTrigger _trigger; ///< 액션의 trigger — 적지 않았으면 Count
                string_view   _sourceName;
                bool&         _bValid;

                /** @brief 바인딩의 레이어입니다(바인딩 → 액션). 없는 레이어는 바인딩 등록(`InputMap::beginBinding`)이 만든다. */
                hashed_string resolveLayer( const hashed_string& written ) const { return written.empty() ? _layer : written; }
                /** @brief 바인딩의 trigger 입니다. 바인딩 → 액션 → @p fallback 순서다(단일 키 · 조합은 Pressed, 축 합성은 Down). */
                ActionTrigger resolveTrigger( ActionTrigger written, ActionTrigger fallback ) const
                {
                    if ( written != ActionTrigger::Count )
                        return written;
                    return _trigger != ActionTrigger::Count ? _trigger : fallback;
                }
                /** @brief 바인딩 하나를 더한 뒤 공통 칸(발화 시간 · 반전 · 탭 수)을 겁니다. 더하지 못했으면 아무것도 하지 않습니다. */
                void applyCommon( uint32 countBefore, float32 time, bool bNegate, int32 taps ) const
                {
                    if ( _map.getBindingCount( _action ) <= countBefore )
                        return;
                    if ( time > 0.0f )
                        (void)_map.setBindingTriggerTime( _action, countBefore, time ); // 바로 위에서 바인딩이 있음을 확인했다
                    if ( bNegate )
                        (void)_map.setBindingNegate( _action, countBefore, true );
                    if ( taps != InputMapXMLInternal::kDefaultTaps )
                        (void)_map.setBindingTapCount( _action, countBefore, static_cast<uint8>( taps ) );
                }
                /** @brief 바인딩을 넣지 못한 까닭을 오류로 남깁니다. */
                void reject( const XMLNode& element, string_view reason ) const
                {
                    SW_LOG_ERROR_ID( "InputMapXML.BindingRejected", "%#: action '%#' <%#> %# - binding dropped", _sourceName, _action.c_str(), element.getName(), reason );
                    _bValid = false;
                }
            };

            static constexpr int32 kDefaultTaps = 2;

            /** @brief 원소 하나를 스키마로 읽습니다(자식 원소는 없어야 한다). */
            template <typename TFields>
            [[nodiscard]] static bool readLeaf( const XMLNode& element, TFields& outFields, const ActionContext& context )
            {
                const bool bFields   = XMLFields::readFields( element, outFields, context._sourceName );
                const bool bChildren = XMLNameCheck::reportUnknownChildren( element, nullptr, 0, context._sourceName );
                context._bValid      = bFields && bChildren && context._bValid;
                return bFields && bChildren;
            }

            /** @brief 패드 번호가 슬롯 안인지 봅니다. 밖이면 오류를 남기고 false 입니다(없는 패드에 말없이 묶지 않는다). */
            [[nodiscard]] static bool checkPad( const XMLNode& element, int32 pad, const ActionContext& context )
            {
                if ( pad < static_cast<int32>( kMaxGamepadSlot ) )
                    return true;
                context.reject( element, "names a pad outside 0..3" );
                return false;
            }

            /** @brief 장치 @p source 의 이름 @p name 을 슬롯으로 읽습니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool tryReadSlot( InputBindSource source, string_view name, int32 pad, InputSlot& outSlot )
            {
                switch ( source )
                {
                    case InputBindSource::Key:
                    {
                        const Key key = KeyCodeUtil::fromName( name );
                        outSlot       = InputSlot::fromKey( key );
                        return key != Key::Unknown;
                    }
                    case InputBindSource::Gamepad:
                    {
                        const GamepadButton button = GamepadButtonUtil::fromName( name );
                        outSlot                    = InputSlot::fromGamepadButton( button, static_cast<uint8>( pad ) );
                        return button != GamepadButton::Count;
                    }
                    case InputBindSource::Mouse:
                    {
                        const MouseButton button = MouseButtonUtil::fromName( name );
                        outSlot                  = InputSlot::fromMouseButton( button );
                        return button != MouseButton::Count;
                    }
                }
                return false;
            }

            /** @brief 슬롯의 장치와 이름입니다. 이름 붙일 수 없는 장치면 false 입니다. */
            [[nodiscard]] static bool tryNameSlot( const InputSlot& slot, InputBindSource& outSource, string& outName, int32& outPad )
            {
                outPad = static_cast<int32>( slot._deviceIndex );
                switch ( slot._deviceKind )
                {
                    case InputDeviceKind::Keyboard:
                        outSource = InputBindSource::Key;
                        outName   = KeyCodeUtil::toName( static_cast<Key>( slot._controlIndex ) );
                        outPad    = 0;
                        return true;
                    case InputDeviceKind::Gamepad:
                        outSource = InputBindSource::Gamepad;
                        outName   = GamepadButtonUtil::toName( static_cast<GamepadButton>( slot._controlIndex ) );
                        return true;
                    case InputDeviceKind::Mouse:
                        outSource = InputBindSource::Mouse;
                        outName   = MouseButtonUtil::toName( static_cast<MouseButton>( slot._controlIndex ) );
                        outPad    = 0;
                        return true;
                    default:
                        return false;
                }
            }

            static void readBind( const XMLNode& element, const ActionContext& context )
            {
                InputMapBindFields fields;
                if ( readLeaf( element, fields, context ) == false || checkPad( element, fields._pad, context ) == false )
                    return;
                const uint32        countBefore = context._map.getBindingCount( context._action );
                const hashed_string layer       = context.resolveLayer( fields._layer );
                const ActionTrigger trigger     = context.resolveTrigger( fields._trigger, ActionTrigger::Pressed );
                if ( fields._modifier.empty() == false )
                {
                    // `modifier` 가 있으면 단축키다(수정 키 마스크 + 키, 정확 일치 — `InputMap::bindShortcut`). 두 키 조합은 `<chord>` 다.
                    uint8     modifierMask = 0;
                    const Key key          = KeyCodeUtil::fromName( fields._code );
                    if ( fields._source != InputBindSource::Key || key == Key::Unknown || InputRemapSerializer::tryParseModifierNames( fields._modifier, modifierMask ) == false )
                    {
                        context.reject( element, "is not a key shortcut (modifier is Ctrl, Shift, Alt, Super joined by '+')" );
                        return;
                    }
                    context._map.bindShortcut( context._action, key, modifierMask, trigger, layer );
                }
                else if ( fields._source == InputBindSource::Gamepad && ( fields._code == "LeftStick" || fields._code == "RightStick" ) )
                {
                    context._map.bindGamepadStick2D( context._action, fields._code == "RightStick" ? GamepadStick::Right : GamepadStick::Left, fields._deadzone, layer, static_cast<uint8>( fields._pad ) );
                }
                else
                {
                    InputSlot slot{};
                    if ( tryReadSlot( fields._source, fields._code, fields._pad, slot ) == false )
                    {
                        context.reject( element, "names an unknown code for its source" );
                        return;
                    }
                    context._map.bind( context._action, slot, trigger, layer );
                }
                context.applyCommon( countBefore, fields._time, fields._bNegate, fields._taps );
            }

            static void readAxis1D( const XMLNode& element, const ActionContext& context )
            {
                InputMapAxis1DFields fields;
                if ( readLeaf( element, fields, context ) == false || checkPad( element, fields._pad, context ) == false )
                    return;
                InputSlot  negative{};
                InputSlot  positive{};
                const bool bNamed = fields._source != InputBindSource::Mouse && tryReadSlot( fields._source, fields._negative, fields._pad, negative ) &&
                                    tryReadSlot( fields._source, fields._positive, fields._pad, positive );
                if ( bNamed == false )
                {
                    context.reject( element, "needs two known keys or two known gamepad buttons" );
                    return;
                }
                // 축 값은 trigger 와 관계없이 누르는 동안 읽힌다. trigger 는 액션 발화(누를 때마다 한 칸 등)만 정한다.
                const uint32 countBefore = context._map.getBindingCount( context._action );
                context._map.bindAxis1DComposite( context._action, negative, positive, context.resolveLayer( fields._layer ), context.resolveTrigger( fields._trigger, ActionTrigger::Down ) );
                context.applyCommon( countBefore, fields._time, fields._bNegate, fields._taps );
            }

            static void readChord( const XMLNode& element, const ActionContext& context )
            {
                InputMapChordFields fields;
                if ( readLeaf( element, fields, context ) == false )
                    return;
                const uint32 countBefore = context._map.getBindingCount( context._action );
                context._map.bindChord( context._action, fields._modifier, fields._trigger, context.resolveTrigger( fields._triggerMode, ActionTrigger::Pressed ), context.resolveLayer( fields._layer ) );
                context.applyCommon( countBefore, fields._time, fields._bNegate, fields._taps );
            }

            static void readElement( const XMLNode& element, const ActionContext& context )
            {
                const string_view name{ element.getName() };
                if ( name == "bind" )
                    return readBind( element, context );
                if ( name == "axis1d" )
                    return readAxis1D( element, context );
                if ( name == "chord" )
                    return readChord( element, context );
                if ( name == "vector2d" )
                {
                    InputMapVector2DFields fields;
                    if ( readLeaf( element, fields, context ) )
                        context._map.bindVector2D( context._action, fields._up, fields._down, fields._left, fields._right, fields._deadzone, context.resolveLayer( fields._layer ) );
                    return;
                }
                if ( name == "stick" )
                {
                    InputMapStickFields fields;
                    if ( readLeaf( element, fields, context ) && checkPad( element, fields._pad, context ) )
                        context._map.bindGamepadStick2D( context._action, fields._stick, fields._deadzone, context.resolveLayer( fields._layer ), static_cast<uint8>( fields._pad ), fields._outerDeadzone,
                                                         fields._responseExponent, float2{ fields._scaleX, fields._scaleY }, fields._bScaleByDelta, fields._axis );
                    return;
                }
                if ( name == "mouseDelta" || name == "mouseWheel" )
                {
                    InputMapMouseAxisFields fields;
                    if ( readLeaf( element, fields, context ) == false )
                        return;
                    if ( name == "mouseDelta" )
                        context._map.bindMouseDelta( context._action, fields._scale, context.resolveLayer( fields._layer ) );
                    else
                        context._map.bindMouseWheel( context._action, fields._scale, context.resolveLayer( fields._layer ) );
                    return;
                }
                if ( name == "virtualJoystick" )
                {
                    InputMapVirtualJoystickFields fields;
                    if ( readLeaf( element, fields, context ) )
                        context._map.bindVirtualJoystick2D( context._action, fields._button, fields._radius, fields._deadzone, context.resolveLayer( fields._layer ), fields._outerDeadzone );
                    return;
                }
                InputMapAnyKeyFields fields; // anyKey — 원소 이름은 readAction 이 걸렀다
                if ( readLeaf( element, fields, context ) )
                    context._map.bindAnyKey( context._action, context.resolveLayer( fields._layer ) );
            }

            /** @brief 바인딩 하나를 원소로 씁니다. */
            [[nodiscard]] static bool writeBinding( const XMLNode& actionNode, const hashed_string& action, const ActionBinding& binding )
            {
                const auto fillCommon = [&binding]( auto& fields )
                {
                    fields._layer   = binding._layer;
                    fields._time    = binding._triggerTime;
                    fields._bNegate = binding._bNegate;
                    fields._taps    = binding._tapCount;
                };
                switch ( binding._kind )
                {
                    case BindingKind::SingleSlot:
                    case BindingKind::Shortcut:
                    {
                        InputMapBindFields fields;
                        fillCommon( fields );
                        fields._trigger = binding._trigger;
                        if ( tryNameSlot( binding._arrSlot[0], fields._source, fields._code, fields._pad ) == false )
                            break;
                        if ( binding._kind == BindingKind::Shortcut )
                            fields._modifier = InputRemapSerializer::formatModifierNames( binding._modifierMask );
                        XMLFields::writeFields( actionNode.appendChild( "bind" ), fields );
                        return true;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        InputMapAxis1DFields fields;
                        fillCommon( fields );
                        fields._trigger = binding._trigger;
                        InputBindSource positiveSource{};
                        int32           positivePad{ 0 };
                        const bool      bNamed = tryNameSlot( binding._arrSlot[0], fields._source, fields._negative, fields._pad ) &&
                                            tryNameSlot( binding._arrSlot[1], positiveSource, fields._positive, positivePad );
                        if ( bNamed == false || positiveSource != fields._source || positivePad != fields._pad || fields._source == InputBindSource::Mouse )
                            break; // 두 장치에 걸친 축은 이 형식이 이름 붙이지 못한다
                        XMLFields::writeFields( actionNode.appendChild( "axis1d" ), fields );
                        return true;
                    }
                    case BindingKind::Chord:
                    {
                        InputMapChordFields fields;
                        fillCommon( fields );
                        fields._modifier    = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                        fields._trigger     = static_cast<Key>( binding._arrSlot[1]._controlIndex );
                        fields._triggerMode = binding._trigger;
                        XMLFields::writeFields( actionNode.appendChild( "chord" ), fields );
                        return true;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        InputMapVector2DFields fields;
                        fields._up       = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                        fields._down     = static_cast<Key>( binding._arrSlot[1]._controlIndex );
                        fields._left     = static_cast<Key>( binding._arrSlot[2]._controlIndex );
                        fields._right    = static_cast<Key>( binding._arrSlot[3]._controlIndex );
                        fields._deadzone = binding._deadzone;
                        fields._layer    = binding._layer;
                        XMLFields::writeFields( actionNode.appendChild( "vector2d" ), fields );
                        return true;
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        InputMapStickFields fields;
                        fields._stick            = binding._stick;
                        fields._pad              = static_cast<int32>( binding._deviceIndex );
                        fields._deadzone         = binding._deadzone;
                        fields._outerDeadzone    = binding._outerDeadzone;
                        fields._responseExponent = binding._responseExponent;
                        fields._scaleX           = binding._axisScale._x;
                        fields._scaleY           = binding._axisScale._y;
                        fields._bScaleByDelta    = binding._bScaleByDelta;
                        fields._axis             = binding._stickAxis;
                        fields._layer            = binding._layer;
                        XMLFields::writeFields( actionNode.appendChild( "stick" ), fields );
                        return true;
                    }
                    case BindingKind::MouseDelta2D:
                    case BindingKind::MouseWheel1D:
                    {
                        InputMapMouseAxisFields fields;
                        fields._scale = binding._scale;
                        fields._layer = binding._layer;
                        XMLFields::writeFields( actionNode.appendChild( binding._kind == BindingKind::MouseDelta2D ? "mouseDelta" : "mouseWheel" ), fields );
                        return true;
                    }
                    case BindingKind::VirtualJoystick2D:
                    {
                        InputMapVirtualJoystickFields fields;
                        fields._button        = static_cast<MouseButton>( binding._arrSlot[0]._controlIndex );
                        fields._radius        = binding._scale;
                        fields._deadzone      = binding._deadzone;
                        fields._outerDeadzone = binding._outerDeadzone;
                        fields._layer         = binding._layer;
                        XMLFields::writeFields( actionNode.appendChild( "virtualJoystick" ), fields );
                        return true;
                    }
                    case BindingKind::AnyKey:
                    {
                        InputMapAnyKeyFields fields;
                        fields._layer = binding._layer;
                        XMLFields::writeFields( actionNode.appendChild( "anyKey" ), fields );
                        return true;
                    }
                    case BindingKind::Count:
                        break;
                }
                SW_LOG_ERROR_ID( "InputMapXML.BindingNotWritable", "Action '%#' has a %# binding the input map format cannot name (two devices or an unnamed device) - not saved", action.c_str(),
                                 BindingKinds::toName( binding._kind ) );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void InputMapXML::readAction( InputMap& map, const XMLNode& actionNode, const hashed_string& inheritedLayer, string_view sourceName, bool& inoutbValid )
    {
        using Internal = InputMapXMLInternal;
        // `valueType` 은 적지 않음이 뜻이다(바인딩이 정한다) — 스키마 칸이 아니라 적었을 때만 읽는다.
        InputMapActionFields                fields;
        std::optional<InputActionValueType> valueType;
        const bool                          bFields   = XMLFields::readFields( actionNode, fields, sourceName, { "valueType" } ) && XMLFields::readOptionalEnum( actionNode, "valueType", valueType, sourceName );
        const bool                          bChildren = XMLNameCheck::reportUnknownChildren( actionNode, Internal::kArrBindingElement, sourceName );
        if ( bFields == false || bChildren == false )
        {
            inoutbValid = false;
            return;
        }
        // 값 종류를 적은 액션은 바인딩보다 먼저 만든다. 바인딩 없이 이름만 있는 액션(에디터의 "Add Action")도 다시 읽혀야 하기 때문이다.
        if ( valueType.has_value() )
            map.createAction( fields._name, *valueType );
        const hashed_string actionLayer = fields._layer.empty() ? ( inheritedLayer.empty() ? map.getDefaultLayerName() : inheritedLayer ) : fields._layer;
        map.ensureLayer( actionLayer );

        // 연속 값 바인딩(2D 합성, 스틱, 마우스 이동량)은 매 프레임 읽는 값이라 Down 으로 고정한다. 다른 trigger 는 조용히 버리지 않고 경고한다.
        const bool bContinuous = actionNode.findChild( "vector2d" ).isValid() || actionNode.findChild( "stick" ).isValid() || actionNode.findChild( "mouseDelta" ).isValid();
        if ( fields._trigger != ActionTrigger::Count && fields._trigger != ActionTrigger::Down && bContinuous )
            SW_LOG_WARNING_ID( "Input.ContinuousTriggerIgnored", "Action '%#' sets trigger '%#', but its vector2d/stick/mouseDelta bindings are continuous values read every frame - the trigger is ignored for them",
                               fields._name.c_str(), getEnumName( fields._trigger ) );

        const Internal::ActionContext context{ map, fields._name, actionLayer, fields._trigger, sourceName, inoutbValid };
        for ( XMLNode element = actionNode.findChild(); element.isValid(); element = element.findNextSibling() )
        {
            Internal::readElement( element, context );
        }
    }

    bool InputMapXML::writeAction( const XMLNode& parent, const InputMap& map, const hashed_string& action )
    {
        InputMapActionFields fields;
        fields._name             = action;
        const XMLNode actionNode = parent.appendChild( "action" );
        XMLFields::writeFields( actionNode, fields );
        const InputMap::ActionEntry* pEntry = map.findAction( action );
        if ( pEntry != nullptr )
            actionNode.appendAttribute( "valueType", getEnumName( pEntry->_valueType ) );
        bool         bAllWritten = true;
        const uint32 bindCount   = map.getBindingCount( action );
        for ( uint32 bindIndex = 0; bindIndex < bindCount; ++bindIndex )
        {
            bAllWritten = InputMapXMLInternal::writeBinding( actionNode, action, *map.getBinding( action, bindIndex ) ) && bAllWritten; // 개수 안의 인덱스라 늘 있다
        }
        return bAllWritten;
    }
} // namespace sw
