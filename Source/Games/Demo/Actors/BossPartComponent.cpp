#include "pch.h"

#include "Games/Demo/Actors/BossPartComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void BossPartComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "BossPart"_tag );
		_attackTimer = 0.0f;
	}

	void BossPartComponent::onEndPlay()
	{
	}

	void BossPartComponent::onTick( float32 deltaTime )
	{
		if ( _bIsActive == false )
			return;

		_attackTimer += deltaTime;
		if ( _attackTimer >= _attackInterval )
			_attackTimer = 0.0f;

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
		{
			GameObject* pParent = pOwner->getParent();
			if ( pParent != nullptr )
			{
				SceneComponent* pParentSceneComp = pParent->getPrimarySceneComponent();
				if ( pParentSceneComp != nullptr )
				{
					SceneComponent* pMySceneComp = pOwner->getPrimarySceneComponent();
					if ( pMySceneComp != nullptr )
					{
						const float3 parentWorldPos = pParentSceneComp->getWorldPosition();
						float3		 myLocalPos		= pMySceneComp->getLocalPosition();
						myLocalPos._x				= parentWorldPos._x + _offsetFromBoss._x;
						myLocalPos._y				= parentWorldPos._y + _offsetFromBoss._y;
						pMySceneComp->setLocalPosition( myLocalPos );
					}
				}
			}
		}
	}
} // namespace sw
