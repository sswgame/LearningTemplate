#include "pch.h"

#include "Games/AbilityArena/AbilityArenaGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "Games/AbilityArena/ArenaAbilities.h"
#include "Games/AbilityArena/ArenaDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    AbilityArenaGame::AbilityArenaGame()
        : _abilityCatalog{}
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<ArenaDirectorComponent>();
    }

    AbilityArenaGame::~AbilityArenaGame() = default;

    bool AbilityArenaGame::onInitialize()
    {
        // 클래스를 먼저 등록하고 데이터를 읽는다 — 읽을 때 모르는 클래스 이름은 경고한다.
        ArenaAbilities::registerClasses( _abilityCatalog );
        // 어빌리티 데이터는 필수다 — 못 읽으면 게임을 띄우지 않는다. 읽으면 어빌리티 시스템이 찾는 게임 범위에 걸린다.
        if ( loadRequiredCatalog( _abilityCatalog ) == false )
            return false;
        // 아레나 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::AbilityArenaGame );
