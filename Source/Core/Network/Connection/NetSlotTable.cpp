#include "pch.h"

#include "Core/Network/Connection/NetSlotTable.h"

#include "Core/Network/Security/INetSecurityProvider.h"

namespace sw
{
    NetSlotTable::NetSlotTable()
        : _listSlot{}
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
    }

    NetHostSlot* NetSlotTable::findConnected( int32 slotIndex )
    {
        return isValid( slotIndex ) && _listSlot[static_cast<size_t>( slotIndex )]._state == NetConnectionState::Connected ? &_listSlot[static_cast<size_t>( slotIndex )] : nullptr;
    }

    const NetHostSlot* NetSlotTable::findConnected( int32 slotIndex ) const
    {
        return isValid( slotIndex ) && _listSlot[static_cast<size_t>( slotIndex )]._state == NetConnectionState::Connected ? &_listSlot[static_cast<size_t>( slotIndex )] : nullptr;
    }

    int32 NetSlotTable::findByAddress( const NetAddress& address ) const
    {
        const auto iter = _mapSlotByAddress.find( makeAddressKey( address ) );
        return iter != _mapSlotByAddress.end() ? iter->second : -1;
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

    int32 NetSlotTable::findFreeSlot() const
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._state == NetConnectionState::Disconnected )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 NetSlotTable::countConnected() const
    {
        int32 count = 0;
        for ( const NetHostSlot& slot : _listSlot )
        {
            count += slot._state == NetConnectionState::Connected ? 1 : 0;
        }
        return count;
    }

    void NetSlotTable::collectConnected( vector<int32>& outListConnection ) const
    {
        outListConnection.clear();
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._state == NetConnectionState::Connected )
                outListConnection.push_back( static_cast<int32>( index ) );
        }
    }
} // namespace sw
