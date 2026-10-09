#include "pch.h"

#include "Core/Network/Security/NetReplayWindow.h"

namespace sw
{
    NetReplayWindow::NetReplayWindow()
        : _arrBit{}
        , _highest{ 0 }
        , _bHasReceived{ SW_FALSE }
    {
    }

    void NetReplayWindow::reset()
    {
        for ( uint64& word : _arrBit )
        {
            word = 0;
        }
        _highest      = 0;
        _bHasReceived = SW_FALSE;
    }

    bool NetReplayWindow::isMarked( uint64 packetNumber ) const
    {
        const uint64 slot = packetNumber % kWindowSize;
        return ( _arrBit[slot / 64] & ( 1ull << ( slot % 64 ) ) ) != 0;
    }

    bool NetReplayWindow::isAcceptable( uint64 packetNumber ) const
    {
        if ( _bHasReceived == SW_FALSE || packetNumber > _highest )
            return true;
        if ( _highest - packetNumber >= kWindowSize )
            return false; // 창보다 옛것 — 받았는지 알 수 없으니 거절
        return isMarked( packetNumber ) == false;
    }

    void NetReplayWindow::markReceived( uint64 packetNumber )
    {
        if ( _bHasReceived == SW_FALSE || packetNumber > _highest )
        {
            // 창이 앞으로 간다 — 새로 창에 들어오는 칸(옛 번호가 쓰던 칸)을 비운다.
            const uint64 advance = _bHasReceived == SW_FALSE ? kWindowSize : packetNumber - _highest;
            if ( advance >= kWindowSize )
            {
                for ( uint64& word : _arrBit )
                {
                    word = 0;
                }
            }
            else
            {
                for ( uint64 number = _highest + 1; number <= packetNumber; ++number )
                {
                    const uint64 slot = number % kWindowSize;
                    _arrBit[slot / 64] &= ~( 1ull << ( slot % 64 ) );
                }
            }
            _highest      = packetNumber;
            _bHasReceived = SW_TRUE;
        }
        else if ( _highest - packetNumber >= kWindowSize )
        {
            return;
        }
        const uint64 slot = packetNumber % kWindowSize;
        _arrBit[slot / 64] |= 1ull << ( slot % 64 );
    }
} // namespace sw
