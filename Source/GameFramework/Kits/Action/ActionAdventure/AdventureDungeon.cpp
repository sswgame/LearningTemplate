#include "pch.h"

#include "GameFramework/Kits/Action/ActionAdventure/AdventureDungeon.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Inventory/ItemBag.h"
#include "GameFramework/Base/World/AreaGraph.h"
#include "GameFramework/Base/World/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "AdventureDungeon" );

    namespace
    {
        struct AdventureDungeonInternal
        {
            static hashed_string readName( const XmlNode& node, const utf8* pName, const hashed_string& fallback )
            {
                const utf8* pValue = node.findAttribute( pName );
                return pValue != nullptr && pValue[0] != '\0' ? hashed_string( pValue ) : fallback;
            }

            static bool isKind( string_view text, const utf8* pKind ) { return StringUtil::equals( text, string_view( pKind ), true ); }

            static AdventureDoorKind parseDoorKind( string_view text, string_view sourceName, const utf8* pId )
            {
                if ( text.empty() || isKind( text, "SmallKey" ) )
                    return AdventureDoorKind::SmallKey;
                if ( isKind( text, "BossKey" ) )
                    return AdventureDoorKind::BossKey;
                if ( isKind( text, "Condition" ) )
                    return AdventureDoorKind::Condition;
                SW_LOG_WARNING( "%#: door '%#' has an unknown kind '%#' - read as SmallKey", sourceName, pId, text );
                return AdventureDoorKind::SmallKey;
            }

            static AdventureDeviceKind parseDeviceKind( string_view text, string_view sourceName, const utf8* pId )
            {
                if ( text.empty() || isKind( text, "Switch" ) )
                    return AdventureDeviceKind::Switch;
                if ( isKind( text, "PressurePlate" ) )
                    return AdventureDeviceKind::PressurePlate;
                if ( isKind( text, "TimedSwitch" ) )
                    return AdventureDeviceKind::TimedSwitch;
                if ( isKind( text, "TorchGroup" ) )
                    return AdventureDeviceKind::TorchGroup;
                SW_LOG_WARNING( "%#: device '%#' has an unknown kind '%#' - read as Switch", sourceName, pId, text );
                return AdventureDeviceKind::Switch;
            }

            static const hashed_string& getSmallKeyName()
            {
                static const hashed_string name( "SmallKey" );
                return name;
            }
            static const hashed_string& getBossKeyName()
            {
                static const hashed_string name( "BossKey" );
                return name;
            }
            static const hashed_string& getMapName()
            {
                static const hashed_string name( "Map" );
                return name;
            }
            static const hashed_string& getCompassName()
            {
                static const hashed_string name( "Compass" );
                return name;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AdventureDoorResult result )
    {
        switch ( result )
        {
            case AdventureDoorResult::Opened:
                return "Opened";
            case AdventureDoorResult::AlreadyOpen:
                return "AlreadyOpen";
            case AdventureDoorResult::UnknownDoor:
                return "UnknownDoor";
            case AdventureDoorResult::NeedSmallKey:
                return "NeedSmallKey";
            case AdventureDoorResult::NeedBossKey:
                return "NeedBossKey";
            case AdventureDoorResult::ConditionNotMet:
                return "ConditionNotMet";
        }
        return "Unknown";
    }

    const AdventureDoorDef* AdventureDungeonDef::findDoor( const hashed_string& id ) const
    {
        for ( const AdventureDoorDef& door : _listDoor )
        {
            if ( door._id == id )
                return &door;
        }
        return nullptr;
    }

    const AdventureTreasureDef* AdventureDungeonDef::findTreasure( const hashed_string& id ) const
    {
        for ( const AdventureTreasureDef& treasure : _listTreasure )
        {
            if ( treasure._id == id )
                return &treasure;
        }
        return nullptr;
    }

    int32 AdventureDungeonDef::findDeviceIndex( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listDevice.size(); ++index )
        {
            if ( _listDevice[index]._id == id )
                return static_cast<int32>( index );
        }
        return -1;
    }

    uint32 AdventureDungeonCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode dungeonNode = root.findChild( "Dungeon" ); dungeonNode; dungeonNode = dungeonNode.findNextSibling( "Dungeon" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( dungeonNode, sourceName );
            if ( pId == nullptr )
                continue;
            AdventureDungeonDef dungeon;
            dungeon._id     = hashed_string( pId );
            dungeon._region = AdventureDungeonInternal::readName( dungeonNode, "region", dungeon._id );
            for ( XmlNode node = dungeonNode.findChild( "Door" ); node; node = node.findNextSibling( "Door" ) )
            {
                const utf8* pDoorId = GameDataXml::findRequiredId( node, sourceName );
                if ( pDoorId == nullptr )
                    continue;
                AdventureDoorDef door;
                door._id                       = hashed_string( pDoorId );
                door._flag                     = AdventureDungeonInternal::readName( node, "flag", door._id );
                door._kind                     = AdventureDungeonInternal::parseDoorKind( node.getAttributeText( "kind" ), sourceName, pDoorId );
                const string_view requiresText = node.getAttributeText( "requires" );
                door._requires                 = string( requiresText.data(), requiresText.size() );
                if ( door._kind == AdventureDoorKind::Condition && door._requires.empty() )
                    SW_LOG_WARNING( "%#: condition door '%#' has no requires - it always opens", sourceName, pDoorId );
                dungeon._listDoor.push_back( door );
            }
            for ( XmlNode node = dungeonNode.findChild( "Treasure" ); node; node = node.findNextSibling( "Treasure" ) )
            {
                const utf8* pTreasureId = GameDataXml::findRequiredId( node, sourceName );
                if ( pTreasureId == nullptr )
                    continue;
                AdventureTreasureDef treasure;
                treasure._id    = hashed_string( pTreasureId );
                treasure._area  = AdventureDungeonInternal::readName( node, "area", hashed_string{} );
                treasure._item  = AdventureDungeonInternal::readName( node, "item", hashed_string{} );
                treasure._flag  = AdventureDungeonInternal::readName( node, "flag", treasure._id );
                treasure._count = MathUtil::max( 1, node.getAttributeInt( "count", 1 ) );
                dungeon._listTreasure.push_back( treasure );
            }
            for ( XmlNode node = dungeonNode.findChild( "Device" ); node; node = node.findNextSibling( "Device" ) )
            {
                const utf8* pDeviceId = GameDataXml::findRequiredId( node, sourceName );
                if ( pDeviceId == nullptr )
                    continue;
                AdventureDeviceDef device;
                device._id         = hashed_string( pDeviceId );
                device._flag       = AdventureDungeonInternal::readName( node, "flag", device._id );
                device._kind       = AdventureDungeonInternal::parseDeviceKind( node.getAttributeText( "kind" ), sourceName, pDeviceId );
                device._duration   = MathUtil::max( 0.0f, node.getAttributeFloat( "duration", 0.0f ) );
                device._torchCount = MathUtil::max( 1, node.getAttributeInt( "torches", 1 ) );
                device._bLatch     = node.getAttributeBool( "latch", false ) ? SW_TRUE : SW_FALSE;
                if ( device._kind == AdventureDeviceKind::TimedSwitch && device._duration <= 0.0f )
                {
                    SW_LOG_WARNING( "%#: timed switch '%#' has no duration - 5 seconds", sourceName, pDeviceId );
                    device._duration = 5.0f;
                }
                dungeon._listDevice.push_back( device );
            }
            (void)_catalog.add( dungeon );
            ++loadedCount;
        }
        return loadedCount;
    }

    AdventureDungeonState::AdventureDungeonState()
        : _pCatalog{ nullptr }
        , _listRuntime{}
        , _eventBuffer{}
    {
    }

    void AdventureDungeonState::initialize( const AdventureDungeonCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listRuntime.clear();
        _eventBuffer.clear();
        if ( pCatalog == nullptr )
            return;
        _listRuntime.resize( pCatalog->getDungeons().size() );
        for ( size_t index = 0; index < _listRuntime.size(); ++index )
            _listRuntime[index]._listDevice.resize( pCatalog->getDungeons()[index]._listDevice.size() );
    }

    AdventureDungeonState::DungeonRuntime* AdventureDungeonState::findRuntime( const hashed_string& dungeonId, const AdventureDungeonDef** ppOutDef )
    {
        if ( _pCatalog == nullptr )
            return nullptr;
        const int32 index = _pCatalog->findDungeonIndex( dungeonId );
        if ( index < 0 || static_cast<size_t>( index ) >= _listRuntime.size() )
            return nullptr;
        if ( ppOutDef != nullptr )
            *ppOutDef = &_pCatalog->getDungeons()[static_cast<size_t>( index )];
        return &_listRuntime[static_cast<size_t>( index )];
    }

    bool AdventureDungeonState::addSmallKey( const hashed_string& dungeonId, int32 count )
    {
        DungeonRuntime* pRuntime = findRuntime( dungeonId, nullptr );
        if ( pRuntime == nullptr || count <= 0 )
            return false;
        pRuntime->_progress._smallKeyCount += count;
        return true;
    }

    AdventureDoorResult AdventureDungeonState::openDoor( const hashed_string& dungeonId, const hashed_string& doorId, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonId, &pDungeon );
        const AdventureDoorDef*    pDoor    = pDungeon != nullptr ? pDungeon->findDoor( doorId ) : nullptr;
        if ( pRuntime == nullptr || pDoor == nullptr )
            return AdventureDoorResult::UnknownDoor;
        if ( flags.hasFlag( pDoor->_flag ) )
            return AdventureDoorResult::AlreadyOpen;
        AdventureDungeonProgress& progress = pRuntime->_progress;
        switch ( pDoor->_kind )
        {
            case AdventureDoorKind::SmallKey:
            {
                if ( progress._smallKeyCount <= 0 )
                    return AdventureDoorResult::NeedSmallKey;
                --progress._smallKeyCount;
                ++progress._smallKeyUsedCount;
                break;
            }
            case AdventureDoorKind::BossKey:
            {
                if ( progress._bBossKey == SW_FALSE )
                    return AdventureDoorResult::NeedBossKey;
                break;
            }
            case AdventureDoorKind::Condition:
            {
                if ( flags.evaluate( pDoor->_requires ) == false )
                    return AdventureDoorResult::ConditionNotMet;
                break;
            }
        }
        flags.setFlag( pDoor->_flag, 1 );
        pushEvent( AdventureDungeonEventType::DoorOpened, dungeonId, doorId );
        return AdventureDoorResult::Opened;
    }

    bool AdventureDungeonState::openTreasure( const hashed_string& dungeonId, const hashed_string& treasureId, GameFlags& flags, ItemBag& outReward )
    {
        const AdventureDungeonDef*  pDungeon  = nullptr;
        DungeonRuntime*             pRuntime  = findRuntime( dungeonId, &pDungeon );
        const AdventureTreasureDef* pTreasure = pDungeon != nullptr ? pDungeon->findTreasure( treasureId ) : nullptr;
        if ( pRuntime == nullptr || pTreasure == nullptr || flags.hasFlag( pTreasure->_flag ) )
            return false;
        flags.setFlag( pTreasure->_flag, 1 );
        AdventureDungeonProgress& progress = pRuntime->_progress;
        const hashed_string&      item     = pTreasure->_item;
        if ( item == AdventureDungeonInternal::getSmallKeyName() )
            progress._smallKeyCount += pTreasure->_count;
        else if ( item == AdventureDungeonInternal::getBossKeyName() )
            progress._bBossKey = SW_TRUE;
        else if ( item == AdventureDungeonInternal::getMapName() )
            progress._bMap = SW_TRUE;
        else if ( item == AdventureDungeonInternal::getCompassName() )
            progress._bCompass = SW_TRUE;
        else
            outReward.addItem( item, pTreasure->_count );
        pushEvent( AdventureDungeonEventType::TreasureOpened, dungeonId, treasureId, item, pTreasure->_count );
        return true;
    }

    int32 AdventureDungeonState::revealMap( const hashed_string& dungeonId, AreaGraph& areaGraph ) const
    {
        const AdventureDungeonProgress* pProgress = findProgress( dungeonId );
        const AdventureDungeonDef*      pDungeon  = _pCatalog != nullptr ? _pCatalog->findDungeon( dungeonId ) : nullptr;
        if ( pProgress == nullptr || pDungeon == nullptr || pProgress->_bMap == SW_FALSE )
            return 0;
        return areaGraph.discoverRegion( pDungeon->_region );
    }

    void AdventureDungeonState::collectCompassMarker( const hashed_string& dungeonId, const GameFlags& flags,
                                                      vector<const AdventureTreasureDef*>& outListTreasure ) const
    {
        outListTreasure.clear();
        const AdventureDungeonProgress* pProgress = findProgress( dungeonId );
        const AdventureDungeonDef*      pDungeon  = _pCatalog != nullptr ? _pCatalog->findDungeon( dungeonId ) : nullptr;
        if ( pProgress == nullptr || pDungeon == nullptr || pProgress->_bCompass == SW_FALSE )
            return;
        for ( const AdventureTreasureDef& treasure : pDungeon->_listTreasure )
        {
            if ( flags.hasFlag( treasure._flag ) == false )
                outListTreasure.push_back( &treasure );
        }
    }

    void AdventureDungeonState::setDeviceActive( const AdventureDungeonDef& dungeon, int32 deviceIndex, bool bActive, GameFlags& flags )
    {
        DungeonRuntime&           runtime = _listRuntime[static_cast<size_t>( _pCatalog->findDungeonIndex( dungeon._id ) )];
        DeviceRuntime&            device  = runtime._listDevice[static_cast<size_t>( deviceIndex )];
        const AdventureDeviceDef& def     = dungeon._listDevice[static_cast<size_t>( deviceIndex )];
        const uint8               bNew    = bActive ? SW_TRUE : SW_FALSE;
        if ( device._bActive == bNew )
            return;
        device._bActive = bNew;
        flags.setFlag( def._flag, bActive ? 1 : 0 );
        pushEvent( bActive ? AdventureDungeonEventType::DeviceActivated : AdventureDungeonEventType::DeviceDeactivated, dungeon._id, def._id );
    }

    bool AdventureDungeonState::hitSwitch( const hashed_string& dungeonId, const hashed_string& deviceId, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonId, &pDungeon );
        const int32                index    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceId ) : -1;
        if ( pRuntime == nullptr || index < 0 )
            return false;
        const AdventureDeviceDef& def = pDungeon->_listDevice[static_cast<size_t>( index )];
        if ( def._kind == AdventureDeviceKind::TimedSwitch )
            pRuntime->_listDevice[static_cast<size_t>( index )]._timer.start( def._duration );
        else if ( def._kind != AdventureDeviceKind::Switch )
            return false;
        setDeviceActive( *pDungeon, index, true, flags );
        return true;
    }

    void AdventureDungeonState::setPlatePressed( const hashed_string& dungeonId, const hashed_string& deviceId, bool bPressed, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonId, &pDungeon );
        const int32                index    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceId ) : -1;
        if ( pRuntime == nullptr || index < 0 )
            return;
        const AdventureDeviceDef& def = pDungeon->_listDevice[static_cast<size_t>( index )];
        if ( def._kind != AdventureDeviceKind::PressurePlate )
            return;
        const bool bLatched = def._bLatch == SW_TRUE && pRuntime->_listDevice[static_cast<size_t>( index )]._bActive == SW_TRUE;
        if ( bPressed == false && bLatched )
            return;
        setDeviceActive( *pDungeon, index, bPressed, flags );
    }

    bool AdventureDungeonState::lightTorch( const hashed_string& dungeonId, const hashed_string& deviceId, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonId, &pDungeon );
        const int32                index    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceId ) : -1;
        if ( pRuntime == nullptr || index < 0 )
            return false;
        const AdventureDeviceDef& def    = pDungeon->_listDevice[static_cast<size_t>( index )];
        DeviceRuntime&            device = pRuntime->_listDevice[static_cast<size_t>( index )];
        if ( def._kind != AdventureDeviceKind::TorchGroup || device._bActive == SW_TRUE || device._litCount >= def._torchCount )
            return false;
        if ( device._litCount == 0 )
            device._timer.start( def._duration );
        ++device._litCount;
        if ( device._litCount >= def._torchCount )
            setDeviceActive( *pDungeon, index, true, flags );
        return true;
    }

    void AdventureDungeonState::update( float32 deltaTime, GameFlags& flags )
    {
        if ( _pCatalog == nullptr || deltaTime <= 0.0f )
            return;
        const vector<AdventureDungeonDef>& listDungeon = _pCatalog->getDungeons();
        for ( size_t dungeonIndex = 0; dungeonIndex < _listRuntime.size(); ++dungeonIndex )
        {
            const AdventureDungeonDef& dungeon = listDungeon[dungeonIndex];
            DungeonRuntime&            runtime = _listRuntime[dungeonIndex];
            for ( size_t deviceIndex = 0; deviceIndex < runtime._listDevice.size(); ++deviceIndex )
            {
                const AdventureDeviceDef& def    = dungeon._listDevice[deviceIndex];
                DeviceRuntime&            device = runtime._listDevice[deviceIndex];
                if ( def._kind == AdventureDeviceKind::TimedSwitch && device._bActive == SW_TRUE )
                {
                    device._timer.tick( deltaTime );
                    if ( device._timer.isActive() == false )
                        setDeviceActive( dungeon, static_cast<int32>( deviceIndex ), false, flags );
                }
                else if ( def._kind == AdventureDeviceKind::TorchGroup && device._bActive == SW_FALSE && device._litCount > 0 && def._duration > 0.0f )
                {
                    device._timer.tick( deltaTime );
                    if ( device._timer.isActive() == false )
                    {
                        device._litCount = 0;
                        pushEvent( AdventureDungeonEventType::TorchesFailed, dungeon._id, def._id );
                    }
                }
            }
        }
    }

    void AdventureDungeonState::drainEvents( vector<AdventureDungeonEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    const AdventureDungeonProgress* AdventureDungeonState::findProgress( const hashed_string& dungeonId ) const
    {
        if ( _pCatalog == nullptr )
            return nullptr;
        const int32 index = _pCatalog->findDungeonIndex( dungeonId );
        return index >= 0 && static_cast<size_t>( index ) < _listRuntime.size() ? &_listRuntime[static_cast<size_t>( index )]._progress : nullptr;
    }

    bool AdventureDungeonState::isDeviceActive( const hashed_string& dungeonId, const hashed_string& deviceId ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const int32 dungeonIndex = _pCatalog->findDungeonIndex( dungeonId );
        if ( dungeonIndex < 0 || static_cast<size_t>( dungeonIndex ) >= _listRuntime.size() )
            return false;
        const int32 deviceIndex = _pCatalog->getDungeons()[static_cast<size_t>( dungeonIndex )].findDeviceIndex( deviceId );
        return deviceIndex >= 0 && _listRuntime[static_cast<size_t>( dungeonIndex )]._listDevice[static_cast<size_t>( deviceIndex )]._bActive == SW_TRUE;
    }

    int32 AdventureDungeonState::getLitTorchCount( const hashed_string& dungeonId, const hashed_string& deviceId ) const
    {
        if ( _pCatalog == nullptr )
            return 0;
        const int32 dungeonIndex = _pCatalog->findDungeonIndex( dungeonId );
        if ( dungeonIndex < 0 || static_cast<size_t>( dungeonIndex ) >= _listRuntime.size() )
            return 0;
        const int32 deviceIndex = _pCatalog->getDungeons()[static_cast<size_t>( dungeonIndex )].findDeviceIndex( deviceId );
        return deviceIndex >= 0 ? _listRuntime[static_cast<size_t>( dungeonIndex )]._listDevice[static_cast<size_t>( deviceIndex )]._litCount : 0;
    }

    void AdventureDungeonState::pushEvent( AdventureDungeonEventType type, const hashed_string& dungeonId, const hashed_string& id, const hashed_string& item,
                                           int32 count )
    {
        AdventureDungeonEvent event;
        event._type    = type;
        event._dungeon = dungeonId;
        event._id      = id;
        event._item    = item;
        event._count   = count;
        _eventBuffer.push( event );
    }
} // namespace sw
