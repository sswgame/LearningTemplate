/**
 * @file MetroidvaniaCatalog.h
 * @brief 메트로배니아 · 2D 소울라이크의 데이터 — 능력(몸 설정 바꾸기) · 부적(슬롯 비용 · 능력치) · 지역 지도(값) · 세이브/빠른 이동 지점 · 줍는 것 ·
 *        적(통화 · 전리품 · 보스 플래그) · 규칙(회복 물약 · 스태미나 · 체력 · 패리 창 · 반격 배율)입니다.
 * @details 할로우 나이트 · 블라스퍼머스 2 · 더 라스트 페이스 · 어스블레이드 · 게슈탈트가 같은 표를 씁니다. 방 · 연결 · 잠금은 기반 `AreaGraph` 의 XML 이고,
 *          여기는 그 위에 얹는 장르 규칙만 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Combat/ResourceGauge.h"
#include "GameFramework/Combat/Vitality.h"
#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Input/TimingJudge.h"

namespace sw
{
    class XmlNode;

    /**
     * @brief 능력 하나입니다(대시 · 2단 점프 · 벽 점프 · 갈고리 …).
     * @details `_motor` 는 기반 `PlatformerSettings` 를 바꾸는 값입니다 — 이름은 필드 이름에서 `_` 를 뺀 것(`extraJumpCount` · `airDashCount` · `runSpeed` …)이고
     *          수치는 더하기, 켜고 끄는 칸(`wallJump` · `dash`)은 0 이 아니면 켭니다. 얻으면 `_flag` 가 `GameFlags` 에 켜져 `AreaGraph` 잠금 조건식이 읽습니다.
     */
    struct MetroAbilityDef
    {
        hashed_string _id{};
        string        _name{};
        hashed_string _flag{}; ///< 얻으면 켜는 플래그(비면 id 그대로)
        StatBlock     _motor{};
    };

    /** @brief 부적 · 문양 하나입니다. 낄 때 슬롯을 `_cost` 만큼 씁니다. */
    struct MetroCharmDef
    {
        hashed_string _id{};
        string        _name{};
        StatBlock     _stats{}; ///< 끼고 있는 동안 더하는 능력치(이름은 게임이 정한다)
        int32         _cost{ 1 };
    };

    /** @brief 지역 지도 하나입니다(지도 상인에게 산다). `_id` 는 `AreaDef::_region` 입니다. */
    struct MetroRegionMapDef
    {
        hashed_string _id{};
        int32         _price{ 0 };
    };

    /** @brief 지도에 찍히는 지점입니다(벤치 · 기도대 · 사슴 정거장). */
    struct MetroSiteDef
    {
        hashed_string _id{};
        hashed_string _area{};
        uint8         _bRest{ SW_FALSE };       ///< 쉬는 곳 — 세이브 · 회복 · 물약 충전 · 적 부활 · 되살아나는 자리
        uint8         _bFastTravel{ SW_FALSE }; ///< 빠른 이동 정거장(한 번 열어야 쓴다)
    };

    /** @brief 방에 놓인 줍는 것입니다(가면 조각 · 능력 · 부적). 지도 표시와 수집률이 셉니다. */
    struct MetroPickupDef
    {
        hashed_string _id{};
        hashed_string _area{};
        hashed_string _ability{}; ///< 주우면 얻는 능력(비면 없음)
        hashed_string _charm{};   ///< 주우면 얻는 부적(비면 없음)
    };

    /** @brief 적 한 종류입니다. */
    struct MetroEnemyDef
    {
        hashed_string _id{};
        hashed_string _lootTable{}; ///< 기반 `LootCatalog` 의 표(비면 없음)
        hashed_string _flag{};      ///< 보스 — 쓰러뜨리면 켜는 플래그(비면 `boss.<id>`)
        int32         _currency{ 0 };
        uint8         _bBoss{ SW_FALSE }; ///< 보스는 쉬어도 다시 나오지 않는다
    };

    /** @brief 장르 규칙 수치입니다. */
    struct MetroRules
    {
        hashed_string         _currency{ "geo" }; ///< 죽으면 떨어뜨리는 통화의 이름(표시용)
        VitalitySettings      _health{};          ///< 체력 · 강인도(`poise`)
        ResourceGaugeSettings _stamina{};
        TimingJudge           _parryJudge{};                      ///< 패리 창 — 목표 시각 = 공격이 닿는 순간
        float32               _flaskHeal{ 40.0f };                ///< 물약 한 번에 차는 체력
        float32               _flaskHealPerUpgrade{ 10.0f };      ///< 물약 강화 한 번마다 더해지는 회복량
        float32               _riposteMultiplier{ 3.0f };         ///< 패리 성공 · 강인도 붕괴 상대에게 주는 피해 배율
        float32               _riposteTime{ 1.0f };               ///< 패리 성공 뒤 반격 배율이 살아 있는 시간
        float32               _attackStaminaCost{ 15.0f };        ///< 공격 한 번
        float32               _dodgeStaminaCost{ 20.0f };         ///< 구르기 · 회피
        float32               _guardStaminaPerDamage{ 1.0f };     ///< 막을 때 피해 1 마다 쓰는 스태미나
        float32               _guardChipRatio{ 0.1f };            ///< 막아도 들어오는 체력 피해 비율
        float32               _corpseRecoverRadius{ 1.5f };       ///< 이 거리 안에서 시체를 되찾는다
        float32               _overcharmDamageTakenScale{ 2.0f }; ///< 슬롯을 넘겨 꼈을 때 받는 피해 배율
        int32                 _flaskCharges{ 2 };                 ///< 처음 물약 충전 수
        int32                 _flaskMaxCharges{ 5 };              ///< 충전 수 업그레이드 상한
        int32                 _charmNotches{ 3 };                 ///< 처음 부적 슬롯
        uint8                 _bAllowOvercharm{ SW_TRUE };        ///< 빈 슬롯이 하나라도 있으면 넘겨 낄 수 있다(할로우 나이트)
    };

    /**
     * @class MetroidvaniaCatalog
     * @brief `<Metroidvania currency="geo"><Rules flaskCharges="2" .../><Health max="100" poise="30"/><Stamina max="100" regenRate="40"/>
     *        <Parry><Window grade="Perfect" early="0.06" late="0.02"/></Parry><Ability id="dash"><Motor dash="1" airDashCount="1"/></Ability>
     *        <Charm id="strength" cost="3"><Stats damage="0.5"/></Charm><Map region="crossroads" price="30"/><Site id="bench1" area="dirtmouth" rest="true"/>
     *        <Pickup id="mask1" area="hall"/><Enemy id="husk" currency="5" loot="husk"/><Enemy id="hornet" boss="true" currency="300"/></Metroidvania>` 를 읽습니다.
     */
    class SW_GF_API MetroidvaniaCatalog
    {
    public:
        MetroidvaniaCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        const MetroAbilityDef*   findAbility( const hashed_string& id ) const { return _abilityCatalog.find( id ); }
        const MetroCharmDef*     findCharm( const hashed_string& id ) const { return _charmCatalog.find( id ); }
        const MetroRegionMapDef* findRegionMap( const hashed_string& region ) const { return _mapCatalog.find( region ); }
        const MetroSiteDef*      findSite( const hashed_string& id ) const { return _siteCatalog.find( id ); }
        const MetroPickupDef*    findPickup( const hashed_string& id ) const { return _pickupCatalog.find( id ); }
        const MetroEnemyDef*     findEnemy( const hashed_string& id ) const { return _enemyCatalog.find( id ); }

        const vector<MetroAbilityDef>&   getAbilities() const { return _abilityCatalog.getAll(); }
        const vector<MetroCharmDef>&     getCharms() const { return _charmCatalog.getAll(); }
        const vector<MetroRegionMapDef>& getRegionMaps() const { return _mapCatalog.getAll(); }
        const vector<MetroSiteDef>&      getSites() const { return _siteCatalog.getAll(); }
        const vector<MetroPickupDef>&    getPickups() const { return _pickupCatalog.getAll(); }
        const MetroRules&                getRules() const { return _rules; }
        MetroRules&                      getRules() { return _rules; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );
        void   loadRules( const XmlNode& root );

        GameCatalog<MetroAbilityDef>   _abilityCatalog;
        GameCatalog<MetroCharmDef>     _charmCatalog;
        GameCatalog<MetroRegionMapDef> _mapCatalog;
        GameCatalog<MetroSiteDef>      _siteCatalog;
        GameCatalog<MetroPickupDef>    _pickupCatalog;
        GameCatalog<MetroEnemyDef>     _enemyCatalog;
        MetroRules                     _rules;
    };
} // namespace sw
