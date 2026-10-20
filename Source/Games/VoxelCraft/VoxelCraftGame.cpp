#include "pch.h"

#include "Games/VoxelCraft/VoxelCraftGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"
#include "Games/VoxelCraft/VoxelPlayerComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    VoxelCraftGame::VoxelCraftGame()
        : _blockCatalog{}
    {
        // 상태 스냅샷에 들어가는 컴포넌트입니다. 저장하기 전에 상태를 기록하고 스폰한 오브젝트를 정리하며, 복원한 뒤에 되돌려 줍니다.
        registerDirector<VoxelDirectorComponent>();
        registerStatefulComponent<VoxelPlayerComponent>();
    }

    VoxelCraftGame::~VoxelCraftGame() = default;

    bool VoxelCraftGame::onInitialize()
    {
        // 블록 데이터는 필수입니다. 읽지 못하면 게임을 시작하지 않습니다. 읽으면 디렉터와 플레이어가 찾는 게임 범위에 등록됩니다.
        // 사운드 이벤트는 플레이어 자신의 소리라 2D 입니다. 베이스 클래스가 gamesettings 의 audioEvents 를 읽어 로드했습니다.
        if ( loadRequiredCatalog( _blockCatalog ) == false )
            return false;
        // 섬 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::VoxelCraftGame );
