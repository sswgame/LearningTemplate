#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	ENUM()
	enum class BossAiState : uint8
	{
		Idle = 0,
		Spawn,
		Laser,
		BulletHell,
		SwordRain
	};

	REFLECT_SCRIPT()
	class BossComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		int32 phase;

		PROPERTY()
		float32 stateTimer;

		PROPERTY()
		float32 patternCooldown;

		PROPERTY()
		int32 currentPattern;

		PROPERTY()
		BossAiState bossState;

		PROPERTY()
		bool bLaserActive;

		PROPERTY()
		float32 phase1Cooldown;

		PROPERTY()
		float32 phase2Cooldown;

		PROPERTY()
		float32 phase3Cooldown;

		PROPERTY()
		float32 phase2HpRatio;

		PROPERTY()
		float32 phase3HpRatio;

		BossComponent();
		virtual ~BossComponent() override					 = default;
		BossComponent( BossComponent&& ) noexcept			 = default;
		BossComponent& operator=( BossComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void updatePhase();
		void executePattern( float32 deltaTime );
	};
} // namespace sw
