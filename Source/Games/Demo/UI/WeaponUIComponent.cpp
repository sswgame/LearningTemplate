#include "pch.h"

#include "Games/Demo/UI/WeaponUIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/GameService.h"

#include "Games/Demo/Actors/WeaponComponent.h"

namespace sw
{
	WeaponUIComponent::WeaponUIComponent()
		: _weaponName{}
		, _coolTimeRatio{ 0.0f }
		, _slotIndex{ 0 }
	{
	}

	void WeaponUIComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PostUpdate );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "UI"_tag );
	}

	void WeaponUIComponent::onEndPlay()
	{
	}

	void WeaponUIComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;

		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		GameObject* pWeaponObj = pGameObjectManager->findGameObjectByTag( "Weapon"_tag );
		if ( pWeaponObj != nullptr )
		{
			WeaponComponent* pWeaponComp = pWeaponObj->getComponent<WeaponComponent>();
			if ( pWeaponComp != nullptr )
			{
				if ( pWeaponComp->_skillCoolTime > 0.0f )
					_coolTimeRatio = pWeaponComp->_currentSkillCoolTime / pWeaponComp->_skillCoolTime;
				else
					_coolTimeRatio = 0.0f;
			}
		}
	}

	void WeaponUIComponent::setCoolTime( float32 ratio )
	{
		_coolTimeRatio = MathUtil::saturate( ratio );
	}
} // namespace sw
