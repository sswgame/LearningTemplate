#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    CityCatalog::CityCatalog()
        : _terrainCatalog{}
        , _serviceCatalog{}
        , _buildingCatalog{}
        , _goodCatalog{}
        , _listHouseLevel{}
        , _roadCost{ CityCatalogRootDef{}._roadCost }
        , _rules{}
        , _rulesCount{ 0 }
    {
    }

    const CityBuildingDef* CityCatalog::findHouseBuilding() const
    {
        return _buildingCatalog.findIf( []( const CityBuildingDef& def )
        { return def._kind == CityBuildingKind::House; } );
    }

    const CityHouseLevelDef* CityCatalog::findHouseLevel( int32 level ) const
    {
        return level >= 0 && level < static_cast<int32>( _listHouseLevel.size() ) ? &_listHouseLevel[static_cast<size_t>( level )] : nullptr;
    }

    bool CityCatalog::loadRules( const XMLNode& node, string_view sourceName )
    {
        CitySettings rules;
        if ( XMLFields::readProperties( node, rules, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "CityCatalog.RulesInvalid", "%#: <Rules> has missing or out-of-range values", sourceName );
            return false;
        }
        _rules = rules; // 덧씌우는 파일(합치기)은 규칙 전체를 바꾼다
        ++_rulesCount;
        return true;
    }

    uint32 CityCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        CityCatalogRootDef rootDef;
        if ( root.readProperties( rootDef, sourceName ) )
            _roadCost = rootDef._roadCost;
        _listHouseLevel.clear();
        using Element                          = XMLCatalogElement<CityCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&CityCatalog::_terrainCatalog>( "Terrain" ).uncounted(),
            Element::of<&CityCatalog::_serviceCatalog>( "Service" ).uncounted(),
            Element::of<&CityCatalog::_goodCatalog>( "Good" ).uncounted(),
            Element::of<&CityCatalog::loadBuilding>( "Building" ),
            Element::of<&CityCatalog::loadHouseLevel>( "HouseLevel" ).uncounted(),
            Element::of<&CityCatalog::loadRules>( "Rules" ).uncounted(),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement ); // 건물 · 집 단계가 땅 · 서비스의 행을 찾는다

        // 규칙은 데이터에만 있다 — 한 번도 `<Rules>` 를 읽지 못한 카탈로그는 로드 실패다(도시가 C++ 기본값으로 열리지 않는다, 감사 K-4).
        if ( _rulesCount == 0 )
        {
            SW_LOG_ERROR_ID( "CityCatalog.RulesMissing", "%#: <CityCatalog> needs one <Rules .../> line (every rule value is data)", sourceName );
            return 0;
        }
        // 땅이 없으면 칸이 무엇인지 모른다 — 기본 땅을 지어내지 않고 로드 실패로 본다(첫 행이 새 도시의 땅이다).
        const size_t terrainCount = _terrainCatalog.getCount();
        if ( terrainCount == 0 || terrainCount > kMaxTerrainCount )
        {
            SW_LOG_ERROR_ID( "CityCatalog.TerrainCount", "%#: %# <Terrain> entries - a city needs 1..%# (the first one is the ground of a new city)", sourceName, terrainCount, kMaxTerrainCount );
            return 0;
        }
        // 집 단계가 없으면 집이 자랄 곳이 없다 — 기본 단계를 지어내지 않고 로드 실패로 본다.
        if ( _listHouseLevel.empty() )
        {
            SW_LOG_ERROR_ID( "CityCatalog.NoHouseLevelEntriesHouses", "%#: no <HouseLevel> entries - houses have no level to live at", sourceName );
            return 0;
        }
        if ( findHouseBuilding() == nullptr )
            SW_LOG_ERROR_ID( "CityCatalog.NoBuildingKindHouse", "%#: no building has kind=\"House\" - nobody can live in this city", sourceName );
        return result._loadedCount;
    }

    bool CityCatalog::loadBuilding( const XMLNode& node, string_view sourceName )
    {
        CityBuildingDef building;
        bool            bValid = XMLFields::readProperties( node, building, sourceName );
        // 땅 조건 · 서비스는 적었을 때만 있다. 없는 행의 이름은 오류다.
        if ( building._requiredTerrain.empty() == false )
        {
            const optional<int32> terrain = _terrainCatalog.findIndex( building._requiredTerrain );
            if ( terrain.has_value() == false )
            {
                SW_LOG_ERROR_ID( "CityCatalog.BuildingNeedsUnknownTerrain", "%#: building '%#' needs an unknown terrain '%#'", sourceName, building._id.c_str(), building._requiredTerrain.c_str() );
                bValid = false;
            }
            building._terrainIndex = terrain.value_or( constant::invalid_index::kInt32 );
        }
        if ( building._service.empty() == false )
        {
            const optional<int32> service = _serviceCatalog.findIndex( building._service );
            if ( service.has_value() == false )
            {
                SW_LOG_ERROR_ID( "CityCatalog.BuildingGivesUnknownService", "%#: building '%#' gives an unknown service '%#'", sourceName, building._id.c_str(), building._service.c_str() );
                bValid = false;
            }
            building._serviceIndex = service.value_or( constant::invalid_index::kInt32 );
        }
        const bool bServiceWithoutService = building._kind == CityBuildingKind::Service && building._service.empty();
        if ( bServiceWithoutService )
        {
            SW_LOG_ERROR_ID( "CityCatalog.ServiceBuildingNeedsService", "%#: service building '%#' needs service=\"...\"", sourceName, building._id.c_str() );
            bValid = false;
        }
        if ( bValid )
            (void)_buildingCatalog.add( building ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool CityCatalog::loadHouseLevel( const XMLNode& node, string_view sourceName )
    {
        CityHouseLevelDef level;
        bool              bValid = XMLFields::readProperties( node, level, sourceName );
        for ( const hashed_string& serviceID : level._listRequiredService )
        {
            if ( const optional<int32> service = _serviceCatalog.findIndex( serviceID ) )
            {
                level._listServiceIndex.push_back( *service );
            }
            else
            {
                SW_LOG_ERROR_ID( "CityCatalog.HouseLevelNeedsUnknown", "%#: house level '%#' needs an unknown service '%#'", sourceName, level._name.c_str(), serviceID.c_str() );
                bValid = false;
            }
        }
        if ( bValid )
            _listHouseLevel.push_back( level ); // 적힌 순서가 단계다(0 = 처음 짓는 집)
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** @brief `CityCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( City, CityCatalog, SW_KIT_API, CityCatalog::kAssetType.withLoader( AssetTypeLoader::of<CityCatalog>( MemoryTag::Game ) ) );
} // namespace sw
