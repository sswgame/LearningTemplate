#include "pch.h"

#include "Games/StarSkirmish/StarSkirmishGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/StarSkirmish/SkirmishDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    StarSkirmishGame::StarSkirmishGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<SkirmishDirectorComponent>();
    }

    StarSkirmishGame::~StarSkirmishGame() = default;

    bool StarSkirmishGame::onInitialize()
    {
        // 사운드 이벤트(알림은 ui 버스 2D, 부서진 유닛은 그 자리)는 베이스가 gamesettings 의 audioEvents 로 올렸다.
        // 전장 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::StarSkirmishGame );
