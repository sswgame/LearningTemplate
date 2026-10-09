/**
 * @file SrpgCatalog.h
 * @brief 택틱스 SRPG 의 데이터 — 지형(이동 타입별 비용 · 방어 · 회피) · 무기(사거리 · 위력 · 명중 · EN · 탄수 · MAP 병기 · 격투/사격 · 반격) ·
 *        기체(HP · EN · 이동력 · 이동 타입 · 지형 적성 · 크기 · 무기 · 개발 트리) · 파일럿(능력치 · 성장 · 경험치 곡선)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 이동 타입입니다. 지형의 이동 비용과 적성이 이 순서로 적힙니다("지상 공중 우주 수중"). */
    enum class SrpgMoveType : uint8
    {
        Ground = 0,
        Air,
        Space,
        Water
    };

    /** @brief 무기가 쓰는 파일럿 능력치입니다. */
    enum class SrpgWeaponKind : uint8
    {
        Shooting = 0, ///< 사격
        Melee,        ///< 격투
        Awaken        ///< 각성(뉴타입 · 핀 판넬 류) — 기력 조건을 함께 둔다
    };

    /** @brief MAP 병기의 범위가 어디를 기준으로 놓이는가입니다. */
    enum class SrpgMapAnchor : uint8
    {
        None = 0, ///< MAP 병기가 아니다
        Self,     ///< 쏘는 쪽 기준 — 무늬는 동쪽(+x)을 보는 모양으로 적고 겨눈 방향으로 돌린다(빔 일직선 · 부채꼴)
        Target    ///< 겨눈 칸 기준 — 사거리 안의 칸을 골라 그 둘레를 친다(폭격 · 미사일 일제사)
    };

    /** @brief 파일럿 능력치 종류입니다. */
    enum class SrpgPilotStat : uint8
    {
        Shooting = 0,
        Melee,
        Reaction, ///< 반응 — 명중과 회피
        Awaken,   ///< 각성 — 각성 무기의 위력과 크리티컬
        Defense,  ///< 수비 — 받는 피해를 줄인다
        Count
    };

    /** @brief 이동 타입 수입니다. */
    static constexpr int32 kSrpgMoveTypeCount = 4;
    /** @brief 파일럿 능력치 수입니다. */
    static constexpr int32 kSrpgPilotStatCount = static_cast<int32>( SrpgPilotStat::Count );

    /** @brief 지형 하나입니다. */
    struct SrpgTerrainDef
    {
        hashed_string _id{};
        string        _name{};
        int32         _arrMoveCost[kSrpgMoveTypeCount]{ 1, 1, 1, 1 }; ///< 이동 타입별 들어가는 비용, 음수면 못 들어간다
        int32         _defenseBonus{ 0 };                             ///< 받는 쪽 방어력 보정(%)
        int32         _evasionBonus{ 0 };                             ///< 받는 쪽 회피 보정(명중률에서 빼는 %p)
        SrpgMoveType  _domain{ SrpgMoveType::Ground };                ///< 이 칸에 선 유닛이 쓰는 적성(공중 유닛은 지상 · 수중 위에서 공중 적성)
    };
} // namespace sw

namespace sw
{
    /** @brief 무기 하나입니다. */
    struct SrpgWeaponDef
    {
        hashed_string  _id{};
        string         _name{};
        vector<int2>   _listMapOffset{}; ///< MAP 병기의 칸 무늬(기준 칸에서의 상대 위치)
        int32          _minRange{ 1 };
        int32          _maxRange{ 1 };
        int32          _power{ 1000 };
        int32          _hitBonus{ 0 };       ///< 명중 보정(%p)
        int32          _critBonus{ 0 };      ///< 크리티컬 보정(%p)
        int32          _enCost{ 0 };         ///< 한 번 쓸 때 드는 EN
        int32          _ammo{ 0 };           ///< 탄수(0 이면 탄 없이 EN 만)
        int32          _moraleRequired{ 0 }; ///< 이 기력 이상이어야 쓴다
        SrpgWeaponKind _kind{ SrpgWeaponKind::Shooting };
        SrpgMapAnchor  _mapAnchor{ SrpgMapAnchor::None };
        uint8          _bCounter{ SW_TRUE };  ///< 반격에 쓸 수 있다
        uint8          _bPostMove{ SW_TRUE }; ///< 이동한 뒤에도 쓸 수 있다(G 제네레이션의 P 무기)

        bool isMap() const { return _mapAnchor != SrpgMapAnchor::None; }
        bool usesAmmo() const { return _ammo > 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 개발 갈래 하나 — 기체 레벨이 @p _requiredLevel 이상이면 @p _unitId 로 바꿀 수 있습니다. */
    struct SrpgDevelopTarget
    {
        hashed_string _unitId{};
        int32         _requiredLevel{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 기체(유닛) 하나입니다. */
    struct SrpgUnitDef
    {
        hashed_string             _id{};
        string                    _name{};
        vector<hashed_string>     _listWeaponId{};
        vector<SrpgDevelopTarget> _listDevelop{};
        int32                     _hp{ 3000 };
        int32                     _en{ 100 };
        int32                     _move{ 5 };
        int32                     _armor{ 500 };                                          ///< 장갑 — 받는 피해에서 빠진다
        int32                     _mobility{ 0 };                                         ///< 운동성 — 회피(명중률에서 빼는 %p)
        int32                     _size{ 1 };                                             ///< 0 = S, 1 = M, 2 = L, 3 = LL — 클수록 맞히기 쉽다
        int32                     _arrAptitude[kSrpgMoveTypeCount]{ 100, 100, 100, 100 }; ///< 지형 적성(%) — 0 이면 그 영역에 못 들어간다
        SrpgMoveType              _moveType{ SrpgMoveType::Ground };
    };
} // namespace sw

namespace sw
{
    /** @brief 파일럿 하나입니다. 레벨 L 의 능력치는 `기본 + 성장 × (L − 1)` 입니다. */
    struct SW_GF_API SrpgPilotDef
    {
        hashed_string _id{};
        string        _name{};
        int32         _arrStat[kSrpgPilotStatCount]{ 10, 10, 10, 0, 10 };
        int32         _arrGrowth[kSrpgPilotStatCount]{ 1, 1, 1, 0, 1 };

        int32 computeStat( SrpgPilotStat stat, int32 level ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SrpgCatalog
     * @brief `<SrpgCatalog><PilotCurve base="100" maxLevel="30"/><UnitCurve .../><Terrain .../><Weapon .../><Unit .../><Pilot .../></SrpgCatalog>` 를 읽습니다.
     * @details 지형 `cost="1 1 - 2"`(지상 공중 우주 수중, `-` 는 못 들어감) · 기체 `aptitude="A B - C"`(S 120 · A 100 · B 90 · C 80 · D 60 · `-` 0) ·
     *          `weapons="vulcan,rifle"` · `developsTo="gundam:5 gmcustom:3"` · 무기 `map="Self" pattern="1,0 2,0 3,0"` · 파일럿 `shooting="20" growShooting="2"`.
     *          경험치 곡선은 기반 `ExperienceCurve` 의 속성을 그대로 씁니다.
     */
    class SW_GF_API SrpgCatalog : public XmlCatalog<SrpgCatalog>
    {
        friend class XmlCatalog<SrpgCatalog>;

    public:
        SrpgCatalog();

        const SrpgTerrainDef*      findTerrain( const hashed_string& id ) const { return _terrainCatalog.find( id ); }
        const SrpgWeaponDef*       findWeapon( const hashed_string& id ) const { return _weaponCatalog.find( id ); }
        const SrpgUnitDef*         findUnit( const hashed_string& id ) const { return _unitCatalog.find( id ); }
        const SrpgPilotDef*        findPilot( const hashed_string& id ) const { return _pilotCatalog.find( id ); }
        const vector<SrpgUnitDef>& getUnits() const { return _unitCatalog.getAll(); }
        const ExperienceCurve&     getPilotCurve() const { return _pilotCurve; }
        const ExperienceCurve&     getUnitCurve() const { return _unitCurve; }

        /** @brief "S" "A" "B" "C" "D" "-" 또는 숫자 → 적성 %입니다. 모르는 글자면 @p fallback 입니다. */
        static int32 parseAptitude( string_view token, int32 fallback );

    private:
        static constexpr const utf8* kXmlRootName = "SrpgCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );
        void                         loadTerrain( const XmlNode& node, const utf8* pId );
        void                         loadWeapon( const XmlNode& node, const utf8* pId, string_view sourceName );
        void                         loadUnit( const XmlNode& node, const utf8* pId, string_view sourceName );
        void                         loadPilot( const XmlNode& node, const utf8* pId );

        GameCatalog<SrpgTerrainDef> _terrainCatalog;
        GameCatalog<SrpgWeaponDef>  _weaponCatalog;
        GameCatalog<SrpgUnitDef>    _unitCatalog;
        GameCatalog<SrpgPilotDef>   _pilotCatalog;
        ExperienceCurve             _pilotCurve;
        ExperienceCurve             _unitCurve;
    };
} // namespace sw
