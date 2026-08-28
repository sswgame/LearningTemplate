#include "pch.h"

#include "Games/Demo/UI/DashUIComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/Demo/Actors/PlayerComponent.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	DashUIComponent::DashUIComponent()
		: _availableDashes{ 0 }
		, _rechargeRatio{ 1.0f }
	{
	}

	void DashUIComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PostUpdate );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "UI"_tag );
		_availableDashes = 2;
		_rechargeRatio	 = 1.0f;
	}

	void DashUIComponent::onEndPlay()
	{
	}

	void DashUIComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;

		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		GameObject* pPlayerObj = pGameObjectManager->findGameObjectByTag( "Player"_tag );
		if ( pPlayerObj != nullptr )
		{
			PlayerComponent* pPlayerComp = pPlayerObj->getComponent<PlayerComponent>();
			if ( pPlayerComp != nullptr )
			{
				_availableDashes = pPlayerComp->_dashCount;
				if ( pPlayerComp->_dashCoolTime > 0.0f )
				{
					_rechargeRatio = pPlayerComp->_dashCoolTimer / pPlayerComp->_dashCoolTime;
					if ( _rechargeRatio > 1.0f )
						_rechargeRatio = 1.0f;
				}
				else
				{
					_rechargeRatio = 1.0f;
				}
			}
		}
	}
} // namespace sw
