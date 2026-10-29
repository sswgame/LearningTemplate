/**
 * @file WildWestGame.cpp
 * @brief 서부 평원 승마 테스트 게임 — 레드 데드 리뎀션 2 · 젤다 BotW 식으로 걷다가 말에 올라타고, 걸음새와 스태미나로 몰고, 내립니다.
 * @details 실행: `App.exe -game=WildWest`(`Ninja-Debug` 빌드). 씬(`game/wildwest/maps/prairie.scene.xml`)의 `Ranch` 오브젝트에 `WildWestDirectorComponent` 가 있고,
 *          말 오브젝트마다 `WildWestHorseComponent` 가 말 상태를 가진다. 이 클래스는 디렉터와 말 컴포넌트를 상태 스냅숏에 올리고 첫 씬을 청한다.
 *          탑승 단계 `Mount` · `Dismount` 와 감시 값 `Pawn.Seat` 을 실제 게임에서 돌리는 자리이다(`automation/mount.scenario.xml`).
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

#include "Games/WildWest/WildWestDirectorComponent.h"
#include "Games/WildWest/WildWestHorseComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 서부 평원 승마 게임 인스턴스입니다. */
    class WildWestGame : public GameInstance
    {
    public:
        WildWestGame();
        ~WildWestGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw

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
