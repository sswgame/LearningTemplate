/**
 * @file VoxelHotbar.h
 * @brief 아홉 슬롯 핫바입니다. 부순 블록을 모으고, 고른 슬롯의 블록을 놓습니다.
 */
#pragma once
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Serialization/Format/StateIO.h"

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
     * @details 카탈로그는 빌려 쓴다(`VoxelWorld` 와 같다). 세이브에 블록을 id 로 적고 읽을 때 번호로 되돌리는 데 쓴다.
     */
    class SW_KIT_API VoxelHotbar
    {
    public:
        static constexpr int32  kSlotCount    = 9;
        static constexpr int32  kMaxStack     = 64;
        static constexpr uint32 kStateTag     = FourCCUtil::make( "VXHB" );
        static constexpr uint32 kStateVersion = 2; ///< 2: 슬롯 블록을 카탈로그 순서 번호가 아니라 블록 id 로 적는다

        /** @brief 세이브의 블록 id ↔ 번호에 @p pCatalog 를 씁니다. 저장하지 않는 핫바(테스트)는 nullptr 이어도 됩니다 — 읽기는 카탈로그 없이 거절합니다. */
        explicit VoxelHotbar( const VoxelBlockCatalog* pCatalog );

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

        /**
         * @brief 슬롯마다 블록 id 와 개수, 그리고 고른 슬롯을 씁니다. 핫 리로드와 세이브에 씁니다.
         *        `writeState` 가 쓴 바이트로 상태를 복원합니다. 블록은 id 로 지금 카탈로그 번호에 짝지으므로 카탈로그 순서가 바뀌어도 같은 블록입니다.
         *        판이 다르거나, 카탈로그가 없거나, 지금 카탈로그에 없는 id 이거나, 슬롯이나 개수가 범위 밖이거나 바이트가 손상됐으면 false 를 반환하고 상태는 그대로 둡니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        VoxelHotbarSlot          _arrSlot[kSlotCount];
        const VoxelBlockCatalog* _pCatalog;
        int32                    _selectedIndex;
    };
} // namespace sw
