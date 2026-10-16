#include "pch.h"

#include "Games/WildWest/WildWestGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/WildWest/WildWestDirectorComponent.h"
#include "Games/WildWest/WildWestHorseComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    WildWestGame::WildWestGame()
    {
        // 디렉터는 판 상태가 없지만(머리만 싣는다) 다른 게임과 같은 자리에 올린다. 말 상태(스태미나 · 유대)는 말 컴포넌트가 싣는다.
        registerDirector<WildWestDirectorComponent>();
        registerStatefulComponent<WildWestHorseComponent>();
    }

    WildWestGame::~WildWestGame() = default;

    bool WildWestGame::onInitialize()
    {
        // 서부극 카탈로그는 디렉터가 씬 데이터의 경로로 읽는다(코드에 경로를 두지 않는다).
        (void)requestFirstScene(); // 실패는 씬 흐름이 알린다
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::WildWestGame );
