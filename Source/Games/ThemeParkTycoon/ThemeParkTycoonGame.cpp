/**
 * @file ThemeParkTycoonGame.cpp
 * @brief 테마파크 키트(GF_ThemePark)를 실제로 쓰는 테스트 게임 — 코스터를 짓고(테스트 운행이 평가를 매긴다) 손님을 받아 돈 · 평점을 키우는 경영 게임입니다.
 *
 * @details 실행: `App.exe -game=ThemeParkTycoon`(`Ninja-Debug` 빌드). 조작은 `Source/Games/ThemeParkTycoon/README.md`.
 *          공원은 씬(`game/themepark/maps/park.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `ParkDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 상태 저장 전에 디렉터의 공원 상태를 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 공원 상태를 돌려줍니다.
 *          `-gv_parkAutoBuild=1` 이면 돈이 모이는 대로 다음 놀이기구를 짓는다(입력 없이 공원이 크는 확인).
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

#include "Games/ThemeParkTycoon/ParkDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 공원 게임 인스턴스입니다. */
    class ThemeParkTycoonGame : public GameInstance
    {
    public:
        ThemeParkTycoonGame();
        ~ThemeParkTycoonGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw

namespace sw
{
    ThemeParkTycoonGame::ThemeParkTycoonGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        getStateSnapshot().registerDirector<ParkDirectorComponent>();
    }

    ThemeParkTycoonGame::~ThemeParkTycoonGame() = default;

    bool ThemeParkTycoonGame::onInitialize()
    {
        // 사운드 이벤트(모두 2D — 위에서 내려다보는 공원)는 베이스가 gamesettings 의 audioEvents 로 올렸다.
        // 공원 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)getSubsystem<GameSceneFlow>().requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::ThemeParkTycoonGame );
