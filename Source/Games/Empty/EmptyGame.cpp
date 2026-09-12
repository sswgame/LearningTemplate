#include "pch.h"

#include "Games/Empty/EmptyGame.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/GameService.h"

#include "Games/Empty/BenchScene.h"
#include "Games/Empty/EmptyGlobalVariable.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    EmptyGame::EmptyGame()
        : _benchScene{ nullptr }
    {
    }

    EmptyGame::~EmptyGame() = default;

    void EmptyGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/empty";
    }

    bool EmptyGame::onInitialize()
    {
        // 이 모듈의 전역 변수를 매니저에 올린다. 커맨드라인은 모듈 로드 전에 파싱되므로 값은
        // 파서의 보류표에 있고, 등록하는 이 순간 적용된다 — 아래에서 gv_benchMeshes 를 읽기 전에
        // 반드시 먼저 와야 한다.
        game::registerGlobalVariables();

        // 이 템플릿이 하는 일은 벤치 하네스를 깨우는 것뿐이다. 새 게임을 시작하면 아래 두 줄과
        // BenchScene.* 를 지우고 자기 씬을 세운다.
        _benchScene = make_unique<BenchScene>();
        if ( _benchScene->spawnFromGlobals() == false )
            _benchScene.reset();

        // 벤치가 아니면 GameConfig 의 시작 씬을 연다. 예전엔 에디터 밖의 실행은 씬 없이 떴다(백로그 0절의 "빈 씬"
        // 함정) — 배포본이 씬 로드 경로(SCN1 · 프리팹 GUID 해석)를 실제로 태우는 자리이기도 하다. 에디터가 자기
        // 시작 씬(-gv_editorStartupScene)을 열면 그 요청이 뒤에 큐잉되어 이긴다(SceneManager 는 마지막 요청을 남긴다).
        if ( _benchScene == nullptr )
        {
            const string& startupScene  = GameConfig::getActive()._startupScene;
            SceneManager* pSceneManager = game::getService<SceneManager>();
            if ( startupScene.empty() == false && pSceneManager != nullptr && pSceneManager->requestLoadAsync( startupScene ) == false )
                SW_LOG_ERROR( "Startup scene load request failed: %#", startupScene.c_str() );
        }

        return true;
    }

    void EmptyGame::onShutdown()
    {
        // 매니저가 들고 있는 것은 이 DLL 안의 주소다 — 모듈이 내려가기 전에 반드시 걷어내야 한다.
        // (서비스는 아직 바인딩돼 있다. ModuleHost 는 shutdown 뒤에 bindService(nullptr) 을 부른다.)
        game::unregisterGlobalVariables();
    }

    void EmptyGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );

        if ( _benchScene != nullptr )
            _benchScene->update( deltaTime );
    }

    void EmptyGame::onBeforeStateSerialize()
    {
        // 벤치는 절차 생성물이다 — 스냅샷에 실으면 복원된 것은 핸들과 다른 오브젝트고 메시도 없다.
        // 걷어 두면(pending kill 은 스냅샷이 건너뛴다) 복원 뒤 onAfterStateDeserialize 가 다시 만든다.
        if ( _benchScene != nullptr )
            _benchScene->despawn();
    }

    void EmptyGame::onAfterStateDeserialize()
    {
        // 모듈 리로드 · RHI 교체는 새 인스턴스를 만들어(onInitialize 가 벤치를 한 번 만든다) 곧바로 상태를
        // 복원하는데, 복원은 씬을 지우고 스냅샷대로 다시 만든다. 벤치는 스냅샷에 없고 방금 만든 것은
        // 지워졌다 — 그래서 여기서 다시 만든다. 핸들 목록은 spawn 이 새로 채운다.
        if ( _benchScene != nullptr && _benchScene->spawnFromGlobals() == false )
            _benchScene.reset();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::EmptyGame );
