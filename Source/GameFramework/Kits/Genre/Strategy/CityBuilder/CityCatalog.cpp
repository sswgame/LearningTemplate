#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    CityCatalog::CityCatalog()
        : _buildingCatalog{}
        , _goodCatalog{}
        , _listHouseLevel{}
        , _roadCost{ CityCatalogRootDef{}._roadCost }
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

    uint32 CityCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        CityCatalogRootDef rootDef;
        if ( root.readProperties( rootDef, sourceName ) )
            _roadCost = rootDef._roadCost;
        _listHouseLevel.clear();
        using Element                          = XMLCatalogElement<CityCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&CityCatalog::_goodCatalog>( "Good" ).uncounted(),
            Element::of<&CityCatalog::loadBuilding>( "Building" ),
            Element::of<&CityCatalog::loadHouseLevel>( "HouseLevel" ).uncounted(),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );

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
        bool            bValid = XMLFields::readProperties( node, building, sourceName, { "terrain" } );
        // 땅 조건은 적었을 때만 있다(적지 않으면 아무 땅). 모르는 땅 이름은 오류다.
        const string_view terrainText = node.getAttributeText( "terrain" );
        if ( terrainText.empty() == false )
        {
            building._bRequiresTerrain = game::getSubsystem<TypeRegistry>().enumFromString( terrainText, building._requiredTerrain );
            if ( building._bRequiresTerrain == false )
            {
                SW_LOG_ERROR_ID( "CityCatalog.BuildingNeedsUnknownTerrain", "%#: building '%#' needs an unknown terrain '%#'", sourceName, building._id.c_str(), terrainText );
                bValid = false;
            }
        }
        const bool bServiceWithoutService = building._kind == CityBuildingKind::Service && building._service == CityService::Count;
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
        bool              bValid = XMLFields::readProperties( node, level, sourceName, { "services" } );
        StringUtil::forEachToken( node.getAttributeText( "services" ), ",; ", [&]( string_view token )
        {
            CityService service = CityService::Count;
            if ( game::getSubsystem<TypeRegistry>().enumFromString( token, service ) && service != CityService::Count )
                level._serviceMask = static_cast<uint8>( level._serviceMask | makeCityServiceBit( service ) );
            else
            {
                SW_LOG_ERROR_ID( "CityCatalog.HouseLevelNeedsUnknown", "%#: house level '%#' needs an unknown service '%#'", sourceName, level._name.c_str(), token );
                bValid = false;
            }
        } );
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
