#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureDungeon.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Gameplay/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

namespace sw
{
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

    optional<int32> AdventureDungeonDef::findDeviceIndex( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listDevice.size(); ++index )
        {
            if ( _listDevice[index]._id == id )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    uint32 AdventureDungeonCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 던전을 넣지 않고 오류다(readFields 가 남긴다).
        using Element                          = XMLCatalogElement<AdventureDungeonCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&AdventureDungeonCatalog::loadDungeon>( "Dungeon" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool AdventureDungeonCatalog::loadDungeon( const XMLNode& dungeonNode, string_view sourceName )
    {
        AdventureDungeonDef dungeon;
        bool                bValid = XMLFields::readFields( dungeonNode, dungeon, sourceName );
        for ( XMLNode node = dungeonNode.findChild( "Door" ); node; node = node.findNextSibling( "Door" ) )
        {
            AdventureDoorDef door;
            bool             bDoorValid = XMLFields::readFields( node, door, sourceName );
            if ( door._kind == AdventureDoorKind::Condition && door._requires.empty() )
            {
                SW_LOG_ERROR_ID( "AdventureDungeon.ConditionDoorNeedsRequires", "%#: condition door '%#' needs requires=\"...\"", sourceName, door._id.c_str() );
                bDoorValid = false;
            }
            bDoorValid = ConditionExpr::compile( door._requires, sourceName, door._condition ) && bDoorValid;
            if ( bDoorValid )
                dungeon._listDoor.push_back( door );
            bValid = bDoorValid && bValid;
        }
        for ( XMLNode node = dungeonNode.findChild( "Treasure" ); node; node = node.findNextSibling( "Treasure" ) )
        {
            AdventureTreasureDef treasure;
            if ( XMLFields::readFields( node, treasure, sourceName ) )
                dungeon._listTreasure.push_back( treasure );
            else
                bValid = false;
        }
        for ( XMLNode node = dungeonNode.findChild( "Device" ); node; node = node.findNextSibling( "Device" ) )
        {
            AdventureDeviceDef device;
            bool               bDeviceValid = XMLFields::readFields( node, device, sourceName );
            if ( device._kind == AdventureDeviceKind::TimedSwitch && device._duration <= 0.0f )
            {
                SW_LOG_ERROR_ID( "AdventureDungeon.TimedSwitchNeedsDuration", "%#: timed switch '%#' needs duration=\"...\" above 0", sourceName, device._id.c_str() );
                bDeviceValid = false;
            }
            if ( bDeviceValid )
                dungeon._listDevice.push_back( device );
            bValid = bDeviceValid && bValid;
        }
        // 같은 id 의 문 · 상자 · 장치는 뒤의 것에 영영 닿지 못한다.
        const AdventureDoorDef*     pDuplicateDoor     = VectorUtil::findDuplicateBy<&AdventureDoorDef::_id>( dungeon._listDoor );
        const AdventureTreasureDef* pDuplicateTreasure = VectorUtil::findDuplicateBy<&AdventureTreasureDef::_id>( dungeon._listTreasure );
        const AdventureDeviceDef*   pDuplicateDevice   = VectorUtil::findDuplicateBy<&AdventureDeviceDef::_id>( dungeon._listDevice );
        const hashed_string         duplicate          = pDuplicateDoor != nullptr ? pDuplicateDoor->_id : ( pDuplicateTreasure != nullptr ? pDuplicateTreasure->_id : ( pDuplicateDevice != nullptr ? pDuplicateDevice->_id : hashed_string{} ) );
        if ( duplicate.empty() == false )
        {
            SW_LOG_ERROR_ID( "AdventureDungeon.DuplicateId", "%#: <Dungeon id=\"%#\"> uses id '%#' twice", sourceName, dungeon._id.c_str(), duplicate.c_str() );
            bValid = false;
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "AdventureDungeon.DungeonIdErrorsNot", "%#: <Dungeon id=\"%#\"> has errors - not added", sourceName, dungeon._id.c_str() );
            return false;
        }
        (void)add( dungeon );
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
        _pCatalog    = pCatalog;
        _listRuntime = makeInitialRuntime();
        _eventBuffer.clear();
    }

    vector<AdventureDungeonState::DungeonRuntime> AdventureDungeonState::makeInitialRuntime() const
    {
        vector<DungeonRuntime> listRuntime;
        if ( _pCatalog == nullptr )
            return listRuntime;
        listRuntime.resize( _pCatalog->getAll().size() );
        for ( size_t index = 0; index < listRuntime.size(); ++index )
        {
            listRuntime[index]._listDevice.resize( _pCatalog->getAll()[index]._listDevice.size() );
        }
        return listRuntime;
    }

    AdventureDungeonState::DungeonRuntime* AdventureDungeonState::findRuntime( const hashed_string& dungeonID, const AdventureDungeonDef** ppOutDef )
    {
        if ( _pCatalog == nullptr )
            return nullptr;
        const optional<int32> found = _pCatalog->findIndex( dungeonID );
        if ( found.has_value() == false || static_cast<size_t>( *found ) >= _listRuntime.size() )
            return nullptr;
        const int32 index = *found;
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
                if ( flags.evaluate( pDoor->_condition ) == false )
                    return AdventureDoorResult::ConditionNotMet;
                break;
            }
        }
        flags.setFlag( pDoor->_flag, 1 );
        pushEvent( AdventureDungeonEventKind::DoorOpened, dungeonID, doorID );
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
        pushEvent( AdventureDungeonEventKind::TreasureOpened, dungeonID, treasureID, item, pTreasure->_count );
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
        DungeonRuntime&           runtime = _listRuntime[static_cast<size_t>( _pCatalog->findIndex( dungeon._id ).value() )]; // 카탈로그의 던전이다 — 없으면 결함으로 멈춘다
        DeviceRuntime&            device  = runtime._listDevice[static_cast<size_t>( deviceIndex )];
        const AdventureDeviceDef& def     = dungeon._listDevice[static_cast<size_t>( deviceIndex )];
        const uint8               bNew    = bActive ? SW_TRUE : SW_FALSE;
        if ( device._bActive == bNew )
            return;
        device._bActive = bNew;
        flags.setFlag( def._flag, bActive ? 1 : 0 );
        pushEvent( bActive ? AdventureDungeonEventKind::DeviceActivated : AdventureDungeonEventKind::DeviceDeactivated, dungeon._id, def._id );
    }

    bool AdventureDungeonState::hitSwitch( const hashed_string& dungeonID, const hashed_string& deviceID, GameFlags& flags )
    {
        const AdventureDungeonDef* pDungeon = nullptr;
        DungeonRuntime*            pRuntime = findRuntime( dungeonID, &pDungeon );
        const optional<int32>      found    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceID ) : optional<int32>{};
        if ( pRuntime == nullptr || found.has_value() == false )
            return false;
        const int32               index = *found;
        const AdventureDeviceDef& def   = pDungeon->_listDevice[static_cast<size_t>( index )];
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
        const optional<int32>      found    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceID ) : optional<int32>{};
        if ( pRuntime == nullptr || found.has_value() == false )
            return;
        const int32               index = *found;
        const AdventureDeviceDef& def   = pDungeon->_listDevice[static_cast<size_t>( index )];
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
        const optional<int32>      found    = pDungeon != nullptr ? pDungeon->findDeviceIndex( deviceID ) : optional<int32>{};
        if ( pRuntime == nullptr || found.has_value() == false )
            return false;
        const int32               index              = *found;
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
                        pushEvent( AdventureDungeonEventKind::TorchesFailed, dungeon._id, def._id );
                    }
                }
            }
        }
    }

    void AdventureDungeonState::pollEvents( vector<AdventureDungeonEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    const AdventureDungeonProgress* AdventureDungeonState::findProgress( const hashed_string& dungeonID ) const
    {
        if ( _pCatalog == nullptr )
            return nullptr;
        const optional<int32> index = _pCatalog->findIndex( dungeonID );
        return index.has_value() && static_cast<size_t>( *index ) < _listRuntime.size() ? &_listRuntime[static_cast<size_t>( *index )]._progress : nullptr;
    }

    bool AdventureDungeonState::isDeviceActive( const hashed_string& dungeonID, const hashed_string& deviceID ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const optional<int32> dungeonIndex = _pCatalog->findIndex( dungeonID );
        if ( dungeonIndex.has_value() == false || static_cast<size_t>( *dungeonIndex ) >= _listRuntime.size() )
            return false;
        const optional<int32> deviceIndex = _pCatalog->getAll()[static_cast<size_t>( *dungeonIndex )].findDeviceIndex( deviceID );
        return deviceIndex.has_value() && _listRuntime[static_cast<size_t>( *dungeonIndex )]._listDevice[static_cast<size_t>( *deviceIndex )]._bActive == SW_TRUE;
    }

    int32 AdventureDungeonState::getLitTorchCount( const hashed_string& dungeonID, const hashed_string& deviceID ) const
    {
        if ( _pCatalog == nullptr )
            return 0;
        const optional<int32> dungeonIndex = _pCatalog->findIndex( dungeonID );
        if ( dungeonIndex.has_value() == false || static_cast<size_t>( *dungeonIndex ) >= _listRuntime.size() )
            return 0;
        const optional<int32> deviceIndex = _pCatalog->getAll()[static_cast<size_t>( *dungeonIndex )].findDeviceIndex( deviceID );
        return deviceIndex.has_value() ? _listRuntime[static_cast<size_t>( *dungeonIndex )]._listDevice[static_cast<size_t>( *deviceIndex )]._litCount : 0;
    }

    void AdventureDungeonState::pushEvent( AdventureDungeonEventKind type, const hashed_string& dungeonID, const hashed_string& id, const hashed_string& item,
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

    void AdventureDungeonState::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        uint32 dungeonCount = static_cast<uint32>( _listRuntime.size() );
        // 던전마다 id(4) + 진행(11) + 장치 수(4) 바이트 이상
        io.count( 19, dungeonCount );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            _listRuntime = makeInitialRuntime(); // 저장되지 않은 던전은 `initialize` 직후의 새 상태로 남는다
            _eventBuffer.clear();
        }
        for ( uint32 entry = 0; entry < dungeonCount; ++entry )
        {
            hashed_string dungeonID = io.isWriting() && _pCatalog != nullptr ? _pCatalog->getAll()[entry]._id : hashed_string{};
            io.value( dungeonID );
            const optional<int32> dungeonIndex = io.isReading() ? _pCatalog->findIndex( dungeonID ) : optional<int32>{ static_cast<int32>( entry ) };
            if ( io.check( dungeonIndex.has_value() && static_cast<size_t>( *dungeonIndex ) < _listRuntime.size() ) == false )
                return;
            DungeonRuntime&           runtime  = _listRuntime[static_cast<size_t>( *dungeonIndex )];
            AdventureDungeonProgress& progress = runtime._progress;
            io.value( progress._smallKeyCount );
            io.value( progress._smallKeyUsedCount );
            io.flag( progress._bBossKey );
            io.flag( progress._bMap );
            io.flag( progress._bCompass );
            uint32 deviceCount = static_cast<uint32>( runtime._listDevice.size() );
            // 장치마다 남은 시간(4) + 켜진 수(4) + 활성(1) 바이트
            io.count( 9, deviceCount );
            if ( io.check( deviceCount == runtime._listDevice.size() && 0 <= progress._smallKeyCount && 0 <= progress._smallKeyUsedCount ) == false )
                return;
            for ( size_t deviceIndex = 0; deviceIndex < runtime._listDevice.size(); ++deviceIndex )
            {
                DeviceRuntime& device = runtime._listDevice[deviceIndex];
                io.value( device._timer );
                io.value( device._litCount );
                io.flag( device._bActive );
                if ( io.isReading() && io.check( 0 <= device._litCount && device._litCount <= _pCatalog->getAll()[static_cast<size_t>( *dungeonIndex )]._listDevice[deviceIndex]._torchCount ) == false )
                    return;
            }
        }
    }

} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<AdventureDungeonCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( AdventureDungeonCatalog, AdventureDungeonCatalog, SW_KIT_API, AdventureDungeonCatalog::kAssetType.withLoader( AssetTypeLoader::of<AdventureDungeonCatalog>( MemoryTag::Game ) ) );
} // namespace sw
