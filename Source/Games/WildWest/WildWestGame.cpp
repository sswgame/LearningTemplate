/**
 * @file WildWestGame.cpp
 * @brief 서부 평원 테스트 게임 — 레드 데드 리뎀션 2 · 젤다 BotW 식으로 걷다가 말에 올라타고, 걸음새와 스태미나로 몰고, 내립니다. 동쪽 마을에서 명예 · 현상금 · 사냥 · 생존 규칙을 돌립니다.
 * @details 실행: `App.exe -game=WildWest`(`Ninja-Debug` 빌드). 씬(`game/wildwest/maps/prairie.scene.xml`)의 `Ranch` 오브젝트에 `WildWestGameModeComponent` 가 있고,
 *          말 오브젝트마다 `WesternHorseComponent` 가 말 상태를 가진다. 이 클래스는 디렉터와 말 컴포넌트를 상태 스냅숏에 올린다(첫 씬은 바탕 `GameInstance` 가 연다).
 *          탑승 단계 `Mount` · `Dismount` 와 감시 값 `Pawn.Seat` 을 실제 게임에서 돌리는 자리이다(`automation/mount.scenario.xml`).
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

#include "Games/WildWest/WildWestGameModeComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 서부 평원 승마 게임 인스턴스입니다. */
    class WildWestGame : public GameInstance
    {
    public:
        WildWestGame();
        ~WildWestGame() override;
    };
} // namespace sw

namespace sw
{
    WildWestGame::WildWestGame()
    {
        // 플레이어의 삶(명예 · 법 · 생존 · 사냥)은 디렉터가, 지갑 · 평판 · 시계 · 날씨는 판 상태가, 말 상태(스태미나 · 유대)는 말 컴포넌트가 싣는다.
        getStateSnapshot().registerGameMode<WildWestGameModeComponent>();
        getStateSnapshot().registerStatefulComponent<GameStateComponent>();
        getStateSnapshot().registerStatefulComponent<WesternHorseComponent>();
    }

    WildWestGame::~WildWestGame() = default;
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::WildWestGame );
