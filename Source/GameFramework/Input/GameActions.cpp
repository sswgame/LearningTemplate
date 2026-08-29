#include "pch.h"

#include "GameFramework/Input/GameActions.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameData.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	namespace
	{
		struct GameActionsInternal
		{
			static void takeId( XmlNode idsRoot, const utf8* pKey, string& dst )
			{
				for ( XmlNode actionNode = idsRoot.child( "id" ); actionNode; actionNode = actionNode.next( "id" ) )
				{
					const utf8* pKeyAttr = actionNode.attr( "key" );
					if ( pKeyAttr == nullptr || StringUtil::strcmp( pKeyAttr, pKey ) != 0 )
						continue;
					const utf8* pText = actionNode.text();
					if ( pText != nullptr && pText[0] != '\0' )
						dst.assign( pText );
					return;
				}
			}
		};

		GameActionIds s_ids;
		ActionMap	  s_actions;
		bool		  s_bound{ false };
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "GameActionIds" );

	bool GameActionIds::loadFromResource( string_view assetRelativePath )
	{
		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( assetRelativePath, &absPath ) == false )
		{
			SW_LOG_WARNING( "Failed to read %# — keeping built-in name fallbacks.", assetRelativePath );
			return false;
		}

		XmlNode root = doc.root( "InputMap" );
		if ( root.isValid() == false )
			return false;

		XmlNode ids = root.child( "gameplayIds" );
		if ( ids.isValid() == false )
		{
			SW_LOG_INFO( "No <gameplayIds> in %# — using default name strings.", absPath );
			return true;
		}

		GameActionsInternal::takeId( ids, "moveUp", _moveUp );
		GameActionsInternal::takeId( ids, "moveDown", _moveDown );
		GameActionsInternal::takeId( ids, "moveLeft", _moveLeft );
		GameActionsInternal::takeId( ids, "moveRight", _moveRight );
		GameActionsInternal::takeId( ids, "interact", _interact );
		GameActionsInternal::takeId( ids, "confirm", _confirm );
		GameActionsInternal::takeId( ids, "cancel", _cancel );
		GameActionsInternal::takeId( ids, "continue", _continue );
		GameActionsInternal::takeId( ids, "fightMove0", _fightMove0 );
		GameActionsInternal::takeId( ids, "fightMove1", _fightMove1 );
		GameActionsInternal::takeId( ids, "attack", _attack );
		GameActionsInternal::takeId( ids, "dash", _dash );
		GameActionsInternal::takeId( ids, "point", _point );
		GameActionsInternal::takeId( ids, "quickSave", _quickSave );
		GameActionsInternal::takeId( ids, "quickLoad", _quickLoad );
		GameActionsInternal::takeId( ids, "reloadShaders", _reloadShaders );
		GameActionsInternal::takeId( ids, "reloadEditor", _reloadEditor );
		GameActionsInternal::takeId( ids, "reloadGame", _reloadGame );
		GameActionsInternal::takeId( ids, "layerGameplay", _layerGameplay );
		GameActionsInternal::takeId( ids, "layerUI", _layerUI );
		GameActionsInternal::takeId( ids, "layerTitle", _layerTitle );
		GameActionsInternal::takeId( ids, "layerCinematic", _layerCinematic );
		GameActionsInternal::takeId( ids, "layerDebug", _layerDebug );

		SW_LOG_INFO( "Loaded gameplay ids from %#", absPath );
		return true;
	}

	GameActionIds& gameActionIds()
	{
		if ( s_bound == false )
		{
			BootstrapConfig bootstrap;
			bootstrap.load();
			s_ids.loadFromResource( bootstrap._data._inputMap );
			s_bound = true;
		}
		return s_ids;
	}

	ActionMap& gameActions()
	{
		if ( game::areGameServicesBound() )
		{
			auto* pInput = game::getService<InputManager>();
			if ( pInput != nullptr )
			{
				ActionMap& actions = pInput->getActionMap();
				if ( s_bound == false )
				{
					BootstrapConfig bootstrap;
					bootstrap.load();
					if ( actions.loadFromResource( bootstrap._data._inputMap ) == false )
						actions.bindEmergencyFallback();
					gameActionIds().loadFromResource( bootstrap._data._inputMap );
					s_bound = true;
				}
				return actions;
			}
		}

		if ( s_bound == false )
		{
			BootstrapConfig bootstrap;
			bootstrap.load();
			if ( s_actions.loadFromResource( bootstrap._data._inputMap ) == false )
				s_actions.bindEmergencyFallback();
			gameActionIds().loadFromResource( bootstrap._data._inputMap );
			s_bound = true;
		}
		return s_actions;
	}
} // namespace sw
