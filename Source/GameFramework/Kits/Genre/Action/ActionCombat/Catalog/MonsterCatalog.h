/**
 * @file MonsterCatalog.h
 * @brief monsters.xml 에서 읽는 몬스터 스탯, AI, 사격 데이터 카탈로그입니다(ActionCombat 킷).
 * @details XML 형식은 다음과 같다.
 * @code
 *     <MonsterCatalog>
 *       <Monster id="boss" name="Boss" archetype="RangedShooter">
 *         <Stats hp="220" maxHP="220" atk="12" def="16" speed="0.9" radius="0.7" invincibility="0"/>
 *         <AI patrolRange="4" detectRange="8" attackRange="1" coolTime="1.6" firstDelay="1.2" projectilePrefab=""/>
 *         <Shot angle="0" speed="4.5" life="2.5" radius="0.22" damage="10"/>
 *         <Shot angle="1.5708" speed="3.2" life="1.8" radius="0.22" damage="10"/>
 *         <Prefab path=""/>
 *         <Drop exp="10" gold="5"/>
 *       </Monster>
 *     </MonsterCatalog>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 몬스터 AI 행동 유형입니다. monsters.xml 의 `archetype` 속성이 열거자 이름 그대로입니다(리플렉션 이름). */
    ENUM()
    enum class MonsterArchetype : uint8
    {
        MeleePatrol = 0,
        RangedShooter,
        FlyingPursuer,
        ChargerRush
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 몬스터가 한 번 쏠 때 나가는 탄 하나입니다. 한 번에 여러 발을 쏘려면 `MonsterDef::_listShot` 에 여러 개를 적습니다(겨냥한 한 발과 옆으로 한 발 같은 패턴).
     * @details monsters.xml 의 `<Shot angle="1.5708" speed="3.2" life="1.8" radius="0.22" damage="10"/>` 한 줄에 해당한다. 단위는 각도(라디안), m/s, s, m 이다.
     */
    REFLECT()
    struct MonsterShotDef
    {
        REFLECT_BODY();

        PROPERTY( Units = "rad" )
        float32 _angle{ 0.0f }; ///< 겨냥 방향에서 돌린 각도(라디안). 0은 겨냥 방향이고, π/2 는 겨냥 방향 (x, y) 를 (−y, x) 로 돌린 쪽이다
        PROPERTY( Min = 0.0 )
        float32 _speed{ 4.5f }; ///< 탄속(m/s)
        PROPERTY( Name = "life", Min = 0.0 )
        float32 _lifeTime{ 2.5f }; ///< 수명(s)
        PROPERTY( Min = 0.0 )
        float32 _radius{ 0.22f }; ///< 맞음 반지름(m)
        PROPERTY( Min = 0 )
        int32 _damage{ 10 }; ///< 맞은 쪽에 주는 피해(맞은 쪽의 방어를 빼기 전 값)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 몬스터 한 종의 기본 스탯입니다(`<Stats>`). `UnitStatsComponent::setStats( const MonsterStatsDef& )` 가 그대로 옮깁니다.
     * @details 단위는 월드 단위다. 거리는 m, 속도는 m/s, 시간은 s 이다. 이동 속도의 상한은 `UnitStatsComponent` 의 Move Speed 상한과 같다 —
     *          넘으면 로드 오류다(픽셀 단위로 적은 값을 잡아낸다).
     */
    REFLECT()
    struct MonsterStatsDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 1 )
        int32 _maxHP{ 100 };
        PROPERTY( Min = 1, Max = "_maxHP" )
        int32 _hp{ 100 }; ///< 처음 HP(범위 필드는 앞에 선언해야 고정 레이아웃 읽기가 먼저 안다)
        PROPERTY( Min = 0 )
        int32 _atk{ 10 }; ///< 공격력. 액션 룸에서는 닿은 플레이어에게 주는 피해다
        PROPERTY( Min = 0 )
        int32 _def{ 0 }; ///< 고정 방어(`DamageMath::applyArmor`)
        PROPERTY( Min = 0.0, Max = 50.0 )
        float32 _speed{ 3.0f }; ///< 이동 속도(m/s)
        PROPERTY( Min = 0.0 )
        float32 _invincibility{ 0.2f }; ///< 피격 뒤 무적 시간(s)
        PROPERTY( Min = 0.01 )
        float32 _radius{ 0.35f }; ///< 몸 반지름(m). 액션 룸의 맞음 상자다
    };
} // namespace sw

namespace sw
{
    /** @brief 몬스터 한 종의 AI 수치입니다(`<AI>`). 거리는 m, 시간은 s 입니다. */
    REFLECT()
    struct MonsterAIDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0 )
        float32 _patrolRange{ 4.0f }; ///< 순찰 반경(m)
        PROPERTY( Min = 0.0 )
        float32 _detectRange{ 8.0f }; ///< 감지 거리(m)
        PROPERTY( Min = 0.0 )
        float32 _attackRange{ 1.0f }; ///< 공격 거리(m)
        PROPERTY( Name = "coolTime", Min = 0.0 )
        float32 _attackCoolTime{ 1.5f }; ///< 공격 간격(s)
        PROPERTY( Name = "firstDelay", Min = 0.0 )
        float32 _firstAttackDelay{ 0.0f }; ///< 나타나서 첫 공격까지의 시간(s)
        PROPERTY()
        string _projectilePrefab{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 몬스터 한 종의 데이터 정의입니다(이름과 행동 유형, 스탯, AI, 사격, 드롭 보상).
     * @details 스탯(`_stats`)과 AI(`_ai`)는 각자의 스키마로 읽는다 — 원소 하나가 스키마 하나다(언리얼 DataTable 행의 하위 구조체 자리).
     *          액션 룸(`ActionRoom`)은 스탯과 사격(`_ai._attackCoolTime`, `_ai._firstAttackDelay`, `_listShot`)을 읽는다.
     */
    REFLECT()
    struct SW_KIT_API MonsterDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY()
        MonsterArchetype _archetype{ MonsterArchetype::MeleePatrol };

        MonsterStatsDef        _stats{};    ///< `<Stats>`
        MonsterAIDef           _ai{};       ///< `<AI>`
        vector<MonsterShotDef> _listShot{}; ///< 한 번 쏠 때 나가는 탄 목록. 비어 있으면 쏘지 않는다

        // 에셋 경로
        string _prefabPath{};

        /**
         * @brief 처치 보상입니다(보상 이름과 수량).
         * @details 보상 종류를 코드가 정하지 않는다. 소울, 탄약, 파편을 주는 게임도 이 킷을 쓴다.
         *          `<Drop exp="10" gold="5" souls="3"/>` 처럼 **속성 이름이 곧 보상 이름**이다.
         */
        unordered_map<hashed_string, int32> _mapDrop{};

        /** @brief 보상 수량을 찾습니다. 적지 않은 보상은 주지 않으므로 0 입니다. */
        int32 getDrop( const hashed_string& rewardID ) const
        {
            const auto mapIter = _mapDrop.find( rewardID );
            return mapIter != _mapDrop.end() ? mapIter->second : 0;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief monsters.xml 몬스터 데이터 카탈로그 서비스입니다. */
    class SW_KIT_API MonsterCatalog : public XMLCatalog<MonsterCatalog>
    {
        friend class XMLCatalog<MonsterCatalog>;

    public:
        MonsterCatalog();
        ~MonsterCatalog();

        MonsterCatalog( const MonsterCatalog& )            = delete;
        MonsterCatalog& operator=( const MonsterCatalog& ) = delete;

        /**
         * @brief XML 에서 몬스터 정의 테이블을 로드합니다. 에셋 상대 경로나 실제 파일 경로를 받습니다(`XMLDocument::loadResource`).
         * @return 하나라도 읽었으면 true 입니다. 하나도 못 읽었으면 오류를 남기고 빈 카탈로그로 false 를 반환합니다(코드 폴백 정의는 없습니다).
         * @details 실패는 세 가지이고 모두 같게 다룬다. 파일이 없는 경우, 루트가 `<MonsterCatalog>` 가 아닌 경우, **읽었는데 `<Monster>` 가 하나도 없는** 경우다.
         *          마지막 경우를 성공으로 취급하면 태그 철자를 틀렸을 때 텅 빈 카탈로그가 조용히 만들어진다.
         */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );

        /** @brief 몬스터 ID 로 정의를 찾습니다. */
        const MonsterDef* findMonster( const hashed_string& id ) const;

        /** @brief 전체 몬스터 테이블을 반환합니다. */
        const unordered_map<hashed_string, MonsterDef>& getAllMonsters() const;

        /** @brief 카탈로그를 비웁니다. */
        void clear();

    private:
        static constexpr const utf8* kXMLRootName = "MonsterCatalog"; ///< 루트 원소(`XMLCatalog`)

        /** @brief `<MonsterCatalog>` 루트의 `<Monster>` 들을 읽고(`XMLCatalog`), 읽은 수를 반환합니다. 0이면 실패입니다. */
        uint32 loadRoot( const XMLNode& root, string_view sourceName );

        /**
         * @brief `<Monster>` 하나를 스키마(`MonsterDef` · `MonsterStatsDef` · `MonsterAIDef` · `MonsterShotDef`)로 읽습니다.
         * @return 오류가 없으면 true 이고 카탈로그에 더합니다. 오류면 더하지 않습니다(모르는 archetype · 범위 밖 속도 포함).
         */
        [[nodiscard]] bool loadMonster( const XMLNode& node, string_view sourceName );

        unordered_map<hashed_string, MonsterDef> _mapMonster;
    };
} // namespace sw
