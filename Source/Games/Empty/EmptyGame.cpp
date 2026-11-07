/**
 * @file EmptyGame.cpp
 * @brief 최소 SWGame 템플릿 (키트 없음)
 *
 * @details 새 게임의 출발점이다. 실제 게임 로직은 없고, 측정용 벤치 하네스(`Client/StressTestScene`)를
 *          `-gv_stressTestMeshes=N` 이 주어졌을 때만 깨운다. 이 폴더를 복사해 새 게임을 시작할
 *          때는 `Client/` 폴더와 아래 `_clientPart` 를 지우면 된다.
 *          벤치 · 렌더 확장 시연 · UI 시연은 그리는 일이라 클라이언트 전용 소스(`Client/`)다 — 게임은 그 부분을 `IGameClientPart` 로만 들고, 전용 서버는 그 소스를 짓지 않아 없이 선다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IGameClientPart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 기동만 지정하는 빈 게임 팩 */
    class EmptyGame : public GameInstance
    {
    public:
        EmptyGame();
        ~EmptyGame() override;

    protected:
        bool onInitialize() override;
        void onUpdate( float32 deltaTime ) override;
        /** @brief 상태 스냅샷을 만들기 직전에 부릅니다. 벤치는 절차적으로 생성한 오브젝트라서 정리해 스냅샷에 싣지 않습니다. */
        void onBeforeStateSerialize() override;
        /** @brief 상태 복원이 씬을 교체한 뒤에 부릅니다. 벤치는 스냅샷에 없으므로 다시 만듭니다. */
        void onAfterStateDeserialize() override;

    private:
        /** @brief 클라이언트 전용 부분(벤치 · 렌더 확장 시연, `Client/EmptyClientPart.cpp`)입니다. 공장이 없는 구성(전용 서버)에는 nullptr 이다. */
        unique_ptr<IGameClientPart> _clientPart;
    };
} // namespace sw

namespace sw
{
    EmptyGame::EmptyGame()
        : _clientPart{ nullptr }
    {
    }

    EmptyGame::~EmptyGame() = default;

    bool EmptyGame::onInitialize()
    {
        if ( IGameClientPartFactory* const pFactory = game::findSubsystem<IGameClientPartFactory>() )
            _clientPart = pFactory->createClientPart();
        const bool bBenchRunning = _clientPart != nullptr && _clientPart->initialize();

        // 벤치가 아니면 처음 씬을 엽니다. 우선순위는 실행 설정의 시작 씬, GameSettings 의 타이틀 씬, 시작 맵 순입니다.
        // 에디터 밖에서 실행해도 씬이 없는 채로 뜨지 않게 하고, 배포본이 씬 로드 경로(SCN1, 프리팹 경로 해석)를 실제로 실행하는 곳이기도 합니다.
        // 에디터가 자기 시작 씬(-gv_editorStartupScene)을 열면 그 요청이 나중에 큐에 들어가므로 이깁니다. SceneManager 는 마지막 요청만 남깁니다.
        if ( bBenchRunning == false )
            (void)getSubsystem<GameSceneFlow>().requestFirstScene(); // 열 씬이 없으면 빈 씬으로 시작합니다. 요청이 실패하면 함수 안에서 알립니다.

        return true;
    }

    void EmptyGame::onUpdate( float32 deltaTime )
    {
        GameInstance::onUpdate( deltaTime );

        if ( _clientPart != nullptr )
            _clientPart->update( deltaTime );
    }

    void EmptyGame::onBeforeStateSerialize()
    {
        if ( _clientPart != nullptr )
            _clientPart->onBeforeStateSerialize();
    }

    void EmptyGame::onAfterStateDeserialize()
    {
        if ( _clientPart != nullptr )
            _clientPart->onAfterStateDeserialize();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::EmptyGame );
