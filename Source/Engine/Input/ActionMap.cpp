#include "pch.h"

#include "Engine/Input/ActionMap.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include <cmath>

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
			};
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
		, _mapActionCallback{}
		, _mapPhaseCallback{}
		, _mapVector2DCallback{}
		, _mapToggleMode{}
		, _mapToggleState{}
		, _listActionName{}
		, _listLayerName{}
		, _listLayerStack{}
		, _listBufferedAction{}
		, _listCommandHistory{}
		, _defaultLayerName{ ActionMapDefaults::kDefaultLayerName }
		, _mouseSensitivity{ 1.0f, 1.0f }
		, _gamepadSensitivity{ 1.0f, 1.0f }
		, _doubleClickTime{ ActionMapDefaults::kDoubleClickTime }
		, _doubleClickMaxDistance{ ActionMapDefaults::kDoubleClickMaxDistance }
		, _holdThreshold{ ActionMapDefaults::kHoldThreshold }
		, _navRepeatDelay{ 0.4f }
		, _navRepeatRate{ 0.08f }
		, _totalElapsedTime{ 0.0f }
		, _deadzoneShape{ DeadzoneShape::Radial }
		, _bInvertX{ SW_FALSE }
		, _bInvertY{ SW_FALSE }
		, _reservedFlags{ 0 }
	{
	}

	void ActionMap::clear()
	{
		_mapAction.clear();
		_mapLayer.clear();
		_mapActionCallback.clear();
		_mapPhaseCallback.clear();
		_mapVector2DCallback.clear();
		_mapToggleMode.clear();
		_mapToggleState.clear();
		_listActionName.clear();
		_listLayerName.clear();
		_listLayerStack.clear();
		_listBufferedAction.clear();
		_listCommandHistory.clear();
		_defaultLayerName.assign( ActionMapDefaults::kDefaultLayerName );
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
			_defaultLayerName = pDefLayer;

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

			string		layer( inheritedLayer );
			const utf8* pLayerAttr = actionNode.attr( ActionMapInternal::InputMapXml::kAttrLayer );
			if ( pLayerAttr != nullptr )
				layer.assign( pLayerAttr );
			if ( layer.empty() )
				layer = _defaultLayerName;
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

				string		bindLayer	   = layer;
				const utf8* pBindLayerAttr = bindNode.attr( ActionMapInternal::InputMapXml::kAttrLayer );
				if ( pBindLayerAttr != nullptr )
				{
					bindLayer = pBindLayerAttr;
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
						bindChord( pActionName, modKey, triggerKey, trigger, bindLayer );
						continue;
					}
				}

				if ( StringUtil::equals( pSource, ActionMapInternal::InputMapXml::kSourceKey, true ) )
				{
					const Key key = KeyCodes::fromName( pCode );
					if ( key != Key::Unknown )
						bind( pActionName, key, trigger, bindLayer );
				}
				else if ( StringUtil::equals( pSource, ActionMapInternal::InputMapXml::kSourceGamepad, true ) )
				{
					if ( StringUtil::equals( pCode, "LeftStick", true ) )
					{
						const float32 deadzone = bindNode.attrFloat( ActionMapInternal::InputMapXml::kAttrDeadzone, 0.15f );
						bindGamepadStick2D( pActionName, GamepadStick::Left, deadzone, bindLayer );
					}
					else if ( StringUtil::equals( pCode, "RightStick", true ) )
					{
						const float32 deadzone = bindNode.attrFloat( ActionMapInternal::InputMapXml::kAttrDeadzone, 0.15f );
						bindGamepadStick2D( pActionName, GamepadStick::Right, deadzone, bindLayer );
					}
					else
					{
						const GamepadButton button = GamepadButtons::fromName( pCode );
						if ( button != GamepadButton::Count )
							bind( pActionName, button, trigger, bindLayer );
					}
				}
				else if ( StringUtil::equals( pSource, ActionMapInternal::InputMapXml::kSourceMouse, true ) )
				{
					const MouseButton mouse = MouseButtons::fromName( pCode );
					if ( mouse != MouseButton::Count )
						bind( pActionName, mouse, trigger, bindLayer );
				}
			}

			// 2) <vector2d> 태그 파싱
			for ( XmlNode compNode = actionNode.child( "vector2d" ); compNode.isValid(); compNode = compNode.next( "vector2d" ) )
			{
				const Key	  upKey	   = KeyCodes::fromName( compNode.attr( "up" ) );
				const Key	  downKey  = KeyCodes::fromName( compNode.attr( "down" ) );
				const Key	  leftKey  = KeyCodes::fromName( compNode.attr( "left" ) );
				const Key	  rightKey = KeyCodes::fromName( compNode.attr( "right" ) );
				const float32 deadzone = compNode.attrFloat( "deadzone", 0.0f );
				string		  compLayer( layer );
				const utf8*	  pCompLayerAttr = compNode.attr( "layer" );
				if ( pCompLayerAttr != nullptr && pCompLayerAttr[0] != '\0' )
				{
					compLayer = pCompLayerAttr;
					ensureLayer( compLayer );
				}
				if ( upKey != Key::Unknown && downKey != Key::Unknown && leftKey != Key::Unknown && rightKey != Key::Unknown )
				{
					bindVector2D( pActionName, upKey, downKey, leftKey, rightKey, deadzone, compLayer );
				}
			}

			// 3) <axis1d> 태그 파싱
			for ( XmlNode axisNode = actionNode.child( "axis1d" ); axisNode.isValid(); axisNode = axisNode.next( "axis1d" ) )
			{
				const Key	posKey = KeyCodes::fromName( axisNode.attr( "positive" ) );
				const Key	negKey = KeyCodes::fromName( axisNode.attr( "negative" ) );
				string		axisLayer( layer );
				const utf8* pAxisLayerAttr = axisNode.attr( "layer" );
				if ( pAxisLayerAttr != nullptr && pAxisLayerAttr[0] != '\0' )
				{
					axisLayer = pAxisLayerAttr;
					ensureLayer( axisLayer );
				}
				if ( posKey != Key::Unknown && negKey != Key::Unknown )
				{
					bindAxis1DComposite( pActionName, negKey, posKey, axisLayer );
				}
			}

			// 4) <stick> 태그 파싱
			for ( XmlNode stickNode = actionNode.child( "stick" ); stickNode.isValid(); stickNode = stickNode.next( "stick" ) )
			{
				const utf8*		   pStickName = stickNode.attr( "stick" );
				const GamepadStick stick	  = ( pStickName != nullptr && StringUtil::equals( pStickName, "Right", true ) ) ? GamepadStick::Right : GamepadStick::Left;
				const float32	   deadzone	  = stickNode.attrFloat( "deadzone", 0.15f );
				string			   stickLayer( layer );
				const utf8*		   pStickLayerAttr = stickNode.attr( "layer" );
				if ( pStickLayerAttr != nullptr && pStickLayerAttr[0] != '\0' )
				{
					stickLayer = pStickLayerAttr;
					ensureLayer( stickLayer );
				}
				bindGamepadStick2D( pActionName, stick, deadzone, stickLayer );
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
				string		chordLayer( layer );
				const utf8* pChordLayerAttr = chordNode.attr( "layer" );
				if ( pChordLayerAttr != nullptr && pChordLayerAttr[0] != '\0' )
				{
					chordLayer = pChordLayerAttr;
					ensureLayer( chordLayer );
				}
				if ( modKey != Key::Unknown && trigKey != Key::Unknown )
				{
					bindChord( pActionName, modKey, trigKey, trig, chordLayer );
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
			loadAction( actionNode, _defaultLayerName );
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

	void ActionMap::bind( string_view action, InputSlot slot, ActionTrigger trigger, string_view layer )
	{
		if ( action.empty() )
			return;

		string layerStr( layer );
		if ( layerStr.empty() )
			layerStr = _defaultLayerName;
		ensureLayer( layerStr );
		ensureActionListed( action );

		ActionEntry&  entry = getOrCreateAction( action, InputActionValueType::Boolean );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::SingleSlot;
		binding._trigger	  = trigger;
		binding._arrSlot[0]	  = slot;
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
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

		string layerStr( layer );
		if ( layerStr.empty() )
			layerStr = _defaultLayerName;
		ensureLayer( layerStr );
		ensureActionListed( action );

		ActionEntry&  entry = getOrCreateAction( action, InputActionValueType::Axis1D );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::Axis1DComposite;
		binding._trigger	  = ActionTrigger::Down;
		binding._arrSlot[0]	  = InputSlot::fromKey( negativeKey );
		binding._arrSlot[1]	  = InputSlot::fromKey( positiveKey );
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	float32 ActionMap::getAxis1D( string_view action ) const
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

	float32 ActionMap::getAxis1D( const hashed_string& action ) const
	{
		return getAxis1D( action.view() );
	}

	void ActionMap::bindVector2D( string_view action, Key up, Key down, Key left, Key right, float32 deadzone, string_view layer )
	{
		if ( action.empty() )
			return;

		string layerStr( layer );
		if ( layerStr.empty() )
			layerStr = _defaultLayerName;
		ensureLayer( layerStr );
		ensureActionListed( action );

		ActionEntry&  entry = getOrCreateAction( action, InputActionValueType::Axis2D );
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
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	void ActionMap::bindGamepadStick2D( string_view action, GamepadStick stick, float32 deadzone, string_view layer )
	{
		if ( action.empty() )
			return;

		string layerStr( layer );
		if ( layerStr.empty() )
			layerStr = _defaultLayerName;
		ensureLayer( layerStr );
		ensureActionListed( action );

		ActionEntry&  entry = getOrCreateAction( action, InputActionValueType::Axis2D );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::GamepadStick2D;
		binding._trigger	  = ActionTrigger::Down;
		binding._stick		  = stick;
		binding._deadzone	  = deadzone;
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	float2 ActionMap::getVector2D( string_view action ) const
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

	float2 ActionMap::getVector2D( const hashed_string& action ) const
	{
		return getVector2D( action.view() );
	}

	void ActionMap::bindChord( string_view action, Key modifierKey, Key triggerKey, ActionTrigger trigger, string_view layer )
	{
		if ( action.empty() || modifierKey == Key::Unknown || triggerKey == Key::Unknown )
			return;

		string layerStr( layer );
		if ( layerStr.empty() )
			layerStr = _defaultLayerName;
		ensureLayer( layerStr );
		ensureActionListed( action );

		ActionEntry&  entry = getOrCreateAction( action, InputActionValueType::Boolean );
		ActionBinding binding{};
		binding._layer		  = layerStr;
		binding._kind		  = BindingKind::Chord;
		binding._trigger	  = trigger;
		binding._arrSlot[0]	  = InputSlot::fromKey( modifierKey );
		binding._arrSlot[1]	  = InputSlot::fromKey( triggerKey );
		binding._pCachedLayer = findLayer( layerStr );
		entry._listBinding.push_back( binding );
		entry._listBindingState.push_back( ActionBindingState{} );
	}

	bool ActionMap::isChordDown( string_view action ) const
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

	bool ActionMap::isChordDown( const hashed_string& action ) const
	{
		return isChordDown( action.view() );
	}

	bool ActionMap::wasChordTriggered( string_view action ) const
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

	bool ActionMap::wasChordTriggered( const hashed_string& action ) const
	{
		return wasChordTriggered( action.view() );
	}

	void ActionMap::bindActionCallback( string_view action, ActionTrigger trigger, ActionCallbackDelegate callback )
	{
		if ( action.empty() || callback.isBound() == false )
			return;
		ActionCallbackEntry entry{};
		entry._trigger	= trigger;
		entry._callback = callback;
		_mapActionCallback[string( action )].push_back( entry );
	}

	void ActionMap::bindPhaseCallback( string_view action, ActionPhase phase, ActionCallbackDelegate callback )
	{
		if ( action.empty() || callback.isBound() == false )
			return;
		PhaseCallbackEntry entry{};
		entry._phase	= phase;
		entry._callback = callback;
		_mapPhaseCallback[string( action )].push_back( entry );
	}

	void ActionMap::bindVector2DCallback( string_view action, Vector2DCallbackDelegate callback )
	{
		if ( action.empty() || callback.isBound() == false )
			return;
		_mapVector2DCallback[string( action )].push_back( callback );
	}

	void ActionMap::clearCallbacks()
	{
		_mapActionCallback.clear();
		_mapPhaseCallback.clear();
		_mapVector2DCallback.clear();
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
		ensureLayer( layer, 0, true, blockLower );
		LayerDef* pDef = findLayer( layer );
		if ( pDef != nullptr )
		{
			pDef->_bEnabled	   = SW_TRUE;
			pDef->_bBlockLower = blockLower ? SW_TRUE : SW_FALSE;
		}

		for ( auto it = _listLayerStack.begin(); it != _listLayerStack.end(); ++it )
		{
			if ( *it == layer )
			{
				_listLayerStack.erase( it );
				break;
			}
		}
		_listLayerStack.push_back( string( layer ) );
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
		for ( auto it = _listLayerStack.rbegin(); it != _listLayerStack.rend(); ++it )
		{
			if ( *it == layer )
			{
				_listLayerStack.erase( std::next( it ).base() );
				break;
			}
		}
	}

	string_view ActionMap::getCurrentTopLayer() const
	{
		if ( _listLayerStack.empty() == false )
			return _listLayerStack.back();
		return _defaultLayerName;
	}

	void ActionMap::enableOnlyLayer( string_view layer )
	{
		for ( auto& [name, def] : _mapLayer )
		{
			if ( def._bAlwaysOn == SW_TRUE )
				continue;
			def._bEnabled = ( name == layer ) ? SW_TRUE : SW_FALSE;
		}
		_listLayerStack.clear();
		_listLayerStack.push_back( string( layer ) );
	}

	void ActionMap::setToggleMode( string_view action, bool bToggle )
	{
		_mapToggleMode[string( action )] = bToggle;
		if ( bToggle == false )
			_mapToggleState[string( action )] = false;
	}

	bool ActionMap::isActionToggled( string_view action ) const
	{
		auto it = _mapToggleState.find( action );
		return it != _mapToggleState.end() ? it->second : false;
	}

	bool ActionMap::isActionToggled( const hashed_string& action ) const
	{
		return isActionToggled( action.view() );
	}

	void ActionMap::bufferAction( string_view action, float32 expirationSeconds )
	{
		if ( action.empty() )
			return;
		BufferedActionItem item{};
		item._action		= string( action );
		item._remainingTime = expirationSeconds;
		_listBufferedAction.push_back( item );
	}

	bool ActionMap::consumeBufferedAction( string_view action )
	{
		for ( auto it = _listBufferedAction.begin(); it != _listBufferedAction.end(); ++it )
		{
			if ( it->_action == action && it->_remainingTime > 0.0f )
			{
				_listBufferedAction.erase( it );
				return true;
			}
		}
		return false;
	}

	bool ActionMap::checkCommandSequence( const vector<string>& listSequence, float32 maxWindowSeconds ) const
	{
		if ( listSequence.empty() || _listCommandHistory.size() < listSequence.size() )
			return false;

		const size_t seqCount	= listSequence.size();
		const size_t histCount	= _listCommandHistory.size();
		const size_t startIndex = histCount - seqCount;

		const float32 firstTime = _listCommandHistory[startIndex]._timestamp;
		const float32 lastTime	= _listCommandHistory[histCount - 1]._timestamp;
		if ( ( lastTime - firstTime ) > maxWindowSeconds )
			return false;

		for ( size_t index = 0; index < seqCount; ++index )
		{
			if ( _listCommandHistory[startIndex + index]._action != listSequence[index] )
				return false;
		}
		return true;
	}

	string ActionMap::getGlyphForAction( string_view action ) const
	{
		const InputDeviceType device = _pInput != nullptr ? _pInput->getActiveDeviceType() : InputDeviceType::KeyboardMouse;
		const ActionEntry*	  pEntry = findAction( action );
		if ( pEntry == nullptr || pEntry->_listBinding.empty() )
			return "[ ? ]";

		for ( const ActionBinding& b : pEntry->_listBinding )
		{
			if ( device == InputDeviceType::KeyboardMouse )
			{
				if ( b._kind == BindingKind::SingleSlot )
				{
					if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
					{
						const Key key = static_cast<Key>( b._arrSlot[0]._controlIndex );
						if ( key != Key::Unknown )
							return string( "[ " ) + KeyCodes::toName( key ) + " ]";
					}
					else if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Mouse )
					{
						const MouseButton btn = static_cast<MouseButton>( b._arrSlot[0]._controlIndex );
						if ( btn != MouseButton::Count )
							return string( "[ " ) + MouseButtons::toName( btn ) + " ]";
					}
				}
				else if ( b._kind == BindingKind::Vector2DComposite )
				{
					return "[ WASD ]";
				}
				else if ( b._kind == BindingKind::Chord )
				{
					const Key modKey  = static_cast<Key>( b._arrSlot[0]._controlIndex );
					const Key trigKey = static_cast<Key>( b._arrSlot[1]._controlIndex );
					return string( "[ " ) + KeyCodes::toName( modKey ) + " + " + KeyCodes::toName( trigKey ) + " ]";
				}
			}
			else
			{
				if ( b._kind == BindingKind::SingleSlot && b._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
				{
					const GamepadButton btn = static_cast<GamepadButton>( b._arrSlot[0]._controlIndex );
					if ( btn != GamepadButton::Count )
					{
						if ( device == InputDeviceType::GamepadPlayStation )
						{
							if ( btn == GamepadButton::A )
								return "[ X ]";
							if ( btn == GamepadButton::B )
								return "[ Circle ]";
							if ( btn == GamepadButton::X )
								return "[ Square ]";
							if ( btn == GamepadButton::Y )
								return "[ Triangle ]";
						}
						return string( "[ " ) + GamepadButtons::toName( btn ) + " ]";
					}
				}
				else if ( b._kind == BindingKind::GamepadStick2D )
				{
					return ( b._stick == GamepadStick::Left ) ? "[ L-Stick ]" : "[ R-Stick ]";
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

	bool ActionMap::saveUserBindings( string_view filePath ) const
	{
		if ( filePath.empty() )
			return false;

		XmlDocument doc;
		XmlNode		root = doc.appendRoot( "UserBindings" );
		for ( const auto& [actionName, entry] : _mapAction )
		{
			for ( const ActionBinding& b : entry._listBinding )
			{
				if ( b._kind == BindingKind::SingleSlot )
				{
					if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
					{
						const Key key = static_cast<Key>( b._arrSlot[0]._controlIndex );
						if ( key != Key::Unknown )
						{
							XmlNode bindNode = root.appendChild( "bind" );
							bindNode.appendAttr( "action", actionName.c_str() );
							bindNode.appendAttr( "source", "key" );
							bindNode.appendAttr( "key", KeyCodes::toName( key ) );
							bindNode.appendAttr( "layer", b._layer.c_str() );
						}
					}
					else if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
					{
						const GamepadButton btn = static_cast<GamepadButton>( b._arrSlot[0]._controlIndex );
						if ( btn != GamepadButton::Count )
						{
							XmlNode bindNode = root.appendChild( "bind" );
							bindNode.appendAttr( "action", actionName.c_str() );
							bindNode.appendAttr( "source", "gamepad" );
							bindNode.appendAttr( "code", GamepadButtons::toName( btn ) );
							bindNode.appendAttr( "layer", b._layer.c_str() );
						}
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
			const utf8* pAction	   = bindNode.attr( "action" );
			const utf8* pKeyStr	   = bindNode.attr( "key" );
			const utf8* pCodeStr   = bindNode.attr( "code" );
			const utf8* pSourceStr = bindNode.attr( "source" );

			if ( pAction == nullptr )
				continue;

			if ( pKeyStr != nullptr )
			{
				const Key key = KeyCodes::fromName( pKeyStr );
				if ( key != Key::Unknown )
					rebindKey( pAction, key, 0 );
			}
			else if ( pCodeStr != nullptr && pSourceStr != nullptr )
			{
				if ( StringUtil::equals( pSourceStr, "key", true ) )
				{
					const Key key = KeyCodes::fromName( pCodeStr );
					if ( key != Key::Unknown )
						rebindKey( pAction, key, 0 );
				}
				else if ( StringUtil::equals( pSourceStr, "gamepad", true ) )
				{
					const GamepadButton btn = GamepadButtons::fromName( pCodeStr );
					if ( btn != GamepadButton::Count )
						rebindSlot( pAction, InputSlot::fromGamepadButton( btn ), 0 );
				}
			}
		}
		return true;
	}

	void ActionMap::update( float32 deltaSeconds )
	{
		_totalElapsedTime += deltaSeconds;

		// 1) 선입력 버퍼 갱신
		for ( auto it = _listBufferedAction.begin(); it != _listBufferedAction.end(); )
		{
			it->_remainingTime -= deltaSeconds;
			if ( it->_remainingTime <= 0.0f )
				it = _listBufferedAction.erase( it );
			else
				++it;
		}

		// 2) 커맨드 이력 만료 제거 (2.0초 초과)
		while ( _listCommandHistory.empty() == false && ( _totalElapsedTime - _listCommandHistory.front()._timestamp ) > 2.0f )
		{
			_listCommandHistory.erase( _listCommandHistory.begin() );
		}

		if ( _pInput == nullptr )
			return;

		int32 curMouseX{ 0 };
		int32 curMouseY{ 0 };
		_pInput->getMousePosition( curMouseX, curMouseY );

		// 3) 통합 액션 런타임 평가 및 ActionPhase 상태 머신
		for ( auto& [actionName, actionEntry] : _mapAction )
		{
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
					const float32 distSq = static_cast<float32>( ( curMouseX - state._lastPressX ) * ( curMouseX - state._lastPressX ) +
																 ( curMouseY - state._lastPressY ) * ( curMouseY - state._lastPressY ) );
					if ( state._timeSinceLastPress <= _doubleClickTime && distSq <= ( _doubleClickMaxDistance * _doubleClickMaxDistance ) )
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

			// 모디파이어 적용 (축 반전 및 클램핑)
			if ( _bInvertX == SW_TRUE )
				totalAccumValue._x = -totalAccumValue._x;
			if ( _bInvertY == SW_TRUE )
				totalAccumValue._y = -totalAccumValue._y;
			totalAccumValue._x		  = totalAccumValue._x < -1.0f ? -1.0f : ( totalAccumValue._x > 1.0f ? 1.0f : totalAccumValue._x );
			totalAccumValue._y		  = totalAccumValue._y < -1.0f ? -1.0f : ( totalAccumValue._y > 1.0f ? 1.0f : totalAccumValue._y );
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
			if ( actionEntry._bPressed == SW_TRUE )
			{
				auto itToggle = _mapToggleMode.find( actionName );
				if ( itToggle != _mapToggleMode.end() && itToggle->second == true )
					_mapToggleState[actionName] = !_mapToggleState[actionName];
			}

			// 커맨드 이력 기록
			if ( actionEntry._bTriggered == SW_TRUE )
			{
				CommandHistoryEntry entry{};
				entry._action	 = actionName;
				entry._timestamp = _totalElapsedTime;
				_listCommandHistory.push_back( entry );
			}

			// 델리게이트 이벤트 디스패치 (Triggered)
			if ( actionEntry._bTriggered == SW_TRUE )
			{
				auto itCb = _mapActionCallback.find( actionName );
				if ( itCb != _mapActionCallback.end() )
				{
					for ( const ActionCallbackEntry& cbEntry : itCb->second )
					{
						if ( cbEntry._callback.isBound() )
							cbEntry._callback();
					}
				}
			}

			// 페이즈 델리게이트 디스패치 (Phase)
			if ( actionEntry._currentPhase != ActionPhase::None )
			{
				auto itPhase = _mapPhaseCallback.find( actionName );
				if ( itPhase != _mapPhaseCallback.end() )
				{
					for ( const PhaseCallbackEntry& phaseEntry : itPhase->second )
					{
						if ( phaseEntry._phase == actionEntry._currentPhase && phaseEntry._callback.isBound() )
							phaseEntry._callback();
					}
				}
			}
		}

		// 4) 2D 벡터 델리게이트 디스패치
		for ( const auto& [actionName, listCb] : _mapVector2DCallback )
		{
			const float2 vec = getVector2D( actionName );
			for ( const auto& cb : listCb )
			{
				if ( cb.isBound() )
					cb( vec );
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
				IInputDevice* pDevice = _pInput->getDevice( binding._arrSlot[0]._deviceKind );
				if ( pDevice != nullptr && pDevice->isControlDown( binding._arrSlot[0]._controlIndex ) )
				{
					outValue = float2{ 1.0f, 0.0f };
					return true;
				}
				return false;
			}
			case BindingKind::Axis1DComposite:
			{
				IInputDevice* pNegDev = _pInput->getDevice( binding._arrSlot[0]._deviceKind );
				IInputDevice* pPosDev = _pInput->getDevice( binding._arrSlot[1]._deviceKind );
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
				IInputDevice* pUpDev	= _pInput->getDevice( binding._arrSlot[0]._deviceKind );
				IInputDevice* pDownDev	= _pInput->getDevice( binding._arrSlot[1]._deviceKind );
				IInputDevice* pLeftDev	= _pInput->getDevice( binding._arrSlot[2]._deviceKind );
				IInputDevice* pRightDev = _pInput->getDevice( binding._arrSlot[3]._deviceKind );

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
				if ( lenSq > 1.0f )
				{
					const float32 invLen = 1.0f / std::sqrt( lenSq );
					kbdVec._x *= invLen;
					kbdVec._y *= invLen;
				}
				outValue = kbdVec;
				return ( kbdVec._x != 0.0f || kbdVec._y != 0.0f );
			}
			case BindingKind::GamepadStick2D:
			{
				GamepadDevice* pPad = _pInput->getGamepad();
				if ( pPad != nullptr && pPad->isConnected() )
				{
					float32 sx{ 0.0f };
					float32 sy{ 0.0f };
					if ( binding._stick == GamepadStick::Left )
						pPad->getLeftStick( sx, sy );
					else
						pPad->getRightStick( sx, sy );

					if ( _deadzoneShape == DeadzoneShape::Radial )
					{
						const float32 mag = std::sqrt( sx * sx + sy * sy );
						if ( mag < binding._deadzone )
						{
							sx = 0.0f;
							sy = 0.0f;
						}
						else
						{
							const float32 norm = ( mag - binding._deadzone ) / ( 1.0f - binding._deadzone );
							sx				   = ( sx / mag ) * norm;
							sy				   = ( sy / mag ) * norm;
						}
					}
					else // Axial
					{
						const float32 absX = sx < 0.0f ? -sx : sx;
						const float32 absY = sy < 0.0f ? -sy : sy;
						if ( absX < binding._deadzone )
							sx = 0.0f;
						if ( absY < binding._deadzone )
							sy = 0.0f;
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
				IInputDevice* pModDev  = _pInput->getDevice( binding._arrSlot[0]._deviceKind );
				IInputDevice* pTrigDev = _pInput->getDevice( binding._arrSlot[1]._deviceKind );
				if ( pModDev != nullptr && pTrigDev != nullptr )
				{
					const bool bModDown	 = pModDev->isControlDown( binding._arrSlot[0]._controlIndex );
					const bool bTrigDown = ( binding._trigger == ActionTrigger::Pressed )
											 ? pTrigDev->wasControlPressed( binding._arrSlot[1]._controlIndex )
											 : pTrigDev->isControlDown( binding._arrSlot[1]._controlIndex );
					if ( bModDown && bTrigDown )
					{
						outValue = float2{ 1.0f, 0.0f };
						return true;
					}
				}
				return false;
			}
			default:
				return false;
		}
	}

	bool ActionMap::evaluateTrigger( ActionTrigger trigger, const ActionBindingState& state, float32 ) const
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
			case ActionTrigger::Count:
			default:
				return false;
		}
	}

	bool ActionMap::isBindingLayerActive( const ActionBinding& binding ) const
	{
		return isLayerActiveInternal( binding._layer );
	}

	bool ActionMap::isLayerActiveInternal( string_view layer ) const
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
		return _mapLayer.find( layer ) != _mapLayer.end();
	}

	bool ActionMap::isLayerEnabled( string_view layer ) const
	{
		return isLayerActiveInternal( layer );
	}

	int32 ActionMap::getLayerPriority( string_view layer ) const
	{
		const LayerDef* pDef = findLayer( layer );
		return pDef != nullptr ? pDef->_priority : 0;
	}

	bool ActionMap::hasAction( string_view action ) const
	{
		return _mapAction.find( action ) != _mapAction.end();
	}

	ActionTrigger ActionMap::getBindingTrigger( string_view action, uint32 bindIndex ) const
	{
		const ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
			return ActionTrigger::Pressed;
		return pEntry->_listBinding[bindIndex]._trigger;
	}

	uint32 ActionMap::getBindingCount( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr ? static_cast<uint32>( pEntry->_listBinding.size() ) : 0;
	}

	const ActionBinding* ActionMap::getBinding( string_view action, uint32 bindIndex ) const
	{
		const ActionEntry* pEntry = findAction( action );
		if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
			return nullptr;
		return &pEntry->_listBinding[bindIndex];
	}

	bool ActionMap::wasActionTriggered( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
	}

	bool ActionMap::wasActionTriggered( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
	}

	bool ActionMap::isActionDown( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
	}

	bool ActionMap::isActionDown( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
	}

	bool ActionMap::wasActionPressed( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bPressed == SW_TRUE;
	}

	bool ActionMap::wasActionPressed( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bPressed == SW_TRUE;
	}

	bool ActionMap::wasActionReleased( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bReleased == SW_TRUE;
	}

	bool ActionMap::wasActionReleased( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bReleased == SW_TRUE;
	}

	bool ActionMap::wasActionDoubleClicked( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bDoubleClicked == SW_TRUE;
	}

	bool ActionMap::wasActionDoubleClicked( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bDoubleClicked == SW_TRUE;
	}

	bool ActionMap::wasActionHoldThreshold( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bHoldThreshold == SW_TRUE;
	}

	bool ActionMap::wasActionHoldThreshold( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr && pEntry->_bHoldThreshold == SW_TRUE;
	}

	float32 ActionMap::getActionHoldDuration( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr ? pEntry->_holdDuration : 0.0f;
	}

	float32 ActionMap::getActionHoldDuration( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr ? pEntry->_holdDuration : 0.0f;
	}

	ActionPhase ActionMap::getActionPhase( string_view action ) const
	{
		const ActionEntry* pEntry = findAction( action );
		return pEntry != nullptr ? pEntry->_currentPhase : ActionPhase::None;
	}

	ActionPhase ActionMap::getActionPhase( const hashed_string& action ) const
	{
		const ActionEntry* pEntry = findAction( action );
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

	void ActionMap::ensureActionListed( string_view action )
	{
		for ( const string& name : _listActionName )
		{
			if ( name == action )
				return;
		}
		_listActionName.push_back( string( action ) );
	}

	LayerDef& ActionMap::ensureLayer( string_view name, int32 priority, bool enabled, bool blockLower, bool alwaysOn )
	{
		auto it = _mapLayer.find( name );
		if ( it != _mapLayer.end() )
			return it->second;

		LayerDef def{};
		def._name		 = string( name );
		def._priority	 = priority;
		def._bEnabled	 = enabled ? SW_TRUE : SW_FALSE;
		def._bBlockLower = blockLower ? SW_TRUE : SW_FALSE;
		def._bAlwaysOn	 = alwaysOn ? SW_TRUE : SW_FALSE;

		_listLayerName.push_back( def._name );
		auto [insertedIt, _] = _mapLayer.emplace( def._name, def );
		return insertedIt->second;
	}

	ActionMap::ActionEntry& ActionMap::getOrCreateAction( string_view action, InputActionValueType valueType )
	{
		ensureActionListed( action );
		auto it = _mapAction.find( action );
		if ( it == _mapAction.end() )
		{
			ActionEntry entry{};
			entry._valueType = valueType;
			auto [newIt, _]	 = _mapAction.emplace( string( action ), std::move( entry ) );
			return newIt->second;
		}
		if ( it->second._valueType == InputActionValueType::Boolean && valueType != InputActionValueType::Boolean )
			it->second._valueType = valueType;
		return it->second;
	}

	LayerDef* ActionMap::findLayer( string_view name )
	{
		auto it = _mapLayer.find( name );
		return it != _mapLayer.end() ? &it->second : nullptr;
	}

	const LayerDef* ActionMap::findLayer( string_view name ) const
	{
		auto it = _mapLayer.find( name );
		return it != _mapLayer.end() ? &it->second : nullptr;
	}

	ActionMap::ActionEntry* ActionMap::findAction( string_view action )
	{
		auto it = _mapAction.find( action );
		return it != _mapAction.end() ? &it->second : nullptr;
	}

	const ActionMap::ActionEntry* ActionMap::findAction( string_view action ) const
	{
		auto it = _mapAction.find( action );
		return it != _mapAction.end() ? &it->second : nullptr;
	}

	const ActionMap::ActionEntry* ActionMap::findAction( const hashed_string& action ) const
	{
		return findAction( action.view() );
	}
} // namespace sw
