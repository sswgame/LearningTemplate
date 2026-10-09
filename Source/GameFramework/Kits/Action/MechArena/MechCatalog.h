/**
 * @file MechCatalog.h
 * @brief 기체 대전의 데이터 — 기체(근 · 중 · 원거리 분류 · 랭크 · 코스트 · 체력 · 속도 · 부스트 · 다운치 · 록온), 형태(변형) · 무기 칸, 분류 보정, 스킬입니다.
 * @details 주무기의 탄창 · 재장전 · 피해는 기반 `WeaponCatalog`, 근접 콤보 한 단 한 단의 발생 · 경직 · 캔슬 창은 기반 `MoveCatalog` 의 id 로 가리킵니다 —
 *          여기는 "어느 기체가 어느 무기 · 기술을 몇 단으로 드는가" 와 기체 대전에만 있는 수치(다운치 · 유도 · 넉백 · 쿨다운)만 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 기체의 거리 분류입니다(캡슐파이터의 근거리 · 중거리 · 원거리). 분류마다 보정(`MechCatalog::getClassModifier`)이 붙습니다. */
    enum class MechRangeClass : uint8
    {
        Near = 0,
        Mid,
        Far,
        Count
    };

    /** @brief 무기 칸의 종류입니다. */
    enum class MechWeaponKind : uint8
    {
        Shot = 0, ///< 주무기 — 기반 `WeaponDef`(탄창 · 재장전 · 연사)를 쓴다
        Melee,    ///< 근접 — `MoveCatalog` 기술 목록이 콤보 단(1 단, 2 단, ...)
        Special   ///< 특수기 — 칸 자체의 피해 · 탄속 · 쿨다운
    };

    /** @brief 스킬이 켜지는 조건입니다. */
    enum class MechSkillTrigger : uint8
    {
        Manual = 0,  ///< 버튼으로 켠다
        HealthBelow, ///< 체력 비율이 `_threshold` 아래로 내려가면(한 목숨에 한 번)
        OnDown,      ///< 다운되면
        OnRespawn    ///< 다시 나올 때
    };

    /** @brief 무기 칸 하나입니다. 시간은 초, 거리는 m, 각속도는 도/초입니다. */
    struct MechWeaponSlotDef
    {
        hashed_string         _id{};
        hashed_string         _weaponId{};              ///< Shot — `WeaponCatalog` 의 id
        vector<hashed_string> _listMoveId{};            ///< Melee — 콤보 단마다 `MoveCatalog` 의 id(개수 = 콤보 단수)
        float32               _damage{ 0.0f };          ///< Special 의 피해(Shot 은 무기, Melee 는 기술의 피해)
        float32               _downValue{ 10.0f };      ///< 한 번 맞힐 때 쌓는 다운치
        float32               _knockback{ 0.0f };       ///< 맞은 쪽을 밀어내는 거리
        float32               _range{ 0.0f };           ///< Melee 의 닿는 거리 · Special 의 사거리
        float32               _projectileSpeed{ 0.0f }; ///< Special 의 탄속(0 = 즉시 — 광선)
        float32               _homing{ 0.0f };          ///< 록온 대상으로 꺾는 최대 각속도(0 = 곧게)
        float32               _cooldown{ 0.0f };        ///< Special 의 재사용 대기
        float32               _staggerTime{ 0.2f };     ///< Shot · Special 이 거는 경직(Melee 는 기술의 hitstun)
        MechWeaponKind        _kind{ MechWeaponKind::Shot };
    };
} // namespace sw

namespace sw
{
    /** @brief 형태 하나(MS · 비행형 등)입니다. 변형하면 속도 · 무기 세트가 통째로 바뀝니다. */
    struct MechModeDef
    {
        hashed_string             _id{};
        vector<MechWeaponSlotDef> _listWeapon{};
        float32                   _speed{ 8.0f };
        float32                   _boostCostScale{ 1.0f }; ///< 이 형태에서 부스트 소비 배율
    };
} // namespace sw

namespace sw
{
    /** @brief 기체 하나입니다. */
    struct SW_GF_API MechDef
    {
        hashed_string       _id{};
        string              _name{};
        hashed_string       _rank{}; ///< "S" · "A" · "B" · "C" — 게임이 정한 표기
        vector<MechModeDef> _listMode{};
        float32             _maxHealth{ 1000.0f };
        float32             _radius{ 1.0f };
        float32             _dashSpeed{ 22.0f };
        float32             _dashTime{ 0.3f };
        float32             _jumpSpeed{ 12.0f };
        float32             _boostMax{ 100.0f };
        float32             _boostRegen{ 40.0f };     ///< 초당 식는(회복하는) 양
        float32             _boostRegenDelay{ 0.5f }; ///< 마지막으로 쓴 뒤 이만큼 지나야 회복
        float32             _overheatPenalty{ 1.0f }; ///< 다 쓰면(오버히트) 이만큼 더 기다린 뒤 식기 시작
        float32             _overheatRecover{ 0.0f }; ///< 열이 이 값까지 내려가야 다시 쓸 수 있다(0 = 다 식어야)
        float32             _dashCost{ 25.0f };
        float32             _jumpCost{ 15.0f };
        float32             _hoverPerSecond{ 30.0f };   ///< 공중 부유의 초당 소비
        float32             _downMax{ 100.0f };         ///< 다운치 한도(기반 `Vitality` 의 poise)
        float32             _downRecovery{ 25.0f };     ///< 초당 빠지는 다운치
        float32             _downRecoveryDelay{ 1.5f }; ///< 마지막으로 맞은 뒤 이만큼 지나야 빠진다
        float32             _downTime{ 1.5f };          ///< 다운(누움) 시간
        float32             _wakeInvulnerable{ 1.5f };  ///< 일어난 뒤 무적
        float32             _lockOnRange{ 60.0f };
        float32             _lockOnAngle{ 60.0f };
        float32             _transformTime{ 1.0f }; ///< 변형 뒤 다시 변형할 수 있기까지
        int32               _cost{ 300 };
        int32               _skillSlots{ 2 };
        MechRangeClass      _rangeClass{ MechRangeClass::Mid };

        /** @brief 형태 @p mode 의 무기 칸이 기체 전체 칸 목록에서 시작하는 자리입니다(형태마다 탄창 상태를 따로 둔다). */
        int32 computeSlotOffset( int32 mode ) const;
        /** @brief 모든 형태의 무기 칸 수입니다. */
        int32 computeSlotCount() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 스킬 하나입니다. `_modifier` 는 "attack" · "defense" · "speed" · "boostRegen" · "downResist" 같은 이름 → 배율(곱)입니다. */
    struct MechSkillDef
    {
        hashed_string    _id{};
        StatBlock        _modifier{};
        float32          _threshold{ 0.3f }; ///< HealthBelow 의 체력 비율
        float32          _duration{ 10.0f };
        float32          _cooldown{ 0.0f };
        MechSkillTrigger _trigger{ MechSkillTrigger::Manual };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MechCatalog
     * @brief `<MechCatalog deckCostLimit="1200"><Class id="Near" melee="1.2" shot="0.9"/><Mech id=".." class="Near" rank="S" cost="400" ...>
     *        <Mode id="ms" speed="9"><Weapon id="rifle" kind="Shot" weapon="beam_rifle" down="30" homing="90"/>
     *        <Weapon id="saber" kind="Melee" moves="slash1,slash2,slash3" range="4"/></Mode></Mech><Skill id=".." trigger="HealthBelow" attack="1.2"/></MechCatalog>` 를 읽습니다.
     * @details `<Mode>` 없이 `<Weapon>` 이 `<Mech>` 바로 아래에 있으면 형태 하나("default")로 읽습니다. 분류 보정 이름은 "melee" · "shot" · "down"(받는 다운치 배율)입니다.
     */
    class SW_GF_API MechCatalog : public XmlCatalog<MechCatalog>
    {
        friend class XmlCatalog<MechCatalog>;

    public:
        MechCatalog();

        const MechDef*              findMech( const hashed_string& id ) const { return _catalogMech.find( id ); }
        int32                       findMechIndex( const hashed_string& id ) const { return _catalogMech.findIndex( id ); }
        const vector<MechDef>&      getMechs() const { return _catalogMech.getAll(); }
        const MechSkillDef*         findSkill( const hashed_string& id ) const { return _catalogSkill.find( id ); }
        const vector<MechSkillDef>& getSkills() const { return _catalogSkill.getAll(); }
        /** @brief 분류 보정입니다(없는 이름은 1 로 읽는다). */
        const StatBlock& getClassModifier( MechRangeClass rangeClass ) const;
        /** @brief 한 조종사가 들고 나오는 기체들 코스트 합의 상한입니다(0 = 없음). */
        int32 getDeckCostLimit() const { return _deckCostLimit; }

        /** @brief "Near" · "Mid" · "Far"(대소문자 무시)를 읽습니다. 모르면 @p fallback 입니다. */
        static MechRangeClass parseRangeClass( string_view text, MechRangeClass fallback );

    private:
        static constexpr const utf8* kXmlRootName = "MechCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<MechDef>      _catalogMech;
        GameCatalog<MechSkillDef> _catalogSkill;
        StatBlock                 _arrClassModifier[static_cast<size_t>( MechRangeClass::Count )];
        int32                     _deckCostLimit;
    };
} // namespace sw
