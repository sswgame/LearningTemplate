/**
 * @file EmptyGame.cpp
 * @brief 최소 SWGame 템플릿 (키트 없음)
 *
 * @details 새 게임의 출발점이다. 실제 게임 로직은 없고, 측정용 벤치 하네스(`StressTestScene`)를
 *          `-gv_stressTestMeshes=N` 이 주어졌을 때만 깨운다. 이 폴더를 복사해 새 게임을 시작할
 *          때는 `StressTestScene.*` 두 파일과 아래 `_benchScene` 을 지우면 된다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

#include "Games/Empty/StressTestScene.h"

#if defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Renderer/Extension/RenderExtensionRegistry.h"

    #include "Games/Empty/EmptyRenderDemo.h"
    #include "GameFramework/Base/Foundation/Framework/GameService.h"
#endif

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    class StressTestScene;
#if defined( SW_WITH_CLIENT_CODE )
    class EmptyRenderDemo;
#endif

    /** @brief 기동만 지정하는 빈 게임 팩 */
    class EmptyGame : public GameInstance
    {
    public:
        EmptyGame();
        /** @brief StressTestScene 이 전방 선언이라 소멸자는 .cpp 에 둔다. */
        ~EmptyGame() override;

    protected:
        bool onInitialize() override;
        void onUpdate( float32 deltaTime ) override;
        /** @brief 상태 스냅샷을 만들기 직전에 부릅니다. 벤치는 절차적으로 생성한 오브젝트라서 정리해 스냅샷에 싣지 않습니다. */
        void onBeforeStateSerialize() override;
        /** @brief 상태 복원이 씬을 교체한 뒤에 부릅니다. 벤치는 스냅샷에 없으므로 다시 만듭니다. */
        void onAfterStateDeserialize() override;

    private:
        /** @brief `-gv_stressTestMeshes=N` 이 주어졌을 때만 생긴다. 없으면 nullptr. */
        unique_ptr<StressTestScene> _benchScene;

        /** @brief 모듈 렌더 확장 시연(`ExtensionRenderPass` M3 · M4)의 등록 수명입니다. 렌더러가 없는 구성(서버)에는 멤버가 없다. 이 객체가 사라지면 flush 한 뒤 등록이 풀린다(핫 리로드 때 남지 않는다). */
#if defined( SW_WITH_CLIENT_CODE )
        unique_ptr<EmptyRenderDemo> _renderDemo;
#endif
    };
} // namespace sw

namespace sw
{
    EmptyGame::EmptyGame()
        : _benchScene{ nullptr }
    {
    }

    EmptyGame::~EmptyGame() = default;

    bool EmptyGame::onInitialize()
    {
#if defined( SW_WITH_CLIENT_CODE )
        // 모듈 렌더 확장 시연(ExtensionRenderPass M3 · M4): 렌더 스레드 확장 객체(패스 둘)와 큐브 프록시를 등록한다. 패스의 셰이더는 파이프라인
        // `game/empty/pipeline/extensionpasspipeline.xml` 의 `_listExtensionShader` 가 선언한다 — 확장 셰이더를 선언하지 않은 파이프라인에서는 확장이 쉰다(오류 아님).
        // 소멸(핫 리로드 · 종료)은 렌더 스레드의 모듈 구간이 끝나기를 기다린 뒤 등록을 푼다.
        RenderExtensionRegistry* pPassRegistry = game::findSubsystem<RenderExtensionRegistry>();
        if ( pPassRegistry != nullptr )
            _renderDemo = make_unique<EmptyRenderDemo>( *pPassRegistry );
#endif

        // 이 템플릿이 하는 일은 벤치 하네스를 시작하는 것뿐입니다. 새 게임을 시작할 때는 아래 두 줄과
        // StressTestScene.* 를 지우고 자기 씬을 만듭니다.
        _benchScene = make_unique<StressTestScene>();
        if ( _benchScene->spawnFromGlobals() == false )
            _benchScene.reset();

        // 벤치가 아니면 처음 씬을 엽니다. 우선순위는 실행 설정의 시작 씬, GameSettings 의 타이틀 씬, 시작 맵 순입니다.
        // 에디터 밖에서 실행해도 씬이 없는 채로 뜨지 않게 하고, 배포본이 씬 로드 경로(SCN1, 프리팹 GUID 해석)를 실제로 실행하는 곳이기도 합니다.
        // 에디터가 자기 시작 씬(-gv_editorStartupScene)을 열면 그 요청이 나중에 큐에 들어가므로 이깁니다. SceneManager 는 마지막 요청만 남깁니다.
        if ( _benchScene == nullptr )
            (void)getSubsystem<GameSceneFlow>().requestFirstScene(); // 열 씬이 없으면 빈 씬으로 시작합니다. 요청이 실패하면 함수 안에서 알립니다.

        return true;
    }

    void EmptyGame::onUpdate( float32 deltaTime )
    {
        GameInstance::onUpdate( deltaTime );

        if ( _benchScene != nullptr )
            _benchScene->update( deltaTime );
    }

    void EmptyGame::onBeforeStateSerialize()
    {
        // 벤치는 절차적으로 생성한 오브젝트입니다. 스냅샷에 실으면 복원된 오브젝트가 기존 핸들과 다른 오브젝트가 되고 메시도 없습니다.
        // 미리 정리해 두면(pending destroy 상태는 스냅샷이 건너뜁니다) 복원한 뒤 onAfterStateDeserialize 가 다시 만듭니다.
        if ( _benchScene != nullptr )
            _benchScene->despawn();
    }

    void EmptyGame::onAfterStateDeserialize()
    {
        // 모듈 리로드나 RHI 교체는 새 인스턴스를 만들고(onInitialize 가 벤치를 한 번 만듭니다) 곧바로 상태를 복원합니다.
        // 복원은 씬을 지우고 스냅샷대로 다시 만드는데, 벤치는 스냅샷에 없고 방금 만든 벤치는 지워집니다.
        // 그래서 여기서 벤치를 다시 만듭니다. 핸들 목록은 spawn 이 새로 채웁니다.
        if ( _benchScene != nullptr && _benchScene->spawnFromGlobals() == false )
            _benchScene.reset();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::EmptyGame );
