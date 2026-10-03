#include "pch.h"

#include "GameFramework/Kits/Voxel/VoxelHotbar.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    VoxelHotbar::VoxelHotbar()
        : _arrSlot{}
        , _selectedIndex{ 0 }
    {
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
