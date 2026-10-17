/**
 * @file CityCatalog.h
 * @brief 도시 건설의 데이터를 담습니다. 물자, 건물(크기, 비용, 일꾼, 서비스, 생산, 저장, 시장, 매력도), 집 단계(수용 인구, 필요한 서비스, 필요한 물자, 매력도)를 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 땅의 종류입니다. */
    enum class CityTerrain : uint8
    {
        Grass = 0,
        Sand,
        Floodplain, ///< 나일 범람원이다. 해마다 범람이 비옥함을 정한다
        Water,
        Rock
    };

    /** @brief 건물의 역할입니다. */
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

    /** @brief 집이 받는 서비스입니다. 선언 순서가 비트 번호입니다. */
    enum class CityService : uint8
    {
        Water = 0,
        Religion,
        Entertainment,
        Health,
        Education,
        Tax, ///< 집이 아니라 시가 받는다. 세리가 지나간 집만 세금을 낸다
        Count
    };

    /** @brief 서비스를 전달하는 방법입니다. */
    enum class CityDelivery : uint8
    {
        Walker = 0, ///< 일꾼이 도로를 돌아다니며 지나는 집에 전달한다(파라오의 순회 일꾼)
        Radius      ///< 반경 안의 집에 항상 전달한다(우물)
    };

    [[nodiscard]] SW_KIT_API bool parseCityService( string_view text, CityService& outService );
    SW_KIT_API const utf8*        toString( CityService service );
    constexpr uint8               makeCityServiceBit( CityService service )
    {
        return static_cast<uint8>( 1u << static_cast<uint32>( service ) );
    }

    /** @brief 물자 하나입니다. */
    struct CityGoodDef
    {
        hashed_string _id{};
        string        _name{};
        int32         _price{ 10 }; ///< 무역과 표시에 쓰는 가격
        uint8         _bFood{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 건물 하나입니다. */
    struct CityBuildingDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<hashed_string> _listGood{};              ///< 생산자는 만드는 물자(첫 항목), 창고는 받는 물자, 시장은 파는 물자
        float32               _productionTime{ 20.0f }; ///< 생산자가 쓴다. 일꾼이 다 찼을 때 물자 하나를 만드는 데 걸리는 시간(초)
        float32               _walkerInterval{ 6.0f };  ///< 일꾼을 내보내는 간격(초)
        int32                 _size{ 1 };
        int32                 _cost{ 10 };
        int32                 _workers{ 0 };
        int32                 _range{ 24 }; ///< 걷는 일꾼의 걸음 수 또는 반경 서비스의 셀 수
        int32                 _productionAmount{ 5 };
        int32                 _capacity{ 0 };     ///< 창고와 시장이 담을 수 있는 물자 상한
        int32                 _desirability{ 0 }; ///< 둘레에 주는 매력도(음수이면 이웃이 싫어하는 건물)
        int32                 _desirabilityRadius{ 0 };
        CityBuildingKind      _kind{ CityBuildingKind::Decoration };
        CityService           _service{ CityService::Count };
        CityDelivery          _delivery{ CityDelivery::Walker };
        CityTerrain           _requiredTerrain{ CityTerrain::Grass }; ///< `_bRequiresTerrain` 이 켜져 있으면 건물 발밑이 모두 이 땅이어야 한다
        uint8                 _bRequiresTerrain{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 집 한 단계입니다. 단계 번호가 곧 배열 index 입니다(0 은 처음 짓는 오두막). */
    struct CityHouseLevelDef
    {
        string                _name{};
        vector<hashed_string> _listRequiredGood{};
        int32                 _population{ 5 };   ///< 수용 인구
        int32                 _taxPerPerson{ 1 }; ///< 한 달마다 내는 세금(세리가 지나간 집만 낸다)
        int32                 _minDesirability{ -100 };
        uint8                 _serviceMask{ 0 }; ///< `makeCityServiceBit` 값을 OR 한 것
    };
} // namespace sw

namespace sw
{
    /**
     * @class CityCatalog
     * @brief `<CityCatalog roadCost="2"><Good .../><Building .../><HouseLevel .../></CityCatalog>` 를 읽습니다. 키 이름은 `Resource/game/nilecity/data/city.xml` 입니다.
     * @details 집은 `kind="House"` 인 건물 하나(보통 1셀)이고, 단계 정의는 `<HouseLevel>` 을 적은 순서대로 읽는다.
     */
    class SW_KIT_API CityCatalog : public XMLCatalog<CityCatalog>
    {
        friend class XMLCatalog<CityCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "city";

        CityCatalog();

        const CityBuildingDef* findBuilding( const hashed_string& id ) const { return _buildingCatalog.find( id ); }
        const CityGoodDef*     findGood( const hashed_string& id ) const { return _goodCatalog.find( id ); }
        /** @brief 집 건물(처음 나오는 `kind="House"`)을 반환합니다. */
        const CityBuildingDef*   findHouseBuilding() const;
        const CityHouseLevelDef* findHouseLevel( int32 level ) const;

        const vector<CityBuildingDef>&   getBuildings() const { return _buildingCatalog.getAll(); }
        const vector<CityGoodDef>&       getGoods() const { return _goodCatalog.getAll(); }
        const vector<CityHouseLevelDef>& getHouseLevels() const { return _listHouseLevel; }
        int32                            getRoadCost() const { return _roadCost; }

    private:
        static constexpr const utf8* kXMLRootName = "CityCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        GameCatalog<CityBuildingDef> _buildingCatalog;
        GameCatalog<CityGoodDef>     _goodCatalog;
        vector<CityHouseLevelDef>    _listHouseLevel;
        int32                        _roadCost;
    };
} // namespace sw
