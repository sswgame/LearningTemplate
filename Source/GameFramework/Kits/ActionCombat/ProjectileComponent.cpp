#include "pch.h"

#include "GameFramework/Kits/ActionCombat/ProjectileComponent.h"

#include "Engine/Object/Component/EcsDataUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void ProjectileComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Bullet"_tag );

		ProjectileData* pData = ensureProjectileData();
		if ( pData != nullptr )
			pData->currentLife = 0.0f;
	}

	void ProjectileComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void ProjectileComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		ProjectileData* pData = ensureProjectileData();
		if ( pData == nullptr )
			return;

		pData->currentLife += deltaTime;
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
		{
			if ( pData->lifeTime > 0.0f && pData->currentLife >= pData->lifeTime )
			{
				pOwner->markPendingKill();
				return;
			}
			SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
			if ( pSceneComp != nullptr )
			{
				float3 pos = pSceneComp->getLocalPosition();
				pos._x += pData->velocity._x * deltaTime;
				pos._y += pData->velocity._y * deltaTime;
				pSceneComp->setLocalPosition( pos );
			}
		}
	}

	Component::EcsDataView ProjectileComponent::ensureEcsData()
	{
		ProjectileData* pData = ensureProjectileData();
		return { pData, ProjectileData::StaticType() };
	}

	Component::EcsDataView ProjectileComponent::getEcsData() const
	{
		return { ( getProjectileData() ), ProjectileData::StaticType() };
	}

	ProjectileData* ProjectileComponent::getProjectileData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			return pOwner->getComponent<ProjectileData>().get();
		return nullptr;
	}

	ProjectileData* ProjectileComponent::ensureProjectileData()
	{
		return sw::ensureEcsData<ProjectileData>( getOwner(), getTypeInfo() );
	}
} // namespace sw
