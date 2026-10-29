/**
 * @file NileCityGame.cpp
 * @brief 도시 건설 키트(GF_CityBuilder)를 실제로 쓰는 테스트 게임 — 나일 강 범람원에 농장을 두고 도로 · 우물 · 창고 · 시장 · 집을 지어 도시를 키우는 파라오 장르입니다.
 *
 * @details 실행: `App.exe -game=NileCity`(`Ninja-Debug` 빌드). 조작은 `Source/Games/NileCity/README.md`.
 *          도시는 씬(`game/nilecity/maps/nile.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `NileDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 상태 저장 전에 디렉터의 도시 상태를 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 도시 상태를 돌려줍니다.
 *          `-gv_nileAutoPlay=1` 이면 자동 계획표대로 짓고 달마다 `[Nile] month N pop P money M` 을 남긴다(입력 없이 도시가 크는 확인).
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

#include "Games/NileCity/NileDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 나일 도시 게임 인스턴스입니다. */
    class NileCityGame : public GameInstance
    {
    public:
        NileCityGame();
        ~NileCityGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw

namespace sw
{
    NileCityGame::NileCityGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        getStateSnapshot().registerDirector<NileDirectorComponent>();
    }

    NileCityGame::~NileCityGame() = default;

    bool NileCityGame::onInitialize()
    {
        // 사운드 이벤트(모두 2D — 위에서 내려다보는 도시)는 베이스가 gamesettings 의 audioEvents 로 올렸다.
        // 도시 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::NileCityGame );
