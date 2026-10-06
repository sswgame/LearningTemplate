/**
 * @file WesternCatalog.h
 * @brief 오픈월드 서부극의 데이터 — 범죄 · 지역 · 추적 단계 · 명예 단계와 행동 · 말 품종 · 유대 단계 · 먹이 · 음식 · 옷 · 생존 수치 · 데드아이 단계 ·
 *        동물 · 사냥 무기 · 명중 부위 · 가죽 등급 값입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Data/GameCatalog.h"
#include "GameFramework/Base/Data/XmlCatalog.h"
#include "GameFramework/Base/Progression/LevelProgress.h"
#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 범죄 한 종류입니다. */
    struct WesternCrimeDef
    {
        hashed_string _id{};
        float32       _reportTime{ 15.0f }; ///< 시민 목격자가 신고하기까지 걸리는 초(보안관은 그 자리에서)
        int32         _bounty{ 5 };         ///< 신고되면 그 지역 현상금에 더한다(달러)
        int32         _wanted{ 1 };         ///< 신고되면 오르는 수배 단계
        int32         _honor{ 0 };          ///< 저지르면 바뀌는 명예(목격과 상관없이)
    };
} // namespace sw

namespace sw
{
    /** @brief 지역(주 · 마을) 하나의 법 집행 규칙입니다. */
    struct WesternRegionDef
    {
        hashed_string _id{};
        float32       _wantedCooldown{ 60.0f };   ///< 법에 보이지 않고 이만큼 지나면 수배 단계가 하나 내려간다(초)
        float32       _disguiseScale{ 2.0f };     ///< 변장(옷 바꾸기)했으면 수배가 이 배로 빨리 식는다
        float32       _maskedBountyScale{ 0.5f }; ///< 복면을 쓰고 저지른 범죄의 현상금 배율(얼굴을 못 봤다)
        int32         _bountyDecayPerDay{ 0 };    ///< 하루마다 줄어드는 현상금
        int32         _maxWanted{ 5 };
    };
} // namespace sw

namespace sw
{
    /** @brief 수배 단계에 따른 보안관 추적입니다. */
    struct WesternPursuitDef
    {
        hashed_string _name{};
        int32         _level{ 1 };  ///< 이 수배 단계 이상이면
        int32         _lawmen{ 2 }; ///< 보내는 보안관 수
        uint8         _bShootOnSight{ SW_FALSE };
        uint8         _bBountyHunter{ SW_FALSE }; ///< 지역을 떠나도 현상금 사냥꾼이 온다
    };
} // namespace sw

namespace sw
{
    /** @brief 명예 단계 하나의 효과입니다(값 범위는 `ReputationTier`). */
    struct WesternHonorTierDef
    {
        hashed_string _name{};
        hashed_string _dialogueFlag{};       ///< 이 단계에서 켜지는 플래그(대사 분기 — `GameFlags`)
        float32       _shopDiscount{ 0.0f }; ///< 상점 값 할인 몫(음수면 웃돈)
    };
} // namespace sw

namespace sw
{
    /** @brief 명예가 바뀌는 행동 하나입니다. */
    struct WesternHonorActionDef
    {
        hashed_string _id{};
        int32         _delta{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 말 품종 하나입니다. */
    struct WesternHorseDef
    {
        hashed_string _id{};
        float32       _health{ 100.0f };
        float32       _stamina{ 100.0f };
        float32       _speed{ 1.0f };
        float32       _courage{ 0.5f };                ///< 0..1 — 겁을 덜 먹는다
        float32       _gallopDrain{ 12.0f };           ///< 질주할 때 초당 스태미나
        float32       _coreDrainPerHour{ 2.0f };       ///< 게임 시간 한 시간마다 코어가 주는 양
        float32       _gallopCoreDrainPerHour{ 6.0f }; ///< 질주 중에는 이만큼 더
        float32       _fearDecayPerSecond{ 0.25f };    ///< 쌓인 겁이 식는 빠르기(초당)
        float32       _buckChanceScale{ 0.8f };        ///< 저항이 0 일 때 탄 사람을 떨어뜨릴 확률
    };
} // namespace sw

namespace sw
{
    /** @brief 말 유대 단계 하나입니다. */
    struct WesternBondLevelDef
    {
        vector<hashed_string> _listUnlock{};       ///< 이 단계에서 열리는 능력(뒷발 들기 · 드리프트 · 피아페 …)
        float32               _experience{ 0.0f }; ///< 이 단계가 되는 누적 경험치(정수로 반올림해 `getBondCurve` 의 표가 된다)
        float32               _staminaBonus{ 0.0f };
        float32               _healthBonus{ 0.0f };
        float32               _fearResist{ 0.0f }; ///< 겁 저항에 더한다
        int32                 _level{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 말의 유대 경험치를 주는 행동입니다. */
    struct WesternBondExperience
    {
        float32 _ridePerSecond{ 0.1f };
        float32 _brush{ 15.0f };
        float32 _feed{ 10.0f };
        float32 _calm{ 5.0f };
        float32 _brushCooldownHours{ 6.0f }; ///< 다시 손질해도 경험치가 붙기까지(게임 시간)
    };
} // namespace sw

namespace sw
{
    /** @brief 먹을 것 하나 — 말 먹이와 사람 음식이 같은 모양입니다(코어 회복 · 바로 회복 · 유대). */
    struct WesternFoodDef
    {
        hashed_string _id{};
        float32       _healthCore{ 0.0f };
        float32       _staminaCore{ 0.0f };
        float32       _deadEyeCore{ 0.0f };
        float32       _health{ 0.0f }; ///< 게이지를 바로 채우는 양
        float32       _stamina{ 0.0f };
        float32       _bond{ 0.0f }; ///< 말 먹이 — 유대 경험치(비면 `WesternBondExperience::_feed`)
    };
} // namespace sw

namespace sw
{
    /** @brief 옷 한 벌(겹쳐 입는다)입니다. */
    struct WesternClothingDef
    {
        hashed_string _id{};
        float32       _warmth{ 0.0f }; ///< 체감 온도에 더한다
    };
} // namespace sw

namespace sw
{
    /** @brief 플레이어 생존 수치입니다. 온도는 섭씨, 코어는 0..100 입니다. */
    struct WesternSurvivalSettings
    {
        float32 _comfortMin{ 5.0f };       ///< 체감 온도가 이보다 낮으면 춥다 — 체력 코어가 더 준다
        float32 _comfortMax{ 30.0f };      ///< 이보다 높으면 덥다 — 스태미나 코어가 더 준다
        float32 _coreDrainPerHour{ 1.5f }; ///< 게임 한 시간마다 모든 코어가 주는 양
        float32 _temperatureDrain{ 0.5f }; ///< 편한 범위를 1 도 벗어날 때마다 한 시간에 더 주는 양
        float32 _minRegenScale{ 0.2f };    ///< 코어 0 일 때의 게이지 회복 배율(가득이면 1)
        float32 _health{ 100.0f };
        float32 _stamina{ 100.0f };
        float32 _deadEye{ 100.0f };
        float32 _healthRegen{ 2.0f };     ///< 초당
        float32 _staminaRegen{ 15.0f };   ///< 초당
        float32 _deadEyeRegen{ 1.0f };    ///< 초당
        float32 _deadEyeDrain{ 20.0f };   ///< 데드아이를 켜 둔 동안 초당
        float32 _deadEyeMinimum{ 10.0f }; ///< 이만큼 있어야 켤 수 있다
    };
} // namespace sw

namespace sw
{
    /** @brief 데드아이 단계 하나입니다. */
    struct WesternDeadEyeLevelDef
    {
        float32 _timeScale{ 0.5f }; ///< 켜 둔 동안의 시간 배율
        int32   _level{ 1 };
        int32   _markCount{ 0 }; ///< 표시할 수 있는 개수(0 = 표시 없이 느려지기만)
    };
} // namespace sw

namespace sw
{
    /** @brief 동물 크기 — 알맞은 무기가 다릅니다. */
    enum class WesternAnimalSize : uint8
    {
        Small = 0,
        Medium,
        Large
    };

    /** @brief 동물 한 종입니다. */
    struct WesternAnimalDef
    {
        hashed_string     _id{};
        hashed_string     _lootTable{};          ///< 손질하면 굴리는 전리품 표(고기 · 깃털 — `LootCatalog`)
        float32           _peltPrice{ 1.0f };    ///< 3 성 가죽 값(달러)
        float32           _carcassPrice{ 2.0f }; ///< 3 성 사체 값
        float32           _decayHours{ 48.0f };  ///< 사체가 다 썩는 게임 시간(반이 지나면 한 단계 떨어진다)
        int32             _quality{ 3 };         ///< 이 개체의 원래 품질(1..3)
        WesternAnimalSize _size{ WesternAnimalSize::Medium };
    };
} // namespace sw

namespace sw
{
    /** @brief 사냥에 쓴 무기 하나의 규칙입니다. */
    struct WesternHuntWeaponDef
    {
        hashed_string _id{};
        uint8         _sizeMask{ 0x7 };        ///< 알맞은 크기(비트 = `WesternAnimalSize`) — 아니면 한 단계 떨어진다
        uint8         _bRuinsPelt{ SW_FALSE }; ///< 폭발물 — 가죽을 못 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 명중 부위 하나입니다. */
    struct WesternHitZoneDef
    {
        hashed_string _id{};
        int32         _penalty{ 0 }; ///< 떨어지는 등급
    };
} // namespace sw

namespace sw
{
    /**
     * @class WesternCatalog
     * @brief 서부극 키트의 XML 하나를 읽습니다.
     * @code
     *     <WesternCatalog currency="Dollar" extraHitPenalty="1">
     *       <Crime id="murder" bounty="50" wanted="2" honor="-40" reportTime="20"/>
     *       <Region id="lemoyne" wantedCooldown="60" disguiseScale="2" maskedBountyScale="0.5" bountyDecayPerDay="1" maxWanted="5"/>
     *       <Pursuit level="1" name="Search" lawmen="2"/><Pursuit level="3" name="Manhunt" lawmen="8" shootOnSight="true" bountyHunter="true"/>
     *       <Honor min="-1000" max="1000" start="0">
     *         <Tier name="Outlaw" min="-1000" discount="-0.1" flag="honor_low"/><Tier name="Neutral" min="-200"/>
     *         <Action id="help_stranger" delta="25"/>
     *       </Honor>
     *       <Horse id="arabian" health="90" stamina="120" speed="1.2" courage="0.7" gallopDrain="12" coreDrain="2" gallopCoreDrain="6"/>
     *       <Bond level="1" xp="0"/><Bond level="2" xp="100" unlocks="rear" stamina="10" fearResist="0.1"/>
     *       <BondExperience ride="0.1" brush="15" feed="10" calm="5" brushCooldown="6"/>
     *       <Food id="hay" healthCore="25" staminaCore="15" bond="5"/>
     *       <Clothing id="sheepskin_coat" warmth="15"/>
     *       <Survival comfortMin="5" comfortMax="30" coreDrain="1.5" temperatureDrain="0.5" minRegenScale="0.2" deadEyeDrain="20"/>
     *       <DeadEye level="1" timeScale="0.5" marks="0"/><DeadEye level="2" timeScale="0.4" marks="3"/>
     *       <Animal id="deer" size="Medium" quality="3" pelt="1.5" carcass="3" decayHours="48" loot="deer_parts"/>
     *       <HuntWeapon id="varmint_rifle" sizes="Small"/><HuntWeapon id="dynamite" ruinsPelt="true"/>
     *       <HitZone id="head" penalty="0"/><HitZone id="body" penalty="1"/>
     *       <PeltGrade scales="0,0.3,0.6,1"/>
     *     </WesternCatalog>
     * @endcode
     */
    class SW_GF_API WesternCatalog : public XmlCatalog<WesternCatalog>
    {
        friend class XmlCatalog<WesternCatalog>;

    public:
        static constexpr const utf8* kHonorFactionId = "western.honor"; ///< 명예가 쓰는 평판 세력 id(키트 접두)

        WesternCatalog();

        const WesternCrimeDef*       findCrime( const hashed_string& id ) const { return _crimeCatalog.find( id ); }
        const WesternRegionDef*      findRegion( const hashed_string& id ) const { return _regionCatalog.find( id ); }
        const WesternHorseDef*       findHorse( const hashed_string& id ) const { return _horseCatalog.find( id ); }
        const WesternFoodDef*        findFood( const hashed_string& id ) const { return _foodCatalog.find( id ); }
        const WesternClothingDef*    findClothing( const hashed_string& id ) const { return _clothingCatalog.find( id ); }
        const WesternAnimalDef*      findAnimal( const hashed_string& id ) const { return _animalCatalog.find( id ); }
        const WesternHuntWeaponDef*  findHuntWeapon( const hashed_string& id ) const { return _huntWeaponCatalog.find( id ); }
        const WesternHitZoneDef*     findHitZone( const hashed_string& id ) const { return _hitZoneCatalog.find( id ); }
        const WesternHonorActionDef* findHonorAction( const hashed_string& id ) const { return _honorActionCatalog.find( id ); }
        const WesternHonorTierDef*   findHonorTier( const hashed_string& name ) const;
        /** @brief 수배 단계 @p wantedLevel 의 추적입니다(그 단계 이하에서 가장 높은 것). 0 이거나 없으면 nullptr 입니다. */
        const WesternPursuitDef* findPursuit( int32 wantedLevel ) const;
        /** @brief 데드아이 단계 @p level 이하에서 가장 높은 것입니다. 없으면 nullptr 입니다. */
        const WesternDeadEyeLevelDef* findDeadEyeLevel( int32 level ) const;

        const vector<WesternBondLevelDef>& getBondLevels() const { return _listBondLevel; }
        /** @brief 유대 단계 사이 경험치 표(기반 `ExperienceCurve`) — 레벨 L 은 `getBondLevels()[L − 1]` 단계입니다. */
        const ExperienceCurve&         getBondCurve() const { return _bondCurve; }
        const WesternBondExperience&   getBondExperience() const { return _bondExperience; }
        const WesternSurvivalSettings& getSurvival() const { return _survival; }
        /** @brief 명예 세력 하나가 든 평판 카탈로그입니다(`ReputationState` 가 빌려 쓴다). */
        const ReputationCatalog& getHonorReputation() const { return _honorReputation; }
        /** @brief 등급(0..3)의 값 배율입니다. */
        float32       getGradeScale( int32 stars ) const;
        int32         getExtraHitPenalty() const { return _extraHitPenalty; }
        hashed_string getCurrency() const { return _currency; }

    private:
        static constexpr const utf8* kXmlRootName = "WesternCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );
        void                         loadHonor( const XmlNode& node );

        GameCatalog<WesternCrimeDef>       _crimeCatalog;
        GameCatalog<WesternRegionDef>      _regionCatalog;
        GameCatalog<WesternHorseDef>       _horseCatalog;
        GameCatalog<WesternFoodDef>        _foodCatalog;
        GameCatalog<WesternClothingDef>    _clothingCatalog;
        GameCatalog<WesternAnimalDef>      _animalCatalog;
        GameCatalog<WesternHuntWeaponDef>  _huntWeaponCatalog;
        GameCatalog<WesternHitZoneDef>     _hitZoneCatalog;
        GameCatalog<WesternHonorActionDef> _honorActionCatalog;
        vector<WesternHonorTierDef>        _listHonorTier;    ///< 낮은 값부터
        vector<WesternPursuitDef>          _listPursuit;      ///< 낮은 단계부터
        vector<WesternBondLevelDef>        _listBondLevel;    ///< 낮은 단계부터
        vector<WesternDeadEyeLevelDef>     _listDeadEyeLevel; ///< 낮은 단계부터
        vector<float32>                    _listGradeScale;   ///< 등급 0..3
        ReputationCatalog                  _honorReputation;
        WesternBondExperience              _bondExperience;
        ExperienceCurve                    _bondCurve; ///< 단계 사이 경험치(정렬한 `_listBondLevel` 의 문턱 차이)
        WesternSurvivalSettings            _survival;
        hashed_string                      _currency;
        int32                              _extraHitPenalty; ///< 첫 발 뒤 한 발마다 떨어지는 등급
    };
} // namespace sw
