#include "pch.h"

#include "Games/Empty/EmptyGame.h"

#include "Games/Empty/BenchScene.h"

#if defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Renderer/Module/ModuleRenderPassRegistry.h"

    #include "GameFramework/Base/Foundation/Framework/GameService.h"
#endif

#include "RuntimeAPI/Export/GameModuleExports.h"

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
        // 모듈 렌더 패스 시연(ModuleRenderPass M2): 파이프라인 `game/empty/pipeline/modulepasspipeline.xml` 의 `_type="Module"` 패스가 이 이름을 찾는다. 그 파이프라인을 고르지 않은
        // 실행에서는 이름만 등록돼 있고 아무도 부르지 않는다. 넘기는 것은 이름과 상수(데이터)뿐이다 — 상수 이름 `g_OutlineColor` 는 셰이더가 틴트로 빌린 엔진 패스 상수 칸이다.
        ModuleRenderPassRegistry* pPassRegistry = game::findSubsystem<ModuleRenderPassRegistry>();
        if ( pPassRegistry != nullptr )
        {
            _modulePassDemo = make_unique<ModuleRenderPassRegistration>( *pPassRegistry, "Empty.ModulePassDemo" );
            if ( _modulePassDemo->isRegistered() )
                (void)_modulePassDemo->setConstant( "g_OutlineColor", float4{ 1.0f, 0.25f, 0.25f, 1.0f } ); // 붉은 틴트, 비율 1
        }
#endif

        // 이 템플릿이 하는 일은 벤치 하네스를 시작하는 것뿐입니다. 새 게임을 시작할 때는 아래 두 줄과
        // BenchScene.* 를 지우고 자기 씬을 만듭니다.
        _benchScene = make_unique<BenchScene>();
        if ( _benchScene->spawnFromGlobals() == false )
            _benchScene.reset();

        // 벤치가 아니면 처음 씬을 엽니다. 우선순위는 실행 설정의 시작 씬, GameSettings 의 타이틀 씬, 시작 맵 순입니다.
        // 에디터 밖에서 실행해도 씬이 없는 채로 뜨지 않게 하고, 배포본이 씬 로드 경로(SCN1, 프리팹 GUID 해석)를 실제로 실행하는 곳이기도 합니다.
        // 에디터가 자기 시작 씬(-gv_editorStartupScene)을 열면 그 요청이 나중에 큐에 들어가므로 이깁니다. SceneManager 는 마지막 요청만 남깁니다.
        if ( _benchScene == nullptr )
            (void)requestFirstScene(); // 열 씬이 없으면 빈 씬으로 시작합니다. 요청이 실패하면 함수 안에서 알립니다.

        return true;
    }

    void EmptyGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );

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
