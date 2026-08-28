#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class DashUIComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		int32 availableDashes;

		PROPERTY()
		float32 rechargeRatio;

		DashUIComponent();
		virtual ~DashUIComponent() override						 = default;
		DashUIComponent( DashUIComponent&& ) noexcept			 = default;
		DashUIComponent& operator=( DashUIComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;
	};
} // namespace sw
