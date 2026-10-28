/**
 * @file SRPGCatalog.h
 * @brief 택틱스 SRPG 의 데이터를 담습니다. 지형(이동 타입별 비용, 방어, 회피), 무기(사거리, 위력, 명중, EN, 탄수, MAP 병기, 격투와 사격, 반격),
 *        기체(HP, EN, 이동력, 이동 타입, 지형 적성, 크기, 무기, 개발 트리), 파일럿(능력치, 성장, 경험치 곡선)을 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 이동 타입입니다. 지형의 이동 비용과 적성은 이 순서("지상 공중 우주 수중")로 적습니다. */
    ENUM()
    enum class SRPGMoveType : uint8
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
        SRPGMoveType _domain{ SRPGMoveType::Ground }; ///< 이 셀에 선 유닛이 쓰는 적성이다(공중 유닛은 지상과 수중 위에서 공중 적성을 쓴다)
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
        string                    _name{};
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
        int32 _mobility{ 0 };                                         ///< 운동성이다. 회피에 쓴다(명중률에서 빼는 %p)
        int32 _size{ 1 };                                             ///< 0 은 S, 1 은 M, 2 는 L, 3 은 LL 이다. 클수록 맞히기 쉽다
        int32 _arrAptitude[kSRPGMoveTypeCount]{ 100, 100, 100, 100 }; ///< 지형 적성(%)이다. 0 이면 그 영역에 들어갈 수 없다
        PROPERTY()
        SRPGMoveType _moveType{ SRPGMoveType::Ground };
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
    /**
     * @class SRPGCatalog
     * @brief `<SRPGCatalog><PilotCurve base="100" maxLevel="30"/><UnitCurve .../><Terrain .../><Weapon .../><Unit .../><Pilot .../></SRPGCatalog>` 를 읽습니다.
     * @details 속성 형식은 다음과 같다. 지형은 `cost="1 1 - 2"`(지상 공중 우주 수중 순서, `-` 는 들어갈 수 없음), 기체는 `aptitude="A B - C"`(S 120, A 100, B 90, C 80, D 60, `-` 는 0),
     *          `weapons="vulcan,rifle"`, `developsTo="gundam:5 gmcustom:3"`, 무기는 `map="Self" pattern="1,0 2,0 3,0"`, 파일럿은 `shooting="20" growShooting="2"` 이다.
     *          경험치 곡선은 기반 클래스 `ExperienceCurve` 의 속성을 그대로 쓴다.
     */
    class SW_KIT_API SRPGCatalog : public XMLCatalog<SRPGCatalog>
    {
        friend class XMLCatalog<SRPGCatalog>;

    public:
        SRPGCatalog();

        const SRPGTerrainDef*      findTerrain( const hashed_string& id ) const { return _terrainCatalog.find( id ); }
        const SRPGWeaponDef*       findWeapon( const hashed_string& id ) const { return _weaponCatalog.find( id ); }
        const SRPGUnitDef*         findUnit( const hashed_string& id ) const { return _unitCatalog.find( id ); }
        const SRPGPilotDef*        findPilot( const hashed_string& id ) const { return _pilotCatalog.find( id ); }
        const vector<SRPGUnitDef>& getUnits() const { return _unitCatalog.getAll(); }
        const ExperienceCurve&     getPilotCurve() const { return _pilotCurve; }
        const ExperienceCurve&     getUnitCurve() const { return _unitCurve; }

        /** @brief "S" "A" "B" "C" "D" "-" 또는 0 이상의 숫자를 적성 %로 바꿉니다. 모르는 글자 · 음수이면 false 이고 @p outAptitude 를 바꾸지 않습니다. */
        [[nodiscard]] static bool parseAptitude( string_view token, int32& outAptitude );

    private:
        static constexpr const utf8* kXMLRootName = "SRPGCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief 원소 하나를 이름에 맞는 스키마로 읽습니다. 오류면 그 원소를 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadElement( const XMLNode& node, string_view name, string_view sourceName );
        [[nodiscard]] bool loadTerrain( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadWeapon( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadUnit( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadPilot( const XMLNode& node, string_view sourceName );

        GameCatalog<SRPGTerrainDef> _terrainCatalog;
        GameCatalog<SRPGWeaponDef>  _weaponCatalog;
        GameCatalog<SRPGUnitDef>    _unitCatalog;
        GameCatalog<SRPGPilotDef>   _pilotCatalog;
        ExperienceCurve             _pilotCurve;
        ExperienceCurve             _unitCurve;
    };
} // namespace sw
