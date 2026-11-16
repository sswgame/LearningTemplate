/**
 * @file MechCatalog.h
 * @brief 기체 대전의 데이터입니다. 기체(근, 중, 원거리 분류, 랭크, 코스트, 체력, 속도, 부스트, 다운치, 록온), 형태(변형)와 무기 슬롯, 분류 보정, 스킬을 담습니다.
 * @details 주무기의 탄창, 재장전은 기반 `WeaponCatalog`, 주무기 · 특수기 탄의 피해 · 탄속 · 사거리는 기반 `ProjectileCatalog` 에 있고, 근접 콤보 단마다의 발생, 경직, 캔슬 창은 기반 `MoveCatalog` 의 id 로 가리킨다.
 *          여기에는 "어느 기체가 어느 무기와 기술을 몇 단으로 가지는가"와 기체 대전에만 있는 수치(다운치, 유도, 넉백, 쿨다운)만 둔다.
 *          정의 구조체는 리플렉션 스키마다 — 기본값은 초기값, 범위는 `PROPERTY( Min · Max )`, 필수는 `Required` 이고 `XMLFields::readProperties` 가 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /** @brief 기체의 거리 분류입니다(캡슐파이터의 근거리, 중거리, 원거리). 분류마다 보정(`MechCatalog::getClassModifier`)이 붙습니다. */
    ENUM()
    enum class MechRangeClass : uint8
    {
        Near = 0,
        Mid,
        Far
    };

    /** @brief 거리 분류의 수입니다(데이터에 적을 수 있는 이름이 아니라서 열거자로 두지 않는다). */
    static constexpr size_t kMechRangeClassCount = 3;

    /** @brief 무기 슬롯의 종류입니다. */
    ENUM()
    enum class MechWeaponKind : uint8
    {
        Shot = 0, ///< 주무기이며 기반 `WeaponDef`(탄창, 재장전, 연사)를 쓴다
        Melee,    ///< 근접이며 `MoveCatalog` 기술 목록이 콤보 단이 된다(1단, 2단, ...)
        Special   ///< 특수기이며 슬롯의 탄(`ProjectileCatalog`)을 쿨다운마다 한 발 쏜다
    };

    /** @brief 스킬이 켜지는 조건입니다. */
    ENUM()
    enum class MechSkillTrigger : uint8
    {
        Manual = 0,  ///< 버튼으로 켠다
        HealthBelow, ///< 체력 비율이 `_threshold` 아래로 내려가면 켠다(한 목숨에 한 번)
        OnDown,      ///< 다운되면 켠다
        OnRespawn    ///< 다시 나올 때 켠다
    };

    /**
     * @brief 무기 슬롯 하나입니다. 시간은 초, 거리는 m, 각속도는 도/초입니다. Melee 는 `range` 를, Special 은 `projectile` 을 반드시 적습니다.
     * @details 날아가는 것(피해 · 탄속 · 사거리 · 중력)은 탄 정의(Shot 은 무기의 기본 탄, Special 은 `projectile`)이고, 슬롯은 맞은 뒤의 반응(다운치 · 넉백 · 경직)과 쏘는 법(유도 더하기 · 쿨다운)만 가진다.
     */
    REFLECT()
    struct MechWeaponSlotDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "weapon" )
        hashed_string _weaponID{}; ///< Shot 일 때 `WeaponCatalog` 의 id
        PROPERTY( Name = "projectile" )
        hashed_string _projectileID{}; ///< Special 일 때 `ProjectileCatalog` 의 id(탄속 0 이면 사거리 안을 바로 맞히는 광선)
        PROPERTY( Name = "moves" )
        vector<hashed_string> _listMoveID{}; ///< Melee 일 때 콤보 단마다의 `MoveCatalog` id(개수가 콤보 단수다, 속성 `moves`)
        PROPERTY( Name = "down", Min = 0.0 )
        float32 _downValue{ 10.0f }; ///< 한 번 맞힐 때 쌓는 다운치
        PROPERTY( Min = 0.0 )
        float32 _knockback{ 0.0f }; ///< 맞은 쪽을 밀어내는 거리
        PROPERTY( Min = 0.0 )
        float32 _range{ 0.0f }; ///< Melee 의 닿는 거리(Shot · Special 은 탄의 사거리를 쓴다)
        PROPERTY( Min = 0.0 )
        float32 _homing{ 0.0f }; ///< 록온 대상 쪽으로 꺾는 최대 각속도(0이면 곧게 간다). Shot · Special 모두 이 값을 탄의 유도(`ProjectileDef::_homingRate`)에 더한다
        PROPERTY( Min = 0.0 )
        float32 _cooldown{ 0.0f }; ///< Special 의 재사용 대기 시간
        PROPERTY( Name = "stagger", Min = 0.0 )
        float32 _staggerTime{ 0.2f }; ///< Shot 과 Special 이 거는 경직(Melee 는 기술의 hitstun 을 쓴다)
        PROPERTY()
        MechWeaponKind _kind{ MechWeaponKind::Shot };
    };
} // namespace sw

namespace sw
{
    /** @brief 형태 하나(MS, 비행형 등)입니다. 변형하면 속도와 무기 세트가 통째로 바뀝니다. */
    REFLECT()
    struct MechModeDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string             _id{};
        vector<MechWeaponSlotDef> _listWeapon{};
        PROPERTY( Min = 0.0 )
        float32 _speed{ 8.0f };
        PROPERTY( Min = 0.0 )
        float32 _boostCostScale{ 1.0f }; ///< 이 형태에서 부스트를 소비하는 배율
    };
} // namespace sw

namespace sw
{
    /** @brief 기체 하나입니다. */
    REFLECT()
    struct SW_KIT_API MechDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY()
        hashed_string       _rank{}; ///< "S", "A", "B", "C" 같은 랭크 표기(게임이 정한다)
        vector<MechModeDef> _listMode{};
        PROPERTY( Name = "hp", Min = 1.0 )
        float32 _maxHealth{ 1000.0f };
        PROPERTY( Min = 0.1 )
        float32 _radius{ 1.0f };
        PROPERTY( Min = 0.0 )
        float32 _dashSpeed{ 22.0f };
        PROPERTY( Min = 0.0 )
        float32 _dashTime{ 0.3f };
        PROPERTY( Min = 0.0 )
        float32 _jumpSpeed{ 12.0f };
        PROPERTY( Name = "boost", Min = 1.0 )
        float32 _boostMax{ 100.0f };
        PROPERTY( Min = 0.0 )
        float32 _boostRegen{ 40.0f }; ///< 초당 식는(회복하는) 양
        PROPERTY( Min = 0.0 )
        float32 _boostRegenDelay{ 0.5f }; ///< 마지막으로 쓴 뒤 이 시간이 지나야 회복한다
        PROPERTY( Min = 0.0 )
        float32 _overheatPenalty{ 1.0f }; ///< 다 쓰면(오버히트) 이 시간만큼 더 기다린 뒤 식기 시작한다
        PROPERTY( Min = 0.0, Max = "_boostMax" )
        float32 _overheatRecover{ 0.0f }; ///< 열이 이 값까지 내려가야 다시 쓸 수 있다(0이면 다 식어야 한다)
        PROPERTY( Min = 0.0 )
        float32 _dashCost{ 25.0f };
        PROPERTY( Min = 0.0 )
        float32 _jumpCost{ 15.0f };
        PROPERTY( Min = 0.0 )
        float32 _hoverPerSecond{ 30.0f }; ///< 공중 부유의 초당 소비량
        PROPERTY( Name = "down", Min = 0.0 )
        float32 _downMax{ 100.0f }; ///< 다운치 한도(기반 `HealthStateMachine` 의 poise)
        PROPERTY( Min = 0.0 )
        float32 _downRecovery{ 25.0f }; ///< 초당 빠지는 다운치
        PROPERTY( Min = 0.0 )
        float32 _downRecoveryDelay{ 1.5f }; ///< 마지막으로 맞은 뒤 이 시간이 지나야 다운치가 빠진다
        PROPERTY( Min = 0.0 )
        float32 _downTime{ 1.5f }; ///< 다운(누움) 시간
        PROPERTY( Min = 0.0 )
        float32 _wakeInvulnerable{ 1.5f }; ///< 일어난 뒤의 무적 시간
        PROPERTY( Name = "lockOn", Min = 0.0 )
        float32 _lockOnRange{ 60.0f };
        PROPERTY( Min = 0.0175, Max = 3.1416, Units = "rad" )
        float32 _lockOnAngle{ 1.0471976f }; ///< 락온 원뿔의 각(라디안, 60°)
        PROPERTY( Min = 0.0 )
        float32 _transformTime{ 1.0f }; ///< 변형한 뒤 다시 변형할 수 있을 때까지의 시간
        PROPERTY( Min = 0 )
        int32 _cost{ 300 };
        PROPERTY( Min = 0 )
        int32 _skillSlots{ 2 };
        PROPERTY( Name = "class" )
        MechRangeClass _rangeClass{ MechRangeClass::Mid };

        /** @brief 형태 @p mode 의 무기 슬롯이 기체 전체 슬롯 목록에서 시작하는 인덱스를 반환합니다(형태마다 탄창 상태를 따로 둡니다). */
        int32 computeSlotOffset( int32 mode ) const;
        /** @brief 모든 형태의 무기 슬롯 수를 반환합니다. */
        int32 computeSlotCount() const;
        /** @brief 기체 전체 슬롯 목록의 @p slotIndex 번째 슬롯입니다. 범위 밖이면 nullptr 입니다. */
        const MechWeaponSlotDef* findSlot( int32 slotIndex ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 스킬 하나입니다. `_modifier` 는 `<Stats attack="1.2"/>` 처럼 "attack", "defense", "speed", "boostRegen", "downResist" 같은 이름과 배율(곱)을 짝지은 것입니다. */
    REFLECT()
    struct MechSkillDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        StatBlock     _modifier{};
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _threshold{ 0.3f }; ///< HealthBelow 의 체력 비율
        PROPERTY( Min = 0.0 )
        float32 _duration{ 10.0f };
        PROPERTY( Min = 0.0 )
        float32 _cooldown{ 0.0f };
        PROPERTY()
        MechSkillTrigger _trigger{ MechSkillTrigger::Manual };
    };
} // namespace sw

namespace sw
{
    /** @brief 루트 `<MechCatalog>` 의 속성입니다. */
    REFLECT()
    struct MechCatalogRootDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0 )
        int32 _deckCostLimit{ 0 }; ///< 0이면 제한 없음
    };
} // namespace sw

namespace sw
{
    /**
     * @class MechCatalog
     * @brief 다음 형식의 XML 을 읽는 카탈로그입니다. `<MechCatalog deckCostLimit="1200"><Class id="Near" melee="1.2" shot="0.9"/><Mech id=".." name=".." class="Near" rank="S" cost="400" ...>
     *        <Mode id="ms" speed="9"><Weapon id="rifle" kind="Shot" weapon="beam_rifle" down="30" homing="90"/>
     *        <Weapon id="saber" kind="Melee" moves="slash1,slash2,slash3" range="4"/></Mode></Mech><Skill id=".." trigger="HealthBelow"><Stats attack="1.2"/></Skill></MechCatalog>`
     * @details `<Mode>` 없이 `<Weapon>` 이 `<Mech>` 바로 아래에 있으면 형태 하나("default")로 읽고, 그 형태의 속도는 `<Mech speed>` 다(`<Mode>` 가 있으면 `<Mech speed>` 는 오류다).
     *          분류 보정 이름은 "melee", "shot", "down"(받는 다운치 배율)이다. 스키마를 어긴 원소와 겹친 id 는 넣지 않고 오류로 남긴다. 루트 속성이 틀리면 파일을 읽지 않는다.
     */
    class SW_KIT_API MechCatalog : public XMLCatalog<MechCatalog>
    {
        friend class XMLCatalog<MechCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<MechCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".mechs.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `MechCatalog.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "MechCatalog", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        MechCatalog();

        const MechDef*                findMech( const hashed_string& id ) const { return _catalogMech.find( id ); }
        [[nodiscard]] optional<int32> findMechIndex( const hashed_string& id ) const { return _catalogMech.findIndex( id ); }
        const vector<MechDef>&        getMechs() const { return _catalogMech.getAll(); }
        const MechSkillDef*           findSkill( const hashed_string& id ) const { return _catalogSkill.find( id ); }
        const vector<MechSkillDef>&   getSkills() const { return _catalogSkill.getAll(); }
        /** @brief 분류 보정을 반환합니다(없는 이름은 1로 읽습니다). */
        const StatBlock& getClassModifier( MechRangeClass rangeClass ) const;
        /** @brief 한 조종사가 들고 나오는 기체들의 코스트 합 상한입니다(0이면 제한 없음). */
        int32 getDeckCostLimit() const { return _deckCostLimit; }

        /** @brief "Near", "Mid", "Far"(대소문자 무시)를 읽습니다. 모르는 이름이면 false 이고 @p outRangeClass 를 바꾸지 않습니다. */
        [[nodiscard]] static bool parseRangeClass( string_view text, MechRangeClass& outRangeClass );

    private:
        static constexpr const utf8* kXMLRootName = "MechCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadClass( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadMech( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadSkill( const XMLNode& node, string_view sourceName );

    private:
        GameCatalog<MechDef>      _catalogMech;
        GameCatalog<MechSkillDef> _catalogSkill;
        StatBlock                 _arrClassModifier[kMechRangeClassCount];
        int32                     _deckCostLimit;
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<MechCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, MechCatalog );
} // namespace sw
