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

	REFLECT()
	class BossComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		int32 _phase;

		PROPERTY()
		float32 _stateTimer;

		PROPERTY()
		float32 _patternCooldown;

		PROPERTY()
		int32 _currentPattern;

		PROPERTY()
		BossAiState _bossState;

		PROPERTY()
		bool _bLaserActive;

		PROPERTY()
		float32 _phase1Cooldown;

		PROPERTY()
		float32 _phase2Cooldown;

		PROPERTY()
		float32 _phase3Cooldown;

		PROPERTY()
		float32 _phase2HpRatio;

		PROPERTY()
		float32 _phase3HpRatio;

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
