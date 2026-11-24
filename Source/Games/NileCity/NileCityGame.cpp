/**
 * @file NileCityGame.cpp
 * @brief 도시 건설 키트(GF_CityBuilder)를 실제로 쓰는 테스트 게임 — 나일 강 범람원에 농장을 두고 도로 · 우물 · 창고 · 시장 · 집을 지어 도시를 키우는 파라오 장르입니다.
 *
 * @details 실행: `App.exe -game=NileCity`(`Ninja-Debug` 빌드).
 *          도시는 씬(`game/nilecity/maps/nile.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `NileGameModeComponent` 가 섭니다.
 *          이 클래스는 상태 저장 전에 디렉터의 도시 상태를 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 도시 상태를 돌려줍니다(첫 씬은 바탕 `GameInstance` 가 연다).
 *          `-gv_nileAutoPlay=1` 이면 자동 계획표대로 짓고 달마다 `[Nile] month N pop P money M` 을 남긴다(입력 없이 도시가 크는 확인).
 */
#include "pch.h"

#include "Engine/Object/Component/ComponentStateCodec.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Flow/GameInstance.h"

#include "Games/NileCity/NileGameModeComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 나일 도시 게임 인스턴스입니다. */
    class NileCityGame : public GameInstance
    {
    public:
        NileCityGame();
        ~NileCityGame() override;
    };
} // namespace sw

namespace sw
{
    NileCityGame::NileCityGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        getStateSnapshot().registerGameMode<NileGameModeComponent>();
    }

    NileCityGame::~NileCityGame() = default;
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::NileCityGame );
