#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class MonsterSummonComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		int32 _summonCount{ 0 };

		PROPERTY()
		float32 _summonRadius{ 0.0f };

		PROPERTY()
		string _monsterPrefab{};

		MonsterSummonComponent()											   = default;
		virtual ~MonsterSummonComponent() override							   = default;
		MonsterSummonComponent( MonsterSummonComponent&& ) noexcept			   = default;
		MonsterSummonComponent& operator=( MonsterSummonComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void spawnMonsters();
	};
} // namespace sw
