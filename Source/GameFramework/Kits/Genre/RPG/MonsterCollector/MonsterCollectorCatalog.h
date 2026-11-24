/**
 * @file MonsterCollectorCatalog.h
 * @brief 몬스터 수집(포켓몬류)의 데이터입니다. 종(기본 능력치 6개, 타입 1~2개, 배울 기술, 진화, 포획률, 경험치 그룹), 기술(위력, 명중, PP, 분류, 우선도,
 *        부가 효과), 성격(±10%), 날씨, 상태이상 면역 타입, 야생 조우 테이블(지역, 시간대, 레벨 범위, 가중치)을 담습니다.
 * @details 타입 상성 자체는 기반 `ElementChart` 가 가진다(이 카탈로그는 타입 이름만 적는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class GameRandom;
    class XMLNode;

    /** @brief 능력치 여섯 가지입니다. XML 의 `stats="HP 공격 방어 특공 특방 스피드"` 가 이 순서입니다. */
    ENUM()
    enum class MonsterStat : uint8
    {
        HP = 0,
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
    ENUM()
    enum class MonsterExpGroup : uint8
    {
        Fast = 0,
        Medium,
        Slow
    };

    /** @brief 기술 분류입니다. 물리는 공격과 방어, 특수는 특공과 특방을 쓰고, 변화는 피해가 없습니다. */
    ENUM()
    enum class MonsterMoveCategory : uint8
    {
        Physical = 0,
        Special,
        Status
    };

    /** @brief 상태이상입니다(한 번에 하나만 걸립니다). */
    ENUM()
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
    ENUM()
    enum class MonsterEffectTarget : uint8
    {
        Foe = 0,
        Self
    };

    /** @brief 레벨에 배우는 기술 하나입니다. */
    REFLECT()
    struct MonsterLearnEntry
    {
        REFLECT_BODY();

        PROPERTY( Name = "move", Required )
        hashed_string _moveID{};
        PROPERTY( Min = 1, Max = MonsterCollectorCatalog::kMaxLevel )
        int32 _level{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 진화 분기 하나입니다. 적힌 조건이 **모두** 맞아야 합니다.
     * @details 아이템 조건이 있으면 그 아이템을 쓸 때만 보고, 없으면 레벨업할 때만 본다(돌 진화, 레벨 진화, 친밀도 진화).
     */
    REFLECT()
    struct MonsterEvolutionDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "to", Required )
        hashed_string _targetID{};
        PROPERTY( Name = "item" )
        hashed_string _itemID{}; ///< 비어 있지 않으면 그 아이템을 썼을 때
        PROPERTY( Min = 0 )
        int32 _level{ 0 }; ///< 0이면 레벨 조건이 없다
        PROPERTY( Min = 0 )
        int32 _friendship{ 0 }; ///< 0이면 친밀도 조건이 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 종 하나입니다. */
    REFLECT()
    struct SW_KIT_API MonsterSpeciesDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Name = "types" )
        vector<hashed_string>       _listType{};  ///< 1~2개
        vector<MonsterLearnEntry>   _listLearn{}; ///< 레벨 오름차순
        vector<MonsterEvolutionDef> _listEvolution{};
        int32                       _arrBaseStat[kMonsterStatCount]{ 50, 50, 50, 50, 50, 50 };
        int32                       _arrEvYield[kMonsterStatCount]{ 0, 0, 0, 0, 0, 0 }; ///< 쓰러뜨린 쪽이 받는 노력치
        PROPERTY( Min = 1, Max = 255 )
        int32 _catchRate{ 45 }; ///< 1..255
        PROPERTY( Min = 1 )
        int32 _baseExp{ 64 }; ///< 경험치 공식의 b
        PROPERTY()
        MonsterExpGroup _expGroup{ MonsterExpGroup::Medium };

        bool hasType( const hashed_string& type ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 기술 하나입니다. */
    REFLECT()
    struct MonsterMoveDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY()
        hashed_string _useSound{}; ///< 쓸 때의 사운드 이벤트(`*.audioevents.xml`). 비면 소리가 없다
        PROPERTY()
        hashed_string _type{};
        PROPERTY( Name = "weather" )
        hashed_string _weatherID{}; ///< 비어 있지 않으면 날씨를 부른다(비바라기)
        PROPERTY( Min = 0 )
        int32 _power{ 0 };
        PROPERTY( Min = 0, Max = 100 )
        int32 _accuracy{ 100 }; ///< 0이면 반드시 맞는다
        PROPERTY( Min = 1 )
        int32 _pp{ 10 };
        PROPERTY( Min = -7, Max = 5 )
        int32 _priority{ 0 };
        PROPERTY( Min = 0, Max = 100 )
        int32 _statusChance{ 0 }; ///< 상태이상 확률(%). 변화 기술은 100
        PROPERTY( Name = "stages", Min = -6, Max = 6 )
        int32 _statStages{ 0 }; ///< 능력 변화 단계(−6..+6, 0이면 없음)
        PROPERTY( Min = 0, Max = 100 )
        int32 _statChance{ 100 }; ///< 능력 변화 확률(%)
        PROPERTY( Min = 0 )
        int32 _critStage{ 0 }; ///< 급소 단계(0은 1/24, 1은 1/8, 2는 1/2, 3 이상은 항상)
        PROPERTY()
        MonsterMoveCategory _category{ MonsterMoveCategory::Physical };
        PROPERTY()
        MonsterStatus _status{ MonsterStatus::None };
        PROPERTY( Name = "stat" )
        MonsterStat _changedStat{ MonsterStat::Attack }; ///< 능력 변화가 걸리는 능력치(HP 는 안 된다 · `_stat` 은 Windows CRT 매크로라 쓰지 않는다)
        PROPERTY( Name = "target" )
        MonsterEffectTarget _statTarget{ MonsterEffectTarget::Foe };

        bool isDamaging() const { return _category != MonsterMoveCategory::Status && _power > 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 성격 하나입니다. 올리는 능력치는 ×1.1, 내리는 능력치는 ×0.9이고, 같으면 보정이 없습니다. HP 는 받지 않습니다. */
    REFLECT()
    struct SW_KIT_API MonsterNatureDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "up" )
        MonsterStat _raised{ MonsterStat::Attack };
        PROPERTY( Name = "down" )
        MonsterStat _lowered{ MonsterStat::Attack };

        /** @brief 이 능력치의 성격 보정(%)입니다. 110, 100, 90 중 하나입니다. */
        int32 computePercent( MonsterStat stat ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 날씨 하나입니다. */
    REFLECT()
    struct MonsterWeatherDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "boost" )
        hashed_string _boostedType{}; ///< 이 타입 기술은 `MonsterBattleRules::_weatherBoostScale` 배
        PROPERTY( Name = "weaken" )
        hashed_string _weakenedType{}; ///< 이 타입 기술은 `MonsterBattleRules::_weatherPenaltyScale` 배
        PROPERTY( Name = "immune" )
        vector<hashed_string> _listChipImmuneType{}; ///< 턴 끝 피해를 받지 않는 타입
        PROPERTY( Name = "chip", Min = 0 )
        int32 _chipDivisor{ 0 }; ///< 턴 끝에 최대 HP 를 이 값으로 나눈 만큼 잃는다(0이면 없음)
        PROPERTY( Min = 1 )
        int32 _turns{ 5 }; ///< 기술로 부를 때의 지속 턴
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 전투 규칙의 수치(`<Rules>`)입니다. 적지 않은 칸은 초기값이다 — 초기값은 5 세대 피해 · 급소, 3 · 4 세대 포획 · 도망 공식이다.
     * @details 정수 나눗셈의 단계마다 내림하므로 배율은 백분율 · 나누는 수로 적는다.
     */
    REFLECT()
    struct SW_KIT_API MonsterBattleRules
    {
        REFLECT_BODY();

        PROPERTY( Name = "critDivisor", Min = 1 )
        vector<int32> _listCritDivisor{ 24, 8, 2 }; ///< 급소 단계마다의 확률 분모(단계가 넘치면 반드시 급소)
        PROPERTY( Min = 0 )
        int32 _levelFactor{ 2 }; ///< 피해 = (레벨 × 이 값 / `_levelDivisor` + `_levelBase`) × 위력 × 공격 / 방어 / `_powerDivisor` + `_damageBase`
        PROPERTY( Min = 1 )
        int32 _levelDivisor{ 5 };
        PROPERTY( Min = 0 )
        int32 _levelBase{ 2 };
        PROPERTY( Min = 1 )
        int32 _powerDivisor{ 50 };
        PROPERTY( Min = 0 )
        int32 _damageBase{ 2 };
        PROPERTY( Min = 0 )
        int32 _critPercent{ 150 };
        PROPERTY( Min = 0 )
        int32 _stabPercent{ 150 }; ///< 기술과 타입이 같은 개체의 피해
        PROPERTY( Min = 0, Max = 100 )
        int32 _rollMinPercent{ 85 }; ///< 피해 난수의 하한(상한은 100)
        PROPERTY( Min = 1 )
        int32 _burnAttackDivisor{ 2 }; ///< 화상 개체의 물리 피해를 나누는 수
        PROPERTY( Min = 1 )
        int32 _stageBase{ 2 }; ///< 능력 변화 +n 은 (이 값 + n) / 이 값, −n 은 이 값 / (이 값 + n)
        PROPERTY( Min = 0.0 )
        float32 _weatherBoostScale{ 1.5f };
        PROPERTY( Min = 0.0 )
        float32 _weatherPenaltyScale{ 0.5f };
        PROPERTY( Min = 0, Max = 100 )
        int32 _paralysisChance{ 25 }; ///< 마비 개체가 움직이지 못할 확률
        PROPERTY( Min = 0.0 )
        float32 _paralysisSpeedScale{ 0.5f };
        PROPERTY( Min = 0, Max = 100 )
        int32 _thawChance{ 20 };
        PROPERTY( Min = 1 )
        int32 _maxSleepTurns{ 3 };
        PROPERTY( Min = 1 )
        int32 _poisonDivisor{ 8 }; ///< 턴 끝에 최대 HP 를 이 값으로 나눈 만큼 잃는다
        PROPERTY( Min = 1 )
        int32 _burnDivisor{ 16 };
        PROPERTY( Min = 1 )
        int32 _toxicDivisor{ 16 }; ///< 맹독은 최대 HP × n / 이 값
        PROPERTY( Min = 1 )
        int32 _toxicMaxTurns{ 15 };
        PROPERTY( Min = 0 )
        int32 _escapeSpeedFactor{ 128 }; ///< 도망 값 = 내 스피드 × 이 값 / 상대 스피드 + `_escapePerAttempt` × 지난 시도 수
        PROPERTY( Min = 0 )
        int32 _escapePerAttempt{ 30 };
        PROPERTY( Min = 1 )
        int32 _escapeRollRange{ 256 }; ///< 0 부터 이 값 미만을 굴려 도망 값 미만이면 도망친다
        PROPERTY( Min = 1 )
        int32 _captureMaxHPFactor{ 3 }; ///< 포획 값 = (최대 HP × 이 값 − HP × `_captureHPFactor`) × 포획률 × 볼 / (최대 HP × 이 값)
        PROPERTY( Min = 0 )
        int32 _captureHPFactor{ 2 };
        PROPERTY( Min = 1 )
        int32 _captureMaxRate{ 255 }; ///< 포획률의 상한이자 반드시 잡히는 포획 값
        PROPERTY( Min = 0.0 )
        float32 _captureHelplessScale{ 2.0f }; ///< 수면 · 얼음
        PROPERTY( Min = 0.0 )
        float32 _captureStatusScale{ 1.5f }; ///< 그 밖의 상태이상
        PROPERTY( Min = 2 )
        int32 _captureRollRange{ 65536 }; ///< 흔들림마다 0 부터 이 값 미만을 굴린다
        PROPERTY( Min = 1 )
        int32 _captureShakeCount{ 4 }; ///< 모두 넘어야 잡힌다(화면에는 하나 적게 흔들린다)
    };
} // namespace sw

namespace sw
{
    /** @brief 조우 슬롯 하나입니다. */
    REFLECT()
    struct MonsterEncounterSlot
    {
        REFLECT_BODY();

        PROPERTY( Name = "species", Required )
        hashed_string _speciesID{};
        PROPERTY( Name = "min", Required, Min = 1, Max = MonsterCollectorCatalog::kMaxLevel )
        int32 _minLevel{ 2 };
        PROPERTY( Name = "max", Required, Min = "_minLevel", Max = MonsterCollectorCatalog::kMaxLevel )
        int32 _maxLevel{ 2 };
        PROPERTY( Min = 0 )
        int32 _weight{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 조우 테이블 하나(지역 하나와 시간대 그룹)입니다. 한 지역에 시간대별로 여러 개를 둘 수 있습니다. */
    REFLECT()
    struct MonsterEncounterDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _area{};
        PROPERTY( Name = "time" )
        vector<hashed_string>        _listTime{}; ///< 비어 있으면 아무 때나
        vector<MonsterEncounterSlot> _listSlot{};
    };
} // namespace sw

namespace sw
{
    /** @brief `<StatusImmunity>` 한 줄입니다. 이 타입들은 이 상태에 걸리지 않습니다. */
    REFLECT()
    struct MonsterStatusImmunityDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        MonsterStatus _status{ MonsterStatus::None };
        PROPERTY( Name = "types" )
        vector<hashed_string> _listType{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class MonsterCollectorCatalog
     * @brief `<MonsterCollectorCatalog>` 아래의 `<Rules>`, `<Move>`, `<Nature>`, `<Weather>`, `<StatusImmunity>`, `<Species>`(`<Learn>`, `<Evolve>`), `<Encounter>`(`<Slot>`)를 읽는 카탈로그입니다.
     * @details 칸은 각 Def 의 `PROPERTY` 가 스키마다(기본값 = 초기값, 범위 = Min · Max). 예시는 다음과 같다.
     *          `<Move id="ember" name="Ember" type="Fire" category="Special" power="40" accuracy="100" pp="25" status="Burn" statusChance="10"/>`
     *          `<Move id="growl" name="Growl" type="Normal" category="Status" pp="40" stat="Attack" stages="-1" target="Foe"/>`
     *          상태이상을 거는 기술은 `statusChance` 를 적어야 한다(변화 기술도 100 을 적는다).
     *          `<Nature id="Adamant" up="Attack" down="SpecialAttack"/>`, `<Weather id="Rain" boost="Water" weaken="Fire" turns="5"/>`
     *          `<StatusImmunity status="Burn" types="Fire"/>`
     *          `<Species id="charmander" name="Charmander" types="Fire" stats="39 52 43 60 50 65" catchRate="45" baseExp="62" expGroup="Medium" evYield="0 0 0 0 0 1">`
     *          `<Learn level="1" move="scratch"/><Evolve to="charmeleon" level="16"/></Species>`
     *          `<Encounter id="route1_night" area="route1" time="Night"><Slot species="hoothoot" min="2" max="4" weight="30"/></Encounter>`
     */
    class SW_KIT_API MonsterCollectorCatalog : public XMLCatalog<MonsterCollectorCatalog>
    {
        friend class XMLCatalog<MonsterCollectorCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다. 게임 기능이 팩의 `data/monstercollector.xml` 을 게임 범위에 건다(`GameFeatureScope::loadCatalog`). */
        static constexpr const utf8* kDataKind = "MonsterCollectorCatalog";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<MonsterCollectorCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".monstercollector.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `MonsterCollectorCatalog.cpp` 의 줄이 로더를 더한다). 팩의 `data/monstercollector.xml` 은 경로 규칙으로 가른다. */
        static constexpr AssetTypeDef kAssetType =
            AssetTypeDef{ kDataKind, kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} }.withPathRule( AssetPathRule{ "game/", {}, "/data/monstercollector.xml" } );

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

        const MonsterSpeciesDef* findSpecies( const hashed_string& id ) const { return _speciesCatalog.find( id ); }
        const MonsterMoveDef*    findMove( const hashed_string& id ) const { return _moveCatalog.find( id ); }
        const MonsterNatureDef*  findNature( const hashed_string& id ) const { return _natureCatalog.find( id ); }
        const MonsterWeatherDef* findWeather( const hashed_string& id ) const { return _weatherCatalog.find( id ); }
        /** @brief 전투 규칙의 수치입니다(`<Rules>` 가 없으면 초기값). */
        const MonsterBattleRules&        getBattleRules() const { return _battleRules; }
        const vector<MonsterNatureDef>&  getNatures() const { return _natureCatalog.getAll(); }
        const vector<MonsterSpeciesDef>& getSpecies() const { return _speciesCatalog.getAll(); }

        /** @brief 이름("Attack", "SpecialDefense" 등)을 능력치로 읽습니다. 모르는 이름이면 false 를 반환합니다. */
        [[nodiscard]] static bool parseStat( string_view text, MonsterStat& outStat );

    private:
        static constexpr const utf8* kXMLRootName = "MonsterCollectorCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadStatusImmunity( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadMove( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadSpecies( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadEncounter( const XMLNode& node, string_view sourceName );

    private:
        GameCatalog<MonsterSpeciesDef>   _speciesCatalog;
        GameCatalog<MonsterMoveDef>      _moveCatalog;
        GameCatalog<MonsterNatureDef>    _natureCatalog;
        GameCatalog<MonsterWeatherDef>   _weatherCatalog;
        GameCatalog<MonsterEncounterDef> _encounterCatalog;
        vector<MonsterStatusImmunityDef> _listStatusImmunity;
        MonsterBattleRules               _battleRules;
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<MonsterCollectorCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, MonsterCollectorCatalog );
} // namespace sw
