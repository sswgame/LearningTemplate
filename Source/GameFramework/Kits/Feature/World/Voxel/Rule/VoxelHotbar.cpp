#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelHotbar.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    VoxelHotbar::VoxelHotbar( const VoxelBlockCatalog* pCatalog )
        : _arrSlot{}
        , _pCatalog{ pCatalog }
        , _selectedIndex{ 0 }
    {
    }

    void VoxelHotbar::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion ); // 판 1(머리표 없음 · 카탈로그 순서 번호)은 거절한다
        if ( io.isReading() && io.check( _pCatalog != nullptr ) == false )
            return;
        for ( VoxelHotbarSlot& slot : _arrSlot )
        {
            // 번호는 실행 중에만 쓰고 세이브에는 id 를 적는다(`VoxelWorld` 팔레트와 같은 까닭 — 마인크래프트 인벤토리도 아이템 이름을 적는다).
            const VoxelBlockDef* pBlock  = io.isWriting() && _pCatalog != nullptr ? _pCatalog->findBlock( slot._block ) : nullptr;
            hashed_string        blockID = pBlock != nullptr ? pBlock->_id : hashed_string{};
            io.value( blockID );
            io.value( slot._count );
            if ( io.isReading() )
            {
                slot._block = blockID.empty() ? kVoxelAirBlock : _pCatalog->findBlockIndex( blockID );
                io.check( blockID.empty() || slot._block != kVoxelAirBlock ); // 지금 카탈로그에 없는 블록은 비우지 않고 거절한다
                io.check( blockID.empty() == false || slot._count == 0 );     // 블록 없이 개수만 있는 칸은 손상이다
            }
            io.check( 0 <= slot._count && slot._count <= kMaxStack );
        }
        io.value( _selectedIndex );
        io.check( 0 <= _selectedIndex && _selectedIndex < kSlotCount );
    }

    int32 VoxelHotbar::addBlock( VoxelBlockIndex block, int32 count )
    {
        if ( block == kVoxelAirBlock || count <= 0 )
            return 0;
        int32 remaining = count;
        for ( VoxelHotbarSlot& slot : _arrSlot )
        {
            if ( remaining == 0 )
                break;
            if ( slot.isEmpty() || slot._block != block )
                continue;
            const int32 moved = MathUtil::min( remaining, kMaxStack - slot._count );
            slot._count += moved;
            remaining -= moved;
        }
        for ( VoxelHotbarSlot& slot : _arrSlot )
        {
            if ( remaining == 0 )
                break;
            if ( slot.isEmpty() == false )
                continue;
            const int32 moved = MathUtil::min( remaining, kMaxStack );
            slot._block       = block;
            slot._count       = moved;
            remaining -= moved;
        }
        return remaining;
    }

    bool VoxelHotbar::consumeSelected( VoxelBlockIndex& outBlock )
    {
        VoxelHotbarSlot& slot = _arrSlot[_selectedIndex];
        if ( slot.isEmpty() )
            return false;
        outBlock = slot._block;
        --slot._count;
        if ( slot._count == 0 )
            slot._block = kVoxelAirBlock;
        return true;
    }

    void VoxelHotbar::select( int32 slotIndex )
    {
        _selectedIndex = MathUtil::clamp( slotIndex, 0, kSlotCount - 1 );
    }

    void VoxelHotbar::selectRelative( int32 offset )
    {
        _selectedIndex = ( ( _selectedIndex + offset ) % kSlotCount + kSlotCount ) % kSlotCount;
    }

    const VoxelHotbarSlot& VoxelHotbar::getSlot( int32 slotIndex ) const
    {
        return _arrSlot[MathUtil::clamp( slotIndex, 0, kSlotCount - 1 )];
    }

    int32 VoxelHotbar::countBlock( VoxelBlockIndex block ) const
    {
        int32 count = 0;
        for ( const VoxelHotbarSlot& slot : _arrSlot )
        {
            if ( slot.isEmpty() == false && slot._block == block )
                count += slot._count;
        }
        return count;
    }
} // namespace sw
