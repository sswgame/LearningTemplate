/**
 * @file DemoGameState.h
 * @brief DemoGame 전용 리플렉션 게임플레이/스냅샷 상태 구조체
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

namespace sw
{
	/** @brief DemoGame 런타임 게임플레이 스냅샷 및 직렬화 상태 */
	REFLECT()
	struct DemoGameState
	{
		REFLECT_BODY();

		PROPERTY()
		uint8 _bTitleHandedOff{ 0 };

		PROPERTY()
		uint8 _bBattleReturnPending{ 0 };

		PROPERTY()
		int32 _returnPlayerX{ 1 };

		PROPERTY()
		int32 _returnPlayerY{ 1 };

		PROPERTY()
		int32 _spawnX{ 1 };

		PROPERTY()
		int32 _spawnY{ 1 };

		PROPERTY()
		string _currentMapPath{};

		PROPERTY()
		string _returnMapPath{};

		PROPERTY()
		string _returnScenePath{};

		PROPERTY()
		vector<PartyMember> _listParty{};

		PROPERTY()
		map<string, int32> _mapFlag{};
	};
} // namespace sw
