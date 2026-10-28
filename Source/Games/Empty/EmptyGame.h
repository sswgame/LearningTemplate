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

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h"

namespace sw
{
    class BenchScene;
#if defined( SW_WITH_CLIENT_CODE )
    class EmptyRenderDemo;
#endif

    /** @brief 부트스트랩만 지정하는 빈 게임 팩 */
    class EmptyGame : public GameInstanceBase
    {
    public:
        EmptyGame();
        /** @brief BenchScene 이 전방 선언이라 소멸자는 .cpp 에 둔다. */
        ~EmptyGame() override;

    protected:
        bool onInitialize() override;
        void onUpdate( float32 deltaTime ) override;
        /** @brief 상태 스냅샷을 만들기 직전에 부릅니다. 벤치는 절차적으로 생성한 오브젝트라서 정리해 스냅샷에 싣지 않습니다. */
        void onBeforeStateSerialize() override;
        /** @brief 상태 복원이 씬을 교체한 뒤에 부릅니다. 벤치는 스냅샷에 없으므로 다시 만듭니다. */
        void onAfterStateDeserialize() override;

    private:
        /** @brief `-gv_benchMeshes=N` 이 주어졌을 때만 생긴다. 없으면 nullptr. */
        unique_ptr<BenchScene> _benchScene;

        /** @brief 모듈 렌더 확장 시연(`ModuleRenderPass` M3 · M4)의 등록 수명입니다. 렌더러가 없는 구성(서버)에는 멤버가 없다. 이 객체가 사라지면 flush 한 뒤 등록이 풀린다(핫 리로드 때 남지 않는다). */
#if defined( SW_WITH_CLIENT_CODE )
        unique_ptr<EmptyRenderDemo> _renderDemo;
#endif
    };
} // namespace sw
