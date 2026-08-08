/**
 * @file SWGame.cpp
 * @brief SWGame module + GameAPI bridge — HD-2D overworld / battle skeleton (pass 1)
 */
#include "IGame.h"
#include "SWGameModuleHeads.h"

SW_DEFINE_MODULE_REGISTRAR_HEAD( swGameGvmHead, ::sw::GlobalVariableRegistrar );
SW_DEFINE_MODULE_REGISTRAR_HEAD( swGameTypeHead, ::sw::TypeRegistrar );
SW_DEFINE_MODULE_REGISTRAR_HEAD( swGameEnumHead, ::sw::EnumRegistrar );
SW_DEFINE_MODULE_REGISTRAR_HEAD( swGameComponentFactoryHead, ::sw::ComponentFactoryRegistrar );

#include "Runtime/GameAPI.h"
#include "Core/Common/CoreServices.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/SceneComponent.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Input/InputManager.h"
#include "Core/Window/IWindow.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Game/GameState.h"
#include "Game/SWGameTypes.h"
#include "Game/Overworld/TileMap.h"
#include "Game/Overworld/PlayerController.h"
#include "Game/Battle/BattleState.h"
#include "Game/Data/GameData.h"
#include "Game/Save/SaveGame.h"

namespace sw
{
	namespace
	{
		void destroyModuleSampleActors()
		{
			Scene* scene = core::getSceneManager().getActiveScene();
			if ( scene == nullptr )
				return;

			GameObjectManager* objects = scene->getObjectManager();
			if ( objects == nullptr )
				return;

			const hashed_string		 sampleName( "SampleActor" );
			std::vector<GameObject*> toDestroy;
			for ( GameObject* obj : objects->getAllGameObjects() )
			{
				if ( obj != nullptr && obj->getName() == sampleName )
					toDestroy.push_back( obj );
			}
			for ( GameObject* obj : toDestroy )
				objects->destroyObjectDeferred( obj );
			objects->processDeferredDestruction();
		}

		void spawnSampleActorIfMissing()
		{
			Scene* scene = core::getSceneManager().getActiveScene();
			if ( scene == nullptr )
				return;

			GameObjectManager* objects = scene->getObjectManager();
			if ( objects == nullptr )
				return;

			const hashed_string sampleName( "SampleActor" );
			if ( objects->findGameObjectByName( sampleName ) != nullptr )
				return;

			GameObject* sample = objects->createGameObject( sampleName );
			if ( sample == nullptr )
				return;

			SceneComponent* root = sample->addComponent<SceneComponent>();
			if ( root != nullptr )
				root->setLocalPosition( float3( 0.0f, 1.0f, 0.0f ) );

			SampleHealthComponent* health = sample->addComponent<SampleHealthComponent>();
			if ( health != nullptr )
				health->_health = 100.0f;

			SW_LOG_INFO( "[SWGame] Spawned SampleActor with SceneComponent + SampleHealthComponent." );
		}

		std::string scenePathForMap( const std::string& mapPath )
		{
			if ( mapPath.find( "route01" ) != std::string::npos )
				return "Game/Maps/route01_scene.xml";
			if ( mapPath.find( "town01" ) != std::string::npos )
				return "Game/Maps/town01_scene.xml";
			if ( mapPath.find( "battle01" ) != std::string::npos )
				return "Game/Maps/battle01_scene.xml";
			return {};
		}

		std::string resolveSavePath( const std::string& relativePath )
		{
			std::string abs = ResourceUtil::getResourcePath( relativePath );
			if ( abs.empty() )
				abs = relativePath;
			return abs;
		}

		/** HD-2D pass 1: orthographic / top-down-ish camera bias (render hook TBD). */
		struct OverworldCameraBias
		{
			float32 _pitchDeg	= 35.0f;
			float32 _yawDeg		= 45.0f;
			float32 _distance	= 12.0f;
			float32 _focusWorldX = 0.0f;
			float32 _focusWorldY = 0.0f;
			float32 _focusWorldZ = 0.0f;
		};
	} // namespace

	class SWGame : public IGame
	{
	public:
		SWGame();
		~SWGame() override = default;

		bool initialize( IWindow* window, IRHIDevice* rhiDevice ) override;
		void shutdown() override;
		void update( float32 deltaTime ) override;

	private:
		bool loadMap( const std::string& mapPath, int32 spawnX = 1, int32 spawnY = 1 );
		void requestSceneForMap( const std::string& mapPath );
		void beginBattleEncounter();
		void finishBattleReturn();
		void syncSaveFromWorld();
		bool applySaveToWorld();
		void updateOverworld( float32 deltaTime );
		void updateBattle( float32 deltaTime );
		void updateHd2dCameraBias();

		TileMap			 _tileMap;
		PlayerController _player;
		BattleState		 _battle;
		GameData		 _data;
		SaveGame		 _save;
		OverworldCameraBias _cameraBias{};
		std::string		 _currentMapPath;
		std::string		 _returnMapPath;
		std::string		 _returnScenePath;
		int32			 _returnPlayerX = 1;
		int32			 _returnPlayerY = 1;
		uint8			 _bTitleHandedOff : 1;
		uint8			 _bBattleReturnPending : 1;
		[[maybe_unused]] uint8 _reserved : 6;
	};

	SWGame::SWGame()
		: _bTitleHandedOff{ 0 }
		, _bBattleReturnPending{ 0 }
		, _reserved{ 0 }
	{
	}

	void SWGame::requestSceneForMap( const std::string& mapPath )
	{
		const std::string scenePath = scenePathForMap( mapPath );
		if ( scenePath.empty() == false )
			core::getSceneManager().requestLoadAsync( scenePath );
	}

	bool SWGame::loadMap( const std::string& mapPath, int32 spawnX, int32 spawnY )
	{
		if ( _tileMap.loadFromXml( mapPath ) == false )
		{
			SW_LOG_WARNING( "[SWGame] Map load failed: %#", mapPath );
			return false;
		}
		_currentMapPath = mapPath;
		_player.setTileMap( &_tileMap );

		if ( spawnX < 0 || spawnY < 0 || _tileMap.isWalkable( spawnX, spawnY ) == false )
		{
			spawnX = 1;
			spawnY = 1;
		}
		_player.setPosition( spawnX, spawnY );
		SW_LOG_INFO( "[SWGame] Overworld map ready: '%#' spawn=(%#,%#)", _tileMap.getName(), spawnX, spawnY );

		requestSceneForMap( mapPath );
		_tileMap.debugLogTileHd2d( spawnX, spawnY );
		return true;
	}

	void SWGame::syncSaveFromWorld()
	{
		_save._mapPath = _currentMapPath;
		_save._playerX = _player.getTileX();
		_save._playerY = _player.getTileY();
	}

	bool SWGame::applySaveToWorld()
	{
		return loadMap( _save._mapPath, _save._playerX, _save._playerY );
	}

	void SWGame::beginBattleEncounter()
	{
		_returnMapPath	 = _currentMapPath;
		_returnScenePath = scenePathForMap( _currentMapPath );
		_returnPlayerX	 = _player.getTileX();
		_returnPlayerY	 = _player.getTileY();
		_bBattleReturnPending = 1;

		const char* foeName = _data.pickRouteEncounterName();
		_battle.startWildEncounter( foeName );
		core::getSceneManager().requestLoadAsync( _data._battleScene );
		SW_LOG_INFO( "[SWGame] Battle scene requested: %# (foe=%#)", _data._battleScene, foeName );
	}

	void SWGame::finishBattleReturn()
	{
		_bBattleReturnPending = 0;
		loadMap( _returnMapPath, _returnPlayerX, _returnPlayerY );
		if ( _returnScenePath.empty() == false )
			core::getSceneManager().requestLoadAsync( _returnScenePath );
		SW_LOG_INFO( "[SWGame] Returned to overworld '%#' @ (%#,%#)", _returnMapPath, _returnPlayerX, _returnPlayerY );
	}

	void SWGame::updateHd2dCameraBias()
	{
		// HD-2D pass 1: tile-space focus + fake height lift for future billboard/mesh pass.
		const float kTileSize = 1.0f;
		const int32 px		  = _player.getTileX();
		const int32 py		  = _player.getTileY();
		const TileVisual vis  = _tileMap.getTileVisual( px, py );

		_cameraBias._focusWorldX = static_cast<float32>( px ) * kTileSize;
		_cameraBias._focusWorldY = static_cast<float32>( py ) * kTileSize;
		_cameraBias._focusWorldZ = static_cast<float32>( vis._height ) * 0.25f;
	}

	bool SWGame::initialize( IWindow* /*window*/, IRHIDevice* /*rhiDevice*/ )
	{
		SW_LOG_INFO( "[SWGame] Initializing Game Module..." );

		core::getGlobalVariableManager().registerPendingVariables( "SWGame", swGameGvmHead() );
		core::getTypeRegistry().registerPendingTypes( "SWGame", swGameTypeHead(), swGameEnumHead() );
		core::getComponentManager().registerPendingFactories( "SWGame", swGameComponentFactoryHead() );
		core::getComponentManager().rebindAllCachedTypeInfo();

		spawnSampleActorIfMissing();

		_bTitleHandedOff	  = 0;
		_bBattleReturnPending = 0;
		_currentMapPath		  = _data._startMap;
		SW_LOG_INFO( "[SWGame] Waiting for Title handoff (Enter=New / C=Continue) before overworld." );
		return true;
	}

	void SWGame::shutdown()
	{
		SW_LOG_INFO( "[SWGame] Shutting down Game Module..." );

		destroyModuleSampleActors();
		_battle.endBattle();
		_tileMap.clear();

		core::getComponentManager().clearAllCachedTypeInfo();
		core::getComponentManager().unregisterFactoriesByModule( "SWGame" );
		core::getTypeRegistry().unregisterTypesByModule( "SWGame" );
		core::getGlobalVariableManager().unregisterVariablesByModule( "SWGame" );
	}

	void SWGame::updateOverworld( float32 deltaTime )
	{
		InputManager& input = core::getInputManager();
		_player.update( deltaTime, input );
		updateHd2dCameraBias();

		if ( input.wasKeyPressed( Key::F5 ) )
		{
			syncSaveFromWorld();
			const std::string savePath = resolveSavePath( _data._defaultSavePath );
			_save.saveToFile( savePath );
		}
		else if ( input.wasKeyPressed( Key::F9 ) )
		{
			const std::string savePath = resolveSavePath( _data._defaultSavePath );
			if ( _save.loadFromFile( savePath ) )
				applySaveToWorld();
		}

		std::string warpMap;
		int32		warpX = 1;
		int32		warpY = 1;
		if ( _player.consumeWarpRequest( warpMap, warpX, warpY ) )
		{
			SW_LOG_INFO( "[SWGame] Warp → map '%#' spawn=(%#,%#)", warpMap, warpX, warpY );
			loadMap( warpMap, warpX, warpY );
		}

		if ( _player.consumeEncounterRequest() )
			beginBattleEncounter();

		if ( _player.consumeMovedFlag() )
		{
			SW_LOG_INFO( "[SWGame] Player tile (%#,%#) on %# [HD-2D cam focus z=%#]",
						 _player.getTileX(), _player.getTileY(), _tileMap.getName(), _cameraBias._focusWorldZ );
			_tileMap.debugLogTileHd2d( _player.getTileX(), _player.getTileY() );
		}
	}

	void SWGame::updateBattle( float32 deltaTime )
	{
		InputManager& input = core::getInputManager();
		const bool	  wasActive = _battle.isActive();
		_battle.update( deltaTime );

		if ( _battle.getPhase() == BattlePhase::PlayerChoice )
		{
			if ( input.wasKeyPressed( Key::Enter ) || input.wasKeyPressed( Key::Space ) )
				_battle.chooseFight();
			else if ( input.wasKeyPressed( Key::Escape ) )
				_battle.chooseRun();
		}

		if ( wasActive && _battle.isActive() == false && _bBattleReturnPending != 0 )
			finishBattleReturn();
	}

	void SWGame::update( float32 deltaTime )
	{
		// Title handoff: first Playing update loads start map or continue save.
		if ( _bTitleHandedOff == 0 )
		{
			_bTitleHandedOff = 1;
			const GameStartMode mode = consumeGameStartMode();
			if ( mode == GameStartMode::Continue )
			{
				const std::string savePath = resolveSavePath( _data._defaultSavePath );
				if ( _save.loadFromFile( savePath ) && applySaveToWorld() )
					SW_LOG_INFO( "[SWGame] Continue — loaded save from %#", savePath );
				else
					loadMap( _data._startMap );
			}
			else
			{
				loadMap( _data._startMap );
				SW_LOG_INFO( "[SWGame] New Game — start map %#", _data._startMap );
			}
			SW_LOG_INFO( "[SWGame] Title handoff complete — overworld active." );
		}

		core::getSceneManager().tickTransitions();

		if ( _battle.isActive() )
			updateBattle( deltaTime );
		else
			updateOverworld( deltaTime );

		if ( auto* scene = core::getSceneManager().getActiveScene() )
			scene->update( deltaTime );
	}
} // namespace sw

namespace
{
	sw::GameHandle GameAPI_Create()
	{
		return static_cast<sw::GameHandle>( new sw::SWGame() );
	}

	void GameAPI_Destroy( sw::GameHandle game )
	{
		auto* pGame = static_cast<sw::IGame*>( game );
		if ( pGame != nullptr )
			delete pGame;
	}

	bool GameAPI_Initialize( sw::GameHandle game, sw::WindowHandle window, sw::RHIDeviceHandle rhiDevice )
	{
		auto* pGame = static_cast<sw::IGame*>( game );
		if ( pGame == nullptr )
			return false;
		return pGame->initialize( static_cast<sw::IWindow*>( window ), static_cast<sw::IRHIDevice*>( rhiDevice ) );
	}

	void GameAPI_Shutdown( sw::GameHandle game )
	{
		auto* pGame = static_cast<sw::IGame*>( game );
		if ( pGame != nullptr )
			pGame->shutdown();
	}

	void GameAPI_Update( sw::GameHandle game, float32 deltaTime )
	{
		auto* pGame = static_cast<sw::IGame*>( game );
		if ( pGame != nullptr )
			pGame->update( deltaTime );
	}
} // namespace

extern "C"
{
	SW_MODULE_API bool fillGameAPI( sw::GameAPI* outApi )
	{
		if ( outApi == nullptr )
			return false;

		outApi->create	   = &GameAPI_Create;
		outApi->destroy	   = &GameAPI_Destroy;
		outApi->initialize = &GameAPI_Initialize;
		outApi->shutdown   = &GameAPI_Shutdown;
		outApi->update	   = &GameAPI_Update;
		return true;
	}
}
