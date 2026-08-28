#include "pch.h"

#include "GameFramework/Kits/ActionCombat/GravityComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	GravityComponent::GravityComponent()
		: _gravity{ 0.0f }
		, _velocityY{ 0.0f }
		, _groundY{ 0.0f }
		, _bIsGrounded{ false }
	{
	}

	void GravityComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Physics"_tag );
	}

	void GravityComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void GravityComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		float3 pos = pSceneComp->getLocalPosition();
		if ( _bIsGrounded == false )
		{
			_velocityY += _gravity * deltaTime;
			pos._y += _velocityY * deltaTime;
			if ( pos._y <= _groundY )
			{
				pos._y		 = _groundY;
				_velocityY	 = 0.0f;
				_bIsGrounded = true;
			}
		}
		pSceneComp->setLocalPosition( pos );
	}
} // namespace sw
