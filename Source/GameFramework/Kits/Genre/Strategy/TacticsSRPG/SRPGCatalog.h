/**
 * @file SRPGCatalog.h
 * @brief 택틱스 SRPG 의 데이터를 담습니다. 지형(이동 타입별 비용, 방어, 회피), 무기(사거리, 위력, 명중, EN, 탄수, MAP 병기, 격투와 사격, 반격),
 *        기체(HP, EN, 이동력, 이동 타입, 지형 적성, 크기, 무기, 개발 트리), 파일럿(능력치, 성장, 경험치 곡선)을 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Runtime/Gameplay/Progression/LevelProgress.h"

namespace sw
{
    class XMLNode;

    /** @brief 유닛 크기입니다. 클수록 맞히기 쉽습니다(값의 차이가 명중 보정이다). */
    ENUM()
    enum class SRPGUnitSize : int32
    {
        S = 0,
        M,
        L,
        LL
    };

    /** @brief 이동 타입입니다. 지형의 이동 비용과 적성은 이 순서("지상 공중 우주 수중")로 적습니다. */
    ENUM()
    enum class SRPGMoveKind : uint8
    {
        Ground = 0,
        Air,
        Space,
        Water
    };

    /** @brief 무기가 사용하는 파일럿 능력치입니다. */
    ENUM()
    enum class SRPGWeaponKind : uint8
    {
        Shooting = 0, ///< 사격
        Melee,        ///< 격투
        Awaken        ///< 각성(뉴타입, 핀 판넬 류)이다. 기력 조건을 함께 둔다
    };

    /** @brief MAP 병기의 범위를 어디에 놓는지 나타냅니다. */
    ENUM()
    enum class SRPGMapAnchor : uint8
    {
        None = 0, ///< MAP 병기가 아니다
        Self,     ///< 쏘는 쪽이 기준이다. 패턴은 동쪽(+x)을 보는 모양으로 적고 겨눈 방향으로 돌린다(빔 일직선, 부채꼴)
        Target    ///< 겨눈 셀이 기준이다. 사거리 안의 셀을 골라 그 둘레를 친다(폭격, 미사일 일제사)
    };

    /** @brief 파일럿 능력치의 종류입니다. */
    enum class SRPGPilotStat : uint8
    {
        Shooting = 0,
        Melee,
        Reaction, ///< 반응이다. 명중과 회피에 쓴다
        Awaken,   ///< 각성이다. 각성 무기의 위력과 크리티컬에 쓴다
        Defense,  ///< 수비이다. 받는 피해를 줄인다
        Count
    };

    /** @brief 이동 타입의 개수입니다. */
    static constexpr int32 kSRPGMoveTypeCount = 4;
    /** @brief 파일럿 능력치의 개수입니다. */
    static constexpr int32 kSRPGPilotStatCount = static_cast<int32>( SRPGPilotStat::Count );

    /** @brief 지형 하나입니다. */
    REFLECT()
    struct SRPGTerrainDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        int32  _arrMoveCost[kSRPGMoveTypeCount]{ 1, 1, 1, 1 }; ///< 이동 타입별로 들어가는 비용이다. 음수이면 들어갈 수 없다
        PROPERTY( Name = "defense" )
        int32 _defenseBonus{ 0 }; ///< 받는 쪽의 방어력 보정(%)
        PROPERTY( Name = "evasion" )
        int32 _evasionBonus{ 0 }; ///< 받는 쪽의 회피 보정(명중률에서 빼는 %p)
        PROPERTY()
        SRPGMoveKind _domain{ SRPGMoveKind::Ground }; ///< 이 셀에 선 유닛이 쓰는 적성이다(공중 유닛은 지상과 수중 위에서 공중 적성을 쓴다)
    };
} // namespace sw

namespace sw
{
    /** @brief 무기 하나입니다. */
    REFLECT()
    struct SRPGWeaponDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string       _name{};
        vector<int2> _listMapOffset{}; ///< MAP 병기의 범위 패턴이다(기준 셀에서의 상대 위치)
        PROPERTY( Min = 0 )
        int32 _minRange{ 1 };
        PROPERTY( Min = "_minRange" )
        int32 _maxRange{ 1 };
        PROPERTY( Min = 0 )
        int32 _power{ 1000 };
        PROPERTY( Name = "hit" )
        int32 _hitBonus{ 0 }; ///< 명중 보정(%p)
        PROPERTY( Name = "crit" )
        int32 _critBonus{ 0 }; ///< 크리티컬 보정(%p)
        PROPERTY( Name = "en", Min = 0 )
        int32 _enCost{ 0 }; ///< 한 번 쓸 때 드는 EN
        PROPERTY( Min = 0 )
        int32 _ammo{ 0 }; ///< 탄수이다(0 이면 탄 없이 EN 만 쓴다)
        PROPERTY( Name = "morale", Min = 0 )
        int32 _moraleRequired{ 0 }; ///< 이 기력 이상이어야 쓸 수 있다
        PROPERTY()
        SRPGWeaponKind _kind{ SRPGWeaponKind::Shooting };
        PROPERTY( Name = "map" )
        SRPGMapAnchor _mapAnchor{ SRPGMapAnchor::None };
        PROPERTY()
        bool _bCounter{ true }; ///< 반격에 쓸 수 있다
        PROPERTY()
        bool _bPostMove{ true }; ///< 이동한 뒤에도 쓸 수 있다(G 제네레이션의 P 무기)

        bool isMap() const { return _mapAnchor != SRPGMapAnchor::None; }
        bool usesAmmo() const { return _ammo > 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 개발 분기 하나입니다. 기체 레벨이 @p _requiredLevel 이상이면 @p _unitID 로 바꿀 수 있습니다. */
    struct SRPGDevelopTarget
    {
        hashed_string _unitID{};
        int32         _requiredLevel{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 기체(유닛) 하나입니다. */
    REFLECT()
    struct SRPGUnitDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Name = "weapons" )
        vector<hashed_string>     _listWeaponID{};
        vector<SRPGDevelopTarget> _listDevelop{};
        PROPERTY( Min = 1 )
        int32 _hp{ 3000 };
        PROPERTY( Min = 0 )
        int32 _en{ 100 };
        PROPERTY( Min = 0 )
        int32 _move{ 5 };
        PROPERTY( Min = 0 )
        int32 _armor{ 500 }; ///< 장갑이다. 받는 피해에서 빠진다
        PROPERTY()
        int32 _mobility{ 0 }; ///< 운동성이다. 회피에 쓴다(명중률에서 빼는 %p)
        PROPERTY()
        SRPGUnitSize _size{ SRPGUnitSize::M };
        int32        _arrAptitude[kSRPGMoveTypeCount]{ 100, 100, 100, 100 }; ///< 지형 적성(%)이다. 0 이면 그 영역에 들어갈 수 없다
        PROPERTY()
        SRPGMoveKind _moveType{ SRPGMoveKind::Ground };
    };
} // namespace sw

namespace sw
{
    /** @brief 파일럿 하나입니다. 레벨 L 의 능력치는 `기본 + 성장 x (L - 1)` 입니다. */
    REFLECT()
    struct SW_KIT_API SRPGPilotDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        int32  _arrStat[kSRPGPilotStatCount]{ 10, 10, 10, 0, 10 };
        int32  _arrGrowth[kSRPGPilotStatCount]{ 1, 1, 1, 0, 1 };

        int32 computeStat( SRPGPilotStat stat, int32 level ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 차례 방식입니다. */
    ENUM()
    enum class SRPGTurnMode : uint8
    {
        Phases = 0, ///< 아군, 적, 제3세력 페이즈 순서로 진행하고, 팀의 모든 유닛이 행동하면 다음 페이즈로 넘긴다(G 제네레이션, 파이어 엠블렘)
        Individual  ///< 유닛마다 반응과 운동성 순서로 진행한다(`TurnOrder` 라운드제)
    };

    /**
     * @brief 규칙의 수치입니다. 확률은 %, 보정은 %p 입니다.
     * @details 규칙 자산이다 — SRPG 카탈로그의 `<Rules …/>` 한 줄이 모든 칸을 적는다(`Required`, 없으면 카탈로그 로드가 실패한다). 전장은 카탈로그에서만 받으므로
     *          게임이 C++ 기본값으로 전장을 열 길이 없다(감사 K-4, Lyra `ULyraExperienceDefinition` · 유니티 `ScriptableObject` 와 같은 자리).
     */
    REFLECT()
    struct SW_KIT_API SRPGSettings
    {
        REFLECT_BODY();
        PROPERTY( Required, Min = 0, Max = 100 )
        int32 _baseHit{};
        PROPERTY( Required, Min = 0, Max = 100 )
        int32 _baseCrit{};
        PROPERTY( Required, Min = 100 )
        int32 _critPercent{}; ///< 크리티컬 피해 배율(%)
        PROPERTY( Required, Min = 0 )
        int32 _minimumDamage{};
        PROPERTY( Required, Min = 0 )
        int32 _sizeHitStep{}; ///< 크기가 한 단계 차이 날 때마다 주는 명중 보정
        PROPERTY( Required, Min = 0 )
        int32 _moraleStart{};
        PROPERTY( Required, Min = 0 )
        int32 _moraleMin{};
        PROPERTY( Required, Min = 0 )
        int32 _moraleMax{};
        PROPERTY( Required, Min = 0 )
        int32 _moraleOnHit{}; ///< 맞혔을 때
        PROPERTY( Required, Min = 0 )
        int32 _moraleOnKill{}; ///< 격파했을 때(맞혔을 때의 증가에 더해진다)
        PROPERTY( Required, Min = 0 )
        int32 _moraleOnEvade{}; ///< 피했을 때
        PROPERTY( Required, Min = 0 )
        int32 _moraleOnDamaged{}; ///< 맞았을 때
        PROPERTY( Required, Min = 0, Max = 100 )
        int32 _supportDefenseReduction{}; ///< 지원 방어로 대신 맞을 때 줄어드는 피해 비율(%)
        PROPERTY( Required, Min = 0 )
        int32 _syncDamagePercent{}; ///< 동기 공격이 주는 피해 비율(%)
        PROPERTY( Required, Min = 0 )
        int32 _dodgePerCell{}; ///< 이동 회피에서 움직인 셀마다 쌓이는 회피(%p)
        PROPERTY( Required, Min = 0, Max = 100 )
        int32 _dodgeMax{};
        PROPERTY( Required, Min = 0 )
        int32 _xpHit{}; ///< 맞혔을 때 얻는 경험치
        PROPERTY( Required, Min = 0 )
        int32 _xpKill{}; ///< 격파했을 때 얻는 경험치(맞혔을 때의 경험치에 더해진다)
        PROPERTY( Required, Min = 0 )
        int32 _xpLevelStep{}; ///< 상대와 레벨이 한 단계 차이 날 때마다 더하거나 빼는 경험치
        PROPERTY( Required )
        SRPGTurnMode _turnMode{};
        PROPERTY( Required )
        bool _bZoneOfControl{}; ///< 적과 이웃한 셀에 들어가면 거기서 멈춘다
        PROPERTY( Required )
        bool _bSupportAttack{}; ///< 공격자와 이웃한 아군이 함께 친다
        PROPERTY( Required )
        bool _bSupportDefense{}; ///< 방어자와 이웃한 아군이 대신 맞는다
        PROPERTY( Required )
        bool _bSyncAttack{}; ///< 메탈슬러그 택틱스 방식이다. 같은 적을 사거리에 둔 아군이 함께 쏜다
        PROPERTY( Required )
        bool _bMoveDodge{}; ///< 메탈슬러그 택틱스 방식이다. 움직인 셀 수만큼 회피가 쌓이고, 자기 차례가 오면 0 이 된다
        PROPERTY( Required )
        bool _bMapFriendlyFire{}; ///< MAP 병기가 아군도 친다
    };
} // namespace sw

namespace sw
{
    /**
     * @class SRPGCatalog
     * @brief `<SRPGCatalog><Rules baseHit="70" .../><PilotCurve base="100" maxLevel="30"/><UnitCurve .../><Terrain .../><Weapon .../><Unit .../><Pilot .../></SRPGCatalog>` 를 읽습니다.
     * @details 속성 형식은 다음과 같다. 지형은 `cost="1 1 - 2"`(지상 공중 우주 수중 순서, `-` 는 들어갈 수 없음), 기체는 `aptitude="A B - C"`(S 120, A 100, B 90, C 80, D 60, `-` 는 0),
     *          `weapons="vulcan,rifle"`, `developsTo="gundam:5 gmcustom:3"`, 무기는 `map="Self" pattern="1,0 2,0 3,0"`, 파일럿은 `shooting="20" growShooting="2"` 이다.
     *          경험치 곡선은 기반 클래스 `ExperienceCurve` 의 속성을 그대로 쓴다.
     */
    class SW_KIT_API SRPGCatalog : public XMLCatalog<SRPGCatalog>
    {
        friend class XMLCatalog<SRPGCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<SRPGCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".srpg.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `SRPGCatalog.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "SRPGCatalog", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };

        SRPGCatalog();

        const SRPGTerrainDef*      findTerrain( const hashed_string& id ) const { return _terrainCatalog.find( id ); }
        const SRPGWeaponDef*       findWeapon( const hashed_string& id ) const { return _weaponCatalog.find( id ); }
        const SRPGUnitDef*         findUnit( const hashed_string& id ) const { return _unitCatalog.find( id ); }
        const SRPGPilotDef*        findPilot( const hashed_string& id ) const { return _pilotCatalog.find( id ); }
        const vector<SRPGUnitDef>& getUnits() const { return _unitCatalog.getAll(); }
        const ExperienceCurve&     getPilotCurve() const { return _pilotCurve; }
        const ExperienceCurve&     getUnitCurve() const { return _unitCurve; }
        /** @brief 규칙(`<Rules>`)입니다. 로드에 성공한 카탈로그는 반드시 한 줄을 가진다. */
        const SRPGSettings& getRules() const { return _rules; }
        /** @brief 규칙을 바꿉니다. 테스트 · 도구가 읽은 규칙의 칸 하나를 바꿔 볼 때 쓴다(기본값은 없다 — `getRules` 로 받은 것을 고친다). */
        void setRules( const SRPGSettings& rules ) { _rules = rules; }

        /** @brief "S" "A" "B" "C" "D" "-" 또는 0 이상의 숫자를 적성 %로 바꿉니다. 모르는 글자 · 음수이면 false 이고 @p outAptitude 를 바꾸지 않습니다. */
        [[nodiscard]] static bool parseAptitude( string_view token, int32& outAptitude );

    private:
        static constexpr const utf8* kXMLRootName = "SRPGCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadTerrain( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadWeapon( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadUnit( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadPilot( const XMLNode& node, string_view sourceName );
        /** @brief `<Rules>` 한 줄을 읽습니다. 칸이 빠졌거나 범위 밖이거나 기력 순서(하한 <= 시작 <= 상한)가 틀리면 false 입니다. */
        [[nodiscard]] bool loadRules( const XMLNode& node, string_view sourceName );

    private:
        GameCatalog<SRPGTerrainDef> _terrainCatalog;
        GameCatalog<SRPGWeaponDef>  _weaponCatalog;
        GameCatalog<SRPGUnitDef>    _unitCatalog;
        GameCatalog<SRPGPilotDef>   _pilotCatalog;
        ExperienceCurve             _pilotCurve;
        ExperienceCurve             _unitCurve;
        SRPGSettings                _rules;
        int32                       _rulesCount; ///< 읽은 `<Rules>` 줄 수(1 이상이어야 로드가 성공한다)
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<SRPGCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, SRPGCatalog );
} // namespace sw
