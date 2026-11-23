#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 디렉터 · 플레이어가 찾는 블록 카탈로그(`VoxelBlockCatalog`, 팩의 `data/blocks.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `voxelblocks` 가 있어야 한다. */
        [[nodiscard]] bool activateVoxel( GameFeatureScope& scope )
        {
            return scope.loadCatalog<VoxelBlockCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( Voxel, "Voxel", &activateVoxel );
} // namespace sw
