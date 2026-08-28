#include "pch.h"

#include "Games/Demo/UI/TitleComponent.h"

#include "Engine/Input/InputManager.h"

#include "GameFramework/Data/GameData.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	SW_LOG_CALLER( "TitleComponent" );

	void TitleComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PrePhysics );
		frontCloudCurrentX = 0.0f;
		backCloudCurrentX  = 0.0f;
		SW_LOG_INFO( "BeginPlay: Next scene='%#'", nextSceneName.c_str() );
	}

	void TitleComponent::onEndPlay()
	{
		SW_LOG_INFO( "EndPlay." );
	}

	void TitleComponent::onTick( float32 deltaTime )
	{
		moveClouds( deltaTime );
		checkMenuSelection();
	}

	void TitleComponent::moveClouds( float32 deltaTime )
	{
		frontCloudCurrentX += frontCloudSpeed * deltaTime;
		if ( frontCloudCurrentX >= maxFrontPosX )
			frontCloudCurrentX = startFrontPosX;

		backCloudCurrentX += backCloudSpeed * deltaTime;
		if ( backCloudCurrentX >= maxBackCloudPosX )
			backCloudCurrentX = startBackCloudPosX;
	}

	void TitleComponent::checkMenuSelection()
	{
		InputManager& inputManager = *game::getService<InputManager>();
		if ( inputManager.wasKeyPressed( Key::Enter ) || inputManager.wasKeyPressed( Key::Space ) )
			doAction( 0 ); // Play
	}

	void TitleComponent::doAction( int32 menuIndex )
	{
		if ( menuIndex == 0 )
		{
			string scene = nextSceneName;
			if ( scene.empty() )
				scene = GameData::get()._entranceScene;
			if ( scene.empty() )
				return;
			SW_LOG_INFO( "Loading next scene: '%#'", scene.c_str() );
			game::getService<SceneManager>()->requestLoadAsync( scene );
		}
	}
} // namespace sw
