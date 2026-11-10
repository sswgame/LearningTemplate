#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostMansion.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

namespace sw
{
    GhostMansion::GhostMansion()
        : _encounter{}
        , _random{}
        , _listBoo{}
        , _listSearched{}
        , _listPendingKeyRoom{}
        , _eventBuffer{}
        , _listGhostEvent{}
        , _currentRoom{}
        , _pCatalog{ nullptr }
        , _pLoot{ nullptr }
        , _pAreaGraph{ nullptr }
        , _pFlags{ nullptr }
        , _pInventory{ nullptr }
        , _pWallet{ nullptr }
        , _seed{ 0 }
    {
    }

    bool GhostMansion::initialize( const GhostCatalog* pCatalog, const LootCatalog* pLoot, AreaGraph* pAreaGraph, const SessionStateRefs& refs, uint32 seed )
    {
        if ( _encounter.initialize( pCatalog, seed ) == false )
            return false;
        _pCatalog   = pCatalog;
        _pLoot      = pLoot;
        _pAreaGraph = pAreaGraph;
        _pFlags     = refs._pFlags;
        _pInventory = refs._pInventory;
        _pWallet    = refs._pWallet;
        _seed       = seed;
        _random.setSeed( GameRandom::deriveSeed( seed, 0xB00B00u ) );
        _eventBuffer.clear();
        _listGhostEvent.clear();
        _currentRoom = hashed_string{};
        _listBoo.clear();
        _listSearched.clear();
        _listPendingKeyRoom.clear();
        for ( const HidingGhostDef& boo : pCatalog->getBoos() )
        {
            HidingGhostRuntime runtime;
            runtime._room      = boo._room;
            runtime._furniture = boo._furniture;
            runtime._hp        = boo._hp;
            _listBoo.push_back( runtime );
        }
        _listSearched.assign( pCatalog->getFurniture().size(), SW_FALSE );
        return true;
    }

    int32 GhostMansion::enterRoom( const hashed_string& roomID )
    {
        if ( _pCatalog == nullptr )
            return constant::invalid_index::kInt32;
        grantPendingKeys(); // 방을 옮기는 사이 가방을 비웠을 수 있다
        const GhostRoomDef* pRoom    = _pCatalog->findRoom( roomID );
        const bool          bOnGraph = _pAreaGraph != nullptr && _pAreaGraph->findArea( roomID ) != nullptr;
        if ( pRoom == nullptr && bOnGraph == false )
            return -1;
        if ( bOnGraph )
            (void)_pAreaGraph->enterArea( roomID );
        // 흡입은 틱 사이에 끝나므로, 잡고 바로 나가면 `clear` 가 잡은 이벤트를 지워 동전과 불이 사라졌다.
        collectEncounterEvents();
        _encounter.clear();
        _currentRoom = roomID;
        // 가구 없이 방에 숨은 부는 들어서면 나온다.
        for ( size_t booIndex = 0; booIndex < _listBoo.size(); ++booIndex )
        {
            const HidingGhostRuntime& boo              = _listBoo[booIndex];
            const bool                bBooHiddenInRoom = boo._state == HidingGhostState::Hiding && boo._room == roomID && boo._furniture.empty();
            if ( bBooHiddenInRoom )
                revealBoo( booIndex );
        }
        if ( pRoom == nullptr || isRoomLit( roomID ) )
            return 0;
        if ( pRoom->_listGhost.empty() )
        {
            lightRoom( roomID ); // 유령이 없는 방은 들어서면 밝다
            return 0;
        }
        int32 spawnedCount = 0;
        for ( const hashed_string& ghostID : pRoom->_listGhost )
        {
            if ( _encounter.spawnGhost( ghostID, float3{} ) != 0 )
                ++spawnedCount;
        }
        return spawnedCount;
    }

    void GhostMansion::update( float32 deltaTime )
    {
        _encounter.update( deltaTime );
        collectEncounterEvents();

        for ( size_t booIndex = 0; booIndex < _listBoo.size(); ++booIndex )
        {
            HidingGhostRuntime& boo = _listBoo[booIndex];
            if ( boo._state != HidingGhostState::Revealed )
                continue;
            boo._timer.update( deltaTime );
            if ( boo._timer.isActive() == false )
                moveBooAway( booIndex );
        }
    }

    void GhostMansion::collectEncounterEvents()
    {
        // 게임에 넘길 목록에 바로 꺼내고, 이번에 들어온 구간만 훑는다(지역 목록과 복사가 없다).
        const size_t firstNew = _listGhostEvent.size();
        _encounter.pollEvents( _listGhostEvent );
        bool bCaughtAny = false;
        for ( size_t index = firstNew; index < _listGhostEvent.size(); ++index )
        {
            const GhostEvent& event = _listGhostEvent.data()[index];
            if ( event._type != GhostEventKind::Caught )
                continue;
            bCaughtAny = true;
            if ( _pWallet != nullptr && _pCatalog != nullptr )
                _pWallet->add( _pCatalog->getCurrency(), event._coins );
            pushEvent( GhostMansionEventKind::CoinsCollected, hashed_string{}, _currentRoom, event._coins );
        }
        const bool bCleared = bCaughtAny && _encounter.getGhosts().empty() == false && _encounter.countRemaining() == 0;
        if ( bCleared && isRoomLit( _currentRoom ) == false )
            lightRoom( _currentRoom );
    }

    GhostDoorResult GhostMansion::unlockDoor( const hashed_string& doorID )
    {
        const GhostDoorDef* pDoor = _pCatalog != nullptr ? _pCatalog->findDoor( doorID ) : nullptr;
        if ( pDoor == nullptr || _pFlags == nullptr )
            return GhostDoorResult::UnknownDoor;
        if ( _pFlags->hasFlag( pDoor->_flag ) )
            return GhostDoorResult::AlreadyOpen;
        if ( pDoor->_key.empty() == false && ( _pInventory == nullptr || _pInventory->removeItem( pDoor->_key, 1 ) == false ) )
            return GhostDoorResult::NeedKey;
        _pFlags->setFlag( pDoor->_flag, 1 );
        pushEvent( GhostMansionEventKind::DoorOpened, doorID );
        return GhostDoorResult::Opened;
    }

    GhostSearchResult GhostMansion::searchFurniture( const hashed_string& furnitureID, GhostSearchMode mode, ItemStackList& outLoot )
    {
        const optional<int32> foundFurniture = findFurnitureIndex( furnitureID );
        if ( foundFurniture.has_value() == false )
            return GhostSearchResult::UnknownFurniture;
        const int32              furnitureIndex = *foundFurniture;
        const GhostFurnitureDef& furniture      = _pCatalog->getFurniture()[static_cast<size_t>( furnitureIndex )];
        const uint8              bAllowed       = mode == GhostSearchMode::Vacuum ? furniture._bVacuum : furniture._bShake;
        if ( bAllowed == SW_FALSE )
            return GhostSearchResult::WrongMode;
        if ( furniture._room != _currentRoom )
            return GhostSearchResult::WrongRoom;
        for ( size_t booIndex = 0; booIndex < _listBoo.size(); ++booIndex )
        {
            const HidingGhostRuntime& boo = _listBoo[booIndex];
            if ( boo._state == HidingGhostState::Hiding && boo._furniture == furnitureID )
            {
                revealBoo( booIndex );
                return GhostSearchResult::BooFound;
            }
        }
        if ( _listSearched[static_cast<size_t>( furnitureIndex )] == SW_TRUE )
            return GhostSearchResult::AlreadySearched;
        _listSearched[static_cast<size_t>( furnitureIndex )] = SW_TRUE;
        if ( _pLoot != nullptr && furniture._lootTable.empty() == false )
        {
            // 가구마다 따로 시드를 내므로, 어느 가구부터 뒤져도 같은 것이 나온다.
            GameRandom random( GameRandom::deriveSeed( _seed, static_cast<uint32>( furnitureID.getHash() ) ) );
            (void)_pLoot->roll( furniture._lootTable, random, outLoot );
        }
        return GhostSearchResult::Found;
    }

    bool GhostMansion::damageBoo( const hashed_string& booID, float32 amount )
    {
        const optional<int32> booIndex = _pCatalog != nullptr ? _pCatalog->findBooIndex( booID ) : optional<int32>{};
        if ( booIndex.has_value() == false || static_cast<size_t>( *booIndex ) >= _listBoo.size() )
            return false;
        HidingGhostRuntime& boo = _listBoo[static_cast<size_t>( *booIndex )];
        if ( boo._state != HidingGhostState::Revealed || amount <= 0.0f )
            return false;
        if ( DepletingValue::take( boo._hp, amount )._bDepleted == SW_FALSE )
            return false;
        boo._state = HidingGhostState::Caught;
        pushEvent( GhostMansionEventKind::BooCaught, booID, boo._room );
        return true;
    }

    void GhostMansion::pollEvents( vector<GhostMansionEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void GhostMansion::pollGhostEvents( vector<GhostEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listGhostEvent.begin(), _listGhostEvent.end() );
        _listGhostEvent.clear();
    }

    bool GhostMansion::isRoomLit( const hashed_string& roomID ) const
    {
        const GhostRoomDef* pRoom = _pCatalog != nullptr ? _pCatalog->findRoom( roomID ) : nullptr;
        if ( pRoom == nullptr )
            return true; // 유령이 정해지지 않은 방(복도)은 항상 밝다
        return _pFlags != nullptr && _pFlags->hasFlag( pRoom->_lightFlag );
    }

    const HidingGhostRuntime* GhostMansion::findBoo( const hashed_string& booID ) const
    {
        const optional<int32> booIndex = _pCatalog != nullptr ? _pCatalog->findBooIndex( booID ) : optional<int32>{};
        return booIndex.has_value() && static_cast<size_t>( *booIndex ) < _listBoo.size() ? &_listBoo[static_cast<size_t>( *booIndex )] : nullptr;
    }

    int32 GhostMansion::countCaughtBoos() const
    {
        int32 count = 0;
        for ( const HidingGhostRuntime& boo : _listBoo )
        {
            if ( boo._state == HidingGhostState::Caught )
                ++count;
        }
        return count;
    }

    bool GhostMansion::isSearched( const hashed_string& furnitureID ) const
    {
        const optional<int32> furnitureIndex = findFurnitureIndex( furnitureID );
        return furnitureIndex.has_value() && _listSearched[static_cast<size_t>( *furnitureIndex )] == SW_TRUE;
    }

    optional<int32> GhostMansion::findFurnitureIndex( const hashed_string& furnitureID ) const
    {
        if ( _pCatalog == nullptr )
            return nullopt;
        const vector<GhostFurnitureDef>& listFurniture = _pCatalog->getFurniture();
        for ( size_t index = 0; index < listFurniture.size(); ++index )
        {
            if ( listFurniture[index]._id == furnitureID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    bool GhostMansion::isKnownRoom( const hashed_string& roomID ) const
    {
        return roomID.empty() || _pCatalog->findRoom( roomID ) != nullptr || ( _pAreaGraph != nullptr && _pAreaGraph->findArea( roomID ) != nullptr );
    }

    void GhostMansion::lightRoom( const hashed_string& roomID )
    {
        const GhostRoomDef* pRoom = _pCatalog != nullptr ? _pCatalog->findRoom( roomID ) : nullptr;
        if ( pRoom == nullptr || _pFlags == nullptr )
            return;
        _pFlags->setFlag( pRoom->_lightFlag, 1 );
        pushEvent( GhostMansionEventKind::RoomLit, roomID, roomID );
        if ( pRoom->_keyReward.empty() )
            return;
        _listPendingKeyRoom.push_back( roomID );
        grantPendingKeys();
    }

    void GhostMansion::grantPendingKeys()
    {
        // 열쇠를 잃으면 그 문을 영영 열 수 없어 진행이 막힌다 - 가방이 차면 맡아 두었다가 자리가 나면 준다.
        for ( size_t index = 0; _pInventory != nullptr && index < _listPendingKeyRoom.size(); )
        {
            const hashed_string roomID = _listPendingKeyRoom[index];
            const GhostRoomDef* pRoom  = _pCatalog->findRoom( roomID );
            if ( pRoom == nullptr || _pInventory->addItem( pRoom->_keyReward, 1 ) == 0 )
            {
                ++index;
                continue;
            }
            _listPendingKeyRoom.erase( _listPendingKeyRoom.begin() + static_cast<ptrdiff_t>( index ) );
            pushEvent( GhostMansionEventKind::KeyAwarded, pRoom->_keyReward, roomID );
        }
    }

    void GhostMansion::revealBoo( size_t booIndex )
    {
        HidingGhostRuntime& boo = _listBoo[booIndex];
        boo._state              = HidingGhostState::Revealed;
        boo._timer.start( _pCatalog->getBoos()[booIndex]._escapeTime );
        pushEvent( GhostMansionEventKind::BooRevealed, _pCatalog->getBoos()[booIndex]._id, boo._room );
    }

    void GhostMansion::moveBooAway( size_t booIndex )
    {
        HidingGhostRuntime& boo = _listBoo[booIndex];
        // 이웃 방을 찾는다. 유령은 벽을 지나므로 잠긴 문과 일방통행을 가리지 않는다. 연결을 읽은 순서대로이므로 항상 같은 목록이다.
        vector<hashed_string> listNeighbor;
        if ( _pAreaGraph != nullptr )
        {
            for ( const AreaLink& link : _pAreaGraph->getLinks() )
            {
                hashed_string other{};
                if ( link._from == boo._room )
                    other = link._to;
                else if ( link._to == boo._room )
                    other = link._from;
                if ( other.empty() )
                    continue;
                bool bListed = false;
                for ( const hashed_string& neighbor : listNeighbor )
                {
                    bListed = bListed || neighbor == other;
                }
                if ( bListed == false )
                    listNeighbor.push_back( other );
            }
        }
        if ( listNeighbor.empty() == false )
            boo._room = listNeighbor[static_cast<size_t>( _random.nextInt( 0, static_cast<int32>( listNeighbor.size() ) - 1 ) )];
        // 새 방의 가구 중 다른 부가 숨지 않은 것 하나에 숨는다. 없으면 방 어딘가에 숨는다.
        vector<hashed_string> listHideout;
        for ( const GhostFurnitureDef& furniture : _pCatalog->getFurniture() )
        {
            if ( ( furniture._room == boo._room ) == false )
                continue;
            bool bTaken = false;
            for ( const HidingGhostRuntime& other : _listBoo )
            {
                bTaken = bTaken || ( other._state == HidingGhostState::Hiding && other._furniture == furniture._id );
            }
            if ( bTaken == false )
                listHideout.push_back( furniture._id );
        }
        boo._furniture = listHideout.empty() ? hashed_string{}
                                             : listHideout[static_cast<size_t>( _random.nextInt( 0, static_cast<int32>( listHideout.size() ) - 1 ) )];
        boo._state     = HidingGhostState::Hiding;
        boo._timer.clear();
        pushEvent( GhostMansionEventKind::BooEscaped, _pCatalog->getBoos()[booIndex]._id, boo._room );
    }

    void GhostMansion::pushEvent( GhostMansionEventKind type, const hashed_string& id, const hashed_string& room, int32 count )
    {
        GhostMansionEvent event;
        event._type  = type;
        event._id    = id;
        event._room  = room;
        event._count = count;
        _eventBuffer.push( event );
    }

    void GhostMansion::serializeState( StateIO& io )
    {
        io.value( _encounter );
        io.value( _random );
        io.expect( static_cast<uint32>( _listBoo.size() ) );
        if ( io.isOk() == false )
            return;
        for ( size_t booIndex = 0; booIndex < _listBoo.size(); ++booIndex )
        {
            HidingGhostRuntime& boo = _listBoo[booIndex];
            io.value( boo._room );
            io.value( boo._furniture );
            io.value( boo._hp );
            io.value( boo._timer );
            io.enumValue( boo._state, HidingGhostState::Caught );
            if ( io.isReading() && io.isOk() )
            {
                // 잡힌 부의 체력은 0 이고, 나머지는 정의의 최대까지이다. 방과 가구는 있는 것이어야 한다.
                const bool bHpValid    = boo._state == HidingGhostState::Caught ? boo._hp == 0.0f : ( 0.0f < boo._hp && boo._hp <= _pCatalog->getBoos()[booIndex]._hp );
                const bool bPlaceValid = isKnownRoom( boo._room ) && ( boo._furniture.empty() || _pCatalog->findFurniture( boo._furniture ) != nullptr );
                if ( io.check( bHpValid && bPlaceValid ) == false )
                    return;
            }
        }
        io.expect( static_cast<uint32>( _listSearched.size() ) );
        for ( uint8& bSearched : _listSearched )
        {
            io.flag( bSearched );
        }
        io.value( _currentRoom );
        if ( io.isReading() )
        {
            io.check( isKnownRoom( _currentRoom ) );
        }
        io.value( _seed );
        io.list( _listPendingKeyRoom, 4, [&]( hashed_string& roomID )
        {
            io.value( roomID );
            if ( io.isReading() )
            {
                io.check( _pCatalog->findRoom( roomID ) != nullptr );
            }
        } );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
            _listGhostEvent.clear();
        }
    }

} // namespace sw
