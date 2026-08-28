#include "pch.h"

#include "GameFramework/Kits/ActionCombat/AttackBaseComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	AttackBaseComponent::AttackBaseComponent()
		: _damage{ 0 }
		, _duration{ 0.0f }
		, _currentDuration{ 0.0f }
		, _bActive{ false }
	{
	}

	void AttackBaseComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Combat"_tag );

		_currentDuration = 0.0f;
	}

	void AttackBaseComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void AttackBaseComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		if ( _bActive == false )
			return;

		_currentDuration += deltaTime;
		if ( _duration > 0.0f && _currentDuration >= _duration )
		{
			_bActive		 = false;
			_currentDuration = 0.0f;
		}
	}

	bool AttackBaseComponent::isAttackActive() const
	{
		return _bActive;
	}

	int32 AttackBaseComponent::getDamage() const
	{
		return _damage;
	}

	void AttackBaseComponent::beginAttack( int32 damage, float32 duration )
	{
		_damage			 = damage;
		_duration		 = duration;
		_currentDuration = 0.0f;
		_bActive		 = true;
	}
} // namespace sw
