#include "pch.h"

#include "Engine/Input/ActionMap.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Windows/GamepadXInput.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "ActionMap" );

	namespace
	{
		namespace InputMapXml
		{
			constexpr const utf8* kRoot				   = "InputMap";
			constexpr const utf8* kLayers			   = "layers";
			constexpr const utf8* kLayer			   = "layer";
			constexpr const utf8* kAction			   = "action";
			constexpr const utf8* kBind				   = "bind";
			constexpr const utf8* kAttrDefaultLayer	   = "defaultLayer";
			constexpr const utf8* kAttrDoubleClick	   = "doubleClickTime";
			constexpr const utf8* kAttrDoubleClickDist = "doubleClickMaxDistance";
			constexpr const utf8* kAttrHoldThreshold   = "holdThreshold";
			constexpr const utf8* kAttrName			   = "name";
			constexpr const utf8* kAttrPriority		   = "priority";
			constexpr const utf8* kAttrEnabled		   = "enabled";
			constexpr const utf8* kAttrBlockLower	   = "blockLower";
			constexpr const utf8* kAttrAlwaysOn		   = "alwaysOn";
			constexpr const utf8* kAttrLayer		   = "layer";
			constexpr const utf8* kAttrTrigger		   = "trigger";
			constexpr const utf8* kAttrSource		   = "source";
			constexpr const utf8* kAttrCode			   = "code";
			constexpr const utf8* kSourceKey		   = "key";
			constexpr const utf8* kSourceGamepad	   = "gamepad";
			constexpr const utf8* kSourceMouse		   = "mouse";
		} // namespace InputMapXml

		bool parseBoolAttr( const utf8* pText, bool fallback )
		{
			if ( pText == nullptr || pText[0] == '\0' )
				return fallback;
			if ( StringUtil::equalsIgnoreCase( pText, "1" ) || StringUtil::equalsIgnoreCase( pText, "true" ) || StringUtil::equalsIgnoreCase( pText, "yes" ) )
				return true;
			if ( StringUtil::equalsIgnoreCase( pText, "0" ) || StringUtil::equalsIgnoreCase( pText, "false" ) || StringUtil::equalsIgnoreCase( pText, "no" ) )
				return false;
			return fallback;
		}

		struct TriggerNameEntry
		{
			const utf8*	  _pName;
			ActionTrigger _trigger;
		};

		constexpr TriggerNameEntry kArrTriggerNames[] = {
			{	  "Pressed",		 ActionTrigger::Pressed},
			{		  "Press",	   ActionTrigger::Pressed},
			{	  "Started",		 ActionTrigger::Pressed},
			{		  "Down",		  ActionTrigger::Down},
			{		  "Held",		  ActionTrigger::Down},
			{	  "Performed",		   ActionTrigger::Down},
			{	  "Released",	  ActionTrigger::Released},
			{	  "Release",		 ActionTrigger::Released},
			{	  "Canceled",	  ActionTrigger::Released},
			{"DoubleClicked", ActionTrigger::DoubleClicked},
			{  "DoubleClick", ActionTrigger::DoubleClicked},
			{"HoldThreshold", ActionTrigger::HoldThreshold},
			{		  "Hold", ActionTrigger::HoldThreshold},
		};

	} // namespace

	ActionMap::ActionMap()
		: _pInput{ nullptr }
		, _mapAction{}
		, _mapLayer{}
		, _mapVector2D{}
		, _mapGamepadStick{}
		, _mapChord{}
		, _listActionName{}
		, _listLayerName{}
		, _defaultLayerName{ ActionMapDefaults::kDefaultLayerName }
		, _doubleClickTime{ ActionMapDefaults::kDoubleClickTime }
		, _doubleClickMaxDistance{ ActionMapDefaults::kDoubleClickMaxDistance }
		, _holdThreshold{ ActionMapDefaults::kHoldThreshold }
		, _cachedBlockFloor{ -1 }
	{
	}

	void ActionMap::clear()
	{
		_mapAction.clear();
		_mapLayer.clear();
		_mapVector2D.clear();
		_mapGamepadStick.clear();
		_mapChord.clear();
		_listActionName.clear();
		_listLayerName.clear();
		_defaultLayerName.assign( ActionMapDefaults::kDefaultLayerName );
		_doubleClickTime		= ActionMapDefaults::kDoubleClickTime;
		_doubleClickMaxDistance = ActionMapDefaults::kDoubleClickMaxDistance;
		_holdThreshold			= ActionMapDefaults::kHoldThreshold;
		_cachedBlockFloor		= -1;
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

		XmlNode root = doc.root( InputMapXml::kRoot );
		if ( root.isValid() == false )
		{
			SW_LOG_WARNING( "Missing <InputMap> in %#", absPath );
			return false;
		}

		const utf8*	  pDoubleClickAttr = root.attr( InputMapXml::kAttrDoubleClick );
		const float32 dblClick		   = pDoubleClickAttr != nullptr
										   ? static_cast<float32>( StringUtil::atof( pDoubleClickAttr ) )
										   : ActionMapDefaults::kDoubleClickTime;
		const utf8*	  pDblDistAttr	   = root.attr( InputMapXml::kAttrDoubleClickDist );
		const float32 dblDist		   = pDblDistAttr != nullptr
										   ? static_cast<float32>( StringUtil::atof( pDblDistAttr ) )
										   : ActionMapDefaults::kDoubleClickMaxDistance;
		const utf8*	  pHoldAttr		   = root.attr( InputMapXml::kAttrHoldThreshold );
		const float32 holdThr		   = pHoldAttr != nullptr
										   ? static_cast<float32>( StringUtil::atof( pHoldAttr ) )
										   : ActionMapDefaults::kHoldThreshold;
		const utf8*	  pDefLayer		   = root.attr( InputMapXml::kAttrDefaultLayer );

		clear();
		setDoubleClickTime( dblClick );
		setDoubleClickMaxDistance( dblDist );
		setHoldThreshold( holdThr );
		if ( pDefLayer != nullptr && pDefLayer[0] != '\0' )
			_defaultLayerName = pDefLayer;

		XmlNode layersNode = root.child( InputMapXml::kLayers );
		if ( layersNode.isValid() )
		{
			for ( XmlNode layerNode = layersNode.child( InputMapXml::kLayer ); layerNode.isValid();
				  layerNode			= layerNode.next( InputMapXml::kLayer ) )
			{
				const utf8* pLayerName = layerNode.attr( InputMapXml::kAttrName );
				if ( pLayerName == nullptr || pLayerName[0] == '\0' )
					continue;
				const int32 priority   = layerNode.attrInt( InputMapXml::kAttrPriority, 0 );
				const bool	enabled	   = parseBoolAttr( layerNode.attr( InputMapXml::kAttrEnabled ), true );
				const bool	blockLower = parseBoolAttr( layerNode.attr( InputMapXml::kAttrBlockLower ), false );
				const bool	alwaysOn   = parseBoolAttr( layerNode.attr( InputMapXml::kAttrAlwaysOn ), false );
				registerLayer( pLayerName, priority, enabled, blockLower, alwaysOn );
			}
		}

		ensureLayer( _defaultLayerName, 0, true, false );

		auto loadAction = [this]( XmlNode actionNode, string_view inheritedLayer )
		{
			const utf8* pActionName = actionNode.attr( InputMapXml::kAttrName );
			if ( pActionName == nullptr || pActionName[0] == '\0' )
				return;

			string		layer( inheritedLayer );
			const utf8* pLayerAttr = actionNode.attr( InputMapXml::kAttrLayer );
			if ( pLayerAttr != nullptr )
				layer.assign( pLayerAttr );
			if ( layer.empty() )
				layer = _defaultLayerName;
			ensureLayer( layer );

			auto		defaultTrigger = ActionTrigger::Pressed;
			const utf8* pTriggerAttr   = actionNode.attr( InputMapXml::kAttrTrigger );
			if ( pTriggerAttr != nullptr )
			{
				const ActionTrigger parsed = actionTriggerFromName( pTriggerAttr );
				if ( parsed != ActionTrigger::Count )
					defaultTrigger = parsed;
			}

			for ( XmlNode bindNode = actionNode.child( InputMapXml::kBind ); bindNode.isValid();
				  bindNode		   = bindNode.next( InputMapXml::kBind ) )
			{
				const utf8* pSource = bindNode.attr( InputMapXml::kAttrSource );
				const utf8* pCode	= bindNode.attr( InputMapXml::kAttrCode );
				if ( pSource == nullptr || pCode == nullptr || pCode[0] == '\0' )
					continue;

				ActionTrigger trigger		   = defaultTrigger;
				const utf8*	  pBindTriggerAttr = bindNode.attr( InputMapXml::kAttrTrigger );
				if ( pBindTriggerAttr != nullptr )
				{
					const ActionTrigger parsed = actionTriggerFromName( pBindTriggerAttr );
					if ( parsed != ActionTrigger::Count )
						trigger = parsed;
				}

				string		bindLayer	   = layer;
				const utf8* pBindLayerAttr = bindNode.attr( InputMapXml::kAttrLayer );
				if ( pBindLayerAttr != nullptr )
				{
					bindLayer = pBindLayerAttr;
					ensureLayer( bindLayer );
				}

				if ( StringUtil::equalsIgnoreCase( pSource, InputMapXml::kSourceKey ) )
				{
					const Key key = keyFromName( pCode );
					if ( key != Key::Unknown )
						bind( pActionName, key, trigger, bindLayer );
				}
				else if ( StringUtil::equalsIgnoreCase( pSource, InputMapXml::kSourceGamepad ) )
				{
					const GamepadButton button = gamepadButtonFromName( pCode );
					if ( button != GamepadButton::Count )
						bind( pActionName, button, trigger, bindLayer );
				}
				else if ( StringUtil::equalsIgnoreCase( pSource, InputMapXml::kSourceMouse ) )
				{
					const MouseButton mouse = mouseButtonFromName( pCode );
					if ( mouse != MouseButton::Count )
						bind( pActionName, mouse, trigger, bindLayer );
				}
			}
		};

		// 중첩 <layer name="UI"> <action/> </layer>
		for ( XmlNode layerNode = root.child( InputMapXml::kLayer ); layerNode.isValid(); layerNode = layerNode.next( InputMapXml::kLayer ) )
		{
			const utf8* pLayerName = layerNode.attr( InputMapXml::kAttrName );
			if ( pLayerName == nullptr || pLayerName[0] == '\0' )
				continue;
			if ( hasLayer( pLayerName ) == false )
			{
				const int32 priority   = layerNode.attrInt( InputMapXml::kAttrPriority, 0 );
				const bool	enabled	   = parseBoolAttr( layerNode.attr( InputMapXml::kAttrEnabled ), true );
				const bool	blockLower = parseBoolAttr( layerNode.attr( InputMapXml::kAttrBlockLower ), false );
				const bool	alwaysOn   = parseBoolAttr( layerNode.attr( InputMapXml::kAttrAlwaysOn ), false );
				registerLayer( pLayerName, priority, enabled, blockLower, alwaysOn );
			}
			for ( XmlNode actionNode = layerNode.child( InputMapXml::kAction ); actionNode.isValid();
				  actionNode		 = actionNode.next( InputMapXml::kAction ) )
			{
				loadAction( actionNode, pLayerName );
			}
		}

		// 최상위 액션 (layer= 속성 또는 기본 레이어)
		for ( XmlNode actionNode = root.child( InputMapXml::kAction ); actionNode.isValid();
			  actionNode		 = actionNode.next( InputMapXml::kAction ) )
		{
			loadAction( actionNode, {} );
		}

		if ( _listActionName.empty() )
		{
			SW_LOG_WARNING( "No actions in %#", absPath );
			return false;
		}

		_cachedBlockFloor = computeBlockFloorPriority();
		SW_LOG_INFO( "Loaded %# actions / %# layers from %# (defaultLayer=%#)",
					 static_cast<uint32>( _listActionName.size() ), static_cast<uint32>( _listLayerName.size() ), absPath,
					 _defaultLayerName );
		return true;
	}

	void ActionMap::bindEmergencyFallback()
	{
		const string& emergencyPath = engine::getEngineData()._shellInputMap;
		if ( emergencyPath.empty() == false && loadFromResource( emergencyPath ) )
		{
			SW_LOG_WARNING( "Using emergency InputMap resource %#", emergencyPath );
			return;
		}

		// 배포 트리에 엔진 리소스가 없을 때의 최후 수단.
		clear();
		registerLayer( ActionMapDefaults::kDebugLayerName, 1000, true, false, true );
		registerLayer( "Title", 50, true, true );
		registerLayer( ActionMapDefaults::kDefaultLayerName, 0, false, false );
		bind( "Confirm", Key::Enter, ActionTrigger::Pressed, "Title" );
		bind( "Confirm", Key::Space, ActionTrigger::Pressed, "Title" );
		bind( "Continue", Key::C, ActionTrigger::Pressed, "Title" );
		bind( "Cancel", Key::Escape, ActionTrigger::Pressed, "Title" );
		bind( ActionMapDefaults::kReloadShadersAction, Key::F8, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );
		bind( ActionMapDefaults::kReloadEditorAction, Key::F6, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );
		bind( ActionMapDefaults::kReloadGameAction, Key::F7, ActionTrigger::Pressed, ActionMapDefaults::kDebugLayerName );
		SW_LOG_WARNING( "Emergency hard-coded fallback (resource %# missing).", emergencyPath );
	}

	void ActionMap::update( float32 deltaSeconds )
	{
		const float32 dt  = deltaSeconds > 0.0f ? deltaSeconds : 0.0f;
		_cachedBlockFloor = computeBlockFloorPriority();

		int32 mouseX{ 0 };
		int32 mouseY{ 0 };
		if ( _pInput != nullptr )
			_pInput->getMousePosition( mouseX, mouseY );

		for ( auto& [name, rt] : _mapAction )
		{
			if ( rt._listBindState.size() != rt._listBinding.size() )
				rt._listBindState.resize( rt._listBinding.size() );

			rt._bDown		   = 0;
			rt._bPressed	   = 0;
			rt._bReleased	   = 0;
			rt._bDoubleClicked = 0;
			rt._bHoldThreshold = 0;
			rt._bTriggered	   = 0;
			rt._holdDuration   = 0.0f;

			for ( size_t bindIndex = 0; bindIndex < rt._listBinding.size(); ++bindIndex )
			{
				const InputBinding& binding		= rt._listBinding[bindIndex];
				BindingState&		state		= rt._listBindState[bindIndex];
				const bool			layerActive = isBindingLayerActive( binding );
				const bool			down		= evaluateDown( binding );

				state._bPressed		  = 0;
				state._bReleased	  = 0;
				state._bDoubleClicked = 0;
				state._bHoldThreshold = 0;
				state._bTriggered	  = 0;

				// 하드웨어 에지는 항상 추적해 레이어를 다시 켜도 스티키 트리거가 나지 않게 합니다.
				if ( down && state._bWasDown == 0 )
				{
					state._bPressed			= 1;
					const float32 deltaX	= static_cast<float32>( mouseX - state._lastPressX );
					const float32 deltaY	= static_cast<float32>( mouseY - state._lastPressY );
					const float32 distSq	= deltaX * deltaX + deltaY * deltaY;
					const float32 maxDistSq = _doubleClickMaxDistance * _doubleClickMaxDistance;
					if ( state._timeSinceLastPress <= _doubleClickTime && distSq <= maxDistSq )
						state._bDoubleClicked = 1;
					state._timeSinceLastPress = 0.0f;
					state._lastPressX		  = mouseX;
					state._lastPressY		  = mouseY;
					state._holdDuration		  = 0.0f;
				}
				else if ( down == false && state._bWasDown != 0 )
				{
					state._bReleased	= 1;
					state._holdDuration = 0.0f;
				}

				if ( down )
				{
					const float32 prevHold = state._holdDuration;
					state._holdDuration += dt;
					if ( prevHold < _holdThreshold && state._holdDuration >= _holdThreshold )
						state._bHoldThreshold = 1;
				}
				else
					state._timeSinceLastPress += dt;

				state._bDown	= down ? 1 : 0;
				state._bWasDown = state._bDown;

				if ( layerActive == false )
					continue;

				if ( evaluateTrigger( binding._trigger, state ) )
				{
					state._bTriggered = 1;
					rt._bTriggered	  = 1;
				}

				if ( state._bDown != 0 )
				{
					rt._bDown = 1;
					if ( state._holdDuration > rt._holdDuration )
						rt._holdDuration = state._holdDuration;
				}
				if ( state._bPressed != 0 )
					rt._bPressed = 1;
				if ( state._bReleased != 0 )
					rt._bReleased = 1;
				if ( state._bDoubleClicked != 0 )
					rt._bDoubleClicked = 1;
				if ( state._bHoldThreshold != 0 )
					rt._bHoldThreshold = 1;
			}
		}
	}

	void ActionMap::bind( string_view action, Key key, ActionTrigger trigger, string_view layer )
	{
		ActionRuntime& rt = getOrCreateRuntime( action );
		InputBinding   binding{};
		binding._source	 = InputBindingSource::Key;
		binding._key	 = key;
		binding._trigger = trigger;
		binding._layer	 = layer.empty() ? _defaultLayerName : string( layer );
		ensureLayer( binding._layer );
		rt._listBinding.push_back( std::move( binding ) );
		rt._listBindState.emplace_back();
	}

	void ActionMap::bind( string_view action, GamepadButton button, ActionTrigger trigger,
						  string_view layer )
	{
		ActionRuntime& rt = getOrCreateRuntime( action );
		InputBinding   binding{};
		binding._source	 = InputBindingSource::GamepadButton;
		binding._button	 = button;
		binding._trigger = trigger;
		binding._layer	 = layer.empty() ? _defaultLayerName : string( layer );
		ensureLayer( binding._layer );
		rt._listBinding.push_back( std::move( binding ) );
		rt._listBindState.emplace_back();
	}

	void ActionMap::bind( string_view action, MouseButton mouse, ActionTrigger trigger, string_view layer )
	{
		ActionRuntime& rt = getOrCreateRuntime( action );
		InputBinding   binding{};
		binding._source	 = InputBindingSource::MouseButton;
		binding._mouse	 = mouse;
		binding._trigger = trigger;
		binding._layer	 = layer.empty() ? _defaultLayerName : string( layer );
		ensureLayer( binding._layer );
		rt._listBinding.push_back( std::move( binding ) );
		rt._listBindState.emplace_back();
	}

	void ActionMap::registerLayer( string_view name, int32 priority, bool enabled, bool blockLower,
								   bool alwaysOn )
	{
		LayerDef& def	  = ensureLayer( name, priority, enabled, blockLower, alwaysOn );
		def._priority	  = priority;
		def._bEnabled	  = enabled ? 1 : 0;
		def._bBlockLower  = blockLower ? 1 : 0;
		def._bAlwaysOn	  = alwaysOn ? 1 : 0;
		_cachedBlockFloor = -1;
	}

	void ActionMap::setLayerEnabled( string_view layer, bool enabled )
	{
		LayerDef* pDef = findLayer( layer );
		if ( pDef == nullptr )
		{
			SW_LOG_WARNING( "Unknown layer '%#'", layer );
			return;
		}
		pDef->_bEnabled	  = enabled ? 1 : 0;
		_cachedBlockFloor = -1;
	}

	void ActionMap::pushLayer( string_view layer )
	{
		setLayerEnabled( layer, true );
	}

	void ActionMap::popLayer( string_view layer )
	{
		setLayerEnabled( layer, false );
	}

	void ActionMap::enableOnlyLayer( string_view layer )
	{
		for ( auto& [name, def] : _mapLayer )
		{
			if ( def._bAlwaysOn != 0 )
				continue;
			def._bEnabled = 0;
		}
		_cachedBlockFloor = -1;
		setLayerEnabled( layer, true );
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
		return findLayer( layer ) != nullptr;
	}

	bool ActionMap::isLayerEnabled( string_view layer ) const
	{
		const LayerDef* pDef = findLayer( layer );
		return pDef != nullptr && pDef->_bEnabled != 0;
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
		const ActionRuntime* pRt = findRuntime( action );
		if ( pRt == nullptr || bindIndex >= pRt->_listBinding.size() )
			return ActionTrigger::Pressed;
		return pRt->_listBinding[bindIndex]._trigger;
	}

	uint32 ActionMap::getBindingCount( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		return pRt != nullptr ? static_cast<uint32>( pRt->_listBinding.size() ) : 0;
	}

	bool ActionMap::wasActionTriggered( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		return pRt != nullptr && pRt->_bTriggered != 0;
	}

	bool ActionMap::wasActionTriggered( const hashed_string& action ) const
	{
		return wasActionTriggered( action.view() );
	}

	bool ActionMap::isActionDown( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		return pRt != nullptr && pRt->_bDown != 0;
	}

	bool ActionMap::isActionDown( const hashed_string& action ) const
	{
		return isActionDown( action.view() );
	}

	bool ActionMap::wasActionPressed( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		return pRt != nullptr && pRt->_bPressed != 0;
	}

	bool ActionMap::wasActionPressed( const hashed_string& action ) const
	{
		return wasActionPressed( action.view() );
	}

	bool ActionMap::wasActionReleased( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		return pRt != nullptr && pRt->_bReleased != 0;
	}

	bool ActionMap::wasActionReleased( const hashed_string& action ) const
	{
		return wasActionReleased( action.view() );
	}

	bool ActionMap::wasActionDoubleClicked( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		return pRt != nullptr && pRt->_bDoubleClicked != 0;
	}

	bool ActionMap::wasActionDoubleClicked( const hashed_string& action ) const
	{
		return wasActionDoubleClicked( action.view() );
	}

	bool ActionMap::wasActionHoldThreshold( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		return pRt != nullptr && pRt->_bHoldThreshold != 0;
	}

	bool ActionMap::wasActionHoldThreshold( const hashed_string& action ) const
	{
		return wasActionHoldThreshold( action.view() );
	}

	float32 ActionMap::getActionHoldDuration( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		return pRt != nullptr ? pRt->_holdDuration : 0.0f;
	}

	float32 ActionMap::getActionHoldDuration( const hashed_string& action ) const
	{
		return getActionHoldDuration( action.view() );
	}

	ActionPhase ActionMap::getActionPhase( string_view action ) const
	{
		const ActionRuntime* pRt = findRuntime( action );
		if ( pRt == nullptr )
			return ActionPhase::None;
		if ( pRt->_bPressed != 0 )
			return ActionPhase::Started;
		if ( pRt->_bReleased != 0 )
			return ActionPhase::Canceled;
		if ( pRt->_bDown != 0 )
			return ActionPhase::Performed;
		return ActionPhase::None;
	}

	ActionPhase ActionMap::getActionPhase( const hashed_string& action ) const
	{
		return getActionPhase( action.view() );
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
		if ( _pInput == nullptr || w <= 0 || h <= 0 )
			return false;
		if ( _pInput->isPointerInside() == false )
			return false;
		int32 mx{ 0 };
		int32 my{ 0 };
		_pInput->getMousePosition( mx, my );
		return mx >= x && my >= y && mx < ( x + w ) && my < ( y + h );
	}

	ActionTrigger ActionMap::actionTriggerFromName( string_view name )
	{
		if ( name.empty() )
			return ActionTrigger::Pressed;
		const string nameNt( name );
		for ( const TriggerNameEntry& entry : kArrTriggerNames )
		{
			if ( StringUtil::equalsIgnoreCase( nameNt.c_str(), entry._pName ) )
				return entry._trigger;
		}
		return ActionTrigger::Count;
	}

	const utf8* ActionMap::actionTriggerToName( ActionTrigger trigger )
	{
		for ( const TriggerNameEntry& entry : kArrTriggerNames )
		{
			if ( entry._trigger == trigger )
				return entry._pName;
		}
		return nullptr;
	}

	bool ActionMap::evaluateDown( const InputBinding& binding ) const
	{
		if ( _pInput == nullptr )
			return false;
		switch ( binding._source )
		{
			case InputBindingSource::Key:
				return _pInput->isKeyDown( binding._key );
			case InputBindingSource::GamepadButton:
			{
				GamepadXInput* pGamepad = _pInput->getGamepad();
				return pGamepad != nullptr && pGamepad->isButtonDown( binding._button );
			}
			case InputBindingSource::MouseButton:
				return _pInput->isMouseButtonDown( binding._mouse );
			default:
				return false;
		}
	}

	bool ActionMap::evaluateTrigger( ActionTrigger trigger, const BindingState& state ) const
	{
		switch ( trigger )
		{
			case ActionTrigger::Pressed:
				return state._bPressed != 0;
			case ActionTrigger::Released:
				return state._bReleased != 0;
			case ActionTrigger::Down:
				return state._bDown != 0;
			case ActionTrigger::DoubleClicked:
				return state._bDoubleClicked != 0;
			case ActionTrigger::HoldThreshold:
				return state._bHoldThreshold != 0;
			case ActionTrigger::Count:
				return false;
		}
		return false;
	}

	bool ActionMap::isBindingLayerActive( const InputBinding& binding ) const
	{
		const string&	layerName = binding._layer.empty() ? _defaultLayerName : binding._layer;
		const LayerDef* pDef	  = findLayer( layerName );
		if ( pDef == nullptr )
			return true;
		if ( pDef->_bEnabled == 0 )
			return false;
		if ( _cachedBlockFloor >= 0 && pDef->_priority < _cachedBlockFloor )
			return false;
		return true;
	}

	int32 ActionMap::computeBlockFloorPriority() const
	{
		int32 floorPri = -1;
		for ( const auto& [name, def] : _mapLayer )
		{
			if ( def._bEnabled == 0 || def._bBlockLower == 0 )
				continue;
			if ( def._priority > floorPri )
				floorPri = def._priority;
		}
		return floorPri;
	}

	void ActionMap::ensureActionListed( string_view action )
	{
		for ( const string& name : _listActionName )
		{
			if ( name == action )
				return;
		}
		_listActionName.emplace_back( action );
	}

	ActionMap::LayerDef& ActionMap::ensureLayer( string_view name, int32 priority, bool enabled,
												 bool blockLower, bool alwaysOn )
	{
		string key( name );
		if ( key.empty() )
			key = _defaultLayerName;

		auto it = _mapLayer.find( key );
		if ( it != _mapLayer.end() )
			return it->second;

		LayerDef def{};
		def._name		 = key;
		def._priority	 = priority;
		def._bEnabled	 = enabled ? 1 : 0;
		def._bBlockLower = blockLower ? 1 : 0;
		def._bAlwaysOn	 = alwaysOn ? 1 : 0;
		_listLayerName.push_back( key );
		return _mapLayer.emplace( key, std::move( def ) ).first->second;
	}

	ActionMap::ActionRuntime& ActionMap::getOrCreateRuntime( string_view action )
	{
		ensureActionListed( action );
		return _mapAction[string( action )];
	}

	ActionMap::LayerDef* ActionMap::findLayer( string_view name )
	{
		const auto it = _mapLayer.find( name );
		return it != _mapLayer.end() ? &it->second : nullptr;
	}

	const ActionMap::LayerDef* ActionMap::findLayer( string_view name ) const
	{
		const auto it = _mapLayer.find( name );
		return it != _mapLayer.end() ? &it->second : nullptr;
	}

	ActionMap::ActionRuntime* ActionMap::findRuntime( string_view action )
	{
		const auto it = _mapAction.find( action );
		return it != _mapAction.end() ? &it->second : nullptr;
	}

	const ActionMap::ActionRuntime* ActionMap::findRuntime( string_view action ) const
	{
		const auto it = _mapAction.find( action );
		return it != _mapAction.end() ? &it->second : nullptr;
	}

	void ActionMap::bindVector2D( string_view action, Key up, Key down, Key left, Key right, float32 deadzone,
								  string_view layer )
	{
		string act( action );
		ensureActionListed( act );

		Vector2DBinding binding{};
		binding._layer	  = layer.empty() ? _defaultLayerName : string( layer );
		binding._up		  = up;
		binding._down	  = down;
		binding._left	  = left;
		binding._right	  = right;
		binding._deadzone = deadzone >= 0.0f ? deadzone : 0.0f;

		_mapVector2D[act] = std::move( binding );
	}

	void ActionMap::bindGamepadStick2D( string_view action, GamepadStick stick, float32 deadzone, string_view layer )
	{
		string act( action );
		ensureActionListed( act );

		GamepadStickBinding binding{};
		binding._layer	  = layer.empty() ? _defaultLayerName : string( layer );
		binding._stick	  = stick;
		binding._deadzone = deadzone >= 0.0f ? deadzone : 0.0f;

		_mapGamepadStick[act].push_back( std::move( binding ) );
	}

	float2 ActionMap::getVector2D( string_view action ) const
	{
		if ( _pInput == nullptr )
			return float2{ 0.0f, 0.0f };

		// 1) 게임패드 아날로그 스틱 우선 조회
		const auto itStick = _mapGamepadStick.find( action );
		if ( itStick != _mapGamepadStick.end() && _pInput->getGamepad() != nullptr )
		{
			const GamepadXInput* pPad = _pInput->getGamepad();
			for ( const GamepadStickBinding& binding : itStick->second )
			{
				if ( binding._layer.empty() == false )
				{
					const LayerDef* pLayer = findLayer( binding._layer );
					if ( pLayer != nullptr && ( pLayer->_bEnabled == 0 || pLayer->_priority < _cachedBlockFloor ) )
						continue;
				}

				float32 sx = 0.0f;
				float32 sy = 0.0f;
				if ( binding._stick == GamepadStick::Left )
					pPad->getLeftStick( sx, sy );
				else
					pPad->getRightStick( sx, sy );

				const float32 lengthSq = sx * sx + sy * sy;
				if ( lengthSq > 1e-6f )
				{
					const float32 length = MathUtil::sqrt( lengthSq );
					if ( binding._deadzone > 0.0f && length < binding._deadzone )
						continue;

					if ( length > 1.0f )
					{
						sx /= length;
						sy /= length;
					}
					return float2{ sx, sy };
				}
			}
		}

		// 2) 키보드 4방향 축 조회
		const auto it = _mapVector2D.find( action );
		if ( it == _mapVector2D.end() )
			return float2{ 0.0f, 0.0f };

		const Vector2DBinding& binding = it->second;
		if ( binding._layer.empty() == false )
		{
			const LayerDef* pLayer = findLayer( binding._layer );
			if ( pLayer != nullptr && ( pLayer->_bEnabled == 0 || pLayer->_priority < _cachedBlockFloor ) )
				return float2{ 0.0f, 0.0f };
		}

		float32 x = 0.0f;
		float32 y = 0.0f;

		if ( binding._right != Key::Unknown && _pInput->isKeyDown( binding._right ) )
			x += 1.0f;
		if ( binding._left != Key::Unknown && _pInput->isKeyDown( binding._left ) )
			x -= 1.0f;
		if ( binding._up != Key::Unknown && _pInput->isKeyDown( binding._up ) )
			y += 1.0f;
		if ( binding._down != Key::Unknown && _pInput->isKeyDown( binding._down ) )
			y -= 1.0f;

		const float32 lengthSq = x * x + y * y;
		if ( lengthSq < 1e-6f )
			return float2{ 0.0f, 0.0f };

		const float32 length = MathUtil::sqrt( lengthSq );
		if ( binding._deadzone > 0.0f && length < binding._deadzone )
			return float2{ 0.0f, 0.0f };

		if ( length > 1.0f )
		{
			x /= length;
			y /= length;
		}

		return float2{ x, y };
	}

	float2 ActionMap::getVector2D( const hashed_string& action ) const
	{
		return getVector2D( action.view() );
	}

	void ActionMap::bindChord( string_view action, Key modifierKey, Key triggerKey, ActionTrigger trigger,
							   string_view layer )
	{
		string act( action );
		ensureActionListed( act );

		ChordBinding chord{};
		chord._layer	  = layer.empty() ? _defaultLayerName : string( layer );
		chord._modifier	  = modifierKey;
		chord._triggerKey = triggerKey;
		chord._trigger	  = trigger;

		_mapChord[act].push_back( std::move( chord ) );
	}

	bool ActionMap::isChordDown( string_view action ) const
	{
		if ( _pInput == nullptr )
			return false;

		const auto it = _mapChord.find( action );
		if ( it == _mapChord.end() )
			return false;

		for ( const ChordBinding& chord : it->second )
		{
			if ( chord._layer.empty() == false )
			{
				const LayerDef* pLayer = findLayer( chord._layer );
				if ( pLayer != nullptr && ( pLayer->_bEnabled == 0 || pLayer->_priority < _cachedBlockFloor ) )
					continue;
			}

			if ( _pInput->isKeyDown( chord._modifier ) && _pInput->isKeyDown( chord._triggerKey ) )
				return true;
		}
		return false;
	}

	bool ActionMap::isChordDown( const hashed_string& action ) const
	{
		return isChordDown( action.view() );
	}

	bool ActionMap::wasChordTriggered( string_view action ) const
	{
		if ( _pInput == nullptr )
			return false;

		const auto it = _mapChord.find( action );
		if ( it == _mapChord.end() )
			return false;

		for ( const ChordBinding& chord : it->second )
		{
			if ( chord._layer.empty() == false )
			{
				const LayerDef* pLayer = findLayer( chord._layer );
				if ( pLayer != nullptr && ( pLayer->_bEnabled == 0 || pLayer->_priority < _cachedBlockFloor ) )
					continue;
			}

			if ( _pInput->isKeyDown( chord._modifier ) && _pInput->wasKeyPressed( chord._triggerKey ) )
				return true;
		}
		return false;
	}

	bool ActionMap::wasChordTriggered( const hashed_string& action ) const
	{
		return wasChordTriggered( action.view() );
	}
} // namespace sw
