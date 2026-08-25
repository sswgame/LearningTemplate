#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT_SCRIPT()
	class SkelBowComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		float32 aimAngle{ 0.0f };

		PROPERTY()
		float32 chargeAmount{ 0.0f };

		PROPERTY()
		bool bAiming{ false };

		PROPERTY()
		float32 chargeSpeed{ 0.0f };

		PROPERTY()
		float32 arrowSpeed{ 0.0f };

		PROPERTY()
		int32 arrowDamage{ 0 };

		SkelBowComponent()										   = default;
		virtual ~SkelBowComponent() override					   = default;
		SkelBowComponent( SkelBowComponent&& ) noexcept			   = default;
		SkelBowComponent& operator=( SkelBowComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void startAiming();
		void stopAiming();
		void setAimAngle( float32 angle );
		void fire();
	};
} // namespace sw
