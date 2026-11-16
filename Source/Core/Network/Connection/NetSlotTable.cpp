#include "pch.h"

#include "Core/Network/Connection/NetSlotTable.h"

#include "Core/Foundation/Constants.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include <algorithm>
#include <limits>

namespace sw
{
    NetSlotTable::NetSlotTable()
        : _listSlot{}
        , _listLiveSlot{}
        , _listFreeSlot{}
        , _mapSlotByAddress{}
    {
    }

    NetSlotTable::~NetSlotTable() = default;

    void NetSlotTable::reset( int32 slotCount )
    {
        _listSlot.clear();
        _listSlot.resize( static_cast<size_t>( slotCount > 0 ? slotCount : 0 ) );
        _mapSlotByAddress.clear();
        _mapSlotByAddress.reserve( _listSlot.size() * 2 );
        _listLiveSlot.clear();
        _listLiveSlot.reserve( _listSlot.size() );
        _listFreeSlot.clear();
        _listFreeSlot.reserve( _listSlot.size() );
        for ( int32 index = getCount() - 1; index >= 0; --index )
        {
            _listFreeSlot.push_back( index );
        }
    }

    void NetSlotTable::open( int32 slotIndex, NetConnectionState state )
    {
        SW_ASSERT( ( state == NetConnectionState::Connecting || state == NetConnectionState::Connected ) && "NetSlotTable::open takes a live state" );
        const auto freeIter = std::find( _listFreeSlot.begin(), _listFreeSlot.end(), slotIndex );
        SW_ASSERT( freeIter != _listFreeSlot.end() && "NetSlotTable::open needs a free slot" );
        _listFreeSlot.erase( freeIter );
        _listSlot[static_cast<size_t>( slotIndex )]._state      = state;
        _listSlot[static_cast<size_t>( slotIndex )]._connection = make_unique<NetConnection>();
        const auto liveIter                                     = std::lower_bound( _listLiveSlot.begin(), _listLiveSlot.end(), slotIndex, []( const NetLiveSlot& live, int32 index )
                                            { return live._slotIndex < index; } );
        _listLiveSlot.insert( liveIter, NetLiveSlot{ std::numeric_limits<float64>::lowest(), slotIndex } );
    }

    void NetSlotTable::close( int32 slotIndex, bool bHoldUntilReleased )
    {
        const optional<size_t> livePosition = findLive( slotIndex );
        SW_ASSERT( livePosition.has_value() && "NetSlotTable::close takes a live slot" );
        _listLiveSlot.erase( _listLiveSlot.begin() + static_cast<std::ptrdiff_t>( *livePosition ) );
        NetEndpointSlot& slot = _listSlot[static_cast<size_t>( slotIndex )];
        slot._state           = NetConnectionState::Disconnected;
        slot._bReleasePending = bHoldUntilReleased ? SW_TRUE : SW_FALSE;
        slot._connection.reset();
        if ( bHoldUntilReleased == false )
        {
            const auto freeIter = std::lower_bound( _listFreeSlot.begin(), _listFreeSlot.end(), slotIndex, []( int32 free, int32 index )
            { return free > index; } );
            _listFreeSlot.insert( freeIter, slotIndex );
        }
    }

    void NetSlotTable::release( int32 slotIndex )
    {
        NetEndpointSlot& slot = _listSlot[static_cast<size_t>( slotIndex )];
        if ( slot._bReleasePending == SW_FALSE || slot._state != NetConnectionState::Disconnected )
            return;
        slot._bReleasePending = SW_FALSE;
        const auto freeIter   = std::lower_bound( _listFreeSlot.begin(), _listFreeSlot.end(), slotIndex, []( int32 free, int32 index )
          { return free > index; } );
        _listFreeSlot.insert( freeIter, slotIndex );
    }

    void NetSlotTable::markDue( int32 slotIndex )
    {
        const optional<size_t> livePosition = findLive( slotIndex );
        if ( livePosition.has_value() )
            _listLiveSlot[*livePosition]._dueTime = std::numeric_limits<float64>::lowest();
    }

    void NetSlotTable::markAllDue()
    {
        for ( NetLiveSlot& live : _listLiveSlot )
        {
            live._dueTime = std::numeric_limits<float64>::lowest();
        }
    }

    void NetSlotTable::setDueTime( int32 slotIndex, float64 dueTime )
    {
        const optional<size_t> livePosition = findLive( slotIndex );
        SW_ASSERT( livePosition.has_value() && "NetSlotTable::setDueTime takes a live slot" );
        _listLiveSlot[*livePosition]._dueTime = dueTime;
    }

    float64 NetSlotTable::getNextDueTime() const
    {
        float64 nextDueTime = std::numeric_limits<float64>::infinity();
        for ( const NetLiveSlot& live : _listLiveSlot )
        {
            nextDueTime = live._dueTime < nextDueTime ? live._dueTime : nextDueTime;
        }
        return nextDueTime;
    }

    void NetSlotTable::collectDueSlots( float64 nowSeconds, vector<int32>& outListSlot ) const
    {
        outListSlot.clear();
        for ( const NetLiveSlot& live : _listLiveSlot )
        {
            if ( live._dueTime <= nowSeconds )
                outListSlot.push_back( live._slotIndex );
        }
    }

    optional<size_t> NetSlotTable::findLive( int32 slotIndex ) const
    {
        const auto iter = std::lower_bound( _listLiveSlot.begin(), _listLiveSlot.end(), slotIndex, []( const NetLiveSlot& live, int32 index )
        { return live._slotIndex < index; } );
        if ( iter == _listLiveSlot.end() || iter->_slotIndex != slotIndex )
            return nullopt;
        return static_cast<size_t>( iter - _listLiveSlot.begin() );
    }

    NetEndpointSlot* NetSlotTable::findConnected( int32 slotIndex )
    {
        return isValid( slotIndex ) && _listSlot[static_cast<size_t>( slotIndex )]._state == NetConnectionState::Connected ? &_listSlot[static_cast<size_t>( slotIndex )] : nullptr;
    }

    const NetEndpointSlot* NetSlotTable::findConnected( int32 slotIndex ) const
    {
        return isValid( slotIndex ) && _listSlot[static_cast<size_t>( slotIndex )]._state == NetConnectionState::Connected ? &_listSlot[static_cast<size_t>( slotIndex )] : nullptr;
    }

    optional<int32> NetSlotTable::findByAddress( const NetAddress& address ) const
    {
        const auto iter = _mapSlotByAddress.find( makeAddressKey( address ) );
        if ( iter == _mapSlotByAddress.end() )
            return nullopt;
        return iter->second;
    }

    void NetSlotTable::bindAddress( int32 slotIndex, const NetAddress& address )
    {
        _listSlot[static_cast<size_t>( slotIndex )]._address = address;
        _mapSlotByAddress[makeAddressKey( address )]         = slotIndex;
    }

    void NetSlotTable::unbindAddress( int32 slotIndex )
    {
        const auto iter = _mapSlotByAddress.find( makeAddressKey( _listSlot[static_cast<size_t>( slotIndex )]._address ) );
        if ( iter != _mapSlotByAddress.end() && iter->second == slotIndex )
            _mapSlotByAddress.erase( iter );
    }

    optional<int32> NetSlotTable::findFreeSlot() const
    {
        if ( _listFreeSlot.empty() )
            return nullopt;
        return _listFreeSlot.back();
    }

    int32 NetSlotTable::countConnected() const
    {
        int32 count = 0;
        for ( const NetLiveSlot& live : _listLiveSlot )
        {
            count += _listSlot[static_cast<size_t>( live._slotIndex )]._state == NetConnectionState::Connected ? 1 : 0;
        }
        return count;
    }

    void NetSlotTable::collectConnected( vector<int32>& outListConnection ) const
    {
        outListConnection.clear();
        for ( const NetLiveSlot& live : _listLiveSlot )
        {
            if ( _listSlot[static_cast<size_t>( live._slotIndex )]._state == NetConnectionState::Connected )
                outListConnection.push_back( live._slotIndex );
        }
    }
} // namespace sw
