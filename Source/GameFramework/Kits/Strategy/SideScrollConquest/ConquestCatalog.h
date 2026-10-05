/**
 * @file ConquestCatalog.h
 * @brief 횡스크롤 정복의 데이터 — 건물(값 · 생산 · 일꾼 자리 · 주거 · 훈련 병종) · 병종(체력 · 공격 · 사거리 · 속도 · 인구 · 공성 역할) ·
 *        거점(x 위치 · 종류 · 주인 · 성문 · 성벽 · 수입 · 일꾼 · 주둔군) · 규칙 수치입니다.
 * @details 자원 이름(나무 · 철 · 금 …)은 코드가 정하지 않습니다 — 값 · 수입은 `StatBlock`(`<Cost wood="20" gold="5"/>`)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Data/GameCatalog.h"
#include "GameFramework/Base/Data/StatBlock.h"
#include "GameFramework/Base/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 편입니다. 플레이어는 왼쪽(작은 x)에서 오른쪽으로 쳐들어갑니다. */
    enum class ConquestTeam : uint8
    {
        Neutral = 0,
        Player,
        Enemy
    };

    /** @brief 거점 종류입니다. */
    enum class ConquestSiteKind : uint8
    {
        Village = 0, ///< 일꾼 · 건물 자리를 준다
        Outpost,     ///< 작은 수입 · 전진 기지
        Fortress     ///< 성문 · 성벽 · 주둔군
    };

    /** @brief 병종의 공성 역할입니다. */
    enum class ConquestSiegeRole : uint8
    {
        None = 0,
        Ram,   ///< 성문(부서지면 성벽)을 크게 때린다
        Ladder ///< 성벽에 걸치면 성문을 부수지 않고 넘는다 — 싸우지 않는다
    };

    [[nodiscard]] SW_GF_API bool parseConquestTeam( string_view text, ConquestTeam& outTeam );
    SW_GF_API const utf8*        toString( ConquestTeam team );

    /** @brief 건물 하나입니다. */
    struct ConquestBuildingDef
    {
        hashed_string         _id{};
        StatBlock             _cost{};
        hashed_string         _produces{};           ///< 만드는 자원(비면 생산 없음)
        vector<hashed_string> _listTrainable{};      ///< 훈련하는 병종
        float32               _cycleTime{ 10.0f };   ///< 생산 한 주기(초)
        int32                 _amountPerWorker{ 1 }; ///< 주기마다 일꾼 한 명이 만드는 양
        int32                 _workerSlots{ 0 };
        int32                 _housing{ 0 }; ///< 늘려 주는 인구 한도
    };
} // namespace sw

namespace sw
{
    /** @brief 병종 하나입니다. */
    struct ConquestUnitDef
    {
        hashed_string     _id{};
        StatBlock         _cost{};
        float32           _health{ 30.0f };
        float32           _damage{ 5.0f };
        float32           _range{ 1.0f }; ///< 1.5 이하면 근접, 더 길면 원거리
        float32           _attackInterval{ 1.0f };
        float32           _speed{ 2.0f }; ///< 초당 x
        float32           _trainTime{ 5.0f };
        float32           _structureScale{ 0.2f }; ///< 성문 · 성벽에 주는 피해 배율(충차는 크게)
        int32             _population{ 1 };
        ConquestSiegeRole _siegeRole{ ConquestSiegeRole::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 거점에 처음 있는 주둔군 한 줄입니다. */
    struct ConquestGarrisonDef
    {
        hashed_string _unitId{};
        int32         _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 거점 하나입니다. */
    struct ConquestSiteDef
    {
        hashed_string               _id{};
        StatBlock                   _income{}; ///< 수입 주기마다 주인에게(플레이어만 경제가 있다)
        vector<ConquestGarrisonDef> _listGarrison{};
        float32                     _x{ 0.0f };
        float32                     _captureRadius{ 4.0f };
        float32                     _gateHealth{ 0.0f }; ///< 0 = 성문 없음
        float32                     _wallHealth{ 0.0f }; ///< 0 = 성벽 없음
        int32                       _workers{ 0 };       ///< 점령하면 쓸 수 있는 일꾼
        int32                       _housing{ 0 };       ///< 점령하면 늘어나는 인구 한도
        int32                       _buildSlots{ 0 };
        ConquestSiteKind            _kind{ ConquestSiteKind::Village };
        ConquestTeam                _owner{ ConquestTeam::Neutral };
    };
} // namespace sw

namespace sw
{
    /** @brief 규칙 수치입니다. 시간은 초, 거리는 x 입니다. */
    struct ConquestRules
    {
        StatBlock     _startResources{};
        hashed_string _waveUnit{}; ///< 반격 웨이브의 병종
        float32       _fixedStep{ 0.1f };
        float32       _incomeInterval{ 10.0f };
        float32       _captureTime{ 3.0f };        ///< 지키는 편 없이 이만큼 머물면 점령
        float32       _moraleRadius{ 6.0f };       ///< 지휘관 이 거리 안의 부대원이 사기 버프
        float32       _moraleDamageBonus{ 0.25f }; ///< 사기 버프의 피해 증가
        float32       _wallProtection{ 0.5f };     ///< 성벽이 서 있는 거점의 수비대가 덜 받는 피해 비율
        float32       _commanderHealth{ 100.0f };
        float32       _commanderDamage{ 8.0f };
        float32       _commanderRange{ 1.5f };
        float32       _commanderSpeed{ 4.0f };
        float32       _commanderAttackInterval{ 0.8f };
        float32       _commanderRespawnTime{ 10.0f };
        float32       _formationSpacing{ 1.0f }; ///< 부대원 사이 간격
        float32       _followLeash{ 8.0f };      ///< 따라오기 — 지휘관에게서 이만큼까지만 적을 쫓는다
        float32       _aggroRange{ 8.0f };       ///< 이 안의 적을 알아챈다
        float32       _waveInterval{ 60.0f };
        float32       _waveCountPerMinute{ 1.0f }; ///< 지난 분마다 웨이브에 더하는 수
        int32         _waveBaseCount{ 2 };
        int32         _waveCountPerSite{ 1 }; ///< 플레이어 거점(첫 마을 제외)마다 더하는 수
        int32         _basePopulation{ 5 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ConquestCatalog
     * @brief `<ConquestCatalog><Rules waveUnit="raider" waveInterval="60" .../><Start wood="50" gold="20"/>
     *        <Building id="lumberMill" produces="wood" workerSlots="3" cycleTime="10" amount="2"><Cost gold="10"/></Building>
     *        <Unit id="spearman" hp="40" damage="6" range="1" speed="2" pop="1" train="5"><Cost gold="10" iron="2"/></Unit>
     *        <Site id="home" x="0" kind="Village" owner="Player" workers="4" slots="3" housing="5"><Income gold="2"/></Site>
     *        <Site id="keep" x="60" kind="Fortress" owner="Enemy" gate="200" wall="300" garrison="spearman:3"/></ConquestCatalog>` 를 읽습니다.
     */
    class SW_GF_API ConquestCatalog : public XmlCatalog<ConquestCatalog>
    {
        friend class XmlCatalog<ConquestCatalog>;

    public:
        ConquestCatalog();

        const ConquestBuildingDef*         findBuilding( const hashed_string& id ) const { return _buildingCatalog.find( id ); }
        const ConquestUnitDef*             findUnit( const hashed_string& id ) const { return _unitCatalog.find( id ); }
        const ConquestSiteDef*             findSite( const hashed_string& id ) const { return _siteCatalog.find( id ); }
        const vector<ConquestSiteDef>&     getSites() const { return _siteCatalog.getAll(); }
        const vector<ConquestBuildingDef>& getBuildings() const { return _buildingCatalog.getAll(); }
        const vector<ConquestUnitDef>&     getUnits() const { return _unitCatalog.getAll(); }
        const ConquestRules&               getRules() const { return _rules; }
        void                               setRules( const ConquestRules& rules ) { _rules = rules; }

    private:
        static constexpr const utf8* kXmlRootName = "ConquestCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<ConquestBuildingDef> _buildingCatalog;
        GameCatalog<ConquestUnitDef>     _unitCatalog;
        GameCatalog<ConquestSiteDef>     _siteCatalog;
        ConquestRules                    _rules;
    };
} // namespace sw
