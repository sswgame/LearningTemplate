#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"

SW_LOG_CALLER( "ParkLayout" );
namespace sw
{
    ParkLayout::ParkLayout()
        : _listPlacement{}
        , _gatePosition{ 0.0f, 0.0f, -30.0f }
        , _entryFee{ 0 }
        , _startingCash{ 12000 }
    {
    }

    bool ParkLayout::loadFromResource( string_view path, const CoasterLayoutCatalog& layouts )
    {
        return GameDataXML::loadFile( *this, &ParkLayout::loadRoot, layouts, path, "ParkLayout" );
    }

    bool ParkLayout::loadFromXMLText( string_view xmlText, const CoasterLayoutCatalog& layouts, string_view sourceName )
    {
        return GameDataXML::loadText( *this, &ParkLayout::loadRoot, layouts, xmlText, sourceName, "ParkLayout" );
    }

    bool ParkLayout::loadRoot( const XMLNode& root, const CoasterLayoutCatalog& layouts, string_view sourceName )
    {
        _gatePosition = float3{ root.getAttributeFloat( "gateX", 0.0f ), 0.0f, root.getAttributeFloat( "gateZ", -30.0f ) };
        _entryFee     = MathUtil::max( 0, root.getAttributeInt( "entryFee", 0 ) );
        _startingCash = root.getAttributeInt( "startingCash", _startingCash );

        _listPlacement.clear();
        for ( XMLNode node = root.findChild( "FlatRide" ); node; node = node.findNextSibling( "FlatRide" ) )
        {
            const utf8* pId = GameDataXML::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ParkRidePlacement placement;
            ParkRide&         ride     = placement._ride;
            ride._id                   = hashed_string( pId );
            const utf8* pName          = node.findAttribute( "name" );
            ride._name                 = pName != nullptr ? pName : pId;
            ride._excitement           = node.getAttributeFloat( "excitement", ride._excitement );
            ride._intensity            = node.getAttributeFloat( "intensity", ride._intensity );
            ride._nausea               = node.getAttributeFloat( "nausea", ride._nausea );
            ride._cycleTime            = MathUtil::max( 5.0f, node.getAttributeFloat( "cycleTime", ride._cycleTime ) );
            ride._capacity             = MathUtil::max( 1, node.getAttributeInt( "capacity", ride._capacity ) );
            ride._price                = MathUtil::max( 0, node.getAttributeInt( "price", ride._price ) );
            ride._runningCostPerMinute = MathUtil::max( 0, node.getAttributeInt( "runningCost", ride._runningCostPerMinute ) );
            placement._position        = float3{ node.getAttributeFloat( "x", 0.0f ), 0.0f, node.getAttributeFloat( "z", 0.0f ) };
            const float32 sizeZ        = node.getAttributeFloat( "sizeZ", 4.0f );
            ride._entrance             = float3{ node.getAttributeFloat( "entranceX", placement._position._x ), 0.0f,
                                     node.getAttributeFloat( "entranceZ", placement._position._z - 4.0f - sizeZ * 0.5f ) };
            placement._buildCost       = MathUtil::max( 0, node.getAttributeInt( "cost", placement._buildCost ) );
            const utf8* pShape         = node.findAttribute( "shape" );
            placement._shape           = pShape != nullptr ? pShape : "Cylinder";
            placement._size            = float3{ node.getAttributeFloat( "sizeX", 4.0f ), node.getAttributeFloat( "sizeY", 1.0f ), sizeZ };
            placement._color           = GameDataXML::parseFloat4( node.getAttributeText( "color" ), placement._color );
            placement._spin            = node.getAttributeFloat( "spin", 0.0f );
            _listPlacement.push_back( placement );
        }
        for ( XMLNode node = root.findChild( "Coaster" ); node; node = node.findNextSibling( "Coaster" ) )
        {
            const utf8*             pLayoutId = node.findAttribute( "layout" );
            const CoasterLayoutDef* pLayout   = StringUtil::isNullOrEmpty( pLayoutId ) ? nullptr : layouts.findLayout( hashed_string( pLayoutId ) );
            if ( pLayout == nullptr )
            {
                SW_LOG_WARNING( "%#: <Coaster> layout '%#' is not in the coaster catalog - skipped", sourceName, pLayoutId != nullptr ? pLayoutId : "" );
                continue;
            }
            ParkRidePlacement placement;
            placement._layoutId       = pLayout->_id;
            placement._ride._id       = pLayout->_id;
            placement._ride._name     = pLayout->_name;
            placement._ride._capacity = MathUtil::max( 1, node.getAttributeInt( "capacity", 24 ) );
            placement._position       = float3{ node.getAttributeFloat( "x", 0.0f ), 0.0f, node.getAttributeFloat( "z", 0.0f ) };
            placement._heading        = node.getAttributeFloat( "heading", 0.0f );
            placement._loadTime       = MathUtil::max( 0.0f, node.getAttributeFloat( "loadTime", 15.0f ) );
            placement._buildCost      = MathUtil::max( 0, node.getAttributeInt( "cost", 5000 ) );
            placement._color          = GameDataXML::parseFloat4( node.getAttributeText( "color" ), placement._color );
            _listPlacement.push_back( placement );
        }
        return _listPlacement.empty() == false;
    }
} // namespace sw
