#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Data/MonsterDataCatalog.h"

namespace sw
{
	ENUM()
	enum class MonsterAiState : uint8
	{
		Patrol = 0,
		Chase,
		Attack
	};

	REFLECT_SCRIPT()
	class MonsterComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		string monsterId;

		PROPERTY()
		MonsterArchetype archetype;

		PROPERTY()
		float32 patrolRange;

		PROPERTY()
		float32 detectRange;

		PROPERTY()
		float32 attackRange;

		PROPERTY()
		float32 moveSpeed;

		PROPERTY()
		float32 attackCoolTime;

		PROPERTY()
		int32 moveDir;

		PROPERTY()
		float32 stateTimer;

		PROPERTY()
		MonsterAiState aiState;

		PROPERTY()
		string projectilePrefab;

		PROPERTY()
		float32 startX;

		PROPERTY()
		float32 attackTimer;

		MonsterComponent();
		virtual ~MonsterComponent() override					   = default;
		MonsterComponent( MonsterComponent&& ) noexcept			   = default;
		MonsterComponent& operator=( MonsterComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void updateAI( float32 deltaTime );
	};
} // namespace sw
