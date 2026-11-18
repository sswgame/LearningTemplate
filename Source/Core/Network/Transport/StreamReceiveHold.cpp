#include "pch.h"

#include "Core/Network/Transport/StreamReceiveHold.h"

namespace sw
{
    StreamReceiveHold::StreamReceiveHold()
        : _heldBytes{}
        , _bPaused{ SW_FALSE }
    {
    }

    StreamReceiveHoldResult StreamReceiveHold::accept( const uint8* pData, int32 size, int32 maxHeldBytes )
    {
        if ( _bPaused == SW_FALSE && _heldBytes.empty() )
            return StreamReceiveHoldResult::Deliver;
        if ( static_cast<int64>( _heldBytes.size() ) + size > maxHeldBytes )
            return StreamReceiveHoldResult::Overflow;
        _heldBytes.insert( _heldBytes.end(), pData, pData + size );
        return StreamReceiveHoldResult::Held;
    }

    vector<uint8> StreamReceiveHold::takeReleasable()
    {
        vector<uint8> released;
        if ( _bPaused == SW_FALSE )
            released.swap( _heldBytes );
        return released;
    }

    void StreamReceiveHold::clear()
    {
        _heldBytes.clear();
        _bPaused = SW_FALSE;
    }
} // namespace sw
