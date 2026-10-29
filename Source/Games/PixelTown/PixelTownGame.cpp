/**
 * @file PixelTownGame.cpp
 * @brief 오버월드 키트(GF_Overworld)를 쓰는 탑다운 RPG 2D 테스트 게임 — 타일 마을을 한 칸씩 걷고, 물 · 집 · 나무에 막히며, 주민과 대화합니다.
 *
 * @details 실행: `App.exe -game=PixelTown`(`Ninja-Debug` 빌드). 조작은 `Source/Games/PixelTown/README.md`.
 *          마을은 씬(`game/pixeltown/maps/town.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `TownDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 디렉터를 상태 스냅샷에 올립니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

#include "Games/PixelTown/TownDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 픽셀 마을 게임 인스턴스입니다. */
    class PixelTownGame : public GameInstance
    {
    public:
        PixelTownGame();
        ~PixelTownGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw

namespace sw
{
    PixelTownGame::PixelTownGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고, 복원 뒤 돌려준다.
        getStateSnapshot().registerDirector<TownDirectorComponent>();
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
