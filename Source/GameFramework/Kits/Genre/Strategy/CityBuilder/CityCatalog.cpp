#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/EnumNameTable.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "CityCatalog" );

    namespace
    {
        struct CityCatalogInternal
        {
            struct NamedService
            {
                CityService _service;
                const utf8* _pName;
            };

            static constexpr NamedService kArrServiceName[] = {
                {        CityService::Water,         "Water"},
                {     CityService::Religion,      "Religion"},
                {CityService::Entertainment, "Entertainment"},
                {       CityService::Health,        "Health"},
                {    CityService::Education,     "Education"},
                {          CityService::Tax,           "Tax"},
            };

            static constexpr const utf8*                kArrTerrainName[] = { "Grass", "Sand", "Floodplain", "Water", "Rock" };
            static constexpr EnumNameTable<CityTerrain> kTerrainTable{ kArrTerrainName };
            static_assert( kTerrainTable.getCount() == static_cast<size_t>( CityTerrain::Rock ) + 1, "terrain names must cover every CityTerrain" );

            [[nodiscard]] static bool parseTerrain( string_view text, CityTerrain& outTerrain ) { return kTerrainTable.parse( text, outTerrain ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool parseCityService( string_view text, CityService& outService )
    {
        for ( const CityCatalogInternal::NamedService& entry : CityCatalogInternal::kArrServiceName )
        {
            if ( StringUtil::equals( text, string_view( entry._pName ) ) )
            {
                outService = entry._service;
                return true;
            }
        }
        return false;
    }

    const utf8* toString( CityService service )
    {
        for ( const CityCatalogInternal::NamedService& entry : CityCatalogInternal::kArrServiceName )
        {
            if ( entry._service == service )
                return entry._pName;
        }
        return "None";
    }

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

    uint32 CityCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        CityCatalogRootDef rootDef;
        if ( GameDataXML::readFields( root, rootDef, sourceName ) )
            _roadCost = rootDef._roadCost;
        _listHouseLevel.clear();
        uint32 buildingCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name    = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            const bool        bLoaded = loadElement( node, name, sourceName );
            if ( bLoaded && StringUtil::equals( name, string_view( "Building" ) ) )
                ++buildingCount;
        }

        // 건물이 가리키는 물자는 모두 읽은 뒤에 확인한다(건물이 물자보다 앞에 적혀도 된다).
        for ( const CityBuildingDef& building : _buildingCatalog.getAll() )
        {
            for ( const hashed_string& goodID : building._listGood )
            {
                if ( _goodCatalog.find( goodID ) == nullptr )
                    SW_LOG_ERROR( "%#: building '%#' names an unknown good '%#'", sourceName, building._id.c_str(), goodID.c_str() );
            }
        }
        // 집 단계가 없으면 집이 자랄 곳이 없다 — 기본 단계를 지어내지 않고 로드 실패로 본다.
        if ( _listHouseLevel.empty() )
        {
            SW_LOG_ERROR( "%#: no <HouseLevel> entries - houses have no level to live at", sourceName );
            return 0;
        }
        if ( findHouseBuilding() == nullptr )
            SW_LOG_ERROR( "%#: no building has kind=\"House\" - nobody can live in this city", sourceName );
        return buildingCount;
    }

    bool CityCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "Good" ) )
        {
            CityGoodDef good;
            if ( GameDataXML::readFields( node, good, sourceName ) == false )
                return false;
            (void)_goodCatalog.add( good ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
            return true;
        }
        if ( XMLReadUtil::isElement( name, "Building" ) )
            return loadBuilding( node, sourceName );
        if ( XMLReadUtil::isElement( name, "HouseLevel" ) )
            return loadHouseLevel( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool CityCatalog::loadBuilding( const XMLNode& node, string_view sourceName )
    {
        CityBuildingDef building;
        bool            bValid = GameDataXML::readFields( node, building, sourceName, { "terrain", "goods" } );
        // 땅 조건은 적었을 때만 있다(적지 않으면 아무 땅). 모르는 땅 이름은 오류다.
        const string_view terrainText = node.getAttributeText( "terrain" );
        if ( terrainText.empty() == false )
        {
            building._bRequiresTerrain = CityCatalogInternal::parseTerrain( terrainText, building._requiredTerrain );
            if ( building._bRequiresTerrain == false )
            {
                SW_LOG_ERROR( "%#: building '%#' needs an unknown terrain '%#'", sourceName, building._id.c_str(), terrainText );
                bValid = false;
            }
        }
        XMLReadUtil::parseNameList( node.getAttributeText( "goods" ), ",; ", building._listGood );
        const bool bServiceWithoutService = building._kind == CityBuildingKind::Service && building._service == CityService::Count;
        if ( bServiceWithoutService )
        {
            SW_LOG_ERROR( "%#: service building '%#' needs service=\"...\"", sourceName, building._id.c_str() );
            bValid = false;
        }
        if ( bValid )
            (void)_buildingCatalog.add( building ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool CityCatalog::loadHouseLevel( const XMLNode& node, string_view sourceName )
    {
        CityHouseLevelDef level;
        bool              bValid = GameDataXML::readFields( node, level, sourceName, { "services", "goods" } );
        StringUtil::forEachToken( node.getAttributeText( "services" ), ",; ", [&]( string_view token )
        {
            CityService service = CityService::Count;
            if ( parseCityService( token, service ) )
                level._serviceMask = static_cast<uint8>( level._serviceMask | makeCityServiceBit( service ) );
            else
            {
                SW_LOG_ERROR( "%#: house level '%#' needs an unknown service '%#'", sourceName, level._name.c_str(), token );
                bValid = false;
            }
        } );
        XMLReadUtil::parseNameList( node.getAttributeText( "goods" ), ",; ", level._listRequiredGood );
        if ( bValid )
            _listHouseLevel.push_back( level ); // 적힌 순서가 단계다(0 = 처음 짓는 집)
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** @brief `CityCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( City, CityCatalog::kDataKind, "game/", nullptr, "/data/city.xml", &AssetDataKindUtil::loadCatalog<CityCatalog> );
} // namespace sw
