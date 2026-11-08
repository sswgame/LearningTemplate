#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/IGameClientPart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/Empty/Client/Dev/EmptyRenderDemo.h"
#include "Games/Empty/Client/Dev/StressTestScene.h"

namespace sw
{
    /**
     * @class EmptyClientPart
     * @brief 빈 게임의 클라이언트 전용 부분입니다 — 측정 벤치(`StressTestScene`)와 모듈 렌더 확장 시연(`EmptyRenderDemo`)을 든다.
     * @details 게임 인스턴스가 기동에 하나 만들어 들고(`IGameClientPart`), 같이 사라진다. 전용 서버와 배포 구성은 이 소스(Client/Dev/)를 짓지 않는다.
     */
    class EmptyClientPart final : public IGameClientPart
    {
    public:
        bool initialize() override
        {
            // 모듈 렌더 확장 시연(ExtensionRenderPass M3 · M4): 렌더 스레드 확장 객체(패스 둘)와 큐브 프록시를 등록한다. 패스의 셰이더는 파이프라인
            // `game/empty/pipeline/extensionpasspipeline.xml` 의 `_listExtensionShader` 가 선언한다 — 확장 셰이더를 선언하지 않은 파이프라인에서는 확장이 쉰다(오류 아님).
            // 소멸(핫 리로드 · 종료)은 렌더 스레드의 모듈 구간이 끝나기를 기다린 뒤 등록을 푼다.
            RenderExtensionRegistry* pPassRegistry = game::findSubsystem<RenderExtensionRegistry>();
            if ( pPassRegistry != nullptr )
                _renderDemo = make_unique<EmptyRenderDemo>( *pPassRegistry );

            // 이 템플릿이 하는 일은 벤치 하네스를 시작하는 것뿐입니다. 새 게임을 시작할 때는 `Client/` 를 지우고 자기 씬을 만듭니다.
            _benchScene = make_unique<StressTestScene>();
            if ( _benchScene->spawnFromGlobals() == false )
                _benchScene.reset();
            return _benchScene != nullptr;
        }

        void update( float32 deltaTime ) override
        {
            if ( _benchScene != nullptr )
                _benchScene->update( deltaTime );
        }

        void onBeforeStateSerialize() override
        {
            // 벤치는 절차적으로 생성한 오브젝트입니다. 스냅샷에 실으면 복원된 오브젝트가 기존 핸들과 다른 오브젝트가 되고 메시도 없습니다.
            // 미리 정리해 두면(pending destroy 상태는 스냅샷이 건너뜁니다) 복원한 뒤 onAfterStateDeserialize 가 다시 만듭니다.
            if ( _benchScene != nullptr )
                _benchScene->despawn();
        }

        void onAfterStateDeserialize() override
        {
            // 모듈 리로드나 RHI 교체는 새 인스턴스를 만들고(initialize 가 벤치를 한 번 만듭니다) 곧바로 상태를 복원합니다.
            // 복원은 씬을 지우고 스냅샷대로 다시 만드는데, 벤치는 스냅샷에 없고 방금 만든 벤치는 지워집니다. 그래서 여기서 다시 만듭니다.
            if ( _benchScene != nullptr && _benchScene->spawnFromGlobals() == false )
                _benchScene.reset();
        }

    private:
        unique_ptr<StressTestScene> _benchScene; ///< `-gv_stressTestMeshes=N` 이 주어졌을 때만 생긴다
        unique_ptr<EmptyRenderDemo> _renderDemo; ///< 렌더 확장 시연의 등록 수명 — 사라지면 flush 한 뒤 등록이 풀린다(핫 리로드 때 남지 않는다)
    };

    /** @class EmptyClientPartFactory @brief 빈 게임의 클라이언트 전용 부분을 만드는 모듈 창구입니다(`SW_MODULE_PART`). 이름 해시가 함수 서명 글이라 `sw` 에 둔다. */
    class EmptyClientPartFactory final : public IGameClientPartFactory
    {
    public:
        static constexpr const utf8* kModulePartName = "EmptyClientPartFactory";

        unique_ptr<IGameClientPart> createClientPart() override { return make_unique<EmptyClientPart>(); }
    };

    SW_MODULE_PART( Engine, EmptyClientPartFactory, IGameClientPartFactory );
} // namespace sw
