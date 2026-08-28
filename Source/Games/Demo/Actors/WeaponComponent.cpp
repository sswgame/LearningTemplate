#include "pch.h"

#include "Games/Demo/Actors/WeaponComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void WeaponComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Weapon"_tag );
		_currentSkillCoolTime = 0.0f;
		_bAttacking			  = false;
	}

	void WeaponComponent::onEndPlay()
	{
	}

	void WeaponComponent::onTick( float32 deltaTime )
	{
		if ( _currentSkillCoolTime > 0.0f )
		{
			_currentSkillCoolTime -= deltaTime;
			if ( _currentSkillCoolTime < 0.0f )
				_currentSkillCoolTime = 0.0f;
		}
	}

	bool WeaponComponent::trySkill()
	{
		if ( _currentSkillCoolTime <= 0.0f )
		{
			_currentSkillCoolTime = _skillCoolTime;
			return true;
		}
		return false;
	}
} // namespace sw
