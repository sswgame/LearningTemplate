#include "pch.h"

#include "Engine/Input/ActionMap.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	namespace
	{
		struct ActionMapInternal
		{
			struct InputMapXml
			{
				static constexpr const utf8* kRoot				  = "InputMap";
				static constexpr const utf8* kLayers			  = "layers";
				static constexpr const utf8* kLayer				  = "layer";
				static constexpr const utf8* kAction			  = "action";
				static constexpr const utf8* kBind				  = "bind";
				static constexpr const utf8* kAttrDefaultLayer	  = "defaultLayer";
				static constexpr const utf8* kAttrDoubleClick	  = "doubleClickTime";
				static constexpr const utf8* kAttrDoubleClickDist = "doubleClickMaxDistance";
				static constexpr const utf8* kAttrHoldThreshold	  = "holdThreshold";
				static constexpr const utf8* kAttrName			  = "name";
				static constexpr const utf8* kAttrPriority		  = "priority";
				static constexpr const utf8* kAttrEnabled		  = "enabled";
				static constexpr const utf8* kAttrBlockLower	  = "blockLower";
				static constexpr const utf8* kAttrAlwaysOn		  = "alwaysOn";
				static constexpr const utf8* kAttrLayer			  = "layer";
				static constexpr const utf8* kAttrTrigger		  = "trigger";
				static constexpr const utf8* kAttrSource		  = "source";
				static constexpr const utf8* kAttrCode			  = "code";
				static constexpr const utf8* kAttrModifier		  = "modifier";
				static constexpr const utf8* kAttrDeadzone		  = "deadzone";
				static constexpr const utf8* kSourceKey			  = "key";
				static constexpr const utf8* kSourceGamepad		  = "gamepad";
				static constexpr const utf8* kSourceMouse		  = "mouse";
			};

			struct TriggerNameEntry
			{
				const utf8*	  _pName;
				ActionTrigger _trigger;
			};

			static constexpr TriggerNameEntry kArrTriggerNames[] = {
				{		  "Pressed",		 ActionTrigger::Pressed},
				{		  "Press",		   ActionTrigger::Pressed},
				{		  "Started",		 ActionTrigger::Pressed},
				{		  "Down",			  ActionTrigger::Down},
				{		  "Held",			  ActionTrigger::Down},
				{	  "Performed",		   ActionTrigger::Down},
				{	  "Released",		  ActionTrigger::Released},
				{		  "Release",		 ActionTrigger::Released},
				{	  "Canceled",		  ActionTrigger::Released},
				{ "DoubleClicked",  ActionTrigger::DoubleClicked},
				{	  "DoubleClick",	 ActionTrigger::DoubleClicked},
				{ "HoldThreshold",  ActionTrigger::HoldThreshold},
				{		  "Hold",  ActionTrigger::HoldThreshold},
				{"HoldAndRelease", ActionTrigger::HoldAndRelease},
				{			  "Tap",			 ActionTrigger::Tap},
				{		  "Pulse",		   ActionTrigger::Pulse},
				{	  "DoubleTap",	   ActionTrigger::DoubleTap},
				{		  "Repeat",			ActionTrigger::Repeat},
				{	  "NavRepeat",		   ActionTrigger::Repeat},
			};

			static string slotToGlyph( const InputSlot& slot, InputDeviceType device )
			{
				if ( slot._deviceKind == InputDeviceKind::Keyboard )
				{
					const Key key = static_cast<Key>( slot._controlIndex );
					if ( key != Key::Unknown )
					{
						const utf8* pName = KeyCodes::toName( key );
						return pName != nullptr ? pName : "?";
					}
				}
				else if ( slot._deviceKind == InputDeviceKind::Mouse )
				{
					const MouseButton btn = static_cast<MouseButton>( slot._controlIndex );
					if ( btn != MouseButton::Count )
					{
						const utf8* pName = MouseButtons::toName( btn );
						return pName != nullptr ? pName : "?";
					}
				}
				else if ( slot._deviceKind == InputDeviceKind::Gamepad )
				{
					const GamepadButton btn = static_cast<GamepadButton>( slot._controlIndex );
					if ( btn != GamepadButton::Count )
					{
						if ( device == InputDeviceType::GamepadPlayStation )
						{
							if ( btn == GamepadButton::A )
								return "X";
							if ( btn == GamepadButton::B )
								return "Circle";
							if ( btn == GamepadButton::X )
								return "Square";
							if ( btn == GamepadButton::Y )
								return "Triangle";
						}
						else if ( device == InputDeviceType::GamepadSwitch )
						{
							// 닌텐도 배치: Xbox 기준 A/B, X/Y 위치가 서로 뒤바뀝니다.
							if ( btn == GamepadButton::A )
								return "B";
							if ( btn == GamepadButton::B )
								return "A";
							if ( btn == GamepadButton::X )
								return "Y";
							if ( btn == GamepadButton::Y )
								return "X";
						}
						const utf8* pName = GamepadButtons::toName( btn );
						return pName != nullptr ? pName : "?";
					}
				}
				return "?";
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "ActionMap" );

	ActionMap::ActionMap()
		: _pInput{ nullptr }
		, _mapAction{}
		, _mapLayer{}
		, _listActionEntry{}
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
		_listActionName.clear();
		_listLayerName.clear();
		_listLayerStack.clear();
		_bufferedActionCount	= 0;
		_bufferedActionHead		= 0;
		_commandHistoryCount	= 0;
		_commandHistoryHead		= 0;
		_defaultLayerName		= hashed_string( ActionMapDefaults::kDefaultLayerName );
		_doubleClickTime		= ActionMapDefaults::kDoubleClickTime;
		_doubleClickMaxDistance = ActionMapDefaults::kDoubleClickMaxDistance;
		_holdThreshold			= ActionMapDefaults::kHoldThreshold;
		_totalElapsedTime		= 0.0f;
	}

	bool ActionMap::loadFromResource( string_view relativePath )
	{
		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( relativePath, &absPath ) == false )
		{
			SW_LOG_WARNING( "Failed to load InputMap %#", relativePath );
			return false;
		}

		XmlNode root = doc.root( ActionMapInternal::InputMapXml::kRoot );
		if ( root.isValid() == false )
		{
			SW_LOG_WARNING( "Missing <InputMap> in %#", absPath );
			return false;
		}

		const float32 dblClick	= root.attrFloat( ActionMapInternal::InputMapXml::kAttrDoubleClick, ActionMapDefaults::kDoubleClickTime );
		const float32 dblDist	= root.attrFloat( ActionMapInternal::InputMapXml::kAttrDoubleClickDist, ActionMapDefaults::kDoubleClickMaxDistance );
		const float32 holdThr	= root.attrFloat( ActionMapInternal::InputMapXml::kAttrHoldThreshold, ActionMapDefaults::kHoldThreshold );
		const utf8*	  pDefLayer = root.attr( ActionMapInternal::InputMapXml::kAttrDefaultLayer );

		clear();
		setDoubleClickTime( dblClick );
		setDoubleClickMaxDistance( dblDist );
		setHoldThreshold( holdThr );
		if ( pDefLayer != nullptr && pDefLayer[0] != '\0' )
			_defaultLayerName = hashed_string( pDefLayer );

		XmlNode layersNode = root.child( ActionMapInternal::InputMapXml::kLayers );
		if ( layersNode.isValid() )
		{
			for ( XmlNode layerNode = layersNode.child( ActionMapInternal::InputMapXml::kLayer ); layerNode.isValid();
				  layerNode			= layerNode.next( ActionMapInternal::InputMapXml::kLayer ) )
			{
				const utf8* pLayerName = layerNode.attr( ActionMapInternal::InputMapXml::kAttrName );
				if ( pLayerName == nullptr || pLayerName[0] == '\0' )
					continue;
				const int32 priority   = layerNode.attrInt( ActionMapInternal::InputMapXml::kAttrPriority, 0 );
				const bool	enabled	   = layerNode.attrBool( ActionMapInternal::InputMapXml::kAttrEnabled, true );
				const bool	blockLower = layerNode.attrBool( ActionMapInternal::InputMapXml::kAttrBlockLower, false );
				const bool	alwaysOn   = layerNode.attrBool( ActionMapInternal::InputMapXml::kAttrAlwaysOn, false );
				registerLayer( pLayerName, priority, enabled, blockLower, alwaysOn );
			}
		}

		ensureLayer( _defaultLayerName, 0, true, false );

		auto loadAction = [this]( XmlNode actionNode, string_view inheritedLayer )
		{
			const utf8* pActionName = actionNode.attr( ActionMapInternal::InputMapXml::kAttrName );
			if ( pActionName == nullptr || pActionName[0] == '\0' )
				return;

			hashed_string layer		 = inheritedLayer.empty() ? _defaultLayerName : hashed_string( inheritedLayer );
			const utf8*	  pLayerAttr = actionNode.attr( ActionMapInternal::InputMapXml::kAttrLayer );
			if ( pLayerAttr != nullptr && pLayerAttr[0] != '\0' )
				layer = hashed_string( pLayerAttr );
			ensureLayer( layer );

			auto		defaultTrigger = ActionTrigger::Pressed;
			const utf8* pTriggerAttr   = actionNode.attr( ActionMapInternal::InputMapXml::kAttrTrigger );
			if ( pTriggerAttr != nullptr )
			{
				const ActionTrigger parsed = actionTriggerFromName( pTriggerAttr );
				if ( parsed != ActionTrigger::Count )
					defaultTrigger = parsed;
			}

			// 1) <bind> 태그 파싱
			for ( XmlNode bindNode = actionNode.child( ActionMapInternal::InputMapXml::kBind ); bindNode.isValid();
				  bindNode		   = bindNode.next( ActionMapInternal::InputMapXml::kBind ) )
			{
				const utf8* pSource = bindNode.attr( ActionMapInternal::InputMapXml::kAttrSource );
				const utf8* pCode	= bindNode.attr( ActionMapInternal::InputMapXml::kAttrCode );
				if ( pSource == nullptr || pCode == nullptr || pCode[0] == '\0' )
					continue;

				ActionTrigger trigger		   = defaultTrigger;
				const utf8*	  pBindTriggerAttr = bindNode.attr( ActionMapInternal::InputMapXml::kAttrTrigger );
				if ( pBindTriggerAttr != nullptr )
				{
					const ActionTrigger parsed = actionTriggerFromName( pBindTriggerAttr );
					if ( parsed != ActionTrigger::Count )
						trigger = parsed;
				}

				hashed_string bindLayer		 = layer;
				const utf8*	  pBindLayerAttr = bindNode.attr( ActionMapInternal::InputMapXml::kAttrLayer );
				if ( pBindLayerAttr != nullptr && pBindLayerAttr[0] != '\0' )
				{
					bindLayer = hashed_string( pBindLayerAttr );
					ensureLayer( bindLayer );
				}

				const utf8* pModifierAttr = bindNode.attr( ActionMapInternal::InputMapXml::kAttrModifier );
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

				if ( StringUtil::equals( pSource, ActionMapInternal::InputMapXml::kSourceKey, true ) )
				{
					const Key key = KeyCodes::fromName( pCode );
					if ( key != Key::Unknown )
						bind( pActionName, key, trigger, bindLayer.view() );
				}
				else if ( StringUtil::equals( pSource, ActionMapInternal::InputMapXml::kSourceGamepad, true ) )
				{
					if ( StringUtil::equals( pCode, "LeftStick", true ) )
					{
						const float32 deadzone = bindNode.attrFloat( ActionMapInternal::InputMapXml::kAttrDeadzone, 0.15f );
						bindGamepadStick2D( pActionName, GamepadStick::Left, deadzone, bindLayer.view() );
					}
					else if ( StringUtil::equals( pCode, "RightStick", true ) )
					{
						const float32 deadzone = bindNode.attrFloat( ActionMapInternal::InputMapXml::kAttrDeadzone, 0.15f );
						bindGamepadStick2D( pActionName, GamepadStick::Right, deadzone, bindLayer.view() );
					}
					else
					{
						const GamepadButton button = GamepadButtons::fromName( pCode );
						if ( button != GamepadButton::Count )
							bind( pActionName, button, trigger, bindLayer.view() );
					}
				}
				else if ( StringUtil::equals( pSource, ActionMapInternal::InputMapXml::kSourceMouse, true ) )
				{
					const MouseButton mouse = MouseButtons::fromName( pCode );
					if ( mouse != MouseButton::Count )
						bind( pActionName, mouse, trigger, bindLayer.view() );
				}
			}

			// 2) <vector2d> 태그 파싱
			for ( XmlNode compNode = actionNode.child( "vector2d" ); compNode.isValid(); compNode = compNode.next( "vector2d" ) )
			{
				const Key	  upKey			 = KeyCodes::fromName( compNode.attr( "up" ) );
				const Key	  downKey		 = KeyCodes::fromName( compNode.attr( "down" ) );
				const Key	  leftKey		 = KeyCodes::fromName( compNode.attr( "left" ) );
				const Key	  rightKey		 = KeyCodes::fromName( compNode.attr( "right" ) );
				const float32 deadzone		 = compNode.attrFloat( "deadzone", 0.0f );
				hashed_string compLayer		 = layer;
				const utf8*	  pCompLayerAttr = compNode.attr( "layer" );
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
				const Key	  posKey		 = KeyCodes::fromName( axisNode.attr( "positive" ) );
				const Key	  negKey		 = KeyCodes::fromName( axisNode.attr( "negative" ) );
				hashed_string axisLayer		 = layer;
				const utf8*	  pAxisLayerAttr = axisNode.attr( "layer" );
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
				const utf8*		   pStickName	   = stickNode.attr( "stick" );
				const GamepadStick stick		   = ( pStickName != nullptr && StringUtil::equals( pStickName, "Right", true ) ) ? GamepadStick::Right : GamepadStick::Left;
				const float32	   deadzone		   = stickNode.attrFloat( "deadzone", 0.15f );
				hashed_string	   stickLayer	   = layer;
				const utf8*		   pStickLayerAttr = stickNode.attr( "layer" );
				if ( pStickLayerAttr != nullptr && pStickLayerAttr[0] != '\0' )
				{
					stickLayer = hashed_string( pStickLayerAttr );
					ensureLayer( stickLayer );
				}
				const uint8	  padIndex		   = static_cast<uint8>( stickNode.attrInt( "pad", 0 ) );
				const float32 outerDeadzone	   = stickNode.attrFloat( "outerDeadzone", 1.0f );
				const float32 responseExponent = stickNode.attrFloat( "responseExponent", 1.0f );
				bindGamepadStick2D( pActionName, stick, deadzone, stickLayer.view(), padIndex, outerDeadzone, responseExponent );
			}

			// 5) <chord> 태그 파싱
			for ( XmlNode chordNode = actionNode.child( "chord" ); chordNode.isValid(); chordNode = chordNode.next( "chord" ) )
			{
				const Key	  modKey	= KeyCodes::fromName( chordNode.attr( "modifier" ) );
				const Key	  trigKey	= KeyCodes::fromName( chordNode.attr( "trigger" ) );
				ActionTrigger trig		= defaultTrigger;
				const utf8*	  pTrigAttr = chordNode.attr( "triggerMode" );
				if ( pTrigAttr != nullptr && pTrigAttr[0] != '\0' )
				{
					const ActionTrigger parsed = actionTriggerFromName( pTrigAttr );
					if ( parsed != ActionTrigger::Count )
						trig = parsed;
				}
				hashed_string chordLayer	  = layer;
				const utf8*	  pChordLayerAttr = chordNode.attr( "layer" );
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

		for ( XmlNode layerNode = root.child( ActionMapInternal::InputMapXml::kLayer ); layerNode.isValid(); layerNode = layerNode.next( ActionMapInternal::InputMapXml::kLayer ) )
		{
			const utf8* pLayerName = layerNode.attr( ActionMapInternal::InputMapXml::kAttrName );
			if ( pLayerName == nullptr || pLayerName[0] == '\0' )
				continue;
			if ( hasLayer( pLayerName ) == false )
			{
				const int32 priority   = layerNode.attrInt( ActionMapInternal::InputMapXml::kAttrPriority, 0 );
				const bool	enabled	   = layerNode.attrBool( ActionMapInternal::InputMapXml::kAttrEnabled, true );
				const bool	blockLower = layerNode.attrBool( ActionMapInternal::InputMapXml::kAttrBlockLower, false );
				const bool	alwaysOn   = layerNode.attrBool( ActionMapInternal::InputMapXml::kAttrAlwaysOn, false );
				registerLayer( pLayerName, priority, enabled, blockLower, alwaysOn );
			}
			for ( XmlNode actionNode = layerNode.child( ActionMapInternal::InputMapXml::kAction ); actionNode.isValid();
				  actionNode		 = actionNode.next( ActionMapInternal::InputMapXml::kAction ) )
			{
				loadAction( actionNode, pLayerName );
			}
		}

		for ( XmlNode actionNode = root.child( ActionMapInternal::InputMapXml::kAction ); actionNode.isValid();
			  actionNode		 = actionNode.next( ActionMapInternal::InputMapXml::kAction ) )
		{
			loadAction( actionNode, _defaultLayerName.view() );
		}

		return true;
	}

	void ActionMap::bindDefaultFallback()
	{
		clear();
		registerLayer( ActionMapDefaults::kDebugLayerName, 1000, true, false, true );
		registerLayer( "UI", 100, true, false, false );
		registerLayer( ActionMapDefaults::kDefaultLayerName, 0, true, false, false );

		// 디버그 핫키 격리: Ctrl + F6, Ctrl + F7, Ctrl + F8 (게임플레이 F1~F12 스킬과 충돌 원천 차단)
		bindChord( ActionMapDefaults::kReloadEditorAction, Key::LeftControl, Key::F6, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );
		bindChord( ActionMapDefaults::kReloadGameAction, Key::LeftControl, Key::F7, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );
		bindChord( ActionMapDefaults::kReloadShadersAction, Key::LeftControl, Key::F8, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );

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

	void ActionMap::createAction( string_view action, InputActionValueType valueType )
	{
		if ( action.empty() )
			return;
		getOrCreateAction( hashed_string( action ), valueType );
	}

	void ActionMap::bind( string_view action, InputSlot slot, ActionTrigger trigger, string_view layer )
	{
		if ( action.empty() )
			return;

		const hashed_string layerStr = layer.empty() ? _defaultLayerName : hashed_string( layer );
		ensureLayer( layerStr );
		const hashed_string actionHS( action );
		ensureActionListed( actionHS );

		ActionEntry&  entry = getOrCreateAction( actionHS, InputActionValueType::Boolean );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::SingleSlot;
		binding._trigger	  = trigger;
		binding._arrSlot[0]	  = slot;
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listDefaultBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	void ActionMap::bind( string_view action, Key key, ActionTrigger trigger, string_view layer )
	{
		if ( key == Key::Unknown )
			return;
		bind( action, InputSlot::fromKey( key ), trigger, layer );
	}

	void ActionMap::bind( string_view action, GamepadButton button, ActionTrigger trigger, string_view layer )
	{
		if ( button == GamepadButton::Count )
			return;
		bind( action, InputSlot::fromGamepadButton( button ), trigger, layer );
	}

	void ActionMap::bind( string_view action, MouseButton mouse, ActionTrigger trigger, string_view layer )
	{
		if ( mouse == MouseButton::Count )
			return;
		bind( action, InputSlot::fromMouseButton( mouse ), trigger, layer );
	}

	void ActionMap::bindAxis1DComposite( string_view action, Key negativeKey, Key positiveKey, string_view layer )
	{
		if ( action.empty() )
			return;

		const hashed_string layerStr = layer.empty() ? _defaultLayerName : hashed_string( layer );
		ensureLayer( layerStr );
		const hashed_string actionHS( action );
		ensureActionListed( actionHS );

		ActionEntry&  entry = getOrCreateAction( actionHS, InputActionValueType::Axis1D );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::Axis1DComposite;
		binding._trigger	  = ActionTrigger::Down;
		binding._arrSlot[0]	  = InputSlot::fromKey( negativeKey );
		binding._arrSlot[1]	  = InputSlot::fromKey( positiveKey );
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listDefaultBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	float32 ActionMap::getAxis1D( string_view action ) const
	{
		return getAxis1D( hashed_string( action ) );
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

	void ActionMap::bindVector2D( string_view action, Key up, Key down, Key left, Key right, float32 deadzone, string_view layer )
	{
		if ( action.empty() )
			return;

		const hashed_string layerStr = layer.empty() ? _defaultLayerName : hashed_string( layer );
		ensureLayer( layerStr );
		const hashed_string actionHS( action );
		ensureActionListed( actionHS );

		ActionEntry&  entry = getOrCreateAction( actionHS, InputActionValueType::Axis2D );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::Vector2DComposite;
		binding._trigger	  = ActionTrigger::Down;
		binding._arrSlot[0]	  = InputSlot::fromKey( up );
		binding._arrSlot[1]	  = InputSlot::fromKey( down );
		binding._arrSlot[2]	  = InputSlot::fromKey( left );
		binding._arrSlot[3]	  = InputSlot::fromKey( right );
		binding._deadzone	  = deadzone;
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listDefaultBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	void ActionMap::bindGamepadStick2D( string_view action, GamepadStick stick, float32 deadzone, string_view layer, uint8 padIndex, float32 outerDeadzone, float32 responseExponent )
	{
		if ( action.empty() )
			return;

		const hashed_string layerStr = layer.empty() ? _defaultLayerName : hashed_string( layer );
		ensureLayer( layerStr );
		const hashed_string actionHS( action );
		ensureActionListed( actionHS );

		ActionEntry&  entry = getOrCreateAction( actionHS, InputActionValueType::Axis2D );
		ActionBinding binding{};
		binding._layer			  = layerStr;
		binding._kind			  = BindingKind::GamepadStick2D;
		binding._trigger		  = ActionTrigger::Down;
		binding._deviceIndex	  = padIndex;
		binding._stick			  = stick;
		binding._deadzone		  = deadzone;
		binding._outerDeadzone	  = outerDeadzone;
		binding._responseExponent = responseExponent;
		binding._pCachedLayer	  = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listDefaultBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	float2 ActionMap::getVector2D( string_view action ) const
	{
		return getVector2D( hashed_string( action ) );
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

	void ActionMap::bindMouseDelta( string_view action, float32 sensitivity, string_view layer )
	{
		bindMouseDelta( hashed_string( action ), sensitivity, hashed_string( layer ) );
	}

	void ActionMap::bindMouseDelta( const hashed_string& action, float32 sensitivity, const hashed_string& layer )
	{
		if ( action.empty() )
			return;

		const hashed_string layerStr = layer.empty() ? _defaultLayerName : layer;
		ensureLayer( layerStr );
		ensureActionListed( action );

		ActionEntry&  entry = getOrCreateAction( action, InputActionValueType::Axis2D );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::MouseDelta2D;
		binding._trigger	  = ActionTrigger::Down;
		binding._scale		  = sensitivity;
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listDefaultBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	void ActionMap::bindShortcut( string_view action, Key key, uint8 modifierMask, ActionTrigger trigger, string_view layer )
	{
		bindShortcut( hashed_string( action ), key, modifierMask, trigger, hashed_string( layer ) );
	}

	void ActionMap::bindShortcut( const hashed_string& action, Key key, uint8 modifierMask, ActionTrigger trigger, const hashed_string& layer )
	{
		if ( action.empty() || key == Key::Unknown )
			return;

		const hashed_string layerStr = layer.empty() ? _defaultLayerName : layer;
		ensureLayer( layerStr );
		ensureActionListed( action );

		ActionEntry&  entry = getOrCreateAction( action, InputActionValueType::Boolean );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::Shortcut;
		binding._trigger	  = trigger;
		binding._modifierMask = modifierMask;
		binding._arrSlot[0]	  = InputSlot::fromKey( key );
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listDefaultBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	void ActionMap::bindAnyKey( string_view action, string_view layer )
	{
		bindAnyKey( hashed_string( action ), hashed_string( layer ) );
	}

	void ActionMap::bindAnyKey( const hashed_string& action, const hashed_string& layer )
	{
		if ( action.empty() )
			return;

		const hashed_string layerStr = layer.empty() ? _defaultLayerName : layer;
		ensureLayer( layerStr );
		ensureActionListed( action );

		ActionEntry&  entry = getOrCreateAction( action, InputActionValueType::Boolean );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::AnyKey;
		binding._trigger	  = ActionTrigger::Pressed;
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listDefaultBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	void ActionMap::bindChord( string_view action, Key modifierKey, Key triggerKey, ActionTrigger trigger, string_view layer )
	{
		if ( action.empty() || modifierKey == Key::Unknown || triggerKey == Key::Unknown )
			return;

		const hashed_string layerStr = layer.empty() ? _defaultLayerName : hashed_string( layer );
		ensureLayer( layerStr );
		const hashed_string actionHS( action );
		ensureActionListed( actionHS );

		ActionEntry&  entry = getOrCreateAction( actionHS, InputActionValueType::Boolean );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::Chord;
		binding._trigger	  = trigger;
		binding._arrSlot[0]	  = InputSlot::fromKey( modifierKey );
		binding._arrSlot[1]	  = InputSlot::fromKey( triggerKey );
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listDefaultBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	bool ActionMap::isChordDown( string_view action ) const
	{
		return isChordDown( hashed_string( action ) );
	}

	bool ActionMap::isChordDown( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || _pInput == nullptr )
			return false;

		for ( const ActionBinding& binding : pEntry->_listBinding )
		{
			if ( binding._kind == BindingKind::Chord && isBindingLayerActive( binding ) )
			{
				const Key modKey  = static_cast<Key>( binding._arrSlot[0]._controlIndex );
				const Key trigKey = static_cast<Key>( binding._arrSlot[1]._controlIndex );
				if ( _pInput->isKeyDown( modKey ) && _pInput->isKeyDown( trigKey ) )
					return true;
			}
		}
		return false;
	}

	bool ActionMap::wasChordTriggered( string_view action ) const
	{
		return wasChordTriggered( hashed_string( action ) );
	}

	bool ActionMap::wasChordTriggered( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || _pInput == nullptr )
			return false;

		for ( const ActionBinding& binding : pEntry->_listBinding )
		{
			if ( binding._kind == BindingKind::Chord && isBindingLayerActive( binding ) )
			{
				const Key modKey  = static_cast<Key>( binding._arrSlot[0]._controlIndex );
				const Key trigKey = static_cast<Key>( binding._arrSlot[1]._controlIndex );
				if ( _pInput->isKeyDown( modKey ) && _pInput->wasKeyPressed( trigKey ) )
					return true;
			}
		}
		return false;
	}

	void ActionMap::bindActionCallback( string_view action, ActionTrigger trigger, ActionCallbackDelegate callback )
	{
		if ( action.empty() || callback.isBound() == false )
			return;
		ActionEntry&		entry = getOrCreateAction( action );
		ActionCallbackEntry cbEntry{};
		cbEntry._trigger  = trigger;
		cbEntry._callback = std::move( callback );
		entry._listActionCallback.push_back( std::move( cbEntry ) );
	}

	void ActionMap::bindPhaseCallback( string_view action, ActionPhase phase, ActionCallbackDelegate callback )
	{
		if ( action.empty() || callback.isBound() == false )
			return;
		ActionEntry&	   entry = getOrCreateAction( action );
		PhaseCallbackEntry cbEntry{};
		cbEntry._phase	  = phase;
		cbEntry._callback = std::move( callback );
		entry._listPhaseCallback.push_back( std::move( cbEntry ) );
	}

	void ActionMap::bindVector2DCallback( string_view action, Vector2DCallbackDelegate callback )
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

	void ActionMap::registerLayer( string_view name, int32 priority, bool enabled, bool blockLower, bool alwaysOn )
	{
		ensureLayer( name, priority, enabled, blockLower, alwaysOn );
	}

	void ActionMap::setLayerEnabled( string_view layer, bool enabled )
	{
		LayerDef* pDef = findLayer( layer );
		if ( pDef != nullptr )
			pDef->_bEnabled = enabled ? SW_TRUE : SW_FALSE;
	}

	void ActionMap::pushLayer( string_view layer, bool blockLower, bool showCursor )
	{
		const hashed_string hLayer( layer );
		ensureLayer( hLayer, 0, true, blockLower );
		LayerDef* pDef = findLayer( hLayer );
		if ( pDef != nullptr )
		{
			pDef->_bEnabled	   = SW_TRUE;
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

	void ActionMap::popLayer( string_view layer )
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

	void ActionMap::enableOnlyLayer( string_view layer )
	{
		const hashed_string hLayer( layer );
		for ( auto& [name, def] : _mapLayer )
		{
			if ( def._bAlwaysOn == SW_TRUE )
				continue;
			def._bEnabled = ( name == hLayer ) ? SW_TRUE : SW_FALSE;
		}
		_listLayerStack.clear();
		_listLayerStack.push_back( hLayer );
	}

	void ActionMap::setToggleMode( string_view action, bool bToggle )
	{
		ActionEntry& entry = getOrCreateAction( hashed_string( action ) );
		entry._bToggleMode = bToggle ? SW_TRUE : SW_FALSE;
		if ( bToggle == false )
			entry._bToggleState = SW_FALSE;
	}

	bool ActionMap::isActionToggled( string_view action ) const
	{
		return isActionToggled( hashed_string( action ) );
	}

	bool ActionMap::isActionToggled( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bToggleState == SW_TRUE;
	}

	void ActionMap::bufferAction( string_view action, float32 expirationSeconds )
	{
		bufferAction( hashed_string( action ), expirationSeconds );
	}

	void ActionMap::bufferAction( const hashed_string& action, float32 expirationSeconds )
	{
		if ( action.empty() )
			return;

		const uint32 insertIdx						 = ( _bufferedActionHead + _bufferedActionCount ) % kMaxBufferedActions;
		_arrBufferedAction[insertIdx]._action		 = action;
		_arrBufferedAction[insertIdx]._remainingTime = expirationSeconds;
		if ( _bufferedActionCount < kMaxBufferedActions )
			++_bufferedActionCount;
		else
			_bufferedActionHead = ( _bufferedActionHead + 1 ) % kMaxBufferedActions;
	}

	bool ActionMap::consumeBufferedAction( string_view action )
	{
		return consumeBufferedAction( hashed_string( action ) );
	}

	bool ActionMap::consumeBufferedAction( const hashed_string& action )
	{
		for ( uint32 index = 0; index < _bufferedActionCount; ++index )
		{
			const uint32 idx = ( _bufferedActionHead + index ) % kMaxBufferedActions;
			if ( _arrBufferedAction[idx]._action == action && _arrBufferedAction[idx]._remainingTime > 0.0f )
			{
				_arrBufferedAction[idx]._remainingTime = 0.0f;
				return true;
			}
		}
		return false;
	}

	bool ActionMap::checkCommandSequence( const vector<hashed_string>& listSequence, float32 maxWindowSeconds ) const
	{
		if ( listSequence.empty() || _commandHistoryCount < listSequence.size() )
			return false;

		const size_t seqCount = listSequence.size();
		size_t		 matchIdx = seqCount;
		float32		 lastTime = 0.0f;

		for ( int32 index = static_cast<int32>( _commandHistoryCount ) - 1; index >= 0; --index )
		{
			const uint32			   histIdx = ( _commandHistoryHead + static_cast<uint32>( index ) ) % kMaxCommandHistory;
			const CommandHistoryEntry& hist	   = _arrCommandHistory[histIdx];

			if ( matchIdx == seqCount )
			{
				if ( hist._action == listSequence[seqCount - 1] )
				{
					lastTime = hist._timestamp;
					--matchIdx;
					if ( matchIdx == 0 )
						return true;
				}
			}
			else
			{
				if ( ( lastTime - hist._timestamp ) > maxWindowSeconds )
					return false;

				if ( hist._action == listSequence[matchIdx - 1] )
				{
					--matchIdx;
					if ( matchIdx == 0 )
						return true;
				}
			}
		}
		return matchIdx == 0;
	}

	bool ActionMap::checkCommandSequence( const vector<string>& listSequence, float32 maxWindowSeconds ) const
	{
		if ( listSequence.empty() )
			return false;
		vector<hashed_string> listHashed;
		listHashed.reserve( listSequence.size() );
		for ( const string& s : listSequence )
			listHashed.push_back( hashed_string( s ) );
		return checkCommandSequence( listHashed, maxWindowSeconds );
	}

	bool ActionMap::checkCommandPattern( string_view pattern, float32 maxWindowSeconds ) const
	{
		return checkCommandPattern( hashed_string( pattern ), maxWindowSeconds );
	}

	bool ActionMap::checkCommandPattern( const hashed_string& pattern, float32 maxWindowSeconds ) const
	{
		string_view sv = pattern.view();
		if ( sv.empty() || _commandHistoryCount == 0 )
			return false;

		vector<hashed_string> listExpected;
		string				  actionToken;

		for ( size_t index = 0; index < sv.size(); ++index )
		{
			const utf8 ch = sv[index];
			if ( ch == '2' )
				listExpected.push_back( hashed_string( "Down" ) );
			else if ( ch == '3' )
				listExpected.push_back( hashed_string( "DownRight" ) );
			else if ( ch == '6' )
				listExpected.push_back( hashed_string( "Right" ) );
			else if ( ch == '4' )
				listExpected.push_back( hashed_string( "Left" ) );
			else if ( ch == '1' )
				listExpected.push_back( hashed_string( "DownLeft" ) );
			else if ( ch == '7' )
				listExpected.push_back( hashed_string( "UpLeft" ) );
			else if ( ch == '8' )
				listExpected.push_back( hashed_string( "Up" ) );
			else if ( ch == '9' )
				listExpected.push_back( hashed_string( "UpRight" ) );
			else
				actionToken.push_back( ch );
		}

		if ( actionToken.empty() == false )
			listExpected.push_back( hashed_string( actionToken ) );

		if ( listExpected.empty() )
			return false;

		return checkCommandSequence( listExpected, maxWindowSeconds );
	}

	string ActionMap::getGlyphForAction( string_view action ) const
	{
		return getGlyphForAction( hashed_string( action ) );
	}

	string ActionMap::getGlyphForAction( const hashed_string& action ) const
	{
		const InputDeviceType device = _pInput != nullptr ? _pInput->getActiveDeviceType() : InputDeviceType::KeyboardMouse;
		return getGlyphForActionInternal( action, device );
	}

	string ActionMap::getGlyphForAction( string_view action, InputDeviceType previewDevice ) const
	{
		return getGlyphForAction( hashed_string( action ), previewDevice );
	}

	string ActionMap::getGlyphForAction( const hashed_string& action, InputDeviceType previewDevice ) const
	{
		return getGlyphForActionInternal( action, previewDevice );
	}

	string ActionMap::getGlyphForActionInternal( const hashed_string& action, InputDeviceType device ) const
	{
		const ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || pEntry->_listBinding.empty() )
			return "[ ? ]";

		for ( const ActionBinding& b : pEntry->_listBinding )
		{
			if ( device == InputDeviceType::KeyboardMouse )
			{
				if ( b._kind == BindingKind::SingleSlot )
				{
					if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard || b._arrSlot[0]._deviceKind == InputDeviceKind::Mouse )
					{
						const string glyph = ActionMapInternal::slotToGlyph( b._arrSlot[0], device );
						if ( glyph != "?" )
							return string( "[ " ) + glyph + " ]";
					}
				}
				else if ( b._kind == BindingKind::Axis1DComposite )
				{
					return string( "[ " ) + ActionMapInternal::slotToGlyph( b._arrSlot[0], device ) + " / " + ActionMapInternal::slotToGlyph( b._arrSlot[1], device ) + " ]";
				}
				else if ( b._kind == BindingKind::Vector2DComposite )
				{
					return string( "[ " ) + ActionMapInternal::slotToGlyph( b._arrSlot[0], device ) + ActionMapInternal::slotToGlyph( b._arrSlot[1], device ) + ActionMapInternal::slotToGlyph( b._arrSlot[2], device ) + ActionMapInternal::slotToGlyph( b._arrSlot[3], device ) + " ]";
				}
				else if ( b._kind == BindingKind::Chord )
				{
					return string( "[ " ) + ActionMapInternal::slotToGlyph( b._arrSlot[0], device ) + " + " + ActionMapInternal::slotToGlyph( b._arrSlot[1], device ) + " ]";
				}
				else if ( b._kind == BindingKind::MouseDelta2D )
				{
					return "[ Mouse Look ]";
				}
				else if ( b._kind == BindingKind::Shortcut )
				{
					string modStr;
					if ( ( b._modifierMask & ModifierKey::Ctrl ) != 0 )
						modStr += "Ctrl + ";
					if ( ( b._modifierMask & ModifierKey::Shift ) != 0 )
						modStr += "Shift + ";
					if ( ( b._modifierMask & ModifierKey::Alt ) != 0 )
						modStr += "Alt + ";
					if ( ( b._modifierMask & ModifierKey::Super ) != 0 )
						modStr += "Win + ";
					return string( "[ " ) + modStr + ActionMapInternal::slotToGlyph( b._arrSlot[0], device ) + " ]";
				}
				else if ( b._kind == BindingKind::AnyKey )
				{
					return "[ Any Key ]";
				}
			}
			else
			{
				if ( b._kind == BindingKind::SingleSlot && b._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
				{
					const string glyph = ActionMapInternal::slotToGlyph( b._arrSlot[0], device );
					if ( glyph != "?" )
						return string( "[ " ) + glyph + " ]";
				}
				else if ( b._kind == BindingKind::GamepadStick2D )
				{
					return ( b._stick == GamepadStick::Left ) ? "[ L-Stick ]" : "[ R-Stick ]";
				}
				else if ( b._kind == BindingKind::Chord )
				{
					return string( "[ " ) + ActionMapInternal::slotToGlyph( b._arrSlot[0], device ) + " + " + ActionMapInternal::slotToGlyph( b._arrSlot[1], device ) + " ]";
				}
				else if ( b._kind == BindingKind::AnyKey )
				{
					return "[ Any Button ]";
				}
			}
		}
		return "[ ? ]";
	}

	bool ActionMap::rebindKey( string_view action, Key newKey, uint32 bindIndex )
	{
		ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
			return false;

		pEntry->_listBinding[bindIndex]._kind		= BindingKind::SingleSlot;
		pEntry->_listBinding[bindIndex]._arrSlot[0] = InputSlot::fromKey( newKey );
		return true;
	}

	bool ActionMap::rebindSlot( string_view action, InputSlot slot, uint32 bindIndex )
	{
		ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
			return false;

		pEntry->_listBinding[bindIndex]._kind		= BindingKind::SingleSlot;
		pEntry->_listBinding[bindIndex]._arrSlot[0] = slot;
		return true;
	}

	bool ActionMap::rebindWithResolution( string_view action, InputSlot newSlot, ConflictResolution strategy, uint32 bindIndex )
	{
		return rebindWithResolution( hashed_string( action ), newSlot, strategy, bindIndex );
	}

	bool ActionMap::rebindWithResolution( const hashed_string& action, InputSlot newSlot, ConflictResolution strategy, uint32 bindIndex )
	{
		ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
			return false;

		const hashed_string targetLayer = pEntry->_listBinding[bindIndex]._layer;

		hashed_string conflictingAction{};
		uint32		  conflictingBindIndex = 0;
		for ( auto& [actName, actIndex] : _mapAction )
		{
			if ( actName == action )
				continue;
			const ActionEntry& entry = _listActionEntry[actIndex];
			for ( uint32 bIdx = 0; bIdx < entry._listBinding.size(); ++bIdx )
			{
				if ( entry._listBinding[bIdx]._layer == targetLayer && entry._listBinding[bIdx]._arrSlot[0] == newSlot )
				{
					conflictingAction	 = actName;
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
				const InputSlot oldSlot										   = pEntry->_listBinding[bindIndex]._arrSlot[0];
				pConflictEntry->_listBinding[conflictingBindIndex]._arrSlot[0] = oldSlot;
				pEntry->_listBinding[bindIndex]._arrSlot[0]					   = newSlot;
				return true;
			}
			else if ( strategy == ConflictResolution::Override && pConflictEntry != nullptr )
			{
				pConflictEntry->_listBinding[conflictingBindIndex]._arrSlot[0] = InputSlot{};
				pEntry->_listBinding[bindIndex]._arrSlot[0]					   = newSlot;
				return true;
			}
			else if ( strategy == ConflictResolution::AddSecondary )
			{
				ActionBinding newBinding = pEntry->_listBinding[bindIndex];
				newBinding._arrSlot[0]	 = newSlot;
				pEntry->_listBinding.push_back( newBinding );
				pEntry->_listBindingState.push_back( ActionBindingState{} );
				return true;
			}
		}

		pEntry->_listBinding[bindIndex]._arrSlot[0] = newSlot;
		return true;
	}

	bool ActionMap::hasBindingConflict( const InputSlot& slot, string_view layer, string& outConflictingAction ) const
	{
		const hashed_string targetLayer = layer.empty() ? _defaultLayerName : hashed_string( layer );

		for ( const auto& [actionName, actIndex] : _mapAction )
		{
			const ActionEntry& entry = _listActionEntry[actIndex];
			for ( const ActionBinding& binding : entry._listBinding )
			{
				if ( binding._layer != targetLayer )
					continue;

				uint32 slotCount = 0;
				switch ( binding._kind )
				{
					case BindingKind::SingleSlot:
					case BindingKind::Shortcut:
						slotCount = 1;
						break;
					case BindingKind::Axis1DComposite:
					case BindingKind::Chord:
						slotCount = 2;
						break;
					case BindingKind::Vector2DComposite:
						slotCount = 4;
						break;
					case BindingKind::GamepadStick2D:
					case BindingKind::MouseDelta2D:
					case BindingKind::AnyKey:
					default:
						slotCount = 0;
						break;
				}

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

	bool ActionMap::resetActionToDefault( string_view action )
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

	bool ActionMap::saveUserBindings( string_view filePath ) const
	{
		if ( filePath.empty() )
			return false;

		XmlDocument doc;
		XmlNode		root = doc.appendRoot( "UserBindings" );
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
						const Key upKey	   = static_cast<Key>( b._arrSlot[0]._controlIndex );
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
			const utf8*		  pAction	= bindNode.attr( "action" );
			const utf8*		  pKindStr	= bindNode.attr( "kind" );
			const utf8*		  pLayerStr = bindNode.attr( "layer" );
			const string_view layer		= ( pLayerStr != nullptr && pLayerStr[0] != '\0' ) ? string_view( pLayerStr ) : string_view{};

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
					const Key	  upKey	   = KeyCodes::fromName( bindNode.attr( "up" ) );
					const Key	  downKey  = KeyCodes::fromName( bindNode.attr( "down" ) );
					const Key	  leftKey  = KeyCodes::fromName( bindNode.attr( "left" ) );
					const Key	  rightKey = KeyCodes::fromName( bindNode.attr( "right" ) );
					const float32 deadzone = bindNode.attrFloat( "deadzone", 0.0f );
					if ( upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown )
						bindVector2D( pAction, upKey, downKey, leftKey, rightKey, deadzone, layer );
					continue;
				}
				else if ( StringUtil::equals( pKindStr, "stick", true ) )
				{
					const utf8*		   pStickStr	 = bindNode.attr( "stick" );
					const GamepadStick stick		 = StringUtil::equals( pStickStr, "Right", true ) ? GamepadStick::Right : GamepadStick::Left;
					const uint8		   pad			 = static_cast<uint8>( bindNode.attrInt( "pad", 0 ) );
					const float32	   deadzone		 = bindNode.attrFloat( "deadzone", 0.15f );
					const float32	   outerDeadzone = bindNode.attrFloat( "outerDeadzone", 1.0f );
					const float32	   exp			 = bindNode.attrFloat( "exponent", 1.0f );
					bindGamepadStick2D( pAction, stick, deadzone, layer, pad, outerDeadzone, exp );
					continue;
				}
				else if ( StringUtil::equals( pKindStr, "mouseDelta", true ) )
				{
					const float32 scale = bindNode.attrFloat( "scale", 1.0f );
					bindMouseDelta( pAction, scale, layer );
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
					const Key	key		= KeyCodes::fromName( bindNode.attr( "key" ) );
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
			const utf8* pKeyStr	   = bindNode.attr( "key" );
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
						InputSlot slot	  = InputSlot::fromGamepadButton( btn );
						slot._deviceIndex = padIndex;
						bind( pAction, slot, ActionTrigger::Pressed, layer );
					}
				}
			}
		}
		return true;
	}

	void ActionMap::update( float32 deltaSeconds )
	{
		_totalElapsedTime += deltaSeconds;

		// 1) 선입력 버퍼 갱신 (Ring Buffer O(1) 정리)
		for ( uint32 index = 0; index < _bufferedActionCount; ++index )
		{
			const uint32 idx = ( _bufferedActionHead + index ) % kMaxBufferedActions;
			_arrBufferedAction[idx]._remainingTime -= deltaSeconds;
		}
		while ( _bufferedActionCount > 0 && _arrBufferedAction[_bufferedActionHead]._remainingTime <= 0.0f )
		{
			_bufferedActionHead = ( _bufferedActionHead + 1 ) % kMaxBufferedActions;
			--_bufferedActionCount;
		}

		// 2) 커맨드 이력 만료 제거 (Ring Buffer O(1) 정리, 2.0초 초과)
		while ( _commandHistoryCount > 0 && ( _totalElapsedTime - _arrCommandHistory[_commandHistoryHead]._timestamp ) > 2.0f )
		{
			_commandHistoryHead = ( _commandHistoryHead + 1 ) % kMaxCommandHistory;
			--_commandHistoryCount;
		}

		if ( _pInput == nullptr )
			return;

		int32 curMouseX{ 0 };
		int32 curMouseY{ 0 };
		_pInput->getMousePosition( curMouseX, curMouseY );

		// 3) 통합 액션 런타임 평가 및 ActionPhase 상태 머신
		for ( auto& [actionName, actIndex] : _mapAction )
		{
			ActionEntry& actionEntry = _listActionEntry[actIndex];

			const size_t bindCount = actionEntry._listBinding.size();
			if ( actionEntry._listBindingState.size() != bindCount )
				actionEntry._listBindingState.resize( bindCount );

			bool	anyDown{ false };
			bool	anyPressed{ false };
			bool	anyReleased{ false };
			bool	anyDoubleClicked{ false };
			bool	anyHoldThreshold{ false };
			bool	anyTriggered{ false };
			float32 maxHold{ 0.0f };
			float2	totalAccumValue{ 0.0f, 0.0f };

			for ( size_t bindIndex = 0; bindIndex < bindCount; ++bindIndex )
			{
				const ActionBinding& binding = actionEntry._listBinding[bindIndex];
				ActionBindingState&	 state	 = actionEntry._listBindingState[bindIndex];

				const bool bLayerActive = isBindingLayerActive( binding );
				float2	   bindingValue{ 0.0f, 0.0f };
				const bool bRawDown = bLayerActive && evaluateBindingDown( binding, bindingValue );

				state._bWasDown = state._bDown;
				state._bDown	= bRawDown ? SW_TRUE : SW_FALSE;

				state._bPressed		  = ( state._bDown == SW_TRUE && state._bWasDown == SW_FALSE ) ? SW_TRUE : SW_FALSE;
				state._bReleased	  = ( state._bDown == SW_FALSE && state._bWasDown == SW_TRUE ) ? SW_TRUE : SW_FALSE;
				state._bDoubleClicked = SW_FALSE;
				state._bHoldThreshold = SW_FALSE;

				if ( state._timeSinceLastPress < ActionMapDefaults::kNeverPressedSentinel * 0.5f )
					state._timeSinceLastPress += deltaSeconds;

				if ( state._bDown == SW_TRUE )
				{
					state._holdDuration += deltaSeconds;
					if ( state._holdDuration >= _holdThreshold )
						state._bHoldThreshold = SW_TRUE;
					state._pulseTimer += deltaSeconds;
				}

				if ( state._bPressed == SW_TRUE )
				{
					const bool bIsMouseBinding = ( binding._arrSlot[0]._deviceKind == InputDeviceKind::Mouse );
					bool	   bDoubleDetected = false;

					if ( state._timeSinceLastPress <= _doubleClickTime )
					{
						if ( bIsMouseBinding )
						{
							const float32 distSq = static_cast<float32>( ( curMouseX - state._lastPressX ) * ( curMouseX - state._lastPressX ) +
																		 ( curMouseY - state._lastPressY ) * ( curMouseY - state._lastPressY ) );
							if ( distSq <= ( _doubleClickMaxDistance * _doubleClickMaxDistance ) )
								bDoubleDetected = true;
						}
						else
						{
							bDoubleDetected = true;
						}
					}

					if ( bDoubleDetected )
					{
						state._bDoubleClicked	  = SW_TRUE;
						state._timeSinceLastPress = ActionMapDefaults::kNeverPressedSentinel;
					}
					else
					{
						state._timeSinceLastPress = 0.0f;
					}
					state._lastPressX = curMouseX;
					state._lastPressY = curMouseY;
				}

				const bool bTriggerFired = evaluateTrigger( binding._trigger, state, deltaSeconds );
				state._bTriggered		 = bTriggerFired ? SW_TRUE : SW_FALSE;

				if ( state._bDown == SW_TRUE )
					anyDown = true;
				if ( state._bPressed == SW_TRUE )
					anyPressed = true;
				if ( state._bReleased == SW_TRUE )
					anyReleased = true;
				if ( state._bDoubleClicked == SW_TRUE )
					anyDoubleClicked = true;
				if ( state._bHoldThreshold == SW_TRUE )
					anyHoldThreshold = true;
				if ( state._bTriggered == SW_TRUE )
					anyTriggered = true;
				if ( state._holdDuration > maxHold )
					maxHold = state._holdDuration;

				if ( state._bDown == SW_FALSE )
				{
					state._holdDuration = 0.0f;
					state._pulseTimer	= 0.0f;
				}

				if ( bRawDown )
				{
					totalAccumValue._x += bindingValue._x;
					totalAccumValue._y += bindingValue._y;
				}
			}

			const bool bPrevDown		= actionEntry._bDown == SW_TRUE;
			actionEntry._bDown			= anyDown ? SW_TRUE : SW_FALSE;
			actionEntry._bPressed		= anyPressed ? SW_TRUE : SW_FALSE;
			actionEntry._bReleased		= anyReleased ? SW_TRUE : SW_FALSE;
			actionEntry._bDoubleClicked = anyDoubleClicked ? SW_TRUE : SW_FALSE;
			actionEntry._bHoldThreshold = anyHoldThreshold ? SW_TRUE : SW_FALSE;
			actionEntry._bTriggered		= anyTriggered ? SW_TRUE : SW_FALSE;
			actionEntry._holdDuration	= maxHold;

			// 모디파이어 적용 (축 반전 및 클램핑/원형 정규화)
			if ( _bInvertX == SW_TRUE )
				totalAccumValue._x = -totalAccumValue._x;
			if ( _bInvertY == SW_TRUE )
				totalAccumValue._y = -totalAccumValue._y;

			if ( _digitalNormalization == DigitalNormalization::Circular )
			{
				const float32 lenSq = totalAccumValue._x * totalAccumValue._x + totalAccumValue._y * totalAccumValue._y;
				if ( lenSq > 1.0f )
				{
					const float32 invLen = 1.0f / MathUtil::sqrt( lenSq );
					totalAccumValue._x *= invLen;
					totalAccumValue._y *= invLen;
				}
			}
			else
			{
				totalAccumValue._x = totalAccumValue._x < -1.0f ? -1.0f : ( totalAccumValue._x > 1.0f ? 1.0f : totalAccumValue._x );
				totalAccumValue._y = totalAccumValue._y < -1.0f ? -1.0f : ( totalAccumValue._y > 1.0f ? 1.0f : totalAccumValue._y );
			}
			actionEntry._currentValue = totalAccumValue;

			// --------------------------------------------------------------------------
			// 상용 엔진 표준 ActionPhase 상태 머신 전이
			// --------------------------------------------------------------------------
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
				actionEntry._currentPhase = ( actionEntry._currentPhase == ActionPhase::Triggered || actionEntry._currentPhase == ActionPhase::Ongoing || maxHold >= _holdThreshold )
											  ? ActionPhase::Completed
											  : ActionPhase::Canceled;
			}
			else
			{
				actionEntry._currentPhase = ActionPhase::None;
			}

			// 접근성 토글 처리
			if ( actionEntry._bPressed == SW_TRUE && actionEntry._bToggleMode == SW_TRUE )
			{
				actionEntry._bToggleState = ( actionEntry._bToggleState == SW_TRUE ) ? SW_FALSE : SW_TRUE;
			}

			// 커맨드 이력 기록 (Ring Buffer)
			if ( actionEntry._bTriggered == SW_TRUE )
			{
				const uint32 insertIdx					 = ( _commandHistoryHead + _commandHistoryCount ) % kMaxCommandHistory;
				_arrCommandHistory[insertIdx]._action	 = actionName;
				_arrCommandHistory[insertIdx]._timestamp = _totalElapsedTime;
				if ( _commandHistoryCount < kMaxCommandHistory )
					++_commandHistoryCount;
				else
					_commandHistoryHead = ( _commandHistoryHead + 1 ) % kMaxCommandHistory;
			}

			// 델리게이트 이벤트 디스패치 (Triggered)
			if ( actionEntry._bTriggered == SW_TRUE )
			{
				for ( const ActionCallbackEntry& cbEntry : actionEntry._listActionCallback )
				{
					if ( cbEntry._callback.isBound() )
						cbEntry._callback();
				}
			}

			// 페이즈 델리게이트 디스패치 (Phase)
			if ( actionEntry._currentPhase != ActionPhase::None )
			{
				for ( const PhaseCallbackEntry& phaseEntry : actionEntry._listPhaseCallback )
				{
					if ( phaseEntry._phase == actionEntry._currentPhase && phaseEntry._callback.isBound() )
						phaseEntry._callback();
				}
			}

			// 2D 벡터 델리게이트 디스패치
			if ( ( actionEntry._currentValue._x != 0.0f || actionEntry._currentValue._y != 0.0f ) ||
				 ( bPrevDown && actionEntry._bDown == SW_FALSE ) )
			{
				for ( const Vector2DCallbackDelegate& vecCb : actionEntry._listVector2DCallback )
				{
					if ( vecCb.isBound() )
						vecCb( actionEntry._currentValue );
				}
			}
		}
	}

	bool ActionMap::evaluateBindingDown( const ActionBinding& binding, float2& outValue ) const
	{
		if ( _pInput == nullptr )
			return false;

		switch ( binding._kind )
		{
			case BindingKind::SingleSlot:
			{
				IInputDevice* pDevice = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				// isControlDown()만 보면 같은 프레임 안에서 Down+Up이 모두 처리된 순간 탭(예: 매크로 주입, 초고속 입력)을
				// 놓칩니다. wasControlPressed()를 함께 확인해 그 프레임엔 "눌렸었다"로 취급합니다.
				if ( pDevice != nullptr && ( pDevice->isControlDown( binding._arrSlot[0]._controlIndex ) || pDevice->wasControlPressed( binding._arrSlot[0]._controlIndex ) ) )
				{
					if ( _bSuppressBaseActionOnChord == SW_TRUE && binding._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
					{
						const Key  key		 = static_cast<Key>( binding._arrSlot[0]._controlIndex );
						const bool bIsModKey = ( key == Key::LeftControl || key == Key::RightControl || key == Key::LeftShift || key == Key::RightShift || key == Key::LeftAlt || key == Key::RightAlt || key == Key::LeftSuper || key == Key::RightSuper );
						if ( bIsModKey == false )
						{
							const bool bCtrlHeld = _pInput->isKeyDown( Key::LeftControl ) || _pInput->isKeyDown( Key::RightControl );
							const bool bAltHeld	 = _pInput->isKeyDown( Key::LeftAlt ) || _pInput->isKeyDown( Key::RightAlt );
							if ( bCtrlHeld || bAltHeld )
								return false;
						}
					}
					outValue = float2{ 1.0f, 0.0f };
					return true;
				}
				return false;
			}
			case BindingKind::Axis1DComposite:
			{
				IInputDevice* pNegDev = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				IInputDevice* pPosDev = _pInput->getDevice( binding._arrSlot[1]._deviceKind, binding._arrSlot[1]._deviceIndex );
				float32		  v		  = 0.0f;
				if ( pNegDev != nullptr && pNegDev->isControlDown( binding._arrSlot[0]._controlIndex ) )
					v -= 1.0f;
				if ( pPosDev != nullptr && pPosDev->isControlDown( binding._arrSlot[1]._controlIndex ) )
					v += 1.0f;
				outValue = float2{ v, 0.0f };
				return v != 0.0f;
			}
			case BindingKind::Vector2DComposite:
			{
				IInputDevice* pUpDev	= _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				IInputDevice* pDownDev	= _pInput->getDevice( binding._arrSlot[1]._deviceKind, binding._arrSlot[1]._deviceIndex );
				IInputDevice* pLeftDev	= _pInput->getDevice( binding._arrSlot[2]._deviceKind, binding._arrSlot[2]._deviceIndex );
				IInputDevice* pRightDev = _pInput->getDevice( binding._arrSlot[3]._deviceKind, binding._arrSlot[3]._deviceIndex );

				float2 kbdVec{ 0.0f, 0.0f };
				if ( pUpDev != nullptr && pUpDev->isControlDown( binding._arrSlot[0]._controlIndex ) )
					kbdVec._y += 1.0f;
				if ( pDownDev != nullptr && pDownDev->isControlDown( binding._arrSlot[1]._controlIndex ) )
					kbdVec._y -= 1.0f;
				if ( pLeftDev != nullptr && pLeftDev->isControlDown( binding._arrSlot[2]._controlIndex ) )
					kbdVec._x -= 1.0f;
				if ( pRightDev != nullptr && pRightDev->isControlDown( binding._arrSlot[3]._controlIndex ) )
					kbdVec._x += 1.0f;

				const float32 lenSq = kbdVec._x * kbdVec._x + kbdVec._y * kbdVec._y;
				if ( _digitalNormalization == DigitalNormalization::Circular && lenSq > 1.0f )
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
				GamepadDevice* pPad = _pInput->getGamepad( binding._deviceIndex );
				if ( pPad != nullptr && pPad->isConnected() )
				{
					float32 sx{ 0.0f };
					float32 sy{ 0.0f };
					if ( binding._stick == GamepadStick::Left )
						pPad->getLeftStick( sx, sy );
					else
						pPad->getRightStick( sx, sy );

					const float32 inDeadzone  = binding._deadzone;
					const float32 outDeadzone = binding._outerDeadzone > inDeadzone ? binding._outerDeadzone : 1.0f;
					const float32 deadRange	  = outDeadzone - inDeadzone;

					if ( _deadzoneShape == DeadzoneShape::Radial )
					{
						const float32 mag = MathUtil::sqrt( sx * sx + sy * sy );
						if ( mag <= inDeadzone )
						{
							sx = 0.0f;
							sy = 0.0f;
						}
						else
						{
							float32 norm = deadRange > 0.0001f ? MathUtil::clamp( ( mag - inDeadzone ) / deadRange, 0.0f, 1.0f ) : 1.0f;
							if ( binding._responseExponent != 1.0f && binding._responseExponent > 0.0f )
							{
								norm = MathUtil::pow( norm, binding._responseExponent );
							}
							sx = ( sx / mag ) * norm;
							sy = ( sy / mag ) * norm;
						}
					}
					else // Axial
					{
						auto applyAxialDeadzone = [&]( float32 val ) -> float32
						{
							const float32 absVal = MathUtil::abs( val );
							if ( absVal <= inDeadzone )
								return 0.0f;
							float32 norm = deadRange > 0.0001f ? MathUtil::clamp( ( absVal - inDeadzone ) / deadRange, 0.0f, 1.0f ) : 1.0f;
							if ( binding._responseExponent != 1.0f && binding._responseExponent > 0.0f )
							{
								norm = MathUtil::pow( norm, binding._responseExponent );
							}
							return ( val > 0.0f ? 1.0f : -1.0f ) * norm;
						};
						sx = applyAxialDeadzone( sx );
						sy = applyAxialDeadzone( sy );
					}

					sx *= _gamepadSensitivity._x;
					sy *= _gamepadSensitivity._y;
					outValue = float2{ sx, sy };
					return ( sx != 0.0f || sy != 0.0f );
				}
				return false;
			}
			case BindingKind::Chord:
			{
				IInputDevice* pModDev  = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				IInputDevice* pTrigDev = _pInput->getDevice( binding._arrSlot[1]._deviceKind, binding._arrSlot[1]._deviceIndex );
				if ( pModDev != nullptr && pTrigDev != nullptr )
				{
					const bool bModDown	 = pModDev->isControlDown( binding._arrSlot[0]._controlIndex );
					const bool bTrigDown = pTrigDev->isControlDown( binding._arrSlot[1]._controlIndex );
					if ( bModDown && bTrigDown )
					{
						outValue = float2{ 1.0f, 0.0f };
						return true;
					}
				}
				return false;
			}
			case BindingKind::MouseDelta2D:
			{
				float32 rdx{ 0.0f };
				float32 rdy{ 0.0f };
				_pInput->getRawMouseDelta( rdx, rdy );
				if ( rdx == 0.0f && rdy == 0.0f )
				{
					int32 dx{ 0 };
					int32 dy{ 0 };
					_pInput->getMouseDelta( dx, dy );
					rdx = static_cast<float32>( dx );
					rdy = static_cast<float32>( dy );
				}
				outValue._x = rdx * binding._scale * _mouseSensitivity._x * ( _bInvertX == SW_TRUE ? -1.0f : 1.0f );
				outValue._y = rdy * binding._scale * _mouseSensitivity._y * ( _bInvertY == SW_TRUE ? -1.0f : 1.0f );
				return ( rdx != 0.0f || rdy != 0.0f );
			}
			case BindingKind::Shortcut:
			{
				bool bModMatch = true;
				if ( ( binding._modifierMask & ModifierKey::Ctrl ) != 0 )
					bModMatch = bModMatch && ( _pInput->isKeyDown( Key::LeftControl ) || _pInput->isKeyDown( Key::RightControl ) );
				if ( ( binding._modifierMask & ModifierKey::Shift ) != 0 )
					bModMatch = bModMatch && ( _pInput->isKeyDown( Key::LeftShift ) || _pInput->isKeyDown( Key::RightShift ) );
				if ( ( binding._modifierMask & ModifierKey::Alt ) != 0 )
					bModMatch = bModMatch && ( _pInput->isKeyDown( Key::LeftAlt ) || _pInput->isKeyDown( Key::RightAlt ) );
				if ( ( binding._modifierMask & ModifierKey::Super ) != 0 )
					bModMatch = bModMatch && ( _pInput->isKeyDown( Key::LeftSuper ) || _pInput->isKeyDown( Key::RightSuper ) );

				if ( bModMatch == false )
					return false;

				IInputDevice* pDev = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				if ( pDev != nullptr && pDev->isControlDown( binding._arrSlot[0]._controlIndex ) )
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
			default:
				return false;
		}
	}

	bool ActionMap::evaluateTrigger( ActionTrigger trigger, const ActionBindingState& state, float32 deltaSeconds ) const
	{
		switch ( trigger )
		{
			case ActionTrigger::Pressed:
				return state._bPressed == SW_TRUE;
			case ActionTrigger::Released:
				return state._bReleased == SW_TRUE;
			case ActionTrigger::Down:
				return state._bDown == SW_TRUE;
			case ActionTrigger::DoubleClicked:
			case ActionTrigger::DoubleTap:
				return state._bDoubleClicked == SW_TRUE;
			case ActionTrigger::HoldThreshold:
				return state._bHoldThreshold == SW_TRUE;
			case ActionTrigger::HoldAndRelease:
				return state._bReleased == SW_TRUE && state._holdDuration >= _holdThreshold;
			case ActionTrigger::Tap:
				return state._bReleased == SW_TRUE && state._holdDuration < ActionMapDefaults::kTapMaxTime;
			case ActionTrigger::Pulse:
				return state._bDown == SW_TRUE && state._pulseTimer >= ActionMapDefaults::kPulseInterval;
			case ActionTrigger::Repeat:
			{
				if ( state._bPressed == SW_TRUE )
					return true;
				if ( state._bDown == SW_TRUE && state._holdDuration >= _navRepeatDelay )
				{
					const float32 repeatTime	 = state._holdDuration - _navRepeatDelay;
					const float32 prevRepeatTime = ( state._holdDuration - deltaSeconds ) - _navRepeatDelay;
					if ( prevRepeatTime < 0.0f )
						return true;
					const int32 stepNow	 = static_cast<int32>( repeatTime / _navRepeatRate );
					const int32 stepPrev = static_cast<int32>( prevRepeatTime / _navRepeatRate );
					return stepNow > stepPrev;
				}
				return false;
			}
			case ActionTrigger::Count:
			default:
				return false;
		}
	}

	bool ActionMap::isBindingLayerActive( const ActionBinding& binding ) const
	{
		if ( binding._pCachedLayer == nullptr )
		{
			binding._pCachedLayer = findLayer( binding._layer );
		}

		if ( binding._pCachedLayer != nullptr && _listLayerStack.empty() )
		{
			if ( binding._pCachedLayer->_bEnabled == SW_FALSE )
				return false;
			return true;
		}

		return isLayerActiveInternal( binding._layer );
	}

	bool ActionMap::isLayerActiveInternal( const hashed_string& layer ) const
	{
		const LayerDef* pDef = findLayer( layer );
		if ( pDef == nullptr || pDef->_bEnabled == SW_FALSE )
			return false;
		if ( pDef->_bAlwaysOn == SW_TRUE )
			return true;

		if ( _listLayerStack.empty() == false )
		{
			for ( auto it = _listLayerStack.rbegin(); it != _listLayerStack.rend(); ++it )
			{
				if ( *it == layer )
					return true;
				const LayerDef* pTopDef = findLayer( *it );
				if ( pTopDef != nullptr && pTopDef->_bBlockLower == SW_TRUE )
					return false;
			}
			return false;
		}

		return true;
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

	bool ActionMap::hasLayer( string_view layer ) const
	{
		return hasLayer( hashed_string( layer ) );
	}

	bool ActionMap::hasLayer( const hashed_string& layer ) const
	{
		return _mapLayer.find( layer ) != _mapLayer.end();
	}

	bool ActionMap::isLayerEnabled( string_view layer ) const
	{
		return isLayerActiveInternal( hashed_string( layer ) );
	}

	bool ActionMap::isLayerEnabled( const hashed_string& layer ) const
	{
		return isLayerActiveInternal( layer );
	}

	int32 ActionMap::getLayerPriority( string_view layer ) const
	{
		return getLayerPriority( hashed_string( layer ) );
	}

	int32 ActionMap::getLayerPriority( const hashed_string& layer ) const
	{
		const LayerDef* pDef = findLayer( layer );
		return pDef != nullptr ? pDef->_priority : 0;
	}

	bool ActionMap::hasAction( string_view action ) const
	{
		return hasAction( hashed_string( action ) );
	}

	bool ActionMap::hasAction( const hashed_string& action ) const
	{
		return _mapAction.find( action ) != _mapAction.end();
	}

	ActionTrigger ActionMap::getBindingTrigger( string_view action, uint32 bindIndex ) const
	{
		return getBindingTrigger( hashed_string( action ), bindIndex );
	}

	ActionTrigger ActionMap::getBindingTrigger( const hashed_string& action, uint32 bindIndex ) const
	{
		const ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
			return ActionTrigger::Pressed;
		return pEntry->_listBinding[bindIndex]._trigger;
	}

	uint32 ActionMap::getBindingCount( string_view action ) const
	{
		return getBindingCount( hashed_string( action ) );
	}

	uint32 ActionMap::getBindingCount( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr ? static_cast<uint32>( pEntry->_listBinding.size() ) : 0;
	}

	const ActionBinding* ActionMap::getBinding( string_view action, uint32 bindIndex ) const
	{
		return getBinding( hashed_string( action ), bindIndex );
	}

	const ActionBinding* ActionMap::getBinding( const hashed_string& action, uint32 bindIndex ) const
	{
		const ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
			return nullptr;
		return &pEntry->_listBinding[bindIndex];
	}

	bool ActionMap::wasActionTriggered( string_view action ) const
	{
		return wasActionTriggered( hashed_string( action ) );
	}

	bool ActionMap::wasActionTriggered( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
	}

	bool ActionMap::isActionDown( string_view action ) const
	{
		return isActionDown( hashed_string( action ) );
	}

	bool ActionMap::isActionDown( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
	}

	ActionHandle ActionMap::getActionHandle( string_view action ) const
	{
		return getActionHandle( hashed_string( action ) );
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

	bool ActionMap::wasActionPressed( string_view action ) const
	{
		return wasActionPressed( hashed_string( action ) );
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

	bool ActionMap::wasActionReleased( string_view action ) const
	{
		return wasActionReleased( hashed_string( action ) );
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

	bool ActionMap::wasActionDoubleClicked( string_view action ) const
	{
		return wasActionDoubleClicked( hashed_string( action ) );
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

	bool ActionMap::wasActionHoldThreshold( string_view action ) const
	{
		return wasActionHoldThreshold( hashed_string( action ) );
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

	float32 ActionMap::getActionHoldDuration( string_view action ) const
	{
		return getActionHoldDuration( hashed_string( action ) );
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

	ActionPhase ActionMap::getActionPhase( string_view action ) const
	{
		return getActionPhase( hashed_string( action ) );
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

	bool ActionMap::isPointerHovering() const
	{
		return _pInput != nullptr && _pInput->isPointerInside();
	}

	bool ActionMap::wasPointerHoverEntered() const
	{
		return _pInput != nullptr && _pInput->wasPointerEntered();
	}

	bool ActionMap::wasPointerHoverLeft() const
	{
		return _pInput != nullptr && _pInput->wasPointerLeft();
	}

	bool ActionMap::isPointerOverRect( int32 x, int32 y, int32 w, int32 h ) const
	{
		if ( _pInput == nullptr || _pInput->isPointerInside() == false )
			return false;
		int32 mx{ 0 };
		int32 my{ 0 };
		_pInput->getMousePosition( mx, my );
		return ( x <= mx && mx < ( x + w ) && y <= my && my < ( y + h ) );
	}

	ActionTrigger ActionMap::actionTriggerFromName( string_view name )
	{
		if ( name.empty() )
			return ActionTrigger::Count;
		for ( const ActionMapInternal::TriggerNameEntry& entry : ActionMapInternal::kArrTriggerNames )
		{
			if ( StringUtil::equals( name, entry._pName, true ) )
				return entry._trigger;
		}
		return ActionTrigger::Count;
	}

	const utf8* ActionMap::actionTriggerToName( ActionTrigger trigger )
	{
		for ( const ActionMapInternal::TriggerNameEntry& entry : ActionMapInternal::kArrTriggerNames )
		{
			if ( entry._trigger == trigger )
				return entry._pName;
		}
		return nullptr;
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
			return it->second;

		LayerDef def{};
		def._name		 = name;
		def._priority	 = priority;
		def._bEnabled	 = enabled ? SW_TRUE : SW_FALSE;
		def._bBlockLower = blockLower ? SW_TRUE : SW_FALSE;
		def._bAlwaysOn	 = alwaysOn ? SW_TRUE : SW_FALSE;

		_listLayerName.push_back( def._name );
		auto [insertedIt, _] = _mapLayer.emplace( def._name, def );
		return insertedIt->second;
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
		return it != _mapLayer.end() ? &it->second : nullptr;
	}

	const LayerDef* ActionMap::findLayer( const hashed_string& name ) const
	{
		auto it = _mapLayer.find( name );
		return it != _mapLayer.end() ? &it->second : nullptr;
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
			state._action		= actName;
			state._layer		= entry._listBinding.empty() ? _defaultLayerName : entry._listBinding[0]._layer;
			state._valueType	= entry._valueType;
			state._phase		= entry._currentPhase;
			state._value		= entry._currentValue;
			state._holdDuration = entry._holdDuration;
			state._bTriggered	= entry._bTriggered;
			state._bDown		= entry._bDown;
			outListState.push_back( state );
		}
	}
} // namespace sw
