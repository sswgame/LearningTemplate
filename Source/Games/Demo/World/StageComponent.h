#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class StageComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		int32 totalMonsters;

		PROPERTY()
		int32 remainingMonsters;

		PROPERTY()
		bool bStageCleared;

		StageComponent();
		virtual ~StageComponent() override					   = default;
		StageComponent( StageComponent&& ) noexcept			   = default;
		StageComponent& operator=( StageComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void monsterKilled();
	};
} // namespace sw
