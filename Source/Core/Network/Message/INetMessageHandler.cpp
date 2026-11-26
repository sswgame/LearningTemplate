#include "pch.h"

#include "Core/Network/Message/INetMessageHandler.h"

namespace sw
{
    bool INetMessageHandler::isMessageKindHandled( uint8 kind ) const
    {
        const uint8 rangeBase = getMessageRangeBase();
        if ( NetMessageRange::isInRange( kind, rangeBase ) == false )
            return false;
        return ( ( getMessageKindMask() >> ( kind - rangeBase ) ) & 1u ) != 0;
    }

    NetHandleResult INetMessageHandler::handleMessage( int32 connectionID, const uint8* pData, int32 size, NetChannelKind channel )
    {
        const bool bMessageNotHandled = pData == nullptr || size <= 0 || isMessageKindHandled( pData[0] ) == false;
        if ( bMessageNotHandled )
            return NetHandleResult::NotMine;
        const int32 headSize = isWorldScoped() ? 2 : 1; // 종류 · 월드 세대(직접 전달은 세대를 보지 않는다)
        if ( size < headSize || connectionID < 0 )
            return NetHandleResult::Malformed;
        BitReader body( pData + headSize, size - headSize );
        return handleNetMessage( NetMessageContext{ pData, size, connectionID, channel, pData[0] }, body );
    }
} // namespace sw
