#include "pch.h"

#include "GameFramework/Kits/Strategy/CityBuilder/CityCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

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

            [[nodiscard]] static bool parseKind( string_view text, CityBuildingKind& outKind )
            {
                constexpr const utf8* kArrKindName[] = { "House", "Service", "Producer", "Storage", "Market", "Decoration" };
                for ( uint32 kindIndex = 0; kindIndex < 6; ++kindIndex )
                {
                    if ( StringUtil::equals( text, string_view( kArrKindName[kindIndex] ), true ) )
                    {
                        outKind = static_cast<CityBuildingKind>( kindIndex );
                        return true;
                    }
                }
                return false;
            }

            [[nodiscard]] static bool parseTerrain( string_view text, CityTerrain& outTerrain )
            {
                constexpr const utf8* kArrTerrainName[] = { "Grass", "Sand", "Floodplain", "Water", "Rock" };
                for ( uint32 terrainIndex = 0; terrainIndex < 5; ++terrainIndex )
                {
                    if ( StringUtil::equals( text, string_view( kArrTerrainName[terrainIndex] ), true ) )
                    {
                        outTerrain = static_cast<CityTerrain>( terrainIndex );
                        return true;
                    }
                }
                return false;
            }

            static void parseGoodList( string_view text, vector<hashed_string>& outListGood )
            {
                outListGood.clear();
                GameDataXml::forEachToken( text, ",; ", [&]( string_view token )
                { outListGood.push_back( hashed_string( string( token ).c_str() ) ); } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool parseCityService( string_view text, CityService& outService )
    {
        for ( const CityCatalogInternal::NamedService& entry : CityCatalogInternal::kArrServiceName )
        {
            if ( StringUtil::equals( text, string_view( entry._pName ), true ) )
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

    const utf8* toString( CityBuildingKind kind )
    {
        switch ( kind )
        {
            case CityBuildingKind::House:
                return "House";
            case CityBuildingKind::Service:
                return "Service";
            case CityBuildingKind::Producer:
                return "Producer";
            case CityBuildingKind::Storage:
                return "Storage";
            case CityBuildingKind::Market:
                return "Market";
            case CityBuildingKind::Decoration:
                return "Decoration";
        }
        return "?";
    }

    CityCatalog::CityCatalog()
        : _buildingCatalog{}
        , _goodCatalog{}
        , _listHouseLevel{}
        , _roadCost{ 2 }
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

    uint32 CityCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _roadCost = MathUtil::max( 0, root.getAttributeInt( "roadCost", _roadCost ) );
        for ( XmlNode node = root.findChild( "Good" ); node; node = node.findNextSibling( "Good" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            CityGoodDef good;
            good._id          = hashed_string( pId );
            const utf8* pName = node.findAttribute( "name" );
            good._name        = pName != nullptr ? pName : pId;
            good._price       = MathUtil::max( 0, node.getAttributeInt( "price", good._price ) );
            good._bFood       = node.getAttributeBool( "food", false ) ? SW_TRUE : SW_FALSE;
            (void)_goodCatalog.add( good );
        }

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Building" ); node; node = node.findNextSibling( "Building" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            CityBuildingDef building;
            building._id               = hashed_string( pId );
            const utf8* pName          = node.findAttribute( "name" );
            building._name             = pName != nullptr ? pName : pId;
            const string_view kindText = node.getAttributeText( "kind" );
            if ( kindText.empty() == false && CityCatalogInternal::parseKind( kindText, building._kind ) == false )
                SW_LOG_WARNING( "%#: building '%#' has an unknown kind '%#'", sourceName, pId, kindText );
            const string_view serviceText = node.getAttributeText( "service" );
            if ( serviceText.empty() == false && parseCityService( serviceText, building._service ) == false )
                SW_LOG_WARNING( "%#: building '%#' has an unknown service '%#'", sourceName, pId, serviceText );
            building._delivery            = StringUtil::equals( node.getAttributeText( "delivery" ), string_view( "Radius" ), true ) ? CityDelivery::Radius : CityDelivery::Walker;
            const string_view terrainText = node.getAttributeText( "terrain" );
            if ( terrainText.empty() == false )
            {
                if ( CityCatalogInternal::parseTerrain( terrainText, building._requiredTerrain ) )
                    building._bRequiresTerrain = SW_TRUE;
                else
                    SW_LOG_WARNING( "%#: building '%#' needs an unknown terrain '%#'", sourceName, pId, terrainText );
            }
            CityCatalogInternal::parseGoodList( node.getAttributeText( "goods" ), building._listGood );
            for ( const hashed_string& goodId : building._listGood )
            {
                if ( _goodCatalog.find( goodId ) == nullptr )
                    SW_LOG_WARNING( "%#: building '%#' names an unknown good '%#'", sourceName, pId, goodId.c_str() );
            }
            building._size               = MathUtil::clamp( node.getAttributeInt( "size", building._size ), 1, 6 );
            building._cost               = MathUtil::max( 0, node.getAttributeInt( "cost", building._cost ) );
            building._workers            = MathUtil::max( 0, node.getAttributeInt( "workers", building._workers ) );
            building._range              = MathUtil::max( 1, node.getAttributeInt( "range", building._range ) );
            building._walkerInterval     = MathUtil::max( 0.5f, node.getAttributeFloat( "walkerInterval", building._walkerInterval ) );
            building._productionTime     = MathUtil::max( 0.5f, node.getAttributeFloat( "productionTime", building._productionTime ) );
            building._productionAmount   = MathUtil::max( 1, node.getAttributeInt( "amount", building._productionAmount ) );
            building._capacity           = MathUtil::max( 0, node.getAttributeInt( "capacity", building._capacity ) );
            building._desirability       = node.getAttributeInt( "desirability", building._desirability );
            building._desirabilityRadius = MathUtil::clamp( node.getAttributeInt( "desirabilityRadius", building._desirabilityRadius ), 0, 8 );
            if ( building._kind == CityBuildingKind::Service && building._service == CityService::Count )
                SW_LOG_WARNING( "%#: service building '%#' names no service", sourceName, pId );
            (void)_buildingCatalog.add( building );
            ++loadedCount;
        }

        _listHouseLevel.clear();
        for ( XmlNode node = root.findChild( "HouseLevel" ); node; node = node.findNextSibling( "HouseLevel" ) )
        {
            CityHouseLevelDef level;
            const utf8*       pName = node.findAttribute( "name" );
            level._name             = pName != nullptr ? pName : "House";
            level._population       = MathUtil::max( 1, node.getAttributeInt( "population", level._population ) );
            level._taxPerPerson     = MathUtil::max( 0, node.getAttributeInt( "tax", level._taxPerPerson ) );
            level._minDesirability  = node.getAttributeInt( "desirability", level._minDesirability );
            GameDataXml::forEachToken( node.getAttributeText( "services" ), ",; ", [&]( string_view token )
            {
                CityService service = CityService::Count;
                if ( parseCityService( token, service ) )
                    level._serviceMask = static_cast<uint8>( level._serviceMask | makeCityServiceBit( service ) );
                else
                    SW_LOG_WARNING( "%#: house level '%#' needs an unknown service '%#'", sourceName, level._name.c_str(), token );
            } );
            CityCatalogInternal::parseGoodList( node.getAttributeText( "goods" ), level._listRequiredGood );
            _listHouseLevel.push_back( level );
        }
        if ( _listHouseLevel.empty() )
        {
            SW_LOG_WARNING( "%#: no <HouseLevel> entries - houses stay at one default level", sourceName );
            _listHouseLevel.push_back( CityHouseLevelDef{} );
        }
        if ( findHouseBuilding() == nullptr )
            SW_LOG_WARNING( "%#: no building has kind=\"House\" - nobody can live in this city", sourceName );
        return loadedCount;
    }
} // namespace sw
