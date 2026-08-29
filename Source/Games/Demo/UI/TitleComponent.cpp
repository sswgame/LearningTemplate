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
		_frontCloudCurrentX = 0.0f;
		_backCloudCurrentX	= 0.0f;
		SW_LOG_INFO( "BeginPlay: Next scene='%#'", _nextSceneName.c_str() );
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
		_frontCloudCurrentX += _frontCloudSpeed * deltaTime;
		if ( _frontCloudCurrentX >= _maxFrontPosX )
			_frontCloudCurrentX = _startFrontPosX;

		_backCloudCurrentX += _backCloudSpeed * deltaTime;
		if ( _backCloudCurrentX >= _maxBackCloudPosX )
			_backCloudCurrentX = _startBackCloudPosX;
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
			string scene = _nextSceneName;
			if ( scene.empty() )
			{
				const GameData* pGameData = game::getService<GameData>();
				if ( pGameData != nullptr )
					scene = pGameData->_entranceScene;
			}
			if ( scene.empty() )
				return;
			SW_LOG_INFO( "Loading next scene: '%#'", scene.c_str() );
			game::getService<SceneManager>()->requestLoadAsync( scene );
		}
	}
} // namespace sw
