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

	REFLECT()
	class MonsterComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		string _monsterId;

		PROPERTY()
		MonsterArchetype _archetype;

		PROPERTY()
		float32 _patrolRange;

		PROPERTY()
		float32 _detectRange;

		PROPERTY()
		float32 _attackRange;

		PROPERTY()
		float32 _moveSpeed;

		PROPERTY()
		float32 _attackCoolTime;

		PROPERTY()
		int32 _moveDir;

		PROPERTY()
		float32 _stateTimer;

		PROPERTY()
		MonsterAiState _aiState;

		PROPERTY()
		string _projectilePrefab;

		PROPERTY()
		float32 _startX;

		PROPERTY()
		float32 _attackTimer;

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
