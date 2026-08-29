#include "pch.h"

#include "Games/Demo/DemoGame.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Data/GameData.h"
#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Data/MonsterDataCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Input/GameActions.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

#include "Games/Demo/DemoGameHelpers.h"

#include "RuntimeAPI/ABI/GameAPI.h"
#include "RuntimeAPI/Export/GameModuleExports.h"
#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	SW_LOG_CALLER( "DemoGame" );

	DemoGame::DemoGame()
		: _tileMap{}
		, _player{}
		, _zones{}
		, _battle{}
		, _actionRoom{}
		, _data{}
		, _monsterCatalog{}
		, _speciesCatalog{}
		, _save{}
		, _transitions{}
		, _hud{}
		, _cameraBias{}
		, _partyList{}
		, _currentMapPath{}
		, _returnMapPath{}
		, _returnScenePath{}
		, _returnPlayerX{ 1 }
		, _returnPlayerY{ 1 }
		, _bTitleHandedOff{ SW_FALSE }
		, _bBattleReturnPending{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	DemoGame::~DemoGame()
	{
	}

	bool DemoGame::onInitialize()
	{
		SW_LOG_INFO( "Initializing Game Module..." );
		registerGameFrameworkTypes();

		_data = _bootstrap._data;
		game::bindLocalService( &_data );
		game::bindLocalService( &_monsterCatalog );
		game::bindLocalService( &_speciesCatalog );

		_speciesCatalog.loadFromResource( _data._speciesData );
		GameStrings::setupLocalization( _data._localizationDirectory, _data._defaultLanguage, _data._fallbackLanguage );
		if ( GameStrings::getAvailableLanguages().empty() )
			GameStrings::setupLocalization( _data._stringsData, _data._defaultLanguage, _data._fallbackLanguage );
		_monsterCatalog.loadFromResource( _data._monstersData );

		spawnSampleActorIfMissing();
		spawnDemoCubeIfMissing();

		ActionMap& actions = gameActions();
		if ( actions.loadFromResource( _data._inputMap ) == false )
		{
			SW_LOG_WARNING( "InputMap load failed (%#) — using emergency fallback.", _data._inputMap );
			actions.bindEmergencyFallback();
		}
		gameActionIds().loadFromResource( _data._inputMap );
		actions.enableOnlyLayer( gameActionIds()._layerGameplay );
		actions.setInputManager( game::getService<InputManager>() );
		_player.setActionMap( &actions );
		wireTransitionCallbacks();

		_bTitleHandedOff	  = SW_FALSE;
		_bBattleReturnPending = SW_FALSE;
		_transitions.reset();
		_currentMapPath = _data._startMap;
		_hud.setVisible( true );
		_hud.setScreenRect( 0.0f, 0.0f, 1.0f, 1.0f );
		SW_LOG_INFO( "Standby for Title handoff before overworld." );
		return true;
	}

	void DemoGame::onShutdown()
	{
		SW_LOG_INFO( "Shutting down Game Module..." );

		game::unbindLocalService<SpeciesCatalog>();
		game::unbindLocalService<MonsterDataCatalog>();
		game::unbindLocalService<GameData>();

		destroyDemoCube();
		destroyModuleSampleActors();
		_battle.endBattle();
		_tileMap.clear();
		_zones.clear();
		_partyList.clear();
	}

	void DemoGame::onUpdate( float32 deltaTime )
	{
		if ( _bTitleHandedOff == SW_FALSE )
		{
			_bTitleHandedOff = SW_TRUE;
			game::getService<SceneManager>()->requestLoadAsync( _data._titleScene );
			SW_LOG_INFO( "Title scene loaded: '%#'", _data._titleScene );
		}

		{
			ActionMap& actions = gameActions();
			actions.setInputManager( game::getService<InputManager>() );
			actions.update( deltaTime );
		}

		_transitions.update( deltaTime );

		const TransitionOrchestrator::Phase phase = _transitions.getPhase();
		const bool							bBattlePhase =
			_battle.isActive() || phase == TransitionOrchestrator::Phase::BattleFadeOut || phase == TransitionOrchestrator::Phase::BattleLoad || phase == TransitionOrchestrator::Phase::BattleFadeIn || phase == TransitionOrchestrator::Phase::ReturnFadeOut;
		if ( bBattlePhase )
			updateBattle( deltaTime );
		else
			updateOverworld( deltaTime );

		updateHud();
	}

} // namespace sw

// ==============================================================================
// SWGame C-ABI 진입점 매크로 자동 구현
// ==============================================================================
SW_IMPLEMENT_GAME_MODULE( sw::DemoGame );
