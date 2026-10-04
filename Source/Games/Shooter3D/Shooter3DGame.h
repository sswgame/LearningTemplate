/**
 * @file Shooter3DGame.h
 * @brief 기반의 무기 규칙(GameFramework/Combat)을 실제로 쓰는 시험 게임 — 상자가 놓인 아레나에서 웨이브로 몰려오는 스켈레톤을 히트스캔 무기로 막는 슈터입니다.
 *
 * @details 빌드: `cmake --preset Ninja-Debug-Shooter3D`. 조작은 `Source/Games/Shooter3D/README.md`. `-gv_shooterAutoPlay=1` 이면 조준 · 사격도 AI 가 한다.
 *          아레나는 씬(`game/shooter3d/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `ShooterDirectorComponent` ·
 *          `ShooterPlayerComponent` 가 섭니다. 이 클래스는 무기 카탈로그 · 장비 아이템 · 외형 데이터(`AppearanceDatabase`)를 게임 서비스로 걸고, 첫 씬을 열고, 상태 저장 전에 판의 진행(웨이브 · 처치 수)을 싣고 디렉터가 세운 것을 걷으며, 복원 뒤 진행을 돌려줍니다.
 */
#pragma once
#include "GameFramework/Appearance/AppearanceDatabase.h"
#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/Framework/GameInstanceBase.h"
#include "GameFramework/Inventory/ItemCatalog.h"

namespace sw
{
    /** @brief 무기 카탈로그 · 아이템 · 외형 데이터를 들고 게임 서비스로 겁니다 — 모듈이 다시 올라오면 새 인스턴스가 다시 건다. */
    class Shooter3DGame : public GameInstanceBase
    {
    public:
        Shooter3DGame();
        ~Shooter3DGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;

    private:
        WeaponCatalog      _weaponCatalog;
        ItemCatalog        _itemCatalog;
        AppearanceDatabase _appearanceDatabase;
    };
} // namespace sw
