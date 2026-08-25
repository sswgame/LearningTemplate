#pragma once
#include "GameFramework/GameFrameworkExports.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	struct SW_GF_API DamageUIData
	{
		REFLECT_BODY();
		PROPERTY()
		int32 damageValue{ 0 };
		PROPERTY()
		float32 lifeTime{ 0.0f };
		PROPERTY()
		float32 currentLife{ 0.0f };
		PROPERTY()
		float32 floatSpeed{ 0.0f };
		PROPERTY()
		float32 alpha{ 0.0f };
	};

	REFLECT()
	class SW_GF_API DamageUIComponent : public Component
	{
	public:
		REFLECT_BODY();
		DamageUIComponent()											 = default;
		virtual ~DamageUIComponent() override						 = default;
		DamageUIComponent( DamageUIComponent&& ) noexcept			 = default;
		DamageUIComponent& operator=( DamageUIComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		DamageUIData* getDamageUIData() const;
		DamageUIData* ensureDamageUIData();
	};
} // namespace sw
