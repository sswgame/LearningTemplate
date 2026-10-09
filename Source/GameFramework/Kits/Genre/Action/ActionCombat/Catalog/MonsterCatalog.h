/**
 * @file MonsterCatalog.h
 * @brief monsters.xml 에서 읽는 몬스터 스탯 · AI · 사격 데이터 카탈로그입니다(ActionCombat 킷).
 * @details XML 모양:
 * @code
 *     <MonsterCatalog>
 *       <Monster id="boss" name="Boss" archetype="RangedShooter">
 *         <Stats hp="220" maxHp="220" atk="12" def="16" speed="0.9" radius="0.7" invincibility="0"/>
 *         <AI patrolRange="4" detectRange="8" attackRange="1" coolTime="1.6" firstDelay="1.2" projectilePrefab=""/>
 *         <Shot angle="0" speed="4.5" life="2.5" radius="0.22" damage="10"/>
 *         <Shot angle="90" speed="3.2" life="1.8" radius="0.22" damage="10"/>
 *         <Prefab path=""/>
 *         <Drop exp="10" gold="5"/>
 *       </Monster>
 *     </MonsterCatalog>
 * @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 몬스터 AI 행동 양식 아키타입입니다. monsters.xml 의 `archetype` 속성이 열거자 이름 그대로입니다(리플렉션 이름표). */
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
     * @brief 몬스터가 한 번 쏠 때 나가는 탄 하나입니다. 한 번에 여러 발이면 `MonsterDef::_listShot` 에 여럿을 적습니다(겨냥 한 발 + 옆으로 한 발 같은 패턴).
     * @details monsters.xml 의 `<Shot angle="90" speed="3.2" life="1.8" radius="0.22" damage="10"/>` 한 줄입니다. 단위는 도 · m/s · s · m 입니다.
     */
    struct MonsterShotDef
    {
        float32 _angleDegrees{ 0.0f }; ///< 겨냥 방향에서 돌린 각(도) — 0 이 겨냥, 90 은 겨냥 (x, y) 를 (−y, x) 로 돌린 쪽
        float32 _speed{ 4.5f };        ///< 탄속(m/s)
        float32 _lifeTime{ 2.5f };     ///< 수명(s)
        float32 _radius{ 0.22f };      ///< 맞음 반지름(m)
        int32   _damage{ 10 };         ///< 맞은 쪽에 주는 피해(맞은 쪽의 방어를 빼기 전)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 몬스터 한 종의 데이터 정의입니다(스탯, AI 사거리, 쿨타임, 사격, 드롭 보상).
     * @details 단위는 월드 단위다 — 거리 m, 속도 m/s, 시간 s. 스탯은 `UnitStatsComponent::setStats( const MonsterDef& )` 가 1:1 로 옮기고,
     *          액션 룸(`ActionRoom`)은 HP · 방어 · 속도 · 반지름 · 닿은 피해(`_atk`) · 사격(`_attackCoolTime` · `_firstAttackDelay` · `_listShot`)을 읽는다.
     */
    struct SW_GF_API MonsterDef
    {
        /** @brief 이동 속도의 상한(m/s)입니다. `UnitStatsComponent` 의 Move Speed 상한과 같고, 넘는 값은 로드가 경고합니다(픽셀 단위로 적은 값). */
        static constexpr float32 kMaxSpeed = 50.0f;

        string           _id{};
        string           _name{};
        MonsterArchetype _archetype{ MonsterArchetype::MeleePatrol };
        uint8            _arrReserved[3]{};

        // 기본 스탯
        int32   _hp{ 100 };
        int32   _maxHp{ 100 };
        int32   _atk{ 10 };             ///< 공격력 — 액션 룸에서는 닿은 플레이어에게 주는 피해
        int32   _def{ 0 };              ///< 고정 방어(`DamageMath::applyArmor`)
        float32 _speed{ 3.0f };         ///< 이동 속도(m/s)
        float32 _invincibility{ 0.2f }; ///< 피격 뒤 무적 시간(s)
        float32 _radius{ 0.35f };       ///< 몸 반지름(m) — 액션 룸의 맞음 상자

        // AI 파라미터
        float32                _patrolRange{ 4.0f };      ///< 순찰 반경(m)
        float32                _detectRange{ 8.0f };      ///< 감지 거리(m)
        float32                _attackRange{ 1.0f };      ///< 공격 거리(m)
        float32                _attackCoolTime{ 1.5f };   ///< 공격 간격(s)
        float32                _firstAttackDelay{ 0.0f }; ///< 나타나서 첫 공격까지(s)
        string                 _projectilePrefab{};
        vector<MonsterShotDef> _listShot{}; ///< 한 번 쏠 때 나가는 탄들 — 비면 쏘지 않는다

        // 애셋 경로
        string _prefabPath{};

        /**
         * @brief 처치 보상입니다(보상 이름 → 수량).
         * @details 보상 종류를 코드가 정하지 않습니다 — 소울 · 탄약 · 파편을 주는 게임도 이 킷을 씁니다.
         *          `<Drop exp="10" gold="5" souls="3"/>` 처럼 **속성 이름이 곧 보상 이름**입니다.
         */
        unordered_map<hashed_string, int32> _mapDrop{};

        /** @brief 보상 수량을 찾습니다. 없으면 fallback 입니다. */
        int32 getDrop( const hashed_string& rewardId, int32 fallback = 0 ) const
        {
            const auto mapIter = _mapDrop.find( rewardId );
            return mapIter != _mapDrop.end() ? mapIter->second : fallback;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief monsters.xml 몬스터 데이터 카탈로그 서비스입니다. */
    class SW_GF_API MonsterCatalog : public XmlCatalog<MonsterCatalog>
    {
        friend class XmlCatalog<MonsterCatalog>;

    public:
        MonsterCatalog();
        ~MonsterCatalog();

        MonsterCatalog( const MonsterCatalog& )            = delete;
        MonsterCatalog& operator=( const MonsterCatalog& ) = delete;

        /**
         * @brief XML 에서 몬스터 정의 테이블을 로드합니다. 에셋 상대 경로 또는 실제 파일 경로입니다(`XmlDocument::loadPath`).
         * @return 하나라도 읽었으면 true. 그 밖에는 **최소 폴백을 심고** false 입니다.
         * @details 실패는 셋이고 셋 다 같게 다룹니다. 파일이 없다, 루트가 `<MonsterCatalog>` 가
         *          아니다, **읽었는데 `<Monster>` 가 하나도 없다.** 마지막 것을 성공으로 취급하면
         *          태그 철자를 틀렸을 때 텅 빈 카탈로그가 조용히 만들어진다. 폴백 없이 읽는 판은 `XmlCatalog` 의 `loadFromXmlText` 입니다.
         */
        [[nodiscard]] bool loadFromResource( string_view assetRelativePath );

        /** @brief 몬스터 ID 로 정의를 조회합니다. */
        const MonsterDef* findMonster( const hashed_string& id ) const;

        /** @brief 전체 몬스터 테이블을 반환합니다. */
        const unordered_map<hashed_string, MonsterDef>& getAllMonsters() const;

        /** @brief 카탈로그를 비웁니다. */
        void clear();

    private:
        static constexpr const utf8* kXmlRootName = "MonsterCatalog"; ///< 루트 원소(`XmlCatalog`)

        void seedFallback();
        /** @brief `<MonsterCatalog>` 루트의 `<Monster>` 들을 읽습니다(`XmlCatalog`). 읽은 수입니다(0 이면 실패). */
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        /**
         * @brief `archetype` 속성을 열거자로 읽습니다. 속성이 없으면 MeleePatrol 이고, 모르는 이름이면 경고하고 MeleePatrol 입니다.
         * @param pMonsterId 경고에 적을 몬스터 id 입니다.
         */
        static MonsterArchetype parseArchetype( const utf8* pStr, const utf8* pMonsterId );

        unordered_map<hashed_string, MonsterDef> _mapMonster;
    };
} // namespace sw
