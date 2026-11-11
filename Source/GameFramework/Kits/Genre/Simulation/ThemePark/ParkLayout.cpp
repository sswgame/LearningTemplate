#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    ParkLayout::ParkLayout()
        : _listPlacement{}
        , _gatePosition{}
        , _entryFee{ 0 }
        , _startingCash{ 0 }
        , _pCoasterLayouts{}
    {
    }

    bool ParkLayout::loadFromAsset( const AssetID& assetID )
    {
        _pCoasterLayouts.reset();
        const string& layoutPath = assetID.str();
        string        coasterPath;
        if ( const expected<void> path = GamePackDataUtil::tryMakeSiblingKindPath( layoutPath, kDataKind, CoasterLayoutCatalog::kDataKind, coasterPath ); path.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ParkLayout.CoasterLayoutsNoPath", "%#: the coaster layouts have no path - %# (%#)", layoutPath.c_str(), path.error().message(), path.error().detail() );
            return false;
        }
        // 코스터 레이아웃은 공유 캐시의 한 벌이다 — 배치를 쓰는 쪽과 같은 객체를 읽고 배치가 쥔다.
        shared_ptr<const CoasterLayoutCatalog> pLayouts = SharedAssetCache<CoasterLayoutCatalog>::acquire( coasterPath );
        if ( pLayouts == nullptr )
            return false;
        if ( GameDataXML::loadFromAsset( *this, &ParkLayout::loadRoot, *pLayouts, assetID, "ParkLayout" ) == false )
            return false;
        _pCoasterLayouts = std::move( pLayouts );
        return true;
    }

    bool ParkLayout::loadFromXMLText( string_view xmlText, const CoasterLayoutCatalog& layouts, string_view sourceName )
    {
        return GameDataXML::loadFromXMLText( *this, &ParkLayout::loadRoot, layouts, xmlText, sourceName, "ParkLayout" );
    }

    bool ParkLayout::loadRoot( const XMLNode& root, const CoasterLayoutCatalog& layouts, string_view sourceName )
    {
        // 형식 · 필수 · 범위는 스키마(ParkLayoutRootDef · ParkFlatRideDef · ParkCoasterDef)가 정한다. 여기에는 기본값 리터럴이 없다.
        ParkLayoutRootDef rootDef;
        const bool        bRoot = XMLFields::readProperties( root, rootDef, sourceName );
        _gatePosition           = float3{ rootDef._gateX, 0.0f, rootDef._gateZ };
        _entryFee               = rootDef._entryFee;
        _startingCash           = rootDef._startingCash;

        _listPlacement.clear();
        /** 코스터는 설계 카탈로그를 받아야 해서 문맥을 든 읽는 쪽을 둔다. 함수 안의 형식이라 이 클래스의 비공개를 볼 수 있다. */
        struct ParkElementReader
        {
            ParkLayout&                 _layout;
            const CoasterLayoutCatalog& _layouts;

            bool loadFlatRide( const XMLNode& node, string_view sourceName ) { return _layout.loadFlatRide( node, sourceName ); }
            bool loadCoaster( const XMLNode& node, string_view sourceName ) { return _layout.loadCoaster( node, _layouts, sourceName ); }
        };
        using Element                          = XMLCatalogElement<ParkElementReader>;
        static constexpr Element kArrElement[] = {
            Element::of<&ParkElementReader::loadFlatRide>( "FlatRide" ).uncounted(),
            Element::of<&ParkElementReader::loadCoaster>( "Coaster" ).uncounted(),
        };
        ParkElementReader reader{ *this, layouts };
        const bool        bElements = XMLCatalogReadResult::readInTableOrder( reader, root, sourceName, kArrElement ).isClean();
        return bRoot && bElements && _listPlacement.empty() == false;
    }

    bool ParkLayout::loadFlatRide( const XMLNode& node, string_view sourceName )
    {

        ParkFlatRideDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false )
        {
            return false;
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
        return true;
    }

    bool ParkLayout::loadCoaster( const XMLNode& node, const CoasterLayoutCatalog& layouts, string_view sourceName )
    {

        ParkCoasterDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false )
        {
            return false;
        }
        const CoasterLayoutDef* pLayout = layouts.find( hashed_string( def._layout ) );
        if ( pLayout == nullptr )
        {
            SW_LOG_ERROR_ID( "ParkLayout.CoasterLayoutNotCoaster", "%#: <Coaster> layout '%#' is not in the coaster catalog - skipped", sourceName, def._layout.c_str() );
            return false;
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
        return true;
    }
} // namespace sw
