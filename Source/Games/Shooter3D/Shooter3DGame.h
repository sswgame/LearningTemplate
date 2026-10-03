/**
 * @file Shooter3DGame.h
 * @brief 기반의 무기 규칙(GameFramework/Combat)을 실제로 쓰는 시험 게임 — 상자가 놓인 아레나에서 웨이브로 몰려오는 드론을 히트스캔 무기로 막는 1인칭 슈터입니다.
 *
 * @details 빌드: `cmake --preset <preset> -DSW_ACTIVE_GAME=Shooter3D`. 조작은 `Source/Games/Shooter3D/README.md`.
 *          `-gv_shooterAutoPlay=1` 이면 조준 · 사격도 AI 가 한다.
 */
#pragma once
#include "GameFramework/Base/GameInstanceBase.h"
#include "GameFramework/Combat/Weapon.h"

#include "Games/Shooter3D/ShooterArena.h"

namespace sw
{
    /** @brief 무기 카탈로그와 아레나 규칙을 듭니다. */
    class Shooter3DGame : public GameInstanceBase
    {
    public:
        Shooter3DGame();
        ~Shooter3DGame() override;

    protected:
        void configureBootstrap( BootstrapConfig& outConfig ) override;
        bool onInitialize() override;
        void onShutdown() override;
        void onUpdate( float32 deltaTime ) override;
        void onBeforeStateSerialize() override;
        void onAfterStateDeserialize() override;

    private:
        WeaponCatalog _weaponCatalog;
        ShooterArena  _arena;
    };
} // namespace sw
