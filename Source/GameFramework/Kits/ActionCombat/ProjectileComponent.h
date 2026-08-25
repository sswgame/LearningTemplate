#pragma once
#include "GameFramework/GameFrameworkExports.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	struct SW_GF_API ProjectileData
	{
		REFLECT_BODY();
		PROPERTY()
		float2 velocity{ 0.0f, 0.0f };
		PROPERTY()
		int32 damage{ 0 };
		PROPERTY()
		float32 lifeTime{ 0.0f };
		PROPERTY()
		float32 currentLife{ 0.0f };
	};

	REFLECT()
	class SW_GF_API ProjectileComponent : public Component
	{
	public:
		REFLECT_BODY();
		ProjectileComponent()											 = default;
		virtual ~ProjectileComponent() override							 = default;
		ProjectileComponent( ProjectileComponent&& ) noexcept			 = default;
		ProjectileComponent& operator=( ProjectileComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		ProjectileData* getProjectileData() const;
		ProjectileData* ensureProjectileData();
	};
} // namespace sw
