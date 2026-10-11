/**
 * @file VoxelHotbar.h
 * @brief 아홉 슬롯 핫바입니다. 부순 블록을 모으고, 고른 슬롯의 블록을 놓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

namespace sw
{
    class Archive;

    /** @brief 핫바 슬롯 하나입니다. 개수가 0이면 빈 슬롯입니다. */
    struct VoxelHotbarSlot
    {
        VoxelBlockIndex _block{ kVoxelAirBlock };
        int32           _count{ 0 };

        bool isEmpty() const { return _count <= 0 || _block == kVoxelAirBlock; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class VoxelHotbar
     * @brief 핫바 전체입니다. 블록을 넣으면 같은 블록이 든 슬롯부터 64개까지 채우고, 남으면 앞쪽 빈 슬롯에 넣습니다. 놓을 때는 고른 슬롯에서 하나씩 꺼냅니다.
     */
    class SW_GF_API VoxelHotbar
    {
    public:
        static constexpr int32 kSlotCount = 9;
        static constexpr int32 kMaxStack  = 64;

        VoxelHotbar();

        /** @brief 블록을 넣습니다. 넣지 못한 개수를 반환하며, 가득 찼을 때 0보다 큽니다. */
        int32 addBlock( VoxelBlockIndex block, int32 count );
        /** @brief 고른 슬롯에서 하나를 꺼냅니다. 비었으면 false 를 반환합니다. */
        [[nodiscard]] bool consumeSelected( VoxelBlockIndex& outBlock );
        void               select( int32 slotIndex );
        /** @brief 휠 입력을 받아 고른 슬롯을 @p offset 만큼 돌립니다. 끝에 닿으면 반대쪽으로 이어집니다. */
        void selectRelative( int32 offset );

        int32                  getSelectedIndex() const { return _selectedIndex; }
        const VoxelHotbarSlot& getSlot( int32 slotIndex ) const;
        const VoxelHotbarSlot& getSelectedSlot() const { return getSlot( _selectedIndex ); }
        int32                  countBlock( VoxelBlockIndex block ) const;

        /** @brief 슬롯마다 블록과 개수, 그리고 고른 슬롯을 씁니다. 핫 리로드와 세이브에 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 슬롯이나 개수가 범위 밖이거나 바이트가 손상됐으면 false 를 반환하고 상태는 그대로 둡니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        VoxelHotbarSlot _arrSlot[kSlotCount];
        int32           _selectedIndex;
    };
} // namespace sw
