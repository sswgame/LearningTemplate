#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureDungeon.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "AdventureDungeon" );

    namespace
    {
        struct AdventureDungeonInternal
        {
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
    const AdventureDoorDef* AdventureDungeonDef::findDoor( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&AdventureDoorDef::_id>( _listDoor, id );
    }

    const AdventureTreasureDef* AdventureDungeonDef::findTreasure( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&AdventureTreasureDef::_id>( _listTreasure, id );
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

    uint32 AdventureDungeonCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 던전을 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode dungeonNode = root.findChild( "Dungeon" ); dungeonNode; dungeonNode = dungeonNode.findNextSibling( "Dungeon" ) )
        {
            if ( loadDungeon( dungeonNode, sourceName ) )
                ++loadedCount;
        }
        return loadedCount;
    }

    bool AdventureDungeonCatalog::loadDungeon( const XMLNode& dungeonNode, string_view sourceName )
    {
        AdventureDungeonDef dungeon;
        bool                bValid = GameDataXML::readFields( dungeonNode, dungeon, sourceName );
        for ( XMLNode node = dungeonNode.findChild( "Door" ); node; node = node.findNextSibling( "Door" ) )
        {
            AdventureDoorDef door;
            bool             bDoorValid = GameDataXML::readFields( node, door, sourceName );
            if ( door._kind == AdventureDoorKind::Condition && door._requires.empty() )
            {
                SW_LOG_ERROR( "%#: condition door '%#' needs requires=\"...\"", sourceName, door._id.c_str() );
                bDoorValid = false;
            }
            if ( bDoorValid )
                dungeon._listDoor.push_back( door );
            bValid = bDoorValid && bValid;
        }
        for ( XMLNode node = dungeonNode.findChild( "Treasure" ); node; node = node.findNextSibling( "Treasure" ) )
        {
            AdventureTreasureDef treasure;
            if ( GameDataXML::readFields( node, treasure, sourceName ) )
                dungeon._listTreasure.push_back( treasure );
            else
                bValid = false;
        }
        for ( XMLNode node = dungeonNode.findChild( "Device" ); node; node = node.findNextSibling( "Device" ) )
        {
            AdventureDeviceDef device;
            bool               bDeviceValid = GameDataXML::readFields( node, device, sourceName );
            if ( device._kind == AdventureDeviceKind::TimedSwitch && device._duration <= 0.0f )
            {
                SW_LOG_ERROR( "%#: timed switch '%#' needs duration=\"...\" above 0", sourceName, device._id.c_str() );
                bDeviceValid = false;
            }
            if ( bDeviceValid )
                dungeon._listDevice.push_back( device );
            bValid = bDeviceValid && bValid;
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR( "%#: <Dungeon id=\"%#\"> has errors - not added", sourceName, dungeon._id.c_str() );
            return false;
        }
        (void)add( dungeon ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
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
        _listRuntime.resize( pCatalog->getAll().size() );
        for ( size_t index = 0; index < _listRuntime.size(); ++index )
        {
            _listRuntime[index]._listDevice.resize( pCatalog->getAll()[index]._listDevice.size() );
        }
    }

    AdventureDungeonState::DungeonRuntime* AdventureDungeonState::findRuntime( const hashed_string& dungeonID, const AdventureDungeonDef** ppOutDef )
    {
        if ( _pCatalog == nullptr )
            return nullptr;
        const int32 index = _pCatalog->findIndex( dungeonID );
        if ( index < 0 || static_cast<size_t>( index ) >= _listRuntime.size() )
            return nullptr;
        if ( ppOutDef != nullptr )
            *ppOutDef = &_pCatalog->getAll()[static_cast<size_t>( index )];
        return &_listRuntime[static_cast<size_t>( index )];
    }

    bool AdventureDungeonState::addSmallKey( const hashed_string& dungeonID, int32 count )
    {
        DungeonRuntime* pRuntime = findRuntime( dungeonID, nullptr );
        if ( pRuntime == nullptr || count <= 0 )
            return false;
        pRuntime->_progress._smallKeyCount += count;
        return true;
    }

    AdventureDoorResult AdventureDungeonState::openDoor( const hashed_string& dungeonID, const hashed_string& doorID, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonID, &pDungeon );
        const AdventureDoorDef*    pDoor    = pDungeon != nullptr ? pDungeon->findDoor( doorID ) : nullptr;
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
        pushEvent( AdventureDungeonEventType::DoorOpened, dungeonID, doorID );
        return AdventureDoorResult::Opened;
    }

    bool AdventureDungeonState::openTreasure( const hashed_string& dungeonID, const hashed_string& treasureID, GameFlags& flags, ItemStackList& outReward )
    {
        const AdventureDungeonDef*  pDungeon             = nullptr;
        DungeonRuntime*             pRuntime             = findRuntime( dungeonID, &pDungeon );
        const AdventureTreasureDef* pTreasure            = pDungeon != nullptr ? pDungeon->findTreasure( treasureID ) : nullptr;
        const bool                  bTreasureUnavailable = pRuntime == nullptr || pTreasure == nullptr || flags.hasFlag( pTreasure->_flag );
        if ( bTreasureUnavailable )
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
        pushEvent( AdventureDungeonEventType::TreasureOpened, dungeonID, treasureID, item, pTreasure->_count );
        return true;
    }

    int32 AdventureDungeonState::revealMap( const hashed_string& dungeonID, AreaGraph& areaGraph ) const
    {
        const AdventureDungeonProgress* pProgress       = findProgress( dungeonID );
        const AdventureDungeonDef*      pDungeon        = _pCatalog != nullptr ? _pCatalog->find( dungeonID ) : nullptr;
        const bool                      bMapUnavailable = pProgress == nullptr || pDungeon == nullptr || pProgress->_bMap == SW_FALSE;
        if ( bMapUnavailable )
            return 0;
        return areaGraph.discoverRegion( pDungeon->_region );
    }

    void AdventureDungeonState::collectCompassMarker( const hashed_string& dungeonID, const GameFlags& flags,
                                                      vector<const AdventureTreasureDef*>& outListTreasure ) const
    {
        outListTreasure.clear();
        const AdventureDungeonProgress* pProgress           = findProgress( dungeonID );
        const AdventureDungeonDef*      pDungeon            = _pCatalog != nullptr ? _pCatalog->find( dungeonID ) : nullptr;
        const bool                      bCompassUnavailable = pProgress == nullptr || pDungeon == nullptr || pProgress->_bCompass == SW_FALSE;
        if ( bCompassUnavailable )
            return;
        for ( const AdventureTreasureDef& treasure : pDungeon->_listTreasure )
        {
            if ( flags.hasFlag( treasure._flag ) == false )
                outListTreasure.push_back( &treasure );
        }
    }

    void AdventureDungeonState::setDeviceActive( const AdventureDungeonDef& dungeon, int32 deviceIndex, bool bActive, GameFlags& flags )
    {
        DungeonRuntime&           runtime = _listRuntime[static_cast<size_t>( _pCatalog->findIndex( dungeon._id ) )];
        DeviceRuntime&            device  = runtime._listDevice[static_cast<size_t>( deviceIndex )];
        const AdventureDeviceDef& def     = dungeon._listDevice[static_cast<size_t>( deviceIndex )];
        const uint8               bNew    = bActive ? SW_TRUE : SW_FALSE;
        if ( device._bActive == bNew )
            return;
        device._bActive = bNew;
        flags.setFlag( def._flag, bActive ? 1 : 0 );
        pushEvent( bActive ? AdventureDungeonEventType::DeviceActivated : AdventureDungeonEventType::DeviceDeactivated, dungeon._id, def._id );
    }

    bool AdventureDungeonState::hitSwitch( const hashed_string& dungeonID, const hashed_string& deviceID, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonID, &pDungeon );
        const int32                index    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceID ) : -1;
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

    void AdventureDungeonState::setPlatePressed( const hashed_string& dungeonID, const hashed_string& deviceID, bool bPressed, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonID, &pDungeon );
        const int32                index    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceID ) : -1;
        if ( pRuntime == nullptr || index < 0 )
            return;
        const AdventureDeviceDef& def = pDungeon->_listDevice[static_cast<size_t>( index )];
        if ( def._kind != AdventureDeviceKind::PressurePlate )
            return;
        const bool bLatched = def._bLatch && pRuntime->_listDevice[static_cast<size_t>( index )]._bActive == SW_TRUE;
        if ( bPressed == false && bLatched )
            return;
        setDeviceActive( *pDungeon, index, bPressed, flags );
    }

    bool AdventureDungeonState::lightTorch( const hashed_string& dungeonID, const hashed_string& deviceID, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonID, &pDungeon );
        const int32                index    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceID ) : -1;
        if ( pRuntime == nullptr || index < 0 )
            return false;
        const AdventureDeviceDef& def                = pDungeon->_listDevice[static_cast<size_t>( index )];
        DeviceRuntime&            device             = pRuntime->_listDevice[static_cast<size_t>( index )];
        const bool                bTorchNotLightable = def._kind != AdventureDeviceKind::TorchGroup || device._bActive == SW_TRUE || device._litCount >= def._torchCount;
        if ( bTorchNotLightable )
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
        const vector<AdventureDungeonDef>& listDungeon = _pCatalog->getAll();
        for ( size_t dungeonIndex = 0; dungeonIndex < _listRuntime.size(); ++dungeonIndex )
        {
            const AdventureDungeonDef& dungeon = listDungeon[dungeonIndex];
            DungeonRuntime&            runtime = _listRuntime[dungeonIndex];
            for ( size_t deviceIndex = 0; deviceIndex < runtime._listDevice.size(); ++deviceIndex )
            {
                const AdventureDeviceDef& def                = dungeon._listDevice[deviceIndex];
                DeviceRuntime&            device             = runtime._listDevice[deviceIndex];
                const bool                bTorchTimerRunning = def._kind == AdventureDeviceKind::TorchGroup && device._bActive == SW_FALSE && device._litCount > 0 && def._duration > 0.0f;
                if ( def._kind == AdventureDeviceKind::TimedSwitch && device._bActive == SW_TRUE )
                {
                    device._timer.update( deltaTime );
                    if ( device._timer.isActive() == false )
                        setDeviceActive( dungeon, static_cast<int32>( deviceIndex ), false, flags );
                }
                else if ( bTorchTimerRunning )
                {
                    device._timer.update( deltaTime );
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

    const AdventureDungeonProgress* AdventureDungeonState::findProgress( const hashed_string& dungeonID ) const
    {
        if ( _pCatalog == nullptr )
            return nullptr;
        const int32 index = _pCatalog->findIndex( dungeonID );
        return index >= 0 && static_cast<size_t>( index ) < _listRuntime.size() ? &_listRuntime[static_cast<size_t>( index )]._progress : nullptr;
    }

    bool AdventureDungeonState::isDeviceActive( const hashed_string& dungeonID, const hashed_string& deviceID ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const int32 dungeonIndex = _pCatalog->findIndex( dungeonID );
        if ( dungeonIndex < 0 || static_cast<size_t>( dungeonIndex ) >= _listRuntime.size() )
            return false;
        const int32 deviceIndex = _pCatalog->getAll()[static_cast<size_t>( dungeonIndex )].findDeviceIndex( deviceID );
        return deviceIndex >= 0 && _listRuntime[static_cast<size_t>( dungeonIndex )]._listDevice[static_cast<size_t>( deviceIndex )]._bActive == SW_TRUE;
    }

    int32 AdventureDungeonState::getLitTorchCount( const hashed_string& dungeonID, const hashed_string& deviceID ) const
    {
        if ( _pCatalog == nullptr )
            return 0;
        const int32 dungeonIndex = _pCatalog->findIndex( dungeonID );
        if ( dungeonIndex < 0 || static_cast<size_t>( dungeonIndex ) >= _listRuntime.size() )
            return 0;
        const int32 deviceIndex = _pCatalog->getAll()[static_cast<size_t>( dungeonIndex )].findDeviceIndex( deviceID );
        return deviceIndex >= 0 ? _listRuntime[static_cast<size_t>( dungeonIndex )]._listDevice[static_cast<size_t>( deviceIndex )]._litCount : 0;
    }

    void AdventureDungeonState::pushEvent( AdventureDungeonEventType type, const hashed_string& dungeonID, const hashed_string& id, const hashed_string& item,
                                           int32 count )
    {
        AdventureDungeonEvent event;
        event._type    = type;
        event._dungeon = dungeonID;
        event._id      = id;
        event._item    = item;
        event._count   = count;
        _eventBuffer.push( event );
    }

    void AdventureDungeonState::writeState( Archive& outArchive ) const
    {
        const vector<AdventureDungeonDef>* pListDungeon = _pCatalog != nullptr ? &_pCatalog->getAll() : nullptr;
        outArchive << static_cast<uint32>( _listRuntime.size() );
        for ( size_t dungeonIndex = 0; dungeonIndex < _listRuntime.size(); ++dungeonIndex )
        {
            const DungeonRuntime& runtime = _listRuntime[dungeonIndex];
            StateArchiveUtil::writeName( outArchive, pListDungeon != nullptr ? ( *pListDungeon )[dungeonIndex]._id : hashed_string{} );
            outArchive << runtime._progress._smallKeyCount;
            outArchive << runtime._progress._smallKeyUsedCount;
            outArchive << runtime._progress._bBossKey;
            outArchive << runtime._progress._bMap;
            outArchive << runtime._progress._bCompass;
            outArchive << static_cast<uint32>( runtime._listDevice.size() );
            for ( const DeviceRuntime& device : runtime._listDevice )
            {
                StateArchiveUtil::writeCountdown( outArchive, device._timer );
                outArchive << device._litCount;
                outArchive << device._bActive;
            }
        }
    }

    bool AdventureDungeonState::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        uint32 dungeonCount = 0;
        // 던전마다 id(4) + 진행(11) + 장치 수(4) 바이트 이상
        if ( StateArchiveUtil::readCount( archive, 19, dungeonCount ) == false )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다. 저장되지 않은 던전은 `initialize` 직후의 새 상태로 남는다.
        vector<DungeonRuntime> listRuntime = _listRuntime;
        for ( uint32 entry = 0; entry < dungeonCount; ++entry )
        {
            hashed_string dungeonID;
            if ( StateArchiveUtil::readName( archive, dungeonID ) == false )
                return false;
            const int32 dungeonIndex = _pCatalog->findIndex( dungeonID );
            if ( dungeonIndex < 0 || static_cast<size_t>( dungeonIndex ) >= listRuntime.size() )
                return false;
            const AdventureDungeonDef& dungeon = _pCatalog->getAll()[static_cast<size_t>( dungeonIndex )];
            DungeonRuntime&            runtime = listRuntime[static_cast<size_t>( dungeonIndex )];
            AdventureDungeonProgress   progress;
            uint32                     deviceCount = 0;
            archive >> progress._smallKeyCount;
            archive >> progress._smallKeyUsedCount;
            archive >> progress._bBossKey;
            archive >> progress._bMap;
            archive >> progress._bCompass;
            // 장치마다 남은 시간(4) + 켜진 수(4) + 활성(1) 바이트
            if ( StateArchiveUtil::readCount( archive, 9, deviceCount ) == false || deviceCount != runtime._listDevice.size() )
                return false;
            const bool bProgressValid = 0 <= progress._smallKeyCount && 0 <= progress._smallKeyUsedCount && progress._bBossKey <= SW_TRUE &&
                                        progress._bMap <= SW_TRUE && progress._bCompass <= SW_TRUE;
            if ( bProgressValid == false )
                return false;
            runtime._progress = progress;
            for ( size_t deviceIndex = 0; deviceIndex < runtime._listDevice.size(); ++deviceIndex )
            {
                DeviceRuntime& device = runtime._listDevice[deviceIndex];
                if ( StateArchiveUtil::readCountdown( archive, device._timer ) == false )
                    return false;
                archive >> device._litCount;
                archive >> device._bActive;
                const bool bDeviceValid = archive.isOk() && 0 <= device._litCount && device._litCount <= dungeon._listDevice[deviceIndex]._torchCount &&
                                          device._bActive <= SW_TRUE;
                if ( bDeviceValid == false )
                    return false;
            }
        }
        if ( archive.isError() )
            return false;
        _listRuntime = std::move( listRuntime );
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
