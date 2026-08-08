/**
 * @file SWGame.cpp
 * @brief SWGame module + GameAPI bridge — HD-2D overworld / battle with fade + party
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
#include "Core/Input/ActionMap.h"
#include "Core/Input/InputManager.h"
#include "Core/Window/IWindow.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Game/GameState.h"
#include "Game/SWGameTypes.h"
#include "Game/Overworld/TileMap.h"
#include "Game/Overworld/PlayerController.h"
#include "Game/Overworld/ZoneRuntime.h"
#include "Game/Battle/BattleState.h"
#include "Game/Data/GameData.h"
#include "Game/Data/SpeciesData.h"
#include "Game/Save/SaveGame.h"
#include "Game/Transition/FadeService.h"
#include "Game/UI/RuntimeHud.h"

namespace sw
{
	namespace
	{
		enum class PendingTransition : uint8
		{
			None = 0,
			WarpFadeOut,
			WarpLoad,
			WarpFadeIn,
			BattleFadeOut,
			BattleLoad,
			BattleFadeIn,
			ReturnFadeOut,
			ReturnLoad,
			ReturnFadeIn
		};

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

		float32 safeFill( int32 cur, int32 max )
		{
			if ( max <= 0 )
				return 0.0f;
			const float32 t = static_cast<float32>( cur ) / static_cast<float32>( max );
			return t < 0.0f ? 0.0f : ( t > 1.0f ? 1.0f : t );
		}

		/** HD-2D pass 1: orthographic / top-down-ish camera bias (render hook TBD). */
		struct OverworldCameraBias
		{
			float32 _pitchDeg	 = 35.0f;
			float32 _yawDeg		 = 45.0f;
			float32 _distance	 = 12.0f;
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
		void syncSaveFromWorld();
		bool applySaveToWorld();
		void initNewGameParty();
		void applyPartyFromSave();
		void syncPartyLeadFromBattle();
		void updateHud();
		void updateTransitions( float32 deltaTime );
		void updateOverworld( float32 deltaTime );
		void updateBattle( float32 deltaTime );
		void updateHd2dCameraBias();
		void beginWarpTransition( const std::string& mapPath, int32 spawnX, int32 spawnY );
		void beginBattleTransition();
		void beginReturnTransition();
		bool canAcceptOverworldInput() const;

		TileMap			 _tileMap;
		PlayerController _player;
		ZoneRuntime		 _zones;
		BattleState		 _battle;
		GameData		 _data;
		SaveGame		 _save;
		FadeService		 _fade;
		RuntimeHud		 _hud;
		OverworldCameraBias _cameraBias{};
		std::vector<PartyMember> _party;
		std::string		 _currentMapPath;
		std::string		 _returnMapPath;
		std::string		 _returnScenePath;
		int32			 _returnPlayerX = 1;
		int32			 _returnPlayerY = 1;
		std::string		 _pendingWarpMap;
		int32			 _pendingWarpX = 1;
		int32			 _pendingWarpY = 1;
		PendingTransition _pending = PendingTransition::None;
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
		_zones.setFromMap( mapPath, _tileMap.getName(), _tileMap.getWidth(), _tileMap.getHeight() );

		if ( spawnX < 0 || spawnY < 0 || _tileMap.isWalkable( spawnX, spawnY ) == false )
		{
			spawnX = 1;
			spawnY = 1;
		}
		_player.setPosition( spawnX, spawnY );
		SW_LOG_INFO( "[SWGame] Overworld map ready: '%#' spawn=(%#,%#) zoneRole=%# gate=%#",
					 _tileMap.getName(), spawnX, spawnY,
					 static_cast<int>( _zones.getActiveRole() ),
					 _zones.isClearGateLocked() ? 1 : 0 );

		requestSceneForMap( mapPath );
		_tileMap.debugLogTileHd2d( spawnX, spawnY );
		return true;
	}

	void SWGame::initNewGameParty()
	{
		_party.clear();
		_party.push_back( GameData::makeStarterPartyMember() );
		_save.clearParty();
		_save.setPartyFrom( _party );
		_save.setFlag( "story_intro", 0 );
	}

	void SWGame::applyPartyFromSave()
	{
		_party = _save._party;
		if ( _party.empty() )
			initNewGameParty();
	}

	void SWGame::syncSaveFromWorld()
	{
		_save._mapPath = _currentMapPath;
		_save._playerX = _player.getTileX();
		_save._playerY = _player.getTileY();
		_save.setPartyFrom( _party );
	}

	bool SWGame::applySaveToWorld()
	{
		applyPartyFromSave();
		if ( loadMap( _save._mapPath, _save._playerX, _save._playerY ) == false )
			return false;
		// loadMap rebuilds zones; re-apply persisted clear-gate unlock.
		if ( _save.getFlag( "clear_gate_unlocked", 0 ) != 0 )
			_zones.setClearGateLocked( false );
		return true;
	}

	void SWGame::syncPartyLeadFromBattle()
	{
		if ( _party.empty() )
			return;
		const PartyMember& lead = _battle.getPlayer();
		_party[0]._hp	 = lead._hp;
		_party[0]._hpMax = lead._hpMax;
		_party[0]._pp0	 = lead._pp0;
		_party[0]._pp1	 = lead._pp1;
		_party[0]._exp	 = lead._exp;
		_party[0]._level = lead._level;
		_party[0]._expNext = lead._expNext;
	}

	bool SWGame::canAcceptOverworldInput() const
	{
		return getGameState() == GameState::Playing
			&& _pending == PendingTransition::None
			&& _battle.isActive() == false
			&& _fade.isBusy() == false;
	}

	void SWGame::beginWarpTransition( const std::string& mapPath, int32 spawnX, int32 spawnY )
	{
		if ( _zones.isWarpBlocked() )
		{
			const ZoneDef* zone = _zones.getActiveZone();
			SW_LOG_INFO( "[SWGame] Warp blocked — clear gate locked (zone=%#)",
						 zone != nullptr ? zone->_id.c_str() : "?" );
			_hud.setDialogue( "The path is sealed..." );
			return;
		}
		_pendingWarpMap = mapPath;
		_pendingWarpX	= spawnX;
		_pendingWarpY	= spawnY;
		_pending		= PendingTransition::WarpFadeOut;
		_fade.beginFadeOut();
		_player.setInputEnabled( false );
		SW_LOG_INFO( "[SWGame] Warp fade-out → '%#' (%#,%#)", mapPath, spawnX, spawnY );
	}

	void SWGame::beginBattleTransition()
	{
		_returnMapPath		  = _currentMapPath;
		_returnScenePath	  = scenePathForMap( _currentMapPath );
		_returnPlayerX		  = _player.getTileX();
		_returnPlayerY		  = _player.getTileY();
		_bBattleReturnPending = 1;
		_pending			  = PendingTransition::BattleFadeOut;
		_fade.beginFadeOut();
		_player.setInputEnabled( false );
		SW_LOG_INFO( "[SWGame] Battle fade-out from '%#'", _returnMapPath );
	}

	void SWGame::beginReturnTransition()
	{
		_pending = PendingTransition::ReturnFadeOut;
		_fade.beginFadeOut();
		SW_LOG_INFO( "[SWGame] Return fade-out (win=%#)", _battle.didPlayerWin() ? 1 : 0 );
	}

	void SWGame::updateHd2dCameraBias()
	{
		const float32 kTileSize = 1.0f;
		const int32 px		  = _player.getTileX();
		const int32 py		  = _player.getTileY();
		const TileVisual vis  = _tileMap.getTileVisual( px, py );

		_cameraBias._focusWorldX = static_cast<float32>( px ) * kTileSize;
		_cameraBias._focusWorldY = static_cast<float32>( py ) * kTileSize;
		_cameraBias._focusWorldZ = static_cast<float32>( vis._height ) * 0.25f;
	}

	void SWGame::updateHud()
	{
		_hud.setFadeOverlay( _fade.getOverlayAlpha() );

		if ( _battle.isActive() )
		{
			const PartyMember& p = _battle.getPlayer();
			const PartyMember& f = _battle.getFoe();
			_hud.setBattleGauges( safeFill( p._hp, p._hpMax ),
								  safeFill( f._hp, f._hpMax ),
								  safeFill( p._exp, p._expNext > 0 ? p._expNext : 1 ),
								  safeFill( p._pp0, 35 ) );
			_hud.setDialogue( _battle.getStatusText() );
		}
		else if ( _party.empty() == false )
		{
			const PartyMember& p = _party[0];
			_hud.setBattleGauges( safeFill( p._hp, p._hpMax ), 0.0f,
								  safeFill( p._exp, p._expNext > 0 ? p._expNext : 1 ),
								  safeFill( p._pp0, 35 ) );
		}
	}

	void SWGame::updateTransitions( float32 deltaTime )
	{
		_fade.update( deltaTime );

		switch ( _pending )
		{
		case PendingTransition::WarpFadeOut:
			if ( _fade.isFinished() )
				_pending = PendingTransition::WarpLoad;
			break;

		case PendingTransition::WarpLoad:
			loadMap( _pendingWarpMap, _pendingWarpX, _pendingWarpY );
			_pendingWarpMap.clear();
			_fade.beginFadeIn();
			_pending = PendingTransition::WarpFadeIn;
			break;

		case PendingTransition::WarpFadeIn:
			if ( _fade.isFinished() )
			{
				_pending = PendingTransition::None;
				_player.setInputEnabled( true );
				SW_LOG_INFO( "[SWGame] Warp complete." );
			}
			break;

		case PendingTransition::BattleFadeOut:
			if ( _fade.isFinished() )
				_pending = PendingTransition::BattleLoad;
			break;

		case PendingTransition::BattleLoad:
		{
			if ( _party.empty() )
				initNewGameParty();
			const char* foeId = _data.pickRouteEncounterId();
			_battle.startWithPartyLead( _party[0], foeId );
			core::getSceneManager().requestLoadAsync( _data._battleScene );
			_hud.setDialogue( _battle.getStatusText() );
			SW_LOG_INFO( "[SWGame] Battle scene requested: %# (foe=%#)", _data._battleScene, foeId );
			_fade.beginFadeIn();
			_pending = PendingTransition::BattleFadeIn;
			break;
		}

		case PendingTransition::BattleFadeIn:
			if ( _fade.isFinished() )
			{
				_pending = PendingTransition::None;
				SW_LOG_INFO( "[SWGame] Battle fade-in complete." );
			}
			break;

		case PendingTransition::ReturnFadeOut:
			if ( _fade.isFinished() )
				_pending = PendingTransition::ReturnLoad;
			break;

		case PendingTransition::ReturnLoad:
		{
			const bool won = _battle.didPlayerWin();
			syncPartyLeadFromBattle();
			_battle.endBattle();
			if ( won )
				_save.setFlag( "clear_gate_unlocked", 1 );
			loadMap( _returnMapPath, _returnPlayerX, _returnPlayerY );
			// loadMap resets zone gates; apply save flag after rebuild.
			if ( _save.getFlag( "clear_gate_unlocked", 0 ) != 0 )
			{
				_zones.setClearGateLocked( false );
				if ( won )
					SW_LOG_INFO( "[SWGame] Clear gate unlocked after win." );
			}
			if ( _returnScenePath.empty() == false )
				core::getSceneManager().requestLoadAsync( _returnScenePath );
			_bBattleReturnPending = 0;
			_hud.clearDialogue();
			_fade.beginFadeIn();
			_pending = PendingTransition::ReturnFadeIn;
			SW_LOG_INFO( "[SWGame] Returned to overworld '%#' @ (%#,%#)",
						 _returnMapPath, _returnPlayerX, _returnPlayerY );
			break;
		}

		case PendingTransition::ReturnFadeIn:
			if ( _fade.isFinished() )
			{
				_pending = PendingTransition::None;
				_player.setInputEnabled( true );
				SW_LOG_INFO( "[SWGame] Return fade-in complete." );
			}
			break;

		case PendingTransition::None:
		default:
			break;
		}
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
		_pending			  = PendingTransition::None;
		_currentMapPath		  = _data._startMap;
		_hud.setVisible( true );
		_hud.setScreenRect( 0.0f, 0.0f, 1.0f, 1.0f );
		SW_LOG_INFO( "[SWGame] Waiting for Title handoff (Enter=New / C=Continue) before overworld." );
		return true;
	}

	void SWGame::shutdown()
	{
		SW_LOG_INFO( "[SWGame] Shutting down Game Module..." );

		destroyModuleSampleActors();
		_battle.endBattle();
		_tileMap.clear();
		_zones.clear();
		_party.clear();

		core::getComponentManager().clearAllCachedTypeInfo();
		core::getComponentManager().unregisterFactoriesByModule( "SWGame" );
		core::getTypeRegistry().unregisterTypesByModule( "SWGame" );
		core::getGlobalVariableManager().unregisterVariablesByModule( "SWGame" );
	}

	void SWGame::updateOverworld( float32 deltaTime )
	{
		InputManager& input = core::getInputManager();

		const bool playing = getGameState() == GameState::Playing;
		_player.setInputEnabled( canAcceptOverworldInput() );
		if ( playing )
			_player.update( deltaTime, input );

		updateHd2dCameraBias();

		if ( playing == false || _pending != PendingTransition::None )
			return;

		if ( input.wasKeyPressed( Key::F5 ) )
		{
			syncSaveFromWorld();
			const std::string savePath = resolveSavePath( _data._defaultSavePath );
			_save.saveToFile( savePath );
			_hud.setDialogue( "Game saved." );
		}
		else if ( input.wasKeyPressed( Key::F9 ) )
		{
			const std::string savePath = resolveSavePath( _data._defaultSavePath );
			if ( _save.loadFromFile( savePath ) )
			{
				applySaveToWorld();
				_hud.setDialogue( "Game loaded." );
			}
		}

		std::string warpMap;
		int32		warpX = 1;
		int32		warpY = 1;
		if ( _player.consumeWarpRequest( warpMap, warpX, warpY ) )
			beginWarpTransition( warpMap, warpX, warpY );

		if ( _player.consumeEncounterRequest() )
			beginBattleTransition();

		if ( _player.consumeInteractRequest() )
		{
			int32 fx = 0;
			int32 fy = 0;
			_player.getFacingTile( fx, fy );
			const TileFlags flags = _tileMap.getFlags( fx, fy );
			SW_LOG_INFO( "[SWGame] Interact facing (%#,%#) flags=%# walk=%# pass=%#",
						 fx, fy, static_cast<int>( flags ),
						 _tileMap.isWalkable( fx, fy ) ? 1 : 0,
						 _tileMap.isPassThrough( fx, fy ) ? 1 : 0 );
			_hud.setDialogue( "Interact..." );
			// Stub: facing interaction can unlock the clear gate (e.g. gym switch / NPC).
			if ( _zones.isClearGateLocked() )
			{
				_zones.setClearGateLocked( false );
				_save.setFlag( "clear_gate_unlocked", 1 );
				_hud.setDialogue( "The gate unlocked!" );
				SW_LOG_INFO( "[SWGame] Interact stub unlocked clear gate." );
			}
		}

		if ( _player.consumeMovedFlag() )
		{
			SW_LOG_INFO( "[SWGame] Player tile (%#,%#) on %# [HD-2D cam focus z=%#]",
						 _player.getTileX(), _player.getTileY(), _tileMap.getName(), _cameraBias._focusWorldZ );
			_tileMap.debugLogTileHd2d( _player.getTileX(), _player.getTileY() );
		}
	}

	void SWGame::updateBattle( float32 deltaTime )
	{
		if ( _pending != PendingTransition::None )
			return;

		InputManager& input		= core::getInputManager();
		const bool	  wasActive = _battle.isActive();
		_battle.update( deltaTime );

		if ( _battle.getPhase() == BattlePhase::PlayerChoice && getGameState() == GameState::Playing )
		{
			static ActionMap s_actions;
			static bool		 s_bound = false;
			if ( s_bound == false )
			{
				s_actions.bindDefaults();
				s_bound = true;
			}
			s_actions.setInputManager( &input );
			s_actions.setGamepad( input.getGamepad() );

			if ( s_actions.wasActionPressed( Action::FightMove0 ) || s_actions.wasActionPressed( Action::Confirm ) )
				_battle.chooseFight( 0 );
			else if ( s_actions.wasActionPressed( Action::FightMove1 ) )
				_battle.chooseFight( 1 );
			else if ( s_actions.wasActionPressed( Action::Cancel ) )
				_battle.chooseRun();
		}

		if ( wasActive && _battle.isActive() == false && _bBattleReturnPending != 0 )
			beginReturnTransition();
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
				{
					initNewGameParty();
					loadMap( _data._startMap );
				}
			}
			else
			{
				initNewGameParty();
				loadMap( _data._startMap );
				SW_LOG_INFO( "[SWGame] New Game — start map %# party lead=%#",
							 _data._startMap, _party.empty() ? "?" : _party[0]._nickname.c_str() );
			}
			SW_LOG_INFO( "[SWGame] Title handoff complete — overworld active." );
		}

		core::getSceneManager().tickTransitions();
		updateTransitions( deltaTime );

		if ( _battle.isActive() || _pending == PendingTransition::BattleFadeOut
			 || _pending == PendingTransition::BattleLoad || _pending == PendingTransition::BattleFadeIn
			 || _pending == PendingTransition::ReturnFadeOut )
		{
			updateBattle( deltaTime );
		}
		else
		{
			updateOverworld( deltaTime );
		}

		updateHud();

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
