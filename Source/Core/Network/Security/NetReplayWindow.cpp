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
            // 창이 앞으로 간다 — 새로 창에 들어오는 비트(옛 번호가 쓰던 비트)를 비운다.
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
                // 번호가 아니라 차이로 돈다 — `number <= packetNumber` 꼴은 packetNumber 가 2^64-1 이면 number 가 0 으로 감겨 끝나지 않았다(상대가 정한 번호로 수신 스레드 정지).
                // 횟수는 advance(< kWindowSize)로 묶이고, packetNumber > _highest 라 _highest + step 은 감기지 않는다(RFC 4303 §3.4.3 · RFC 6347 §4.1.2.6).
                for ( uint64 step = 1; step <= advance; ++step )
                {
                    const uint64 slot = ( _highest + step ) % kWindowSize;
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
