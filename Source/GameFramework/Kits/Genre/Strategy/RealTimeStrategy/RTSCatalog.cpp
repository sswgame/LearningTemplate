#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "RTSCatalog" );

    RTSCatalog::RTSCatalog()
        : _catalog{}
        , _supplyMax{ 200 }
    {
    }

    void RTSCatalog::findProducts( const hashed_string& producerID, vector<const RTSUnitDef*>& outListDef ) const
    {
        outListDef.clear();
        for ( const RTSUnitDef& def : _catalog.getAll() )
        {
            if ( def._producedBy == producerID )
                outListDef.push_back( &def );
        }
    }

    uint32 RTSCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        _supplyMax         = MathUtil::max( 1, root.getAttributeInt( "supplyMax", _supplyMax ) );
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Unit" ); node; node = node.findNextSibling( "Unit" ) )
        {
            const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            RTSUnitDef def;
            def._id                = hashed_string( pID );
            const utf8* pName      = node.findAttribute( "name" );
            def._name              = pName != nullptr ? pName : pID;
            const string_view kind = node.getAttributeText( "kind" );
            if ( StringUtil::equals( kind, string_view( "Building" ), true ) )
                def._kind = RTSUnitKind::Building;
            else if ( StringUtil::equals( kind, string_view( "Resource" ), true ) )
                def._kind = RTSUnitKind::Resource;
            else if ( kind.empty() == false && StringUtil::equals( kind, string_view( "Unit" ), true ) == false )
                SW_LOG_WARNING( "%#: '%#' has an unknown kind '%#' - read as a unit", sourceName, pID, kind );
            const string_view resource = node.getAttributeText( "resource" );
            if ( StringUtil::equals( resource, string_view( "Minerals" ), true ) )
                def._resourceType = RTSResourceType::Minerals;
            else if ( StringUtil::equals( resource, string_view( "Gas" ), true ) )
                def._resourceType = RTSResourceType::Gas;
            const utf8* pProducedBy   = node.findAttribute( "producedBy" );
            const utf8* pRequires     = node.findAttribute( "requires" );
            def._producedBy           = pProducedBy != nullptr ? hashed_string( pProducedBy ) : hashed_string{};
            def._requires             = pRequires != nullptr ? hashed_string( pRequires ) : hashed_string{};
            def._hp                   = MathUtil::max( 1.0f, node.getAttributeFloat( "hp", def._hp ) );
            def._armor                = MathUtil::max( 0.0f, node.getAttributeFloat( "armor", def._armor ) );
            def._speed                = MathUtil::max( 0.0f, node.getAttributeFloat( "speed", def._kind == RTSUnitKind::Unit ? def._speed : 0.0f ) );
            def._radius               = MathUtil::max( 0.1f, node.getAttributeFloat( "radius", def._radius ) );
            def._sight                = MathUtil::max( 0.0f, node.getAttributeFloat( "sight", def._sight ) );
            def._buildTime            = MathUtil::max( 0.1f, node.getAttributeFloat( "buildTime", def._buildTime ) );
            def._damage               = MathUtil::max( 0.0f, node.getAttributeFloat( "damage", def._damage ) );
            def._range                = MathUtil::max( 0.0f, node.getAttributeFloat( "range", def._range ) );
            def._cooldown             = MathUtil::max( 0.05f, node.getAttributeFloat( "cooldown", def._cooldown ) );
            def._gatherTime           = MathUtil::max( 0.1f, node.getAttributeFloat( "gatherTime", def._gatherTime ) );
            def._minerals             = MathUtil::max( 0, node.getAttributeInt( "minerals", def._minerals ) );
            def._gas                  = MathUtil::max( 0, node.getAttributeInt( "gas", def._gas ) );
            def._supplyCost           = MathUtil::max( 0, node.getAttributeInt( "supply", def._supplyCost ) );
            def._supplyProvided       = MathUtil::max( 0, node.getAttributeInt( "provides", def._supplyProvided ) );
            def._footprint            = MathUtil::clamp( node.getAttributeInt( "footprint", def._kind == RTSUnitKind::Unit ? 1 : 2 ), 1, 8 );
            def._cargo                = MathUtil::max( 1, node.getAttributeInt( "cargo", def._cargo ) );
            def._resourceAmount       = MathUtil::max( 0, node.getAttributeInt( "amount", def._resourceAmount ) );
            def._bWorker              = node.getAttributeBool( "worker", false ) ? SW_TRUE : SW_FALSE;
            def._bDepot               = node.getAttributeBool( "depot", false ) ? SW_TRUE : SW_FALSE;
            def._bAir                 = node.getAttributeBool( "air", false ) ? SW_TRUE : SW_FALSE;
            def._bExtractor           = node.getAttributeBool( "extractor", false ) ? SW_TRUE : SW_FALSE;
            bool              bGround = true;
            bool              bAir    = false;
            const string_view targets = node.getAttributeText( "targets" );
            if ( targets.empty() == false )
            {
                bGround = false;
                GameDataXML::forEachToken( targets, ",; ", [&]( string_view token )
                {
                    bGround = bGround || StringUtil::equals( token, string_view( "Ground" ), true );
                    bAir    = bAir || StringUtil::equals( token, string_view( "Air" ), true );
                } );
            }
            def._bTargetsGround = bGround ? SW_TRUE : SW_FALSE;
            def._bTargetsAir    = bAir ? SW_TRUE : SW_FALSE;
            if ( def._kind == RTSUnitKind::Building || def._kind == RTSUnitKind::Resource )
                def._radius = static_cast<float32>( def._footprint ) * 0.5f;
            (void)_catalog.add( def );
            ++loadedCount;
        }
        for ( const RTSUnitDef& def : _catalog.getAll() )
        {
            if ( def._producedBy.empty() == false && _catalog.find( def._producedBy ) == nullptr )
                SW_LOG_WARNING( "%#: '%#' is produced by unknown '%#'", sourceName, def._id.c_str(), def._producedBy.c_str() );
            if ( def._requires.empty() == false && _catalog.find( def._requires ) == nullptr )
                SW_LOG_WARNING( "%#: '%#' requires unknown '%#'", sourceName, def._id.c_str(), def._requires.c_str() );
        }
        return loadedCount;
    }
} // namespace sw
