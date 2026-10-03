/**
 * @file VoxelCraftGame.h
 * @brief 복셀 키트(GF_Voxel)를 실제로 쓰는 시험 게임 — 씨앗으로 지은 섬을 걸어 다니며 블록을 부수고 쌓는 마인크래프트 장르입니다.
 *
 * @details 빌드: `cmake --preset <preset> -DSW_ACTIVE_GAME=VoxelCraft`. 조작은 `Source/Games/VoxelCraft/README.md`.
 *          `-gv_voxelAutoPlay=1` 이면 걷기 · 점프 · 부수기 · 놓기를 AI 가 한다.
 */
#pragma once
#include "GameFramework/Base/GameInstanceBase.h"
#include "GameFramework/Kits/Voxel/VoxelBlock.h"

#include "Games/VoxelCraft/VoxelCraftWorld.h"

namespace sw
{
    /** @brief 블록 카탈로그와 월드를 듭니다. */
    class VoxelCraftGame : public GameInstanceBase
    {
    public:
        VoxelCraftGame();
        ~VoxelCraftGame() override;

    protected:
        void configureBootstrap( BootstrapConfig& outConfig ) override;
        bool onInitialize() override;
        void onShutdown() override;
        void onUpdate( float32 deltaTime ) override;
        void onBeforeStateSerialize() override;
        void onAfterStateDeserialize() override;

    private:
        VoxelBlockCatalog _blockCatalog;
        VoxelCraftWorld   _world;
    };
} // namespace sw
