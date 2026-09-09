/**
 * @file EmptyGame.h
 * @brief 최소 SWGame 템플릿 (키트 없음)
 *
 * @details 새 게임의 출발점이다. 실제 게임 로직은 없고, 측정용 벤치 하네스(`BenchScene`)를
 *          `-gv_benchMeshes=N` 이 주어졌을 때만 깨운다. 이 폴더를 복사해 새 게임을 시작할
 *          때는 `BenchScene.*` 두 파일과 아래 `_benchScene` 을 지우면 된다.
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/GameInstanceBase.h"

namespace sw
{
    class BenchScene;

    /** @brief 부트스트랩만 지정하는 빈 게임 팩 */
    class EmptyGame : public GameInstanceBase
    {
    public:
        EmptyGame();
        /** @brief BenchScene 이 전방 선언이라 소멸자는 .cpp 에 둔다. */
        ~EmptyGame() override;

    protected:
        void configureBootstrap( BootstrapConfig& outConfig ) override;
        bool onInitialize() override;
        void onUpdate( float32 deltaTime ) override;

    private:
        /** @brief `-gv_benchMeshes=N` 이 주어졌을 때만 생긴다. 없으면 nullptr. */
        unique_ptr<BenchScene> _benchScene;
    };
} // namespace sw
