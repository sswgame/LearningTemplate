/**
 * @file SWGame.cpp
 * @brief SWGame 모듈 구현 및 GameAPI 브릿지
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
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Window/IWindow.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Game/SWGameTypes.h"

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
	} // namespace

	class SWGame : public IGame
	{
	public:
		SWGame()		   = default;
		~SWGame() override = default;

		bool initialize( IWindow* window, IRHIDevice* rhiDevice ) override;
		void shutdown() override;
		void update( float32 deltaTime ) override;
	};

	bool SWGame::initialize( IWindow* /*window*/, IRHIDevice* /*rhiDevice*/ )
	{
		SW_LOG_INFO( "[SWGame] Initializing Game Module..." );

		core::getGlobalVariableManager().registerPendingVariables( "SWGame", swGameGvmHead() );
		core::getTypeRegistry().registerPendingTypes( "SWGame", swGameTypeHead(), swGameEnumHead() );
		core::getComponentManager().registerPendingFactories( "SWGame", swGameComponentFactoryHead() );
		core::getComponentManager().rebindAllCachedTypeInfo();

		spawnSampleActorIfMissing();
		return true;
	}

	void SWGame::shutdown()
	{
		SW_LOG_INFO( "[SWGame] Shutting down Game Module..." );

		destroyModuleSampleActors();

		core::getComponentManager().clearAllCachedTypeInfo();
		core::getComponentManager().unregisterFactoriesByModule( "SWGame" );
		core::getTypeRegistry().unregisterTypesByModule( "SWGame" );
		core::getGlobalVariableManager().unregisterVariablesByModule( "SWGame" );
	}

	void SWGame::update( float32 deltaTime )
	{
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
