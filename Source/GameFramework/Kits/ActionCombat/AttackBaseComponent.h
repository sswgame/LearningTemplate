#pragma once
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	REFLECT()
	struct SW_GF_API AttackBaseData
	{
		REFLECT_BODY();
		PROPERTY()
		int32 damage{ 0 };
		PROPERTY()
		float32 duration{ 0.0f };
		PROPERTY()
		float32 currentDuration{ 0.0f };
		PROPERTY()
		bool bActive{ false };
	};

	REFLECT()
	class SW_GF_API AttackBaseComponent : public Component
	{
	public:
		REFLECT_BODY();
		AttackBaseComponent()											 = default;
		virtual ~AttackBaseComponent() override							 = default;
		AttackBaseComponent( AttackBaseComponent&& ) noexcept			 = default;
		AttackBaseComponent& operator=( AttackBaseComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		AttackBaseData* getAttackData() const;
		AttackBaseData* ensureAttackData();
	};
} // namespace sw
