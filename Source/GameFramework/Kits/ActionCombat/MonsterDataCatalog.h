/**
 * @file MonsterDataCatalog.h
 * @brief monsters.xml 에서 읽는 몬스터 스탯 · AI 데이터 카탈로그입니다(ActionCombat 킷).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 몬스터 AI 행동 양식 아키타입입니다. */
    enum class MonsterArchetype : uint8
    {
        MeleePatrol = 0,
        RangedShooter,
        FlyingPursuer,
        ChargerRush
    };

    /** @brief 몬스터 한 종의 데이터 정의입니다(스탯, AI 사거리, 쿨타임, 투사체, 드롭 보상). */
    struct SW_GF_API MonsterDef
    {
        string           _id{};
        string           _name{};
        MonsterArchetype _archetype{ MonsterArchetype::MeleePatrol };
        uint8            _arrReserved[3]{};

        // 기본 스탯
        int32   _hp{ 100 };
        int32   _maxHp{ 100 };
        int32   _atk{ 10 };
        int32   _def{ 0 };
        float32 _speed{ 150.0f };
        float32 _invincibility{ 0.2f };

        // AI 파라미터
        float32 _patrolRange{ 200.0f };
        float32 _detectRange{ 400.0f };
        float32 _attackRange{ 50.0f };
        float32 _attackCoolTime{ 1.5f };
        string  _projectilePrefab{};

        // 애셋 경로
        string _prefabPath{};

        /**
         * @brief 처치 보상입니다(보상 이름 → 수량).
         * @details 예전에는 `_dropExp` / `_dropGold` 두 칸이었습니다. 액션 게임의 보상이 경험치와 금화
         *          둘뿐이라고 정해 둔 셈이라, 소울 · 탄약 · 파편을 주는 게임은 이 킷을 못 썼습니다.
         *          `<Drop exp="10" gold="5" souls="3"/>` 처럼 **속성 이름이 곧 보상 이름**입니다.
         *          같은 프레임워크의 RuntimeHud 가 게이지를 이름 맵으로 다루는 것과 같은 방식입니다.
         */
        unordered_map<hashed_string, int32> _mapDrop{};

        /** @brief 보상 수량을 찾습니다. 없으면 fallback 입니다. */
        int32 getDrop( const hashed_string& rewardId, int32 fallback = 0 ) const
        {
            const auto mapIter = _mapDrop.find( rewardId );
            return mapIter != _mapDrop.end() ? mapIter->second : fallback;
        }
    };

    /** @brief monsters.xml 몬스터 데이터 카탈로그 서비스입니다. */
    class SW_GF_API MonsterDataCatalog
    {
    public:
        MonsterDataCatalog();
        ~MonsterDataCatalog();

        MonsterDataCatalog( const MonsterDataCatalog& )            = delete;
        MonsterDataCatalog& operator=( const MonsterDataCatalog& ) = delete;

        /**
         * @brief XML 리소스 경로에서 몬스터 정의 테이블을 로드합니다.
         * @return 하나라도 읽었으면 true. 그 밖에는 **최소 폴백을 심고** false 입니다.
         * @details 실패는 셋이고 셋 다 같게 다룹니다. 파일이 없다, 루트가 `<MonsterCatalog>` 가
         *          아니다, **읽었는데 `<Monster>` 가 하나도 없다.** 마지막 것이 한동안 성공으로
         *          취급돼서, 태그 철자를 틀리면 텅 빈 카탈로그가 조용히 만들어졌습니다.
         */
        bool loadFromResource( string_view assetRelativePath );

        /** @brief 몬스터 ID 로 정의를 조회합니다. */
        const MonsterDef* findMonster( const hashed_string& id ) const;

        /** @brief 전체 몬스터 테이블을 반환합니다. */
        const unordered_map<hashed_string, MonsterDef>& getAllMonsters() const;

        /** @brief 카탈로그를 비웁니다. */
        void clear();

    private:
        void seedFallback();

        static MonsterArchetype parseArchetype( const utf8* pStr );

        unordered_map<hashed_string, MonsterDef> _mapMonster;
    };
} // namespace sw
