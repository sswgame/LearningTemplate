/**
 * @file AbilityArenaGame.cpp
 * @brief 어빌리티 시스템(GameFramework/Runtime/Gameplay/Ability)을 실제로 쓰는 테스트 게임 — 웨이브를 막는 탑다운 아레나입니다.
 *
 * @details 실행: `App.exe -game=AbilityArena`(`Ninja-Debug` 빌드). 조작: WASD 이동, J/Space 근접, K/2 화염구(마나 · 화상), L/3 회복(데이터만),
 *          LeftShift/4 대시(무적). `-gv_arenaAutoPlay=1` 이면 플레이어도 AI 가 움직인다. 자세한 것은 `Source/Games/AbilityArena/README.md`.
 *          아레나는 씬(`game/abilityarena/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `ArenaGameModeComponent` 가 섭니다.
 *          이 클래스는 어빌리티 카탈로그를 들고 게임 서비스로 걸고, 첫 씬을 열고, 상태 저장 전에 판의 진행(웨이브 · 처치 수)을 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 진행을 돌려줍니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

#include "Games/AbilityArena/ArenaAbilities.h"
#include "Games/AbilityArena/ArenaGameModeComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 어빌리티 데이터(공유 캐시의 한 벌)와 클래스 레지스트리(이 모듈의 코드)를 들고 게임 서비스로 겁니다 — 모듈이 다시 올라오면 새 인스턴스가 다시 건다. */
    class AbilityArenaGame : public GameInstance
    {
    public:
        AbilityArenaGame();
        ~AbilityArenaGame() override;

    protected:
        bool onInitialize() override;

    private:
        AbilityClassRegistry             _abilityClasses;  ///< 이 게임의 어빌리티 클래스(코드)
        shared_ptr<const AbilityCatalog> _pAbilityCatalog; ///< 어빌리티 데이터(공유 캐시의 한 벌)
    };
} // namespace sw

namespace sw
{
    AbilityArenaGame::AbilityArenaGame()
        : _abilityClasses{}
        , _pAbilityCatalog{}
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        getStateSnapshot().registerGameMode<ArenaGameModeComponent>();
    }

    AbilityArenaGame::~AbilityArenaGame() = default;

    bool AbilityArenaGame::onInitialize()
    {
        // 클래스는 코드라 데이터와 따로 건다 — 없는 클래스 이름은 만들 때(`AbilityClassRegistry::createAbility`) 경고한다.
        ArenaAbilities::registerClasses( _abilityClasses );
        bindScopedService( &_abilityClasses );
        // 어빌리티 데이터는 필수다 — 못 읽으면 게임을 띄우지 않는다. 읽으면 어빌리티 시스템이 찾는 게임 범위에 걸린다.
        if ( loadRequiredCatalog( _pAbilityCatalog ) == false )
            return false;
        // 아레나 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)getSubsystem<GameSceneFlow>().requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::AbilityArenaGame );
