/**
 * @file VoxelCraftGame.cpp
 * @brief 복셀 키트(GF_Voxel)를 실제로 쓰는 테스트 게임입니다. 시드로 생성한 섬을 걸어 다니며 블록을 부수고 쌓는 마인크래프트 장르입니다.
 *
 * @details 실행은 `App.exe -game=VoxelCraft` 이고 `Ninja-Debug` 빌드를 씁니다. 조작은 `Source/Games/VoxelCraft/README.md` 에 있습니다.
 *          `-gv_voxelAutoPlay=1` 을 주면 걷기, 점프, 부수기, 놓기를 AI 가 대신합니다.
 *          섬은 씬(`game/voxelcraft/maps/island.scene.xml`, 팩의 `data/gamesettings.xml` 에 적힌 시작 맵)이 만들고,
 *          그 안의 `VoxelDirectorComponent` 와 `VoxelPlayerComponent` 가 동작시킵니다.
 *          이 클래스는 블록 카탈로그를 게임 서비스로 등록하고 첫 씬을 엽니다.
 *          상태를 저장하기 전에는 블록과 플레이어 상태를 기록하고 디렉터가 스폰한 청크를 정리합니다. 복원한 뒤에는 그 상태를 되돌려 줍니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"
#include "Games/VoxelCraft/VoxelPlayerComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 블록 카탈로그를 보관하고 게임 서비스로 등록합니다. 모듈을 다시 로드하면 새 인스턴스가 다시 등록합니다. */
    class VoxelCraftGame : public GameInstance
    {
    public:
        VoxelCraftGame();
        ~VoxelCraftGame() override;

    protected:
        bool onInitialize() override;

    private:
        VoxelBlockCatalog _blockCatalog;
    };
} // namespace sw

namespace sw
{
    VoxelCraftGame::VoxelCraftGame()
        : _blockCatalog{}
    {
        // 상태 스냅샷에 들어가는 컴포넌트입니다. 저장하기 전에 상태를 기록하고 스폰한 오브젝트를 정리하며, 복원한 뒤에 되돌려 줍니다.
        getStateSnapshot().registerDirector<VoxelDirectorComponent>();
        getStateSnapshot().registerStatefulComponent<VoxelPlayerComponent>();
    }

    VoxelCraftGame::~VoxelCraftGame() = default;

    bool VoxelCraftGame::onInitialize()
    {
        // 블록 데이터는 필수입니다. 읽지 못하면 게임을 시작하지 않습니다. 읽으면 디렉터와 플레이어가 찾는 게임 범위에 등록됩니다.
        // 사운드 이벤트는 플레이어 자신의 소리라 2D 입니다. 베이스 클래스가 gamesettings 의 audioEvents 를 읽어 로드했습니다.
        if ( loadRequiredCatalog( _blockCatalog ) == false )
            return false;
        // 섬 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)getSubsystem<GameSceneFlow>().requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::VoxelCraftGame );
