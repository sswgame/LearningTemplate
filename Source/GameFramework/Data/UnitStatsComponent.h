#pragma once
#include "GameFramework/GameFrameworkExports.h"

#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	struct SW_GF_API UnitStatsData
	{
		REFLECT_BODY();
		PROPERTY()
		int32 hp{ 0 };
		PROPERTY()
		int32 maxHp{ 0 };
		PROPERTY()
		int32 attack{ 0 };
		PROPERTY()
		int32 defense{ 0 };
		PROPERTY()
		float32 moveSpeed{ 0.0f };
		PROPERTY()
		float32 invincibilityTime{ 0.0f };
		PROPERTY()
		float32 maxInvincibilityTime{ 0.0f };
		PROPERTY()
		bool bIsDead{ false };
	};

	REFLECT()
	class SW_GF_API UnitStatsComponent : public Component
	{
	public:
		REFLECT_BODY();
		UnitStatsComponent()										   = default;
		virtual ~UnitStatsComponent() override						   = default;
		UnitStatsComponent( UnitStatsComponent&& ) noexcept			   = default;
		UnitStatsComponent& operator=( UnitStatsComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		UnitStatsData* getStatsData() const;
		UnitStatsData* ensureStatsData();

		void takeDamage( int32 amount );
		void heal( int32 amount );

	private:
		void applyTakeDamage( int32 amount );
		void applyHeal( int32 amount );
	};
} // namespace sw
