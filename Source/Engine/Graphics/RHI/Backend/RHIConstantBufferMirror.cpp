#include "pch.h"

#include "Engine/Graphics/RHI/Backend/RHIConstantBufferMirror.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    void RHIConstantBufferMirror::remove( RHIBufferHandle buffer )
    {
        Shard&            shard = getShard( buffer );
        ScopedLock<mutex> lock{ shard._mutex };
        removeKeys( shard, buffer );
    }

    void RHIConstantBufferMirror::clear()
    {
        for ( Shard& shard : _arrShard )
        {
            ScopedLock<mutex> lock{ shard._mutex };
            shard._mapPendingWrite.clear();
            shard._listFilledScratch.clear();
        }
    }

    uint32 RHIConstantBufferMirror::getPendingBufferCount() const
    {
        uint32 count{ 0 };
        for ( const Shard& shard : _arrShard )
        {
            ScopedLock<mutex> lock{ shard._mutex };
            count += static_cast<uint32>( shard._mapPendingWrite.size() );
        }
        return count;
    }

    void RHIConstantBufferMirror::removeKeys( Shard& shard, RHIBufferHandle buffer )
    {
        // 페이지는 구간마다 열쇠가 있다 — 그 버퍼의 열쇠를 모아 지운다(대기 목록은 최근에 바뀐 자리뿐이라 짧다).
        shard._listFilledScratch.clear();
        for ( const auto& [key, pending] : shard._mapPendingWrite )
        {
            if ( key._buffer == buffer )
                shard._listFilledScratch.push_back( key );
        }
        for ( const Key& key : shard._listFilledScratch )
        {
            shard._mapPendingWrite.erase( key );
        }
    }

    void RHIConstantBufferMirror::rememberWrite( Shard& shard, const Key& key, uint32 slot, const void* pData, uint32 size )
    {
        if ( pData == nullptr || size == 0 )
            return;
        constexpr uint32 kAllSlotMask = ( 1u << constant::render::kMaxFrameCountInFlight ) - 1u;
        static_assert( constant::render::kMaxFrameCountInFlight < 32, "칸 비트가 uint32 에 담겨야 한다" );

        const uint32 staleSlotMask = kAllSlotMask & ~getSlotBitOf( slot );
        if ( staleSlotMask == 0 )
        {
            shard._mapPendingWrite.erase( key ); // 슬롯이 하나뿐이다. 채울 것이 없다.
            return;
        }

        PendingWrite& pending = shard._mapPendingWrite[key];
        // 쓰기는 항상 그 자리(버퍼 또는 구간) 앞에서부터다. 이번 쓰기가 지난번보다 짧으면 뒤쪽은 지난 값이 그대로 슬롯에 남아 있어야 하므로 앞부분만 덮는다.
        if ( pending._bytes.size() < size )
            pending._bytes.resize( size );
        Memory::copy( pending._bytes.data(), pData, size );
        pending._staleSlotMask = staleSlotMask;
    }
} // namespace sw
