#pragma once
#include "Engine/Object/Component/Component.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	REFLECT()
	struct SW_GF_API HPBarBaseData
	{
		REFLECT_BODY();
		PROPERTY()
		float32 hpRatio{ 0.0f };
		PROPERTY()
		float32 remainRatio{ 0.0f };
		PROPERTY()
		float32 targetRatio{ 0.0f };
		PROPERTY()
		float32 lerpSpeed{ 0.0f };
		PROPERTY()
		float2 offsetPos{ 0.0f, 0.0f };
		PROPERTY()
		bool bVisible{ false };
	};

	REFLECT()
	class SW_GF_API HPBarBaseComponent : public Component
	{
	public:
		REFLECT_BODY();
		HPBarBaseComponent()										   = default;
		virtual ~HPBarBaseComponent() override						   = default;
		HPBarBaseComponent( HPBarBaseComponent&& ) noexcept			   = default;
		HPBarBaseComponent& operator=( HPBarBaseComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		HPBarBaseData* getHPBarData() const;
		HPBarBaseData* ensureHPBarData();
	};
} // namespace sw
