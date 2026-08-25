#include "pch.h"

#include "Games/Demo/UI/WeaponUIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/Demo/Actors/WeaponComponent.h"

#include "RuntimeAPI/GameService.h"

namespace sw
{
	WeaponUIComponent::WeaponUIComponent()
		: weaponName{}
		, coolTimeRatio{ 0.0f }
		, slotIndex{ 0 }
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
			WeaponComponent* pWeaponComp = pWeaponObj->getComponent<WeaponComponent>().get();
			if ( pWeaponComp != nullptr )
			{
				if ( pWeaponComp->skillCoolTime > 0.0f )
					coolTimeRatio = pWeaponComp->currentSkillCoolTime / pWeaponComp->skillCoolTime;
				else
					coolTimeRatio = 0.0f;
			}
		}
	}

	void WeaponUIComponent::setCoolTime( float32 ratio )
	{
		coolTimeRatio = MathUtil::clamp( ratio, 0.0f, 1.0f );
	}
} // namespace sw
