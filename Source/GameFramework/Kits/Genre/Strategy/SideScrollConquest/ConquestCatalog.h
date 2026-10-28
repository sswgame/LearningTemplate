/**
 * @file ConquestCatalog.h
 * @brief 횡스크롤 정복의 데이터를 담습니다. 건물(비용, 생산, 일꾼 슬롯, 주거, 훈련 병종), 병종(체력, 공격, 사거리, 속도, 인구, 공성 역할),
 *        거점(x 위치, 종류, 주인, 성문, 성벽, 수입, 일꾼, 주둔군), 규칙 수치를 정의합니다.
 * @details 자원 이름(나무, 철, 금 등)은 코드가 정하지 않는다. 비용과 수입은 `StatBlock`(`<Cost wood="20" gold="5"/>`)으로 읽는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/StatBlock.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 진영입니다. 플레이어는 왼쪽(작은 x)에서 오른쪽으로 쳐들어갑니다. */
    ENUM()
    enum class ConquestTeam : uint8
    {
        Neutral = 0,
        Player,
        Enemy
    };

    /** @brief 거점 종류입니다. */
    ENUM()
    enum class ConquestSiteKind : uint8
    {
        Village = 0, ///< 일꾼과 건물 슬롯을 준다
        Outpost,     ///< 작은 수입을 주는 전진 기지이다
        Fortress     ///< 성문, 성벽, 주둔군이 있다
    };

    /** @brief 병종의 공성 역할입니다. */
    ENUM()
    enum class ConquestSiegeRole : uint8
    {
        None = 0,
        Ram,   ///< 성문을 크게 때리고, 성문이 부서지면 성벽을 때린다
        Ladder ///< 성벽에 걸치면 성문을 부수지 않고 넘어간다. 싸우지 않는다
    };

    [[nodiscard]] SW_KIT_API bool parseConquestTeam( string_view text, ConquestTeam& outTeam );

    /** @brief 건물 하나입니다. */
    REFLECT()
    struct ConquestBuildingDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        StatBlock     _cost{};
        PROPERTY()
        hashed_string         _produces{};      ///< 만드는 자원이다(비어 있으면 생산하지 않는다)
        vector<hashed_string> _listTrainable{}; ///< 훈련할 수 있는 병종
        PROPERTY( Min = 0.1 )
        float32 _cycleTime{ 10.0f }; ///< 생산 한 주기의 길이(초)
        PROPERTY( Name = "amount", Min = 0 )
        int32 _amountPerWorker{ 1 }; ///< 주기마다 일꾼 한 명이 만드는 양
        PROPERTY( Min = 0 )
        int32 _workerSlots{ 0 };
        PROPERTY( Min = 0 )
        int32 _housing{ 0 }; ///< 늘려 주는 인구 한도
    };
} // namespace sw

namespace sw
{
    /** @brief 병종 하나입니다. */
    REFLECT()
    struct ConquestUnitDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        StatBlock     _cost{};
        PROPERTY( Name = "hp", Min = 1.0 )
        float32 _health{ 30.0f };
        PROPERTY( Min = 0.0 )
        float32 _damage{ 5.0f };
        PROPERTY( Min = 0.1 )
        float32 _range{ 1.0f }; ///< 1.5 이하이면 근접, 더 길면 원거리이다
        PROPERTY( Min = 0.05 )
        float32 _attackInterval{ 1.0f };
        PROPERTY( Min = 0.0 )
        float32 _speed{ 2.0f }; ///< 초당 이동하는 x 거리
        PROPERTY( Name = "train", Min = 0.0 )
        float32 _trainTime{ 5.0f };
        PROPERTY( Min = 0.0 )
        float32 _structureScale{ 0.2f }; ///< 성문과 성벽에 주는 피해 배율이다(충차는 크다)
        PROPERTY( Name = "pop", Min = 0 )
        int32 _population{ 1 };
        PROPERTY( Name = "siege" )
        ConquestSiegeRole _siegeRole{ ConquestSiegeRole::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 거점에 처음 배치하는 주둔군 한 줄입니다. */
    struct ConquestGarrisonDef
    {
        hashed_string _unitID{};
        int32         _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 거점 하나입니다. */
    REFLECT()
    struct ConquestSiteDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string               _id{};
        StatBlock                   _income{}; ///< 수입 주기마다 주인에게 준다(경제가 있는 쪽은 플레이어뿐이다)
        vector<ConquestGarrisonDef> _listGarrison{};
        PROPERTY()
        float32 _x{ 0.0f };
        PROPERTY( Min = 0.5 )
        float32 _captureRadius{ 4.0f };
        PROPERTY( Name = "gate", Min = 0.0 )
        float32 _gateHealth{ 0.0f }; ///< 0 이면 성문이 없다
        PROPERTY( Name = "wall", Min = 0.0 )
        float32 _wallHealth{ 0.0f }; ///< 0 이면 성벽이 없다
        PROPERTY( Min = 0 )
        int32 _workers{ 0 }; ///< 점령하면 쓸 수 있는 일꾼 수
        PROPERTY( Min = 0 )
        int32 _housing{ 0 }; ///< 점령하면 늘어나는 인구 한도
        PROPERTY( Name = "slots", Min = 0 )
        int32 _buildSlots{ 0 };
        PROPERTY()
        ConquestSiteKind _kind{ ConquestSiteKind::Village };
        PROPERTY()
        ConquestTeam _owner{ ConquestTeam::Neutral };
    };
} // namespace sw

namespace sw
{
    /** @brief 규칙 수치입니다. 시간 단위는 초, 거리 단위는 x 입니다. */
    REFLECT()
    struct ConquestRules
    {
        REFLECT_BODY();

        StatBlock _startResources{};
        PROPERTY()
        hashed_string _waveUnit{}; ///< 반격 웨이브를 이루는 병종
        PROPERTY( Min = 0.01, Max = 1.0 )
        float32 _fixedStep{ 0.1f };
        PROPERTY( Min = 0.1 )
        float32 _incomeInterval{ 10.0f };
        PROPERTY( Min = 0.0 )
        float32 _captureTime{ 3.0f }; ///< 지키는 편 없이 이만큼 머물면 점령한다
        PROPERTY( Min = 0.0 )
        float32 _moraleRadius{ 6.0f }; ///< 지휘관에게서 이 거리 안의 부대원이 사기 버프를 받는다
        PROPERTY( Min = 0.0 )
        float32 _moraleDamageBonus{ 0.25f }; ///< 사기 버프가 주는 피해 증가량
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _wallProtection{ 0.5f }; ///< 성벽이 서 있는 거점의 수비대가 덜 받는 피해 비율
        PROPERTY( Min = 1.0 )
        float32 _commanderHealth{ 100.0f };
        PROPERTY( Min = 0.0 )
        float32 _commanderDamage{ 8.0f };
        PROPERTY( Min = 0.0 )
        float32 _commanderStructureRate{ 0.25f }; ///< 지휘관이 성문과 성벽에 주는 피해 배율
        PROPERTY( Min = 0.1 )
        float32 _commanderRange{ 1.5f };
        PROPERTY( Min = 0.0 )
        float32 _commanderSpeed{ 4.0f };
        PROPERTY( Min = 0.05 )
        float32 _commanderAttackInterval{ 0.8f };
        PROPERTY( Name = "commanderRespawn", Min = 0.0 )
        float32 _commanderRespawnTime{ 10.0f };
        PROPERTY( Min = 0.1 )
        float32 _formationSpacing{ 1.0f }; ///< 부대원 사이의 간격
        PROPERTY( Min = 0.0 )
        float32 _followLeash{ 8.0f }; ///< 따라오기 상태에서 지휘관에게서 이 거리까지만 적을 쫓는다
        PROPERTY( Min = 0.0 )
        float32 _aggroRange{ 8.0f }; ///< 이 거리 안의 적을 알아챈다
        PROPERTY( Min = 0.0 )
        float32 _waveInterval{ 60.0f };
        PROPERTY( Min = 0.0 )
        float32 _waveCountPerMinute{ 1.0f }; ///< 지난 분마다 웨이브에 더하는 수
        PROPERTY( Min = 0 )
        int32 _waveBaseCount{ 2 };
        PROPERTY( Min = 0 )
        int32 _waveCountPerSite{ 1 }; ///< 플레이어 거점(첫 마을 제외)마다 더하는 수
        PROPERTY( Min = 0 )
        int32 _basePopulation{ 5 };
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
    class SW_KIT_API ConquestCatalog : public XMLCatalog<ConquestCatalog>
    {
        friend class XMLCatalog<ConquestCatalog>;

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
        static constexpr const utf8* kXMLRootName = "ConquestCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief 원소 하나를 이름에 맞는 스키마로 읽습니다. 오류면 그 원소를 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadElement( const XMLNode& node, string_view name, string_view sourceName );
        [[nodiscard]] bool loadUnit( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadBuilding( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadSite( const XMLNode& node, string_view sourceName );

        GameCatalog<ConquestBuildingDef> _buildingCatalog;
        GameCatalog<ConquestUnitDef>     _unitCatalog;
        GameCatalog<ConquestSiteDef>     _siteCatalog;
        ConquestRules                    _rules;
    };
} // namespace sw
