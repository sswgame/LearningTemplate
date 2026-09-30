#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIConstantBufferShadow.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    void RHIConstantBufferShadow::forget( RHIBufferHandle buffer )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapPendingWrite.erase( buffer );
    }

    void RHIConstantBufferShadow::clear()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapPendingWrite.clear();
        _listFilledScratch.clear();
    }

    uint32 RHIConstantBufferShadow::getPendingBufferCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return static_cast<uint32>( _mapPendingWrite.size() );
    }

    void RHIConstantBufferShadow::rememberWrite( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size )
    {
        if ( pData == nullptr || size == 0 )
            return;
        constexpr uint32 kAllSlotMask = ( 1u << constant::kMaxFrameCountInFlight ) - 1u;
        static_assert( constant::kMaxFrameCountInFlight < 32, "칸 비트가 uint32 에 담겨야 한다" );

        const uint32 staleSlotMask = kAllSlotMask & ~slotBitOf( slot );
        if ( staleSlotMask == 0 )
        {
            _mapPendingWrite.erase( buffer ); // 칸이 하나뿐이다. 채울 것이 없다.
            return;
        }

        PendingWrite& pending = _mapPendingWrite[buffer];
        // 쓰기는 늘 버퍼 앞에서부터다. 이번 쓰기가 지난번보다 짧으면 뒤쪽은 지난 값이 그대로 칸에 남아 있어야 하므로 앞부분만 덮는다.
        if ( pending._bytes.size() < size )
            pending._bytes.resize( size );
        Memory::copy( pending._bytes.data(), pData, size );
        pending._staleSlotMask = staleSlotMask;
    }
} // namespace sw
