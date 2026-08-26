#include "pch.h"

#include "Games/Demo/Combat/SkelBowComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/ActionCombat/ProjectileComponent.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	void SkelBowComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Weapon"_tag );
		chargeAmount = 0.0f;
		bAiming		 = false;
	}

	void SkelBowComponent::onEndPlay()
	{
	}

	void SkelBowComponent::onTick( float32 deltaTime )
	{
		if ( bAiming )
		{
			chargeAmount += deltaTime * chargeSpeed;
			if ( chargeAmount > 1.0f )
				chargeAmount = 1.0f;
		}
	}

	void SkelBowComponent::startAiming()
	{
		bAiming		 = true;
		chargeAmount = 0.0f;
	}

	void SkelBowComponent::stopAiming()
	{
		bAiming		 = false;
		chargeAmount = 0.0f;
	}

	void SkelBowComponent::setAimAngle( float32 angle )
	{
		aimAngle = angle;
	}

	void SkelBowComponent::fire()
	{
		if ( chargeAmount <= 0.0f )
			return;

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pOwnerSceneComp = pOwner->getPrimarySceneComponent();
		if ( pOwnerSceneComp == nullptr )
			return;

		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		const float3  ownerWorldPos = pOwnerSceneComp->getWorldPosition();
		const float32 firedCharge	= chargeAmount;
		const float32 firedAngle	= aimAngle;
		const float32 firedSpeed	= arrowSpeed;
		const int32	  firedDamage	= arrowDamage;

		auto spawnArrow = [pGameObjectManager, ownerWorldPos, firedCharge, firedAngle, firedSpeed, firedDamage]()
		{
			GameObject* pArrow = pGameObjectManager->createGameObject( hashed_string( "Arrow" ) );
			if ( pArrow == nullptr )
				return;

			SceneComponent* pSceneComp = pArrow->addComponent<SceneComponent>();
			if ( pSceneComp != nullptr )
				pSceneComp->setLocalPosition( ownerWorldPos );

			ProjectileComponent* pProjComp = pArrow->addComponent<ProjectileComponent>();
			if ( pProjComp != nullptr )
			{
				ProjectileData* pProjData = pProjComp->ensureProjectileData();
				if ( pProjData != nullptr )
				{
					const float32 radians = MathUtil::toRadian( firedAngle );
					const float32 speed	  = firedSpeed * ( 0.5f + 0.5f * firedCharge );
					pProjData->velocity	  = float2{ MathUtil::cos( radians ) * speed, MathUtil::sin( radians ) * speed };
					pProjData->damage	  = static_cast<int32>( static_cast<float32>( firedDamage ) * ( 0.5f + 0.5f * firedCharge ) );
					pProjData->lifeTime	  = 3.0f;
				}
			}

			pArrow->addTag( "Bullet"_tag );
		};

		pGameObjectManager->executeOrDeferPostTick( spawnArrow );

		bAiming		 = false;
		chargeAmount = 0.0f;
	}
} // namespace sw
