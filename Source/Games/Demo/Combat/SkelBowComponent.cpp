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
		_chargeAmount = 0.0f;
		_bAiming	  = false;
	}

	void SkelBowComponent::onEndPlay()
	{
	}

	void SkelBowComponent::onTick( float32 deltaTime )
	{
		if ( _bAiming )
		{
			_chargeAmount += deltaTime * _chargeSpeed;
			if ( _chargeAmount > 1.0f )
				_chargeAmount = 1.0f;
		}
	}

	void SkelBowComponent::startAiming()
	{
		_bAiming	  = true;
		_chargeAmount = 0.0f;
	}

	void SkelBowComponent::stopAiming()
	{
		_bAiming	  = false;
		_chargeAmount = 0.0f;
	}

	void SkelBowComponent::setAimAngle( float32 angle )
	{
		_aimAngle = angle;
	}

	void SkelBowComponent::fire()
	{
		if ( _chargeAmount <= 0.0f )
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
		const float32 firedCharge	= _chargeAmount;
		const float32 firedAngle	= _aimAngle;
		const float32 firedSpeed	= _arrowSpeed;
		const int32	  firedDamage	= _arrowDamage;

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
				const float32 radians = MathUtil::toRadian( firedAngle );
				const float32 speed	  = firedSpeed * ( 0.5f + 0.5f * firedCharge );
				pProjComp->setVelocity( float2{ MathUtil::cos( radians ) * speed, MathUtil::sin( radians ) * speed } );
				pProjComp->setDamage( static_cast<int32>( static_cast<float32>( firedDamage ) * ( 0.5f + 0.5f * firedCharge ) ) );
				pProjComp->setLifeTime( 3.0f );
			}

			pArrow->addTag( "Bullet"_tag );
		};

		pGameObjectManager->executeOrDeferPostTick( spawnArrow );

		_bAiming	  = false;
		_chargeAmount = 0.0f;
	}
} // namespace sw
