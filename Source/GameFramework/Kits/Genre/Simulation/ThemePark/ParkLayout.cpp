#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"

SW_LOG_CALLER( "ParkLayout" );
namespace sw
{
    ParkLayout::ParkLayout()
        : _listPlacement{}
        , _gatePosition{}
        , _entryFee{ 0 }
        , _startingCash{ 0 }
    {
    }

    bool ParkLayout::loadFromResource( const ResourceID& resourceID, const CoasterLayoutCatalog& layouts )
    {
        return GameDataXML::loadFile( *this, &ParkLayout::loadRoot, layouts, resourceID, "ParkLayout" );
    }

    bool ParkLayout::loadFromXMLText( string_view xmlText, const CoasterLayoutCatalog& layouts, string_view sourceName )
    {
        return GameDataXML::loadText( *this, &ParkLayout::loadRoot, layouts, xmlText, sourceName, "ParkLayout" );
    }

    bool ParkLayout::loadRoot( const XMLNode& root, const CoasterLayoutCatalog& layouts, string_view sourceName )
    {
        // 형식 · 필수 · 범위는 스키마(ParkLayoutRootDef · ParkFlatRideDef · ParkCoasterDef)가 정한다. 여기에는 기본값 리터럴이 없다.
        ParkLayoutRootDef rootDef;
        bool              bOk = GameDataXML::readFields( root, rootDef, sourceName );
        _gatePosition         = float3{ rootDef._gateX, 0.0f, rootDef._gateZ };
        _entryFee             = rootDef._entryFee;
        _startingCash         = rootDef._startingCash;

        _listPlacement.clear();
        for ( XMLNode node = root.findChild( "FlatRide" ); node; node = node.findNextSibling( "FlatRide" ) )
        {
            ParkFlatRideDef def;
            if ( GameDataXML::readFields( node, def, sourceName ) == false )
            {
                bOk = false;
                continue;
            }
            ParkRidePlacement placement;
            ParkRide&         ride     = placement._ride;
            ride._id                   = hashed_string( def._id );
            ride._name                 = def._name;
            ride._excitement           = def._excitement;
            ride._intensity            = def._intensity;
            ride._nausea               = def._nausea;
            ride._cycleTime            = def._cycleTime;
            ride._capacity             = def._capacity;
            ride._price                = def._price;
            ride._runningCostPerMinute = def._runningCost;
            ride._entrance             = float3{ def._entranceX, 0.0f, def._entranceZ };
            placement._position        = float3{ def._x, 0.0f, def._z };
            placement._buildCost       = def._cost;
            placement._shape           = def._shape;
            placement._size            = float3{ def._sizeX, def._sizeY, def._sizeZ };
            placement._color           = def._color;
            placement._spin            = def._spin;
            _listPlacement.push_back( placement );
        }
        for ( XMLNode node = root.findChild( "Coaster" ); node; node = node.findNextSibling( "Coaster" ) )
        {
            ParkCoasterDef def;
            if ( GameDataXML::readFields( node, def, sourceName ) == false )
            {
                bOk = false;
                continue;
            }
            const CoasterLayoutDef* pLayout = layouts.find( hashed_string( def._layout ) );
            if ( pLayout == nullptr )
            {
                SW_LOG_ERROR( "%#: <Coaster> layout '%#' is not in the coaster catalog - skipped", sourceName, def._layout.c_str() );
                bOk = false;
                continue;
            }
            ParkRidePlacement placement;
            placement._layoutID       = pLayout->_id;
            placement._ride._id       = pLayout->_id;
            placement._ride._name     = pLayout->_name;
            placement._ride._capacity = def._capacity;
            placement._position       = float3{ def._x, 0.0f, def._z };
            placement._heading        = def._heading;
            placement._loadTime       = def._loadTime;
            placement._buildCost      = def._cost;
            placement._color          = def._color;
            _listPlacement.push_back( placement );
        }
        return bOk && _listPlacement.empty() == false;
    }
} // namespace sw
