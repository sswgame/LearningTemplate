/**
 * @file MetroidvaniaCatalog.h
 * @brief 메트로배니아와 2D 소울라이크의 데이터입니다. 능력(몸 설정 바꾸기), 부적(슬롯 비용과 능력치), 지역 지도(가격), 세이브와 빠른 이동 지점, 줍는 것,
 *        적(통화, 전리품, 보스 플래그), 규칙(회복 물약, 스태미나, 체력, 패리 창, 반격 배율)을 담습니다.
 * @details 할로우 나이트, 블라스퍼머스 2, 더 라스트 페이스, 어스블레이드, 게슈탈트가 같은 테이블을 쓴다. 방, 연결, 잠금은 기반 `AreaGraph` 의 XML 이 가지고,
 *          여기에서는 그 위에 얹는 장르 규칙만 읽는다. 정의 구조체는 리플렉션 스키마다 — 기본값은 초기값, 범위는 `PROPERTY( Min · Max )`, 필수는 `Required` 이고
 *          `GameDataXML::readFields` 가 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Combat/Health/ResourceGauge.h"
#include "GameFramework/Base/Character/Combat/Health/Vitality.h"
#include "GameFramework/Base/Character/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/StatBlock.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /**
     * @brief 능력 하나입니다(대시, 2단 점프, 벽 점프, 갈고리 등).
     * @details `_motor` 는 기반 `PlatformerSettings` 를 바꾸는 값이다. 이름은 필드 이름에서 `_` 를 뺀 것이고(`extraJumpCount`, `airDashCount`, `runSpeed` 등),
     *          수치는 더하고, 켜고 끄는 항목(`wallJump`, `dash`)은 0이 아니면 켠다. 능력을 얻으면 `_flag` 가 `GameFlags` 에 켜지고, `AreaGraph` 잠금 조건식이 그 플래그를 읽는다.
     */
    REFLECT()
    struct MetroAbilityDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Required )
        hashed_string _flag{}; ///< 얻으면 켜는 플래그(다른 키트의 같은 id 와 겹치지 않게 `ability.<id>` 처럼 적는다)
        StatBlock     _motor{};
    };
} // namespace sw

namespace sw
{
    /** @brief 부적(문양) 하나입니다. 낄 때 슬롯을 `_cost` 만큼 씁니다. */
    REFLECT()
    struct MetroCharmDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string    _name{};
        StatBlock _stats{}; ///< 끼고 있는 동안 더하는 능력치(이름은 게임이 정한다)
        PROPERTY( Min = 0 )
        int32 _cost{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지역 지도 하나입니다(지도 상인에게 삽니다). `_id` 는 `AreaDef::_region` 입니다. */
    REFLECT()
    struct MetroRegionMapDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "region", Required )
        hashed_string _id{};
        PROPERTY( Min = 0 )
        int32 _price{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지도에 표시되는 지점입니다(벤치, 기도대, 사슴 정거장). */
    REFLECT()
    struct MetroSiteDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _area{};
        PROPERTY()
        bool _bRest{ false }; ///< 쉬는 곳이다. 세이브, 회복, 물약 충전, 적 부활이 일어나고 되살아날 위치가 된다
        PROPERTY()
        bool _bFastTravel{ false }; ///< 빠른 이동 정거장이다(한 번 열어야 쓸 수 있다)
    };
} // namespace sw

namespace sw
{
    /** @brief 방에 놓인 줍는 것입니다(가면 조각, 능력, 부적). 지도 표시와 수집률이 이것을 셉니다. */
    REFLECT()
    struct MetroPickupDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _area{};
        PROPERTY()
        hashed_string _ability{}; ///< 주우면 얻는 능력(비어 있으면 없음, 있으면 앞에 적은 `<Ability>` 여야 한다)
        PROPERTY()
        hashed_string _charm{}; ///< 주우면 얻는 부적(비어 있으면 없음, 있으면 앞에 적은 `<Charm>` 이어야 한다)
    };
} // namespace sw

namespace sw
{
    /** @brief 적 한 종류입니다. */
    REFLECT()
    struct MetroEnemyDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "loot" )
        hashed_string _lootTable{}; ///< 기반 `LootCatalog` 의 테이블(비어 있으면 없음)
        PROPERTY()
        hashed_string _flag{}; ///< 보스를 쓰러뜨리면 켜는 플래그(보스는 반드시 적는다)
        PROPERTY( Min = 0 )
        int32 _currency{ 0 };
        PROPERTY()
        bool _bBoss{ false }; ///< 보스는 쉬어도 다시 나오지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief `<Health>` 의 스키마입니다. 읽은 값은 `MetroRules::_health`(기반 `VitalitySettings`)로 옮깁니다. */
    REFLECT()
    struct MetroHealthDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "max", Min = 1.0 )
        float32 _maxHealth{ 100.0f };
        PROPERTY( Name = "poise", Min = 0.0 )
        float32 _poiseMax{ 0.0f }; ///< 0 이면 경직 없음
        PROPERTY( Min = 0.0 )
        float32 _poiseRegenDelay{ 2.0f };
        PROPERTY( Min = 0.0 )
        float32 _poiseRegenRate{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _poiseBreakDuration{ 1.0f };
        PROPERTY( Name = "regenRate", Min = 0.0 )
        float32 _healthRegenRate{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief `<Stamina>` 의 스키마입니다. 읽은 값은 `MetroRules::_stamina`(기반 `ResourceGaugeSettings`)로 옮깁니다. */
    REFLECT()
    struct MetroStaminaDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 1.0 )
        float32 _max{ 100.0f };
        PROPERTY( Min = 0.0 )
        float32 _regenRate{ 20.0f };
        PROPERTY( Min = 0.0 )
        float32 _regenDelay{ 1.0f };
        PROPERTY( Min = 0.0, Max = "_max" )
        float32 _exhaustThreshold{ 0.0f }; ///< 스태미나 최대 이하(기반 `ResourceGauge` 가 같은 범위로 거절한다)
    };
} // namespace sw

namespace sw
{
    /** @brief 장르 규칙 수치입니다. 스칼라 칸은 `<Rules>` 의 스키마이고, 통화는 루트, 체력 · 스태미나 · 패리 창은 각자의 원소에서 읽습니다. */
    REFLECT()
    struct MetroRules
    {
        REFLECT_BODY();

        hashed_string         _currency{ "geo" }; ///< 죽으면 떨어뜨리는 통화의 이름(표시용, 루트 `currency`)
        VitalitySettings      _health{};          ///< 체력과 강인도(`poise`)
        ResourceGaugeSettings _stamina{};
        TimingJudge           _parryJudge{}; ///< 패리 창이다. 목표 시각은 공격이 닿는 순간이다(`<Parry>` 에 창을 하나 이상 적는다)
        PROPERTY( Min = 0.0 )
        float32 _flaskHeal{ 40.0f }; ///< 물약 한 번에 차는 체력
        PROPERTY( Min = 0.0 )
        float32 _flaskHealPerUpgrade{ 10.0f }; ///< 물약을 강화할 때마다 더해지는 회복량
        PROPERTY( Min = 1.0 )
        float32 _riposteMultiplier{ 3.0f }; ///< 패리에 성공했거나 강인도가 무너진 상대에게 주는 피해 배율
        PROPERTY( Min = 0.0 )
        float32 _riposteTime{ 1.0f }; ///< 패리에 성공한 뒤 반격 배율이 유지되는 시간
        PROPERTY( Name = "attackStamina", Min = 0.0 )
        float32 _attackStaminaCost{ 15.0f }; ///< 공격 한 번의 스태미나 비용
        PROPERTY( Name = "dodgeStamina", Min = 0.0 )
        float32 _dodgeStaminaCost{ 20.0f }; ///< 구르기와 회피의 스태미나 비용
        PROPERTY( Min = 0.0 )
        float32 _guardStaminaPerDamage{ 1.0f }; ///< 막을 때 피해 1마다 쓰는 스태미나
        PROPERTY( Name = "guardChip", Min = 0.0, Max = 1.0 )
        float32 _guardChipRatio{ 0.1f }; ///< 막아도 들어오는 체력 피해 비율
        PROPERTY( Name = "corpseRadius", Min = 0.0 )
        float32 _corpseRecoverRadius{ 1.5f }; ///< 이 거리 안에서 시체를 되찾을 수 있다
        PROPERTY( Name = "overcharmDamageScale", Min = 1.0 )
        float32 _overcharmDamageTakenScale{ 2.0f }; ///< 슬롯을 넘겨 꼈을 때 받는 피해 배율
        PROPERTY( Min = 0 )
        int32 _flaskCharges{ 2 }; ///< 처음 물약 충전 수
        PROPERTY( Min = "_flaskCharges" )
        int32 _flaskMaxCharges{ 5 }; ///< 충전 수 업그레이드의 상한
        PROPERTY( Min = 0 )
        int32 _charmNotches{ 3 }; ///< 처음 부적 슬롯 수
        PROPERTY( Name = "overcharm" )
        bool _bAllowOvercharm{ true }; ///< 빈 슬롯이 하나라도 있으면 넘겨 낄 수 있다(할로우 나이트)
    };
} // namespace sw

namespace sw
{
    /** @brief 루트 `<Metroidvania>` 의 속성입니다. */
    REFLECT()
    struct MetroCatalogRootDef
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _currency{ "geo" };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MetroidvaniaCatalog
     * @brief 다음 형식의 XML 을 읽는 카탈로그입니다. `<Metroidvania currency="geo"><Rules flaskCharges="2" .../><Health max="100" poise="30"/><Stamina max="100" regenRate="40"/>
     *        <Parry><Window grade="Perfect" early="0.06" late="0.02"/></Parry><Ability id="dash" name="Dash" flag="ability.dash"><Motor dash="1" airDashCount="1"/></Ability>
     *        <Charm id="strength" name="Strength" cost="3"><Stats damage="0.5"/></Charm><Map region="crossroads" price="30"/><Site id="bench1" area="dirtmouth" rest="true"/>
     *        <Pickup id="mask1" area="hall"/><Enemy id="husk" currency="5" loot="husk"/><Enemy id="hornet" boss="true" flag="boss.hornet" currency="300"/></Metroidvania>`
     * @details 규칙(루트 · Rules · Health · Stamina · Parry)에 오류가 있거나 패리 창이 없으면 읽기 실패다. 스키마를 어긴 원소는 넣지 않고 오류로 남긴다.
     */
    class SW_KIT_API MetroidvaniaCatalog : public XMLCatalog<MetroidvaniaCatalog>
    {
        friend class XMLCatalog<MetroidvaniaCatalog>;

    public:
        MetroidvaniaCatalog();

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
        static constexpr const utf8* kXMLRootName = "Metroidvania"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           loadRules( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           loadAbility( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadCharm( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadPickup( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadEnemy( const XMLNode& node, string_view sourceName );

        GameCatalog<MetroAbilityDef>   _abilityCatalog;
        GameCatalog<MetroCharmDef>     _charmCatalog;
        GameCatalog<MetroRegionMapDef> _mapCatalog;
        GameCatalog<MetroSiteDef>      _siteCatalog;
        GameCatalog<MetroPickupDef>    _pickupCatalog;
        GameCatalog<MetroEnemyDef>     _enemyCatalog;
        MetroRules                     _rules;
    };
} // namespace sw
