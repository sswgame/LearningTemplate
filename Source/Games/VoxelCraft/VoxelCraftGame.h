/**
 * @file VoxelCraftGame.h
 * @brief 복셀 키트(GF_Voxel)를 실제로 쓰는 시험 게임 — 씨앗으로 지은 섬을 걸어 다니며 블록을 부수고 쌓는 마인크래프트 장르입니다.
 *
 * @details 빌드: `cmake --preset Ninja-Debug-VoxelCraft`. 조작은 `Source/Games/VoxelCraft/README.md`. `-gv_voxelAutoPlay=1` 이면 걷기 · 점프 · 부수기 · 놓기를 AI 가 한다.
 *          섬은 씬(`game/voxelcraft/maps/island.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `VoxelDirectorComponent` · `VoxelPlayerComponent`
 *          가 섭니다. 이 클래스는 블록 카탈로그를 게임 서비스로 걸고, 첫 씬을 열고, 상태 저장 전에 블록 · 플레이어 상태를 싣고 디렉터가 세운 청크를
 *          걷으며, 복원 뒤 그 상태를 돌려줍니다.
 */
#pragma once
#include "GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

namespace sw
{
    /** @brief 블록 카탈로그를 들고 게임 서비스로 겁니다 — 모듈이 다시 올라오면 새 인스턴스가 다시 건다. */
    class VoxelCraftGame : public GameInstanceBase
    {
    public:
        VoxelCraftGame();
        ~VoxelCraftGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;

    private:
        VoxelBlockCatalog _blockCatalog;
    };
} // namespace sw
