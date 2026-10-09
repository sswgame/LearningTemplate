/**
 * @file VoxelHotbar.h
 * @brief 아홉 칸 핫바 — 부순 블록을 모으고 고른 칸의 블록을 놓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

namespace sw
{
    class Archive;

    /** @brief 핫바 한 칸입니다. 개수가 0 이면 빈 칸입니다. */
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
     * @brief 블록을 넣으면 같은 블록이 든 칸부터 64 개까지 채우고 남으면 앞쪽 빈 칸에 넣습니다. 고른 칸에서 하나씩 꺼내 놓습니다.
     */
    class SW_GF_API VoxelHotbar
    {
    public:
        static constexpr int32 kSlotCount = 9;
        static constexpr int32 kMaxStack  = 64;

        VoxelHotbar();

        /** @brief 블록을 넣습니다. 넣지 못한 개수(가득 참)를 돌려줍니다. */
        int32 addBlock( VoxelBlockIndex block, int32 count );
        /** @brief 고른 칸에서 하나를 꺼냅니다. 비었으면 false 입니다. */
        [[nodiscard]] bool consumeSelected( VoxelBlockIndex& outBlock );
        void               select( int32 slotIndex );
        /** @brief 휠 — @p offset 만큼 고른 칸을 돌립니다(끝에서 감긴다). */
        void selectRelative( int32 offset );

        int32                  getSelectedIndex() const { return _selectedIndex; }
        const VoxelHotbarSlot& getSlot( int32 slotIndex ) const;
        const VoxelHotbarSlot& getSelectedSlot() const { return getSlot( _selectedIndex ); }
        int32                  countBlock( VoxelBlockIndex block ) const;

        /** @brief 칸마다 블록 · 개수와 고른 칸을 씁니다(핫 리로드 · 세이브). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 칸 · 개수가 범위 밖이거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        VoxelHotbarSlot _arrSlot[kSlotCount];
        int32           _selectedIndex;
    };
} // namespace sw
