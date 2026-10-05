#include "pch.h"

#include "GameFramework/Kits/Horror/GhostHunt/GhostMansion.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/LootTable.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/World/AreaGraph.h"
#include "GameFramework/Base/World/GameFlags.h"
#include "GameFramework/Kits/Horror/GhostHunt/GhostCatalog.h"

namespace sw
{
    const utf8* toString( GhostSearchResult result )
    {
        switch ( result )
        {
            case GhostSearchResult::Found:
                return "Found";
            case GhostSearchResult::AlreadySearched:
                return "AlreadySearched";
            case GhostSearchResult::WrongMode:
                return "WrongMode";
            case GhostSearchResult::BooFound:
                return "BooFound";
            case GhostSearchResult::UnknownFurniture:
                return "UnknownFurniture";
        }
        return "Unknown";
    }

    GhostMansion::GhostMansion()
        : _encounter{}
        , _random{}
        , _listBoo{}
        , _listSearched{}
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

    void GhostMansion::initialize( const GhostCatalog* pCatalog, const LootCatalog* pLoot, AreaGraph* pAreaGraph, const GameStateRefs& refs, uint32 seed )
    {
        _pCatalog   = pCatalog;
        _pLoot      = pLoot;
        _pAreaGraph = pAreaGraph;
        _pFlags     = refs._pFlags;
        _pInventory = refs._pInventory;
        _pWallet    = refs._pWallet;
        _seed       = seed;
        _random.setSeed( GameHash::mix32( seed ^ 0xB00B00u ) );
        _encounter.initialize( pCatalog, seed );
        _eventBuffer.clear();
        _listGhostEvent.clear();
        _currentRoom = hashed_string{};
        _listBoo.clear();
        _listSearched.clear();
        if ( pCatalog == nullptr )
            return;
        for ( const GhostBooDef& boo : pCatalog->getBoos() )
        {
            GhostBooRuntime runtime;
            runtime._room      = boo._room;
            runtime._furniture = boo._furniture;
            runtime._hp        = boo._hp;
            _listBoo.push_back( runtime );
        }
        _listSearched.assign( pCatalog->getFurniture().size(), SW_FALSE );
    }

    int32 GhostMansion::enterRoom( const hashed_string& roomId )
    {
        if ( _pCatalog == nullptr )
            return -1;
        const GhostRoomDef* pRoom    = _pCatalog->findRoom( roomId );
        const bool          bOnGraph = _pAreaGraph != nullptr && _pAreaGraph->findArea( roomId ) != nullptr;
        if ( pRoom == nullptr && bOnGraph == false )
            return -1;
        if ( bOnGraph )
            (void)_pAreaGraph->enterArea( roomId );
        _encounter.clear();
        _currentRoom = roomId;
        // 가구 없이 방에 숨은 부는 들어서면 나온다.
        for ( size_t booIndex = 0; booIndex < _listBoo.size(); ++booIndex )
        {
            const GhostBooRuntime& boo = _listBoo[booIndex];
            if ( boo._state == GhostBooState::Hiding && boo._room == roomId && boo._furniture.empty() )
                revealBoo( booIndex );
        }
        if ( pRoom == nullptr || isRoomLit( roomId ) )
            return 0;
        if ( pRoom->_listGhost.empty() )
        {
            lightRoom( roomId ); // 유령이 없는 방은 들어서면 밝다
            return 0;
        }
        int32 spawnedCount = 0;
        for ( const hashed_string& ghostId : pRoom->_listGhost )
        {
            if ( _encounter.spawnGhost( ghostId, float3{} ) != 0 )
                ++spawnedCount;
        }
        return spawnedCount;
    }

    void GhostMansion::update( float32 deltaTime )
    {
        _encounter.update( deltaTime );
        // 게임에 넘길 목록에 바로 꺼내고, 이번에 들어온 구간만 훑는다(지역 목록 · 복사 없음).
        const size_t firstNew = _listGhostEvent.size();
        _encounter.drainEvents( _listGhostEvent );
        bool bCaughtAny = false;
        for ( size_t index = firstNew; index < _listGhostEvent.size(); ++index )
        {
            const GhostEvent& event = _listGhostEvent.data()[index];
            if ( event._type != GhostEventType::Caught )
                continue;
            bCaughtAny = true;
            if ( _pWallet != nullptr && _pCatalog != nullptr )
                _pWallet->add( _pCatalog->getCurrency(), event._coins );
            pushEvent( GhostMansionEventType::CoinsCollected, hashed_string{}, _currentRoom, event._coins );
        }
        const bool bCleared = bCaughtAny && _encounter.getGhosts().empty() == false && _encounter.countRemaining() == 0;
        if ( bCleared && isRoomLit( _currentRoom ) == false )
            lightRoom( _currentRoom );

        for ( size_t booIndex = 0; booIndex < _listBoo.size(); ++booIndex )
        {
            GhostBooRuntime& boo = _listBoo[booIndex];
            if ( boo._state != GhostBooState::Revealed )
                continue;
            boo._timer.tick( deltaTime );
            if ( boo._timer.isActive() == false )
                moveBooAway( booIndex );
        }
    }

    GhostDoorResult GhostMansion::unlockDoor( const hashed_string& doorId )
    {
        const GhostDoorDef* pDoor = _pCatalog != nullptr ? _pCatalog->findDoor( doorId ) : nullptr;
        if ( pDoor == nullptr || _pFlags == nullptr )
            return GhostDoorResult::UnknownDoor;
        if ( _pFlags->hasFlag( pDoor->_flag ) )
            return GhostDoorResult::AlreadyOpen;
        if ( pDoor->_key.empty() == false && ( _pInventory == nullptr || _pInventory->removeItem( pDoor->_key, 1 ) == false ) )
            return GhostDoorResult::NeedKey;
        _pFlags->setFlag( pDoor->_flag, 1 );
        pushEvent( GhostMansionEventType::DoorOpened, doorId );
        return GhostDoorResult::Opened;
    }

    GhostSearchResult GhostMansion::searchFurniture( const hashed_string& furnitureId, GhostSearchMode mode, ItemBag& outLoot )
    {
        const int32 furnitureIndex = findFurnitureIndex( furnitureId );
        if ( furnitureIndex < 0 )
            return GhostSearchResult::UnknownFurniture;
        const GhostFurnitureDef& furniture = _pCatalog->getFurniture()[static_cast<size_t>( furnitureIndex )];
        const uint8              bAllowed  = mode == GhostSearchMode::Vacuum ? furniture._bVacuum : furniture._bShake;
        if ( bAllowed == SW_FALSE )
            return GhostSearchResult::WrongMode;
        for ( size_t booIndex = 0; booIndex < _listBoo.size(); ++booIndex )
        {
            const GhostBooRuntime& boo = _listBoo[booIndex];
            if ( boo._state == GhostBooState::Hiding && boo._furniture == furnitureId )
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
            // 가구마다 따로 씨앗을 내어 — 어느 가구부터 뒤져도 같은 것이 나온다.
            GameRandom random( GameHash::mix32( _seed ^ static_cast<uint32>( furnitureId.getHash() ) ) );
            (void)_pLoot->roll( furniture._lootTable, random, outLoot );
        }
        return GhostSearchResult::Found;
    }

    bool GhostMansion::damageBoo( const hashed_string& booId, float32 amount )
    {
        const int32 booIndex = _pCatalog != nullptr ? _pCatalog->findBooIndex( booId ) : -1;
        if ( booIndex < 0 || static_cast<size_t>( booIndex ) >= _listBoo.size() )
            return false;
        GhostBooRuntime& boo = _listBoo[static_cast<size_t>( booIndex )];
        if ( boo._state != GhostBooState::Revealed || amount <= 0.0f )
            return false;
        boo._hp -= amount;
        if ( boo._hp > 0.0f )
            return false;
        boo._hp    = 0.0f;
        boo._state = GhostBooState::Caught;
        pushEvent( GhostMansionEventType::BooCaught, booId, boo._room );
        return true;
    }

    void GhostMansion::drainEvents( vector<GhostMansionEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void GhostMansion::drainGhostEvents( vector<GhostEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listGhostEvent.begin(), _listGhostEvent.end() );
        _listGhostEvent.clear();
    }

    bool GhostMansion::isRoomLit( const hashed_string& roomId ) const
    {
        const GhostRoomDef* pRoom = _pCatalog != nullptr ? _pCatalog->findRoom( roomId ) : nullptr;
        if ( pRoom == nullptr )
            return true; // 유령이 정해지지 않은 방(복도)은 늘 밝다
        return _pFlags != nullptr && _pFlags->hasFlag( pRoom->_lightFlag );
    }

    const GhostBooRuntime* GhostMansion::findBoo( const hashed_string& booId ) const
    {
        const int32 booIndex = _pCatalog != nullptr ? _pCatalog->findBooIndex( booId ) : -1;
        return booIndex >= 0 && static_cast<size_t>( booIndex ) < _listBoo.size() ? &_listBoo[static_cast<size_t>( booIndex )] : nullptr;
    }

    int32 GhostMansion::countCaughtBoos() const
    {
        int32 count = 0;
        for ( const GhostBooRuntime& boo : _listBoo )
        {
            if ( boo._state == GhostBooState::Caught )
                ++count;
        }
        return count;
    }

    bool GhostMansion::isSearched( const hashed_string& furnitureId ) const
    {
        const int32 furnitureIndex = findFurnitureIndex( furnitureId );
        return furnitureIndex >= 0 && _listSearched[static_cast<size_t>( furnitureIndex )] == SW_TRUE;
    }

    int32 GhostMansion::findFurnitureIndex( const hashed_string& furnitureId ) const
    {
        if ( _pCatalog == nullptr )
            return -1;
        const vector<GhostFurnitureDef>& listFurniture = _pCatalog->getFurniture();
        for ( size_t index = 0; index < listFurniture.size(); ++index )
        {
            if ( listFurniture[index]._id == furnitureId )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void GhostMansion::lightRoom( const hashed_string& roomId )
    {
        const GhostRoomDef* pRoom = _pCatalog != nullptr ? _pCatalog->findRoom( roomId ) : nullptr;
        if ( pRoom == nullptr || _pFlags == nullptr )
            return;
        _pFlags->setFlag( pRoom->_lightFlag, 1 );
        pushEvent( GhostMansionEventType::RoomLit, roomId, roomId );
        if ( pRoom->_keyReward.empty() )
            return;
        // 가방이 없거나 차면 열쇠를 받지 못한다(알림도 없다) — 칸 수는 게임이 정한다.
        if ( _pInventory == nullptr || _pInventory->addItem( pRoom->_keyReward, 1 ) == 0 )
            return;
        pushEvent( GhostMansionEventType::KeyAwarded, pRoom->_keyReward, roomId );
    }

    void GhostMansion::revealBoo( size_t booIndex )
    {
        GhostBooRuntime& boo = _listBoo[booIndex];
        boo._state           = GhostBooState::Revealed;
        boo._timer.start( _pCatalog->getBoos()[booIndex]._escapeTime );
        pushEvent( GhostMansionEventType::BooRevealed, _pCatalog->getBoos()[booIndex]._id, boo._room );
    }

    void GhostMansion::moveBooAway( size_t booIndex )
    {
        GhostBooRuntime& boo = _listBoo[booIndex];
        // 이웃 방 — 유령은 벽을 지나므로 잠긴 문 · 일방통행을 가리지 않는다. 연결을 읽은 순서라 늘 같은 목록이다.
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
                    bListed = bListed || neighbor == other;
                if ( bListed == false )
                    listNeighbor.push_back( other );
            }
        }
        if ( listNeighbor.empty() == false )
            boo._room = listNeighbor[static_cast<size_t>( _random.nextInt( 0, static_cast<int32>( listNeighbor.size() ) - 1 ) )];
        // 새 방의 가구 중 다른 부가 숨지 않은 것 하나에 숨는다 — 없으면 방 어딘가.
        vector<hashed_string> listHideout;
        for ( const GhostFurnitureDef& furniture : _pCatalog->getFurniture() )
        {
            if ( ( furniture._room == boo._room ) == false )
                continue;
            bool bTaken = false;
            for ( const GhostBooRuntime& other : _listBoo )
                bTaken = bTaken || ( other._state == GhostBooState::Hiding && other._furniture == furniture._id );
            if ( bTaken == false )
                listHideout.push_back( furniture._id );
        }
        boo._furniture = listHideout.empty() ? hashed_string{}
                                             : listHideout[static_cast<size_t>( _random.nextInt( 0, static_cast<int32>( listHideout.size() ) - 1 ) )];
        boo._state     = GhostBooState::Hiding;
        boo._timer.clear();
        pushEvent( GhostMansionEventType::BooEscaped, _pCatalog->getBoos()[booIndex]._id, boo._room );
    }

    void GhostMansion::pushEvent( GhostMansionEventType type, const hashed_string& id, const hashed_string& room, int32 count )
    {
        GhostMansionEvent event;
        event._type  = type;
        event._id    = id;
        event._room  = room;
        event._count = count;
        _eventBuffer.push( event );
    }
} // namespace sw
