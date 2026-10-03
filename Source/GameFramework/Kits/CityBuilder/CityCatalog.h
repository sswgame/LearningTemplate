/**
 * @file CityCatalog.h
 * @brief 도시 건설의 데이터 — 물자 · 건물(크기 · 값 · 일꾼 · 서비스 · 생산 · 저장 · 시장 · 매력도) · 집 단계(수용 인구 · 필요한 서비스 · 물자 · 매력도)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 땅의 종류입니다. */
    enum class CityTerrain : uint8
    {
        Grass = 0,
        Sand,
        Floodplain, ///< 나일 범람원 — 해마다 범람이 비옥함을 정한다
        Water,
        Rock
    };

    /** @brief 건물의 일입니다. */
    enum class CityBuildingKind : uint8
    {
        House = 0, ///< 사람이 산다 — 서비스 · 물자를 받으면 단계가 오른다
        Service,   ///< 서비스를 낸다(걸어 다니는 일꾼 또는 반경)
        Producer,  ///< 물자를 만든다(농장 · 공방) — 만든 것은 수레가 창고로
        Storage,   ///< 물자를 쌓는다(곡물 창고 · 저장 마당)
        Market,    ///< 창고에서 사 와 상인이 집집마다 판다(바자)
        Decoration ///< 매력도만(정원 · 조각)
    };

    /** @brief 집이 받는 서비스입니다. 순서가 비트 번호입니다. */
    enum class CityService : uint8
    {
        Water = 0,
        Religion,
        Entertainment,
        Health,
        Education,
        Tax, ///< 집이 아니라 시가 받는다 — 세리가 지나간 집만 세금을 낸다
        Count
    };

    /** @brief 서비스를 나르는 방법입니다. */
    enum class CityDelivery : uint8
    {
        Walker = 0, ///< 일꾼이 도로를 돌아다니며 지나는 집에 준다(파라오의 순회 일꾼)
        Radius      ///< 반경 안의 집에 늘 준다(우물)
    };

    [[nodiscard]] SW_GF_API bool parseCityService( string_view text, CityService& outService );
    SW_GF_API const utf8*        toString( CityService service );
    SW_GF_API const utf8*        toString( CityBuildingKind kind );
    constexpr uint8              makeCityServiceBit( CityService service ) { return static_cast<uint8>( 1u << static_cast<uint32>( service ) ); }

    /** @brief 물자 하나입니다. */
    struct CityGoodDef
    {
        hashed_string _id{};
        string        _name{};
        int32         _price{ 10 }; ///< 무역 · 표시용 값
        uint8         _bFood{ SW_FALSE };
    };

    /** @brief 건물 하나입니다. */
    struct CityBuildingDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<hashed_string> _listGood{};              ///< 생산자: 만드는 것(첫 칸) · 창고: 받는 것 · 시장: 파는 것
        float32               _productionTime{ 20.0f }; ///< 생산자 — 일꾼이 다 차서 하나 만드는 데 걸리는 초
        float32               _walkerInterval{ 6.0f };  ///< 일꾼을 내보내는 간격(초)
        int32                 _size{ 1 };
        int32                 _cost{ 10 };
        int32                 _workers{ 0 };
        int32                 _range{ 24 }; ///< 걷는 일꾼의 걸음 수 · 반경 서비스의 칸 수
        int32                 _productionAmount{ 5 };
        int32                 _capacity{ 0 };     ///< 창고 · 시장의 물자 상한
        int32                 _desirability{ 0 }; ///< 둘레의 매력도(음수 = 싫은 이웃)
        int32                 _desirabilityRadius{ 0 };
        CityBuildingKind      _kind{ CityBuildingKind::Decoration };
        CityService           _service{ CityService::Count };
        CityDelivery          _delivery{ CityDelivery::Walker };
        CityTerrain           _requiredTerrain{ CityTerrain::Grass }; ///< `_bRequiresTerrain` 일 때 발밑이 모두 이 땅이어야 한다
        uint8                 _bRequiresTerrain{ SW_FALSE };
    };

    /** @brief 집 한 단계입니다. 단계 번호가 곧 자리입니다(0 = 처음 짓는 오두막). */
    struct CityHouseLevelDef
    {
        string                _name{};
        vector<hashed_string> _listRequiredGood{};
        int32                 _population{ 5 };   ///< 수용 인구
        int32                 _taxPerPerson{ 1 }; ///< 달마다(세리가 지나간 집)
        int32                 _minDesirability{ -100 };
        uint8                 _serviceMask{ 0 }; ///< `makeCityServiceBit` 의 합
    };

    /**
     * @class CityCatalog
     * @brief `<CityCatalog roadCost="2"><Good .../><Building .../><HouseLevel .../></CityCatalog>` 를 읽습니다(키 이름은 `Resource/game/nilecity/data/city.xml`).
     * @details 집은 `kind="House"` 인 건물 하나(보통 1 칸)이고 단계 정의는 `<HouseLevel>` 을 적은 순서입니다.
     */
    class SW_GF_API CityCatalog
    {
    public:
        CityCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        const CityBuildingDef* findBuilding( const hashed_string& id ) const { return _buildingCatalog.find( id ); }
        const CityGoodDef*     findGood( const hashed_string& id ) const { return _goodCatalog.find( id ); }
        /** @brief 집 건물(첫 `kind="House"`)입니다. */
        const CityBuildingDef*   findHouseBuilding() const;
        const CityHouseLevelDef* findHouseLevel( int32 level ) const;

        const vector<CityBuildingDef>&   getBuildings() const { return _buildingCatalog.getAll(); }
        const vector<CityGoodDef>&       getGoods() const { return _goodCatalog.getAll(); }
        const vector<CityHouseLevelDef>& getHouseLevels() const { return _listHouseLevel; }
        int32                            getRoadCost() const { return _roadCost; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<CityBuildingDef> _buildingCatalog;
        GameCatalog<CityGoodDef>     _goodCatalog;
        vector<CityHouseLevelDef>    _listHouseLevel;
        int32                        _roadCost;
    };
} // namespace sw
