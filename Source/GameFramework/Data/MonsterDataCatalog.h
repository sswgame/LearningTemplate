/**
 * @file MonsterDataCatalog.h
 * @brief monsters.xml에서 로드하는 몬스터 스탯 및 AI 데이터 카탈로그
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

namespace sw
{
	/** @brief 몬스터 AI 행동 양식 아키타입 */
	enum class MonsterArchetype : uint8
	{
		MeleePatrol = 0,
		RangedShooter,
		FlyingPursuer,
		ChargerRush
	};

	/** @brief 몬스터 1종 데이터 정의 (스탯, AI 사거리, 쿨타임, 투사체, 드롭 보상) */
	struct SW_GF_API MonsterDef
	{
		string			 _id{};
		string			 _name{};
		MonsterArchetype _archetype{ MonsterArchetype::MeleePatrol };
		uint8			 _arrReserved[3]{};

		// 기본 스탯
		int32	_hp{ 100 };
		int32	_maxHp{ 100 };
		int32	_atk{ 10 };
		int32	_def{ 0 };
		float32 _speed{ 150.0f };
		float32 _invincibility{ 0.2f };

		// AI 파라미터
		float32 _patrolRange{ 200.0f };
		float32 _detectRange{ 400.0f };
		float32 _attackRange{ 50.0f };
		float32 _attackCoolTime{ 1.5f };
		string	_projectilePrefab{};

		// 애셋 경로 및 드롭
		string _prefabPath{};
		int32  _dropExp{ 10 };
		int32  _dropGold{ 5 };
	};

	/** @brief monsters.xml 전역 데이터 카탈로그 */
	class SW_GF_API MonsterDataCatalog
	{
	public:
		/** @brief XML 리소스 경로에서 몬스터 정의 테이블을 로드합니다. */
		static bool loadFromResource( string_view assetRelativePath );

		/** @brief 몬스터 ID로 정의를 조회합니다. */
		static const MonsterDef* findMonster( const hashed_string& id );

		/** @brief 몬스터 ID(문자열)로 정의를 조회합니다. */
		static const MonsterDef* findMonster( const string& id );

		/** @brief 전체 몬스터 테이블을 반환합니다. */
		static const unordered_map<hashed_string, MonsterDef>& getAllMonsters();

		/** @brief 카탈로그를 비웁니다. */
		static void clear();

	private:
		static MonsterArchetype parseArchetype( const utf8* pStr );
	};
} // namespace sw
