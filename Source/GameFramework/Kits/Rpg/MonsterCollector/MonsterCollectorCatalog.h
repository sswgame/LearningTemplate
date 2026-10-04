/**
 * @file MonsterCollectorCatalog.h
 * @brief 몬스터 수집(포켓몬 류)의 데이터 — 종(기본 능력치 6 · 타입 1~2 · 배울 기술 · 진화 · 포획률 · 경험치 그룹), 기술(위력 · 명중 · PP · 분류 · 우선도 ·
 *        부가 효과), 성격(±10%), 날씨, 상태이상 면역 타입, 야생 조우 테이블(지역 · 시간대 · 레벨 범위 · 가중치)입니다.
 * @details 타입 상성 자체는 기반 `ElementChart` 가 갖습니다(이 카탈로그는 타입 이름만 적는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;
    class XmlNode;

    /** @brief 능력치 여섯 가지입니다. XML 의 `stats="HP 공격 방어 특공 특방 스피드"` 가 이 순서입니다. */
    enum class MonsterStat : uint8
    {
        Hp = 0,
        Attack,
        Defense,
        SpecialAttack,
        SpecialDefense,
        Speed,
        Count
    };

    /** @brief 능력치 수입니다. */
    static constexpr int32 kMonsterStatCount = static_cast<int32>( MonsterStat::Count );

    /** @brief 경험치 그룹 — 레벨 n 까지의 총 경험치가 빠름 4n³/5 · 보통 n³ · 느림 5n³/4 입니다. */
    enum class MonsterExpGroup : uint8
    {
        Fast = 0,
        Medium,
        Slow
    };

    /** @brief 기술 분류 — 물리는 공격/방어, 특수는 특공/특방, 변화는 피해 없음입니다. */
    enum class MonsterMoveCategory : uint8
    {
        Physical = 0,
        Special,
        Status
    };

    /** @brief 상태이상입니다(한 번에 하나). */
    enum class MonsterStatus : uint8
    {
        None = 0,
        Poison,    ///< 턴 끝마다 최대 HP 1/8
        Toxic,     ///< 턴 끝마다 n/16(n 은 1 부터 매 턴 오른다)
        Burn,      ///< 턴 끝마다 1/16, 물리 피해 절반
        Paralysis, ///< 스피드 절반, 25% 로 행동 불가
        Sleep,     ///< 1~3 턴 행동 불가
        Freeze     ///< 행동 불가, 매 턴 20% 로 녹는다
    };

    /** @brief 능력 변화가 누구에게 걸리는가입니다. */
    enum class MonsterEffectTarget : uint8
    {
        Foe = 0,
        Self
    };

    SW_GF_API const utf8* toString( MonsterStatus status );

    /** @brief 레벨에 배우는 기술 하나입니다. */
    struct MonsterLearnEntry
    {
        hashed_string _moveId{};
        int32         _level{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 진화 갈래 하나입니다. 적힌 조건이 **모두** 맞아야 합니다.
     * @details 아이템 조건이 있으면 그 아이템을 쓸 때만, 없으면 레벨업 때만 봅니다(돌 진화 · 레벨 진화 · 친밀도 진화).
     */
    struct MonsterEvolutionDef
    {
        hashed_string _targetId{};
        hashed_string _itemId{};        ///< 비지 않으면 그 아이템을 썼을 때
        int32         _level{ 0 };      ///< 0 = 레벨 조건 없음
        int32         _friendship{ 0 }; ///< 0 = 친밀도 조건 없음
    };
} // namespace sw

namespace sw
{
    /** @brief 종 하나입니다. */
    struct SW_GF_API MonsterSpeciesDef
    {
        hashed_string               _id{};
        string                      _name{};
        vector<hashed_string>       _listType{};  ///< 1~2 개
        vector<MonsterLearnEntry>   _listLearn{}; ///< 레벨 오름차순
        vector<MonsterEvolutionDef> _listEvolution{};
        int32                       _arrBaseStat[kMonsterStatCount]{ 50, 50, 50, 50, 50, 50 };
        int32                       _arrEvYield[kMonsterStatCount]{ 0, 0, 0, 0, 0, 0 }; ///< 쓰러뜨린 쪽이 받는 노력치
        int32                       _catchRate{ 45 };                                   ///< 1..255
        int32                       _baseExp{ 64 };                                     ///< 경험치 공식의 b
        MonsterExpGroup             _expGroup{ MonsterExpGroup::Medium };

        bool hasType( const hashed_string& type ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 기술 하나입니다. */
    struct MonsterMoveDef
    {
        hashed_string       _id{};
        string              _name{};
        hashed_string       _type{};
        hashed_string       _weatherId{}; ///< 비지 않으면 날씨를 부른다(비바라기)
        int32               _power{ 0 };
        int32               _accuracy{ 100 }; ///< 0 = 반드시 맞는다
        int32               _pp{ 10 };
        int32               _priority{ 0 };
        int32               _statusChance{ 0 }; ///< 상태이상 확률(%) — 변화 기술은 100
        int32               _statStages{ 0 };   ///< 능력 변화 단계(−6..+6, 0 = 없음)
        int32               _statChance{ 100 }; ///< 능력 변화 확률(%)
        int32               _critStage{ 0 };    ///< 급소 단계(0 = 1/24, 1 = 1/8, 2 = 1/2, 3+ = 늘)
        MonsterMoveCategory _category{ MonsterMoveCategory::Physical };
        MonsterStatus       _status{ MonsterStatus::None };
        MonsterStat         _stat{ MonsterStat::Attack }; ///< 능력 변화가 걸리는 능력치
        MonsterEffectTarget _statTarget{ MonsterEffectTarget::Foe };

        bool isDamaging() const { return _category != MonsterMoveCategory::Status && _power > 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 성격 하나 — 올리는 능력치 ×1.1, 내리는 능력치 ×0.9(같으면 무보정)입니다. HP 는 받지 않습니다. */
    struct SW_GF_API MonsterNatureDef
    {
        hashed_string _id{};
        MonsterStat   _raised{ MonsterStat::Attack };
        MonsterStat   _lowered{ MonsterStat::Attack };

        /** @brief 이 능력치의 성격 보정(%) — 110 · 100 · 90 입니다. */
        int32 computePercent( MonsterStat stat ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 날씨 하나입니다. */
    struct MonsterWeatherDef
    {
        hashed_string         _id{};
        hashed_string         _boostedType{};        ///< 이 타입 기술 ×1.5
        hashed_string         _weakenedType{};       ///< 이 타입 기술 ×0.5
        vector<hashed_string> _listChipImmuneType{}; ///< 턴 끝 피해를 받지 않는 타입
        int32                 _chipDivisor{ 0 };     ///< 턴 끝에 최대 HP / 이것(0 = 없음)
        int32                 _turns{ 5 };           ///< 기술로 부를 때의 지속 턴
    };
} // namespace sw

namespace sw
{
    /** @brief 조우 칸 하나입니다. */
    struct MonsterEncounterSlot
    {
        hashed_string _speciesId{};
        int32         _minLevel{ 2 };
        int32         _maxLevel{ 2 };
        int32         _weight{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 조우 테이블 하나(지역 하나 · 시간대 묶음)입니다. 한 지역에 시간대별로 여러 개를 둘 수 있습니다. */
    struct MonsterEncounterDef
    {
        hashed_string                _id{};
        hashed_string                _area{};
        vector<hashed_string>        _listTime{}; ///< 비면 아무 때나
        vector<MonsterEncounterSlot> _listSlot{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class MonsterCollectorCatalog
     * @brief `<MonsterCollectorCatalog>` 아래의 `<Move>` · `<Nature>` · `<Weather>` · `<StatusImmunity>` · `<Species>`(`<Learn>` · `<Evolve>`) · `<Encounter>`(`<Slot>`) 를 읽습니다.
     * @details 예:
     *          `<Move id="ember" type="Fire" category="Special" power="40" accuracy="100" pp="25" status="Burn" statusChance="10"/>`
     *          `<Move id="growl" type="Normal" category="Status" pp="40" stat="Attack" stages="-1" target="Foe"/>`
     *          `<Nature id="Adamant" up="Attack" down="SpecialAttack"/>` · `<Weather id="Rain" boost="Water" weaken="Fire" turns="5"/>`
     *          `<StatusImmunity status="Burn" types="Fire"/>`
     *          `<Species id="charmander" types="Fire" stats="39 52 43 60 50 65" catchRate="45" baseExp="62" expGroup="Medium" evYield="0 0 0 0 0 1">`
     *          `<Learn level="1" move="scratch"/><Evolve to="charmeleon" level="16"/></Species>`
     *          `<Encounter id="route1_night" area="route1" time="Night"><Slot species="hoothoot" min="2" max="4" weight="30"/></Encounter>`
     */
    class SW_GF_API MonsterCollectorCatalog
    {
    public:
        static constexpr int32 kMaxLevel = 100;

        MonsterCollectorCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               clear();

        /** @brief 레벨 @p level 이 되기까지의 총 경험치입니다(레벨 1 = 0). */
        static int64 computeTotalExp( MonsterExpGroup group, int32 level );
        /** @brief 총 경험치 @p exp 로 도달한 레벨입니다(최대 `kMaxLevel`). */
        static int32 computeLevelForExp( MonsterExpGroup group, int64 exp );

        /**
         * @brief 지역 · 시간대에 맞는 모든 테이블의 칸을 모아 가중치로 하나를 뽑고 레벨을 범위 안에서 굴립니다.
         * @return 맞는 칸이 없으면 false 입니다(그 지역은 조우가 없다). 씨앗이 같으면 같은 결과입니다.
         */
        [[nodiscard]] bool rollEncounter( const hashed_string& area, const hashed_string& timeOfDay, GameRandom& random, hashed_string& outSpeciesId,
                                          int32& outLevel ) const;
        /** @brief @p type 이 @p status 에 면역인가입니다(불꽃은 화상, 전기는 마비 …). */
        bool isStatusImmune( MonsterStatus status, const vector<hashed_string>& listType ) const;

        const MonsterSpeciesDef*         findSpecies( const hashed_string& id ) const { return _speciesCatalog.find( id ); }
        const MonsterMoveDef*            findMove( const hashed_string& id ) const { return _moveCatalog.find( id ); }
        const MonsterNatureDef*          findNature( const hashed_string& id ) const { return _natureCatalog.find( id ); }
        const MonsterWeatherDef*         findWeather( const hashed_string& id ) const { return _weatherCatalog.find( id ); }
        const vector<MonsterNatureDef>&  getNatures() const { return _natureCatalog.getAll(); }
        const vector<MonsterSpeciesDef>& getSpecies() const { return _speciesCatalog.getAll(); }

        /** @brief 이름("Attack" · "SpecialDefense" …)을 능력치로 읽습니다. 모르면 false 입니다. */
        [[nodiscard]] static bool parseStat( string_view text, MonsterStat& outStat );
        /** @brief 이름("Burn" · "Toxic" …)을 상태이상으로 읽습니다. 모르면 false 입니다. */
        [[nodiscard]] static bool parseStatus( string_view text, MonsterStatus& outStatus );

    private:
        struct StatusImmunity
        {
            MonsterStatus         _status{ MonsterStatus::None };
            vector<hashed_string> _listType{};
        };

        uint32 loadRoot( const XmlNode& root, string_view sourceName );
        void   loadMove( const XmlNode& node, const utf8* pId, string_view sourceName );
        void   loadSpecies( const XmlNode& node, const utf8* pId, string_view sourceName );
        void   loadEncounter( const XmlNode& node, const utf8* pId );

        GameCatalog<MonsterSpeciesDef>   _speciesCatalog;
        GameCatalog<MonsterMoveDef>      _moveCatalog;
        GameCatalog<MonsterNatureDef>    _natureCatalog;
        GameCatalog<MonsterWeatherDef>   _weatherCatalog;
        GameCatalog<MonsterEncounterDef> _encounterCatalog;
        vector<StatusImmunity>           _listStatusImmunity;
    };
} // namespace sw
