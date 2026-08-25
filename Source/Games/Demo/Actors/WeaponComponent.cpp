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
		currentSkillCoolTime = 0.0f;
		bAttacking			 = false;
	}

	void WeaponComponent::onEndPlay()
	{
	}

	void WeaponComponent::onTick( float32 deltaTime )
	{
		if ( currentSkillCoolTime > 0.0f )
		{
			currentSkillCoolTime -= deltaTime;
			if ( currentSkillCoolTime < 0.0f )
				currentSkillCoolTime = 0.0f;
		}
	}

	bool WeaponComponent::trySkill()
	{
		if ( currentSkillCoolTime <= 0.0f )
		{
			currentSkillCoolTime = skillCoolTime;
			return true;
		}
		return false;
	}
} // namespace sw
