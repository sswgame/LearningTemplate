#include "pch.h"

#include "Games/PixelTown/PixelTownGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/PixelTown/TownDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    PixelTownGame::PixelTownGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고, 복원 뒤 돌려준다.
        registerDirector<TownDirectorComponent>();
    }

    PixelTownGame::~PixelTownGame() = default;

    bool PixelTownGame::onInitialize()
    {
        // 마을 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::PixelTownGame );
