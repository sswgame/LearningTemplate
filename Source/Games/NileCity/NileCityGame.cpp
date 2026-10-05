#include "pch.h"

#include "Games/NileCity/NileCityGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/NileCity/NileDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    NileCityGame::NileCityGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<NileDirectorComponent>();
    }

    NileCityGame::~NileCityGame() = default;

    bool NileCityGame::onInitialize()
    {
        // 도시 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::NileCityGame );
