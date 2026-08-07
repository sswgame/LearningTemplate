/**
 * @file SWGame.cpp
 * @brief SWGame 모듈 구현 및 GameAPI 브릿지
 */
#include "IGame.h"
#include "Runtime/GameAPI.h"
#include "Core/Common/CoreServices.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Window/IWindow.h"
#include "Core/Graphics/RHI/IRHIDevice.h"

namespace sw
{
	class SWGame : public IGame
	{
	public:
		SWGame()			  = default;
		~SWGame() override	  = default;

		bool initialize( IWindow* window, IRHIDevice* rhiDevice ) override;
		void shutdown() override;
		void update( float32 deltaTime ) override;
	};

	bool SWGame::initialize( IWindow* /*window*/, IRHIDevice* /*rhiDevice*/ )
	{
		SW_LOG_INFO( "[SWGame] Initializing Game Module..." );

		getGlobalVariableManager().registerPendingVariables(
			"SWGame", GlobalVariableRegistrar::getHead() );
		getTypeRegistry().registerPendingTypes(
			"SWGame", TypeRegistrar::getHead(), EnumRegistrar::getHead() );

		return true;
	}

	void SWGame::shutdown()
	{
		SW_LOG_INFO( "[SWGame] Shutting down Game Module..." );
		getTypeRegistry().unregisterTypesByModule( "SWGame" );
		getGlobalVariableManager().unregisterVariablesByModule( "SWGame" );
	}

	void SWGame::update( float32 deltaTime )
	{
		if ( auto* scene = getSceneManager().getActiveScene() )
			scene->update( deltaTime );
	}
}

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
}

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
