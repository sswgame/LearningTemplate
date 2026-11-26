/**
 * @file CityCatalog.h
 * @brief 도시 건설의 데이터를 담습니다. 땅, 서비스, 물자, 건물(크기, 비용, 일꾼, 서비스, 생산, 저장, 시장, 매력도), 집 단계(수용 인구, 필요한 서비스, 필요한 물자, 매력도)를 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /**
     * @brief 건물의 역할입니다.
     * @details 서비스 · 땅과 달리 행이 아니라 열거형으로 둔다 — 종류마다 시뮬레이션의 코드(생산 · 창고 · 시장 사 오기 · 서비스 전달 · 집 단계)가 다르고, 새 종류는 새 코드다.
     */
    ENUM()
    enum class CityBuildingKind : uint8
    {
        House = 0, ///< 사람이 산다. 서비스와 물자를 받으면 단계가 오른다
        Service,   ///< 서비스를 낸다(걸어 다니는 일꾼이나 반경으로 전달한다)
        Producer,  ///< 물자를 만든다(농장, 공방). 만든 물자는 수레가 창고로 나른다
        Storage,   ///< 물자를 쌓아 둔다(곡물 창고, 저장 마당)
        Market,    ///< 창고에서 물자를 사 와서 상인이 집집마다 판다(바자)
        Decoration ///< 매력도만 높인다(정원, 조각)
    };

    /** @brief 서비스를 전달하는 방법입니다. */
    ENUM()
    enum class CityDelivery : uint8
    {
        Walker = 0, ///< 일꾼이 도로를 돌아다니며 지나는 집에 전달한다(파라오의 순회 일꾼)
        Radius      ///< 반경 안의 집에 항상 전달한다(우물)
    };

    /**
     * @brief 땅 한 종류입니다(`<Terrain>` 행). 첫 행이 새 도시의 땅이다.
     * @details 규칙(지을 수 있는가 · 범람이 농장의 수확을 정하는가)과 모습(색 · 높이 · 솟은 돌)이 모두 행의 칸이다 — 땅을 더하는 일에 코드가 없다.
     */
    REFLECT()
    struct CityTerrainDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Required )
        float4 _color{}; ///< 땅 상자의 색
        PROPERTY( Units = m )
        float32 _surfaceHeight{ 0.0f }; ///< 땅 상자 윗면의 높이(물은 둘레보다 낮다)
        PROPERTY( Min = 0.0, Units = m )
        float32 _rockHeight{ 0.0f }; ///< 칸마다 솟은 돌 상자의 높이(0 이면 없다)
        PROPERTY()
        bool _bBlocked{ false }; ///< 도로 · 건물을 놓지 못한다
        PROPERTY()
        bool _bFlood{ false }; ///< 이 땅을 요구하는 농장의 수확을 해마다의 범람이 정한다
    };

    /** @brief 집이 받는 서비스 하나입니다(`<Service>` 행). 시가 하는 일(노동 우선 · 세금 · 문화)은 행의 깃발이다. */
    REFLECT()
    struct CityServiceDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Required )
        float4 _color{}; ///< 순회 일꾼의 색
        PROPERTY()
        bool _bLaborFirst{ false }; ///< 이 서비스의 건물이 일꾼을 가장 먼저 받는다(물)
        PROPERTY()
        bool _bTax{ false }; ///< 다녀간 집이 달마다 세금을 낸다(세리)
        PROPERTY()
        bool _bCulture{ false }; ///< 이런 서비스를 모두 받는 집이 문화 평가에 든다
    };

    /** @brief 물자 하나입니다. 집이 먹는 물자는 집 단계의 `goods` 가 이름으로 적는다 — 식량 깃발(`food`)은 읽고 아무도 쓰지 않아 지웠다. */
    REFLECT()
    struct CityGoodDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Min = 0 )
        int32 _price{ 10 }; ///< 무역과 표시에 쓰는 가격
    };
} // namespace sw

namespace sw
{
    /** @brief 건물 하나입니다. */
    REFLECT()
    struct CityBuildingDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Required )
        float4 _modelTint{}; ///< 모델에 곱하는 색(흰색이면 팔레트 그대로)
        PROPERTY( Required )
        float4 _blockColor{}; ///< 모델이 없는 건물을 그리는 색 상자의 색
        PROPERTY( Name = "goods" )
        vector<hashed_string> _listGood{}; ///< 생산자는 만드는 물자(첫 항목), 창고는 받는 물자, 시장은 파는 물자
        PROPERTY( Min = 0.5 )
        float32 _productionTime{ 20.0f }; ///< 생산자가 쓴다. 일꾼이 다 찼을 때 물자 하나를 만드는 데 걸리는 시간(초)
        PROPERTY( Min = 0.01, Units = m )
        float32 _blockHeight{ 1.0f }; ///< 모델이 없는 건물을 그리는 색 상자의 높이(집은 빈 땅의 높이 — 사는 집은 단계가 정한다)
        PROPERTY( Min = 0.5 )
        float32 _walkerInterval{ 6.0f }; ///< 일꾼을 내보내는 간격(초)
        PROPERTY( Min = 1, Max = 6 )
        int32 _size{ 1 };
        PROPERTY( Min = 0 )
        int32 _cost{ 10 };
        PROPERTY( Min = 0 )
        int32 _workers{ 0 };
        PROPERTY( Min = 1 )
        int32 _range{ 24 }; ///< 걷는 일꾼의 걸음 수 또는 반경 서비스의 셀 수
        PROPERTY( Name = "amount", Min = 1 )
        int32 _productionAmount{ 5 };
        PROPERTY( Min = 0 )
        int32 _capacity{ 0 }; ///< 창고와 시장이 담을 수 있는 물자 상한
        PROPERTY()
        int32 _desirability{ 0 }; ///< 둘레에 주는 매력도(음수이면 이웃이 싫어하는 건물)
        PROPERTY( Min = 0, Max = 8 )
        int32 _desirabilityRadius{ 0 };
        PROPERTY( Required )
        CityBuildingKind _kind{ CityBuildingKind::Decoration };
        PROPERTY()
        hashed_string _service{}; ///< 내는 서비스의 id(`kind="Service"` 는 필수)
        PROPERTY( Name = "terrain" )
        hashed_string _requiredTerrain{}; ///< 적었으면 건물 발밑이 모두 이 땅이어야 한다
        PROPERTY()
        CityDelivery _delivery{ CityDelivery::Walker };
        int32        _serviceIndex{ constant::invalid_index::kInt32 }; ///< `_service` 의 행 번호(없으면 -1) — 읽을 때 푼다
        int32        _terrainIndex{ constant::invalid_index::kInt32 }; ///< `_requiredTerrain` 의 행 번호(없으면 -1) — 읽을 때 푼다

        bool requiresTerrain() const { return _terrainIndex >= 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 집 한 단계입니다. 단계 번호가 곧 배열 index 입니다(0 은 처음 짓는 오두막). */
    REFLECT()
    struct CityHouseLevelDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY( Name = "goods" )
        vector<hashed_string> _listRequiredGood{};
        PROPERTY( Min = 0.01, Units = m )
        float32 _blockHeight{ 0.5f }; ///< 모델이 없는 사는 집을 그리는 색 상자의 높이
        PROPERTY( Min = 1 )
        int32 _population{ 5 }; ///< 수용 인구
        PROPERTY( Name = "tax", Min = 0 )
        int32 _taxPerPerson{ 1 }; ///< 한 달마다 내는 세금(세리가 지나간 집만 낸다)
        PROPERTY( Name = "desirability" )
        int32 _minDesirability{ -100 };
        PROPERTY( Name = "services" )
        vector<hashed_string> _listRequiredService{};
        vector<int32>         _listServiceIndex{}; ///< `_listRequiredService` 의 행 번호 — 읽을 때 푼다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 도시 규칙의 수치입니다. 시간 단위는 게임 초입니다.
     * @details 규칙 자산이다 — 도시 카탈로그의 `<Rules …/>` 한 줄이 모든 칸을 적는다(`Required`, 없으면 카탈로그 로드가 실패한다). 도시는 카탈로그에서만 받으므로
     *          게임이 C++ 기본값(`CitySettings{}`)으로 도시를 열 길이 없다(감사 K-4, Lyra `ULyraExperienceDefinition` 자리).
     *          범위는 필드의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 읽기가 그 줄을 넣지 않는 오류로 거절한다. 값을 조용히 맞추지 않는다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API CitySettings
    {
        REFLECT_BODY();
        PROPERTY( Required, Min = 0.0 )
        float32 _walkerSpeed{}; ///< 셀 / 초
        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _workerRatio{}; ///< 인구 중 일하는 사람의 비율
        PROPERTY( Required, Min = 0.0 )
        float32 _serviceDuration{}; ///< 서비스를 받은 뒤 그 효과가 남아 있는 시간(초)
        PROPERTY( Required, Min = 0.0 )
        float32 _immigrationInterval{}; ///< 빈 집 하나에 한 사람이 들어오는 간격
        PROPERTY( Required, Min = 0 )
        int32 _immigrationJobMargin{}; ///< 일할 사람이 일자리 + 이 수보다 적으면 사람이 온다
        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _immigrationMaxIdleRatio{}; ///< 실업률이 이보다 낮아도 사람이 온다
        PROPERTY( Required, Min = 0.0 )
        float32 _floodFertilityStart{}; ///< 첫 범람 앞의 땅 힘(범람원 농지의 수확 비율)
        PROPERTY( Required, Min = 0.0 )
        float32 _floodFertilityMin{}; ///< 범람 한 번이 정하는 땅 힘의 하한
        PROPERTY( Required, Min = 0.0 )
        float32 _floodFertilityMax{}; ///< 범람 한 번이 정하는 땅 힘의 상한
        PROPERTY( Required, Min = 0.0 )
        float32 _evolveDelay{}; ///< 조건이 이만큼 이어져야 집 단계가 오르내린다
        PROPERTY( Required, Min = 0.0 )
        float32 _wagePerWorkerPerMonth{};
        PROPERTY( Required, Min = 0.001 )
        float32 _fixedStep{};
        PROPERTY( Required, Min = 0 )
        int32 _serviceReach{}; ///< 일꾼이 지나는 셀에서 이 거리 안의 집에 서비스를 준다
        PROPERTY( Required, Min = 0 )
        int32 _goodsPerFourPeople{}; ///< 한 달에 네 사람이 먹는 물자 수
        PROPERTY( Required )
        uint32 _randomSeed{}; ///< 범람과 일꾼의 갈림길 선택에 쓰는 난수 시드
        PROPERTY( Required )
        hashed_string _currency{}; ///< 빌린 지갑에서 쓰는 통화
    };
} // namespace sw

namespace sw
{
    /** @brief `<CityCatalog>` 루트 속성의 스키마입니다. */
    REFLECT()
    struct CityCatalogRootDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0 )
        int32 _roadCost{ 2 }; ///< 도로 한 칸의 값
    };
} // namespace sw

namespace sw
{
    /**
     * @class CityCatalog
     * @brief `<CityCatalog roadCost="2"><Terrain .../><Service .../><Good .../><Building .../><HouseLevel .../></CityCatalog>` 를 읽습니다. 키 이름은 `Content/game/nilecity/data/city.xml` 입니다.
     * @details 집은 `kind="House"` 인 건물 하나(보통 1셀)이고, 단계 정의는 `<HouseLevel>` 을 적은 순서대로 읽는다.
     */
    class SW_KIT_API CityCatalog : public XMLCatalog<CityCatalog>
    {
        friend class XMLCatalog<CityCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_ASSET_TYPE` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "city";
        /** @brief 이 데이터 종류입니다(등록은 `CityCatalog.cpp` 의 공유 캐시 줄이 로더를 더한다). 접미사가 없어 타입 있는 참조 `AssetRef<CityCatalog>` 는 경로 규칙으로 가른다. */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( kDataKind, AssetPathRule{ "game/", {}, "/data/city.xml" }, {} );

        CityCatalog();

        const CityBuildingDef* findBuilding( const hashed_string& id ) const { return _buildingCatalog.find( id ); }
        const CityGoodDef*     findGood( const hashed_string& id ) const { return _goodCatalog.find( id ); }
        /** @brief 땅 @p id 의 행 번호입니다. 도시의 칸(`CityTile::_terrain`)은 이 번호다. */
        optional<int32> findTerrainIndex( const hashed_string& id ) const { return _terrainCatalog.findIndex( id ); }
        /** @brief 서비스 @p id 의 행 번호입니다. 집의 서비스 시각 · 일꾼의 서비스는 이 번호다. */
        optional<int32> findServiceIndex( const hashed_string& id ) const { return _serviceCatalog.findIndex( id ); }
        /** @brief 집 건물(처음 나오는 `kind="House"`)을 반환합니다. */
        const CityBuildingDef*   findHouseBuilding() const;
        const CityHouseLevelDef* findHouseLevel( int32 level ) const;

        const vector<CityBuildingDef>&   getBuildings() const { return _buildingCatalog.getAll(); }
        const vector<CityGoodDef>&       getGoods() const { return _goodCatalog.getAll(); }
        const vector<CityTerrainDef>&    getTerrainRows() const { return _terrainCatalog.getAll(); }
        const vector<CityServiceDef>&    getServiceRows() const { return _serviceCatalog.getAll(); }
        const vector<CityHouseLevelDef>& getHouseLevels() const { return _listHouseLevel; }
        int32                            getRoadCost() const { return _roadCost; }
        /** @brief 규칙(`<Rules>`)입니다. 로드에 성공한 카탈로그는 반드시 한 줄을 가진다. */
        const CitySettings& getRules() const { return _rules; }

    private:
        static constexpr size_t      kMaxTerrainCount = 256;           ///< 도시의 칸이 땅을 uint8 번호로 든다
        static constexpr const utf8* kXMLRootName     = "CityCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadBuilding( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadHouseLevel( const XMLNode& node, string_view sourceName );
        /** @brief `<Rules>` 한 줄을 읽습니다. 칸이 빠졌거나 범위 밖이면 false 입니다. */
        [[nodiscard]] bool loadRules( const XMLNode& node, string_view sourceName );

    private:
        GameCatalog<CityTerrainDef>  _terrainCatalog;
        GameCatalog<CityServiceDef>  _serviceCatalog;
        GameCatalog<CityBuildingDef> _buildingCatalog;
        GameCatalog<CityGoodDef>     _goodCatalog;
        vector<CityHouseLevelDef>    _listHouseLevel;
        int32                        _roadCost;
        CitySettings                 _rules;
        int32                        _rulesCount; ///< 읽은 `<Rules>` 줄 수(0 이면 로드 실패)
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<CityCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, CityCatalog );
} // namespace sw
