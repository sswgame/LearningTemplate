#include "pch.h"

#include "Engine/Graphics/RHI/ConstantBufferSlotAllocator.h"

namespace sw
{
    uint32 ConstantBufferSlotAllocator::findSlotBytes( const uint32 sizeBytes )
    {
        if ( sizeBytes == 0 || sizeBytes > kMaxSlotBytes )
            return 0;
        uint32 slotBytes = kMinSlotBytes;
        while ( slotBytes < sizeBytes )
        {
            slotBytes <<= 1u;
        }
        return slotBytes;
    }

    ConstantBufferSlot ConstantBufferSlotAllocator::allocate( const uint32 sizeBytes, bool& outbNewPage )
    {
        outbNewPage            = false;
        const uint32 slotBytes = findSlotBytes( sizeBytes );
        if ( slotBytes == 0 )
            return ConstantBufferSlot{};
        // 같은 등급의 열린 페이지에 빈 칸이 있으면 그것을 쓴다(앞 페이지부터 — 뒤 페이지가 먼저 비어 닫히게).
        for ( uint32 pageIndex = 0; pageIndex < static_cast<uint32>( _listPage.size() ); ++pageIndex )
        {
            Page& page = _listPage[pageIndex];
            if ( page._slotBytes != slotBytes || page._listFreeOffset.empty() )
                continue;
            const uint32 offset = page._listFreeOffset.back();
            page._listFreeOffset.pop_back();
            ++page._usedCount;
            return ConstantBufferSlot{ pageIndex, offset, slotBytes };
        }
        // 새 페이지 — 닫힌 번호가 있으면 다시 쓴다.
        uint32 pageIndex = static_cast<uint32>( _listPage.size() );
        for ( uint32 index = 0; index < static_cast<uint32>( _listPage.size() ); ++index )
        {
            if ( _listPage[index]._slotBytes == 0 )
            {
                pageIndex = index;
                break;
            }
        }
        if ( pageIndex == _listPage.size() )
            _listPage.emplace_back();
        Page& page      = _listPage[pageIndex];
        page._slotBytes = slotBytes;
        page._usedCount = 1;
        page._listFreeOffset.clear();
        const uint32 slotCount = kPageBytes / slotBytes;
        // 0 번 칸을 지금 주고, 나머지는 높은 오프셋부터 쌓아 낮은 오프셋이 먼저 나가게 한다.
        for ( uint32 slot = slotCount; slot > 1; --slot )
        {
            page._listFreeOffset.push_back( ( slot - 1 ) * slotBytes );
        }
        outbNewPage = true;
        return ConstantBufferSlot{ pageIndex, 0, slotBytes };
    }

    bool ConstantBufferSlotAllocator::release( const ConstantBufferSlot& slot )
    {
        if ( slot.isValid() == false || slot._pageIndex >= _listPage.size() )
            return false;
        Page& page = _listPage[slot._pageIndex];
        SW_ASSERT( page._slotBytes == slot._sizeBytes && page._usedCount > 0 && "ConstantBufferSlotAllocator::release: the slot does not belong to this page" );
        if ( page._slotBytes != slot._sizeBytes || page._usedCount == 0 )
            return false;
        page._listFreeOffset.push_back( slot._offset );
        if ( --page._usedCount > 0 )
            return false;
        page._slotBytes = 0; // 닫는다 — 다음 새 페이지가 이 번호를 다시 쓴다
        page._listFreeOffset.clear();
        return true;
    }

    uint32 ConstantBufferSlotAllocator::getOpenPageCount() const
    {
        uint32 count = 0;
        for ( const Page& page : _listPage )
        {
            count += page._slotBytes != 0 ? 1u : 0u;
        }
        return count;
    }

    ConstantBufferSlot ConstantBufferSlotAllocator::reallocate( const ConstantBufferSlot& slot, const uint32 sizeBytes, bool& outbNewPage, bool& outbOldPageClosed )
    {
        outbNewPage       = false;
        outbOldPageClosed = false;
        if ( slot.isValid() && findSlotBytes( sizeBytes ) == slot._sizeBytes )
            return slot;
        const ConstantBufferSlot moved = allocate( sizeBytes, outbNewPage );
        outbOldPageClosed              = release( slot );
        return moved;
    }
} // namespace sw
