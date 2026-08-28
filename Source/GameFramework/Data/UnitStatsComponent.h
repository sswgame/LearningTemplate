#pragma once
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	namespace generated
	{
		struct sw_UnitStatsComponent_Registrar;
	} // namespace generated

	REFLECT()
	class SW_GF_API UnitStatsComponent : public Component
	{
		friend struct ::sw::generated::sw_UnitStatsComponent_Registrar;

	public:
		REFLECT_BODY();
		UnitStatsComponent();
		virtual ~UnitStatsComponent() override						   = default;
		UnitStatsComponent( UnitStatsComponent&& ) noexcept			   = default;
		UnitStatsComponent& operator=( UnitStatsComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void takeDamage( int32 amount );
		void heal( int32 amount );

		int32	getHp() const;
		int32	getMaxHp() const;
		int32	getAttack() const;
		int32	getDefense() const;
		float32 getMoveSpeed() const;
		bool	isDead() const;

		void setStats( int32 hp, int32 maxHp, int32 attack, int32 defense, float32 moveSpeed, float32 maxInvincibilityTime );

	private:
		void applyTakeDamage( int32 amount );
		void applyHeal( int32 amount );

		PROPERTY( Alias="hp" )
		int32 _hp;
		PROPERTY( Alias="maxHp" )
		int32 _maxHp;
		PROPERTY( Alias="attack" )
		int32 _attack;
		PROPERTY( Alias="defense" )
		int32 _defense;
		PROPERTY( Alias="moveSpeed" )
		float32 _moveSpeed;
		PROPERTY( Alias="invincibilityTime" )
		float32 _invincibilityTime;
		PROPERTY( Alias="maxInvincibilityTime" )
		float32 _maxInvincibilityTime;
		PROPERTY( Alias="bIsDead" )
		bool _bIsDead;
	};
} // namespace sw
