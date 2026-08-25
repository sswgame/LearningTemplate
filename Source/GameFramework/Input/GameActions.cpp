#include "pch.h"

#include "GameFramework/Data/GameData.h"
#include "GameFramework/Input/GameActions.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	namespace
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
	} // namespace

	bool GameActionIds::loadFromResource( string_view assetRelativePath )
	{
		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( assetRelativePath, &absPath ) == false )
		{
			SW_LOG_WARNING( "[GameActionIds] Failed to read %# — keeping built-in name fallbacks.", assetRelativePath );
			return false;
		}

		XmlNode root = doc.root( "InputMap" );
		if ( root.isValid() == false )
			return false;

		XmlNode ids = root.child( "gameplayIds" );
		if ( ids.isValid() == false )
		{
			SW_LOG_INFO( "[GameActionIds] No <gameplayIds> in %# — using default name strings.", absPath );
			return true;
		}

		takeId( ids, "moveUp", _moveUp );
		takeId( ids, "moveDown", _moveDown );
		takeId( ids, "moveLeft", _moveLeft );
		takeId( ids, "moveRight", _moveRight );
		takeId( ids, "interact", _interact );
		takeId( ids, "confirm", _confirm );
		takeId( ids, "cancel", _cancel );
		takeId( ids, "continue", _continue );
		takeId( ids, "fightMove0", _fightMove0 );
		takeId( ids, "fightMove1", _fightMove1 );
		takeId( ids, "attack", _attack );
		takeId( ids, "dash", _dash );
		takeId( ids, "point", _point );
		takeId( ids, "quickSave", _quickSave );
		takeId( ids, "quickLoad", _quickLoad );
		takeId( ids, "reloadShaders", _reloadShaders );
		takeId( ids, "reloadEditor", _reloadEditor );
		takeId( ids, "reloadGame", _reloadGame );
		takeId( ids, "layerGameplay", _layerGameplay );
		takeId( ids, "layerUI", _layerUI );
		takeId( ids, "layerTitle", _layerTitle );
		takeId( ids, "layerCinematic", _layerCinematic );
		takeId( ids, "layerDebug", _layerDebug );

		SW_LOG_INFO( "[GameActionIds] Loaded gameplay ids from %#", absPath );
		return true;
	}

	GameActionIds& gameActionIds()
	{
		static GameActionIds s_ids;
		return s_ids;
	}

	ActionMap& gameActions()
	{
		static ActionMap s_actions;
		static bool		 s_bound{ false };
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
