/**
 * @file MonsterCollectorCatalog.h
 * @brief 몬스터 수집(포켓몬류)의 데이터입니다. 종(기본 능력치 6개, 타입 1~2개, 배울 기술, 진화, 포획률, 경험치 그룹), 기술(위력, 명중, PP, 분류, 우선도,
 *        부가 효과), 성격(±10%), 날씨, 상태이상 면역 타입, 야생 조우 테이블(지역, 시간대, 레벨 범위, 가중치)을 담습니다.
 * @details 타입 상성 자체는 기반 `ElementChart` 가 가진다(이 카탈로그는 타입 이름만 적는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;
    class XMLNode;

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

    /** @brief 경험치 그룹입니다. 레벨 n 까지의 총 경험치가 빠름은 4n³/5, 보통은 n³, 느림은 5n³/4 입니다. */
    enum class MonsterExpGroup : uint8
    {
        Fast = 0,
        Medium,
        Slow
    };

    /** @brief 기술 분류입니다. 물리는 공격과 방어, 특수는 특공과 특방을 쓰고, 변화는 피해가 없습니다. */
    enum class MonsterMoveCategory : uint8
    {
        Physical = 0,
        Special,
        Status
    };

    /** @brief 상태이상입니다(한 번에 하나만 걸립니다). */
    enum class MonsterStatus : uint8
    {
        None = 0,
        Poison,    ///< 턴 끝마다 최대 HP 의 1/8 을 잃는다
        Toxic,     ///< 턴 끝마다 n/16 을 잃는다(n 은 1부터 매 턴 오른다)
        Burn,      ///< 턴 끝마다 1/16 을 잃고 물리 피해가 절반이 된다
        Paralysis, ///< 스피드가 절반이 되고 25% 확률로 행동하지 못한다
        Sleep,     ///< 1~3턴 동안 행동하지 못한다
        Freeze     ///< 행동하지 못하고, 매 턴 20% 확률로 녹는다
    };

    /** @brief 능력 변화가 누구에게 걸리는지 나타냅니다. */
    enum class MonsterEffectTarget : uint8
    {
        Foe = 0,
        Self
    };

    SW_GF_API const utf8* toString( MonsterStatus status );

    /** @brief 레벨에 배우는 기술 하나입니다. */
    struct MonsterLearnEntry
    {
        hashed_string _moveID{};
        int32         _level{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 진화 분기 하나입니다. 적힌 조건이 **모두** 맞아야 합니다.
     * @details 아이템 조건이 있으면 그 아이템을 쓸 때만 보고, 없으면 레벨업할 때만 본다(돌 진화, 레벨 진화, 친밀도 진화).
     */
    struct MonsterEvolutionDef
    {
        hashed_string _targetID{};
        hashed_string _itemID{};        ///< 비어 있지 않으면 그 아이템을 썼을 때
        int32         _level{ 0 };      ///< 0이면 레벨 조건이 없다
        int32         _friendship{ 0 }; ///< 0이면 친밀도 조건이 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 종 하나입니다. */
    struct SW_GF_API MonsterSpeciesDef
    {
        hashed_string               _id{};
        string                      _name{};
        vector<hashed_string>       _listType{};  ///< 1~2개
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
        hashed_string       _weatherID{}; ///< 비어 있지 않으면 날씨를 부른다(비바라기)
        int32               _power{ 0 };
        int32               _accuracy{ 100 }; ///< 0이면 반드시 맞는다
        int32               _pp{ 10 };
        int32               _priority{ 0 };
        int32               _statusChance{ 0 }; ///< 상태이상 확률(%). 변화 기술은 100
        int32               _statStages{ 0 };   ///< 능력 변화 단계(−6..+6, 0이면 없음)
        int32               _statChance{ 100 }; ///< 능력 변화 확률(%)
        int32               _critStage{ 0 };    ///< 급소 단계(0은 1/24, 1은 1/8, 2는 1/2, 3 이상은 항상)
        MonsterMoveCategory _category{ MonsterMoveCategory::Physical };
        MonsterStatus       _status{ MonsterStatus::None };
        MonsterStat         _stat{ MonsterStat::Attack }; ///< 능력 변화가 걸리는 능력치
        MonsterEffectTarget _statTarget{ MonsterEffectTarget::Foe };

        bool isDamaging() const { return _category != MonsterMoveCategory::Status && _power > 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 성격 하나입니다. 올리는 능력치는 ×1.1, 내리는 능력치는 ×0.9이고, 같으면 보정이 없습니다. HP 는 받지 않습니다. */
    struct SW_GF_API MonsterNatureDef
    {
        hashed_string _id{};
        MonsterStat   _raised{ MonsterStat::Attack };
        MonsterStat   _lowered{ MonsterStat::Attack };

        /** @brief 이 능력치의 성격 보정(%)입니다. 110, 100, 90 중 하나입니다. */
        int32 computePercent( MonsterStat stat ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 날씨 하나입니다. */
    struct MonsterWeatherDef
    {
        hashed_string         _id{};
        hashed_string         _boostedType{};        ///< 이 타입 기술은 ×1.5
        hashed_string         _weakenedType{};       ///< 이 타입 기술은 ×0.5
        vector<hashed_string> _listChipImmuneType{}; ///< 턴 끝 피해를 받지 않는 타입
        int32                 _chipDivisor{ 0 };     ///< 턴 끝에 최대 HP 를 이 값으로 나눈 만큼 잃는다(0이면 없음)
        int32                 _turns{ 5 };           ///< 기술로 부를 때의 지속 턴
    };
} // namespace sw

namespace sw
{
    /** @brief 조우 슬롯 하나입니다. */
    struct MonsterEncounterSlot
    {
        hashed_string _speciesID{};
        int32         _minLevel{ 2 };
        int32         _maxLevel{ 2 };
        int32         _weight{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 조우 테이블 하나(지역 하나와 시간대 그룹)입니다. 한 지역에 시간대별로 여러 개를 둘 수 있습니다. */
    struct MonsterEncounterDef
    {
        hashed_string                _id{};
        hashed_string                _area{};
        vector<hashed_string>        _listTime{}; ///< 비어 있으면 아무 때나
        vector<MonsterEncounterSlot> _listSlot{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class MonsterCollectorCatalog
     * @brief `<MonsterCollectorCatalog>` 아래의 `<Move>`, `<Nature>`, `<Weather>`, `<StatusImmunity>`, `<Species>`(`<Learn>`, `<Evolve>`), `<Encounter>`(`<Slot>`)를 읽는 카탈로그입니다.
     * @details 예시는 다음과 같다.
     *          `<Move id="ember" type="Fire" category="Special" power="40" accuracy="100" pp="25" status="Burn" statusChance="10"/>`
     *          `<Move id="growl" type="Normal" category="Status" pp="40" stat="Attack" stages="-1" target="Foe"/>`
     *          `<Nature id="Adamant" up="Attack" down="SpecialAttack"/>`, `<Weather id="Rain" boost="Water" weaken="Fire" turns="5"/>`
     *          `<StatusImmunity status="Burn" types="Fire"/>`
     *          `<Species id="charmander" types="Fire" stats="39 52 43 60 50 65" catchRate="45" baseExp="62" expGroup="Medium" evYield="0 0 0 0 0 1">`
     *          `<Learn level="1" move="scratch"/><Evolve to="charmeleon" level="16"/></Species>`
     *          `<Encounter id="route1_night" area="route1" time="Night"><Slot species="hoothoot" min="2" max="4" weight="30"/></Encounter>`
     */
    class SW_GF_API MonsterCollectorCatalog : public XMLCatalog<MonsterCollectorCatalog>
    {
        friend class XMLCatalog<MonsterCollectorCatalog>;

    public:
        static constexpr int32 kMaxLevel = 100;

        MonsterCollectorCatalog();

        void clear();

        /** @brief 레벨 @p level 이 되기까지의 총 경험치를 반환합니다(레벨 1은 0). */
        static int64 computeTotalExp( MonsterExpGroup group, int32 level );
        /** @brief 총 경험치 @p exp 로 도달한 레벨을 반환합니다(최대 `kMaxLevel`). */
        static int32 computeLevelForExp( MonsterExpGroup group, int64 exp );

        /**
         * @brief 지역과 시간대에 맞는 모든 테이블의 슬롯을 모아 가중치로 하나를 뽑고, 레벨을 범위 안에서 굴립니다.
         * @return 맞는 슬롯이 없으면 false 입니다(그 지역은 조우가 없습니다). 시드가 같으면 같은 결과가 나옵니다.
         */
        [[nodiscard]] bool rollEncounter( const hashed_string& area, const hashed_string& timeOfDay, GameRandom& random, hashed_string& outSpeciesID,
                                          int32& outLevel ) const;
        /** @brief @p type 이 @p status 에 면역인지 반환합니다(불꽃은 화상, 전기는 마비 등). */
        bool isStatusImmune( MonsterStatus status, const vector<hashed_string>& listType ) const;

        const MonsterSpeciesDef*         findSpecies( const hashed_string& id ) const { return _speciesCatalog.find( id ); }
        const MonsterMoveDef*            findMove( const hashed_string& id ) const { return _moveCatalog.find( id ); }
        const MonsterNatureDef*          findNature( const hashed_string& id ) const { return _natureCatalog.find( id ); }
        const MonsterWeatherDef*         findWeather( const hashed_string& id ) const { return _weatherCatalog.find( id ); }
        const vector<MonsterNatureDef>&  getNatures() const { return _natureCatalog.getAll(); }
        const vector<MonsterSpeciesDef>& getSpecies() const { return _speciesCatalog.getAll(); }

        /** @brief 이름("Attack", "SpecialDefense" 등)을 능력치로 읽습니다. 모르는 이름이면 false 를 반환합니다. */
        [[nodiscard]] static bool parseStat( string_view text, MonsterStat& outStat );
        /** @brief 이름("Burn", "Toxic" 등)을 상태이상으로 읽습니다. 모르는 이름이면 false 를 반환합니다. */
        [[nodiscard]] static bool parseStatus( string_view text, MonsterStatus& outStatus );

    private:
        struct StatusImmunity
        {
            MonsterStatus         _status{ MonsterStatus::None };
            vector<hashed_string> _listType{};
        };

        static constexpr const utf8* kXMLRootName = "MonsterCollectorCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        void                         loadMove( const XMLNode& node, const utf8* pID, string_view sourceName );
        void                         loadSpecies( const XMLNode& node, const utf8* pID, string_view sourceName );
        void                         loadEncounter( const XMLNode& node, const utf8* pID );

        GameCatalog<MonsterSpeciesDef>   _speciesCatalog;
        GameCatalog<MonsterMoveDef>      _moveCatalog;
        GameCatalog<MonsterNatureDef>    _natureCatalog;
        GameCatalog<MonsterWeatherDef>   _weatherCatalog;
        GameCatalog<MonsterEncounterDef> _encounterCatalog;
        vector<StatusImmunity>           _listStatusImmunity;
    };
} // namespace sw
