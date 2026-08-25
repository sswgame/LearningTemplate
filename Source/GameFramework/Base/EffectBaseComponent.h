#pragma once
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	REFLECT()
	struct SW_GF_API EffectBaseData
	{
		REFLECT_BODY();
		PROPERTY()
		float32 duration{ 0.0f };
		PROPERTY()
		float32 currentTimer{ 0.0f };
		PROPERTY()
		float32 currentAlpha{ 0.0f };
	};

	REFLECT()
	class SW_GF_API EffectBaseComponent : public Component
	{
	public:
		REFLECT_BODY();
		EffectBaseComponent()											 = default;
		virtual ~EffectBaseComponent() override							 = default;
		EffectBaseComponent( EffectBaseComponent&& ) noexcept			 = default;
		EffectBaseComponent& operator=( EffectBaseComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		EffectBaseData* getEffectData() const;
		EffectBaseData* ensureEffectData();
	};
} // namespace sw
