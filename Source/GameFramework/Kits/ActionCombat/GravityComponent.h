#pragma once
#include "GameFramework/GameFrameworkExports.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	struct SW_GF_API GravityData
	{
		REFLECT_BODY();
		PROPERTY()
		float32 gravity{ 0.0f };
		PROPERTY()
		float32 velocityY{ 0.0f };
		PROPERTY()
		float32 groundY{ 0.0f };
		PROPERTY()
		bool bIsGrounded{ false };
	};

	REFLECT()
	class SW_GF_API GravityComponent : public Component
	{
	public:
		REFLECT_BODY();
		GravityComponent()										   = default;
		virtual ~GravityComponent() override					   = default;
		GravityComponent( GravityComponent&& ) noexcept			   = default;
		GravityComponent& operator=( GravityComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		GravityData* getGravityData() const;
		GravityData* ensureGravityData();
	};
} // namespace sw
