/**
 * @file ISceneService.h
 * @brief 기능 폴더가 씬(`GameObjectManager`)마다 하나씩 두는 서비스의 계약입니다. 레지스트리와 등록 매크로는 `SceneServiceRegistry.h` 이고, 서비스 클래스의 .cpp 가 그것을 포함해 `SW_SCENE_SERVICE` 한 줄을 둡니다.
 * @details 코어(`Object/GameObject`)는 레지스트리만 갖고 기능 서비스(내비게이션 …)의 타입을 모릅니다. 기능 폴더가 정적 초기화에서 이 표에 만드는 함수와
 *          돌 프레임 단계를 올리고, 매니저는 생성자에서 표를 훑어 서비스를 만들고 소유합니다 — 의존은 기능 → 코어 한 방향입니다.
 *          언리얼 `UWorldSubsystem`(모듈이 타입을 올리고 월드가 인스턴스를 만든다, `GetSubsystem<T>()`)과 같은 모양입니다.
 *
 *          서비스는 서브시스템(`Subsystem`)이고 매니저의 **씬 범위**(`SubsystemScope`)가 소유한다 — 엔진 범위가 부모라 씬 범위에서 엔진 서비스도 찾는다.
 *          - **순서**: 서비스가 선언한 의존(`getSubsystemDependencies`)으로 위상 정렬한 순서로 세우고(초기화), 같은 단계의 갱신도 그 순서로 돈다.
 *            매니저가 소멸하면 역순으로 내리고 지운다.
 *          - **갱신**: 매니저가 등록 줄의 단계(`SceneFrameStepList.xxx` 의 한 줄)에서 `update` 를 부릅니다(게임 스레드). 등록 줄은 `update` 가 읽고 쓰는 씬 데이터
 *            (`SceneFrameAccess`)를 반드시 적는다 — 매니저는 그 선언을 다른 서비스의 넘긴 일(아래)과 견주어, 일이 도는 사이에 겹치는 데이터를 고치는 갱신을 만들 때 오류로 알린다.
 *          - **프레임 일**: `update` 가 워커에 넘긴 일(`SW_SCENE_SERVICE_WITH_FRAME_JOB`)은 등록 줄의 마치는 단계 **본문 앞**에서 `completeFrameJob` 이 기다리고 결과를 씁니다.
 *            그 사이 단계들(`update` 단계 끝 ~ 마치는 단계 앞)은 일과 겹쳐 돕니다. 일이 그 사이에 읽고 쓰는 씬 데이터는 등록 줄의 접근(`SceneFrameAccess`)이 말하고,
 *            매니저는 사이가 겹치는 두 서비스의 일이 겹치는 접근을 선언했거나 마치는 단계가 넘기는 단계 뒤가 아니면 만들 때 오류로 알립니다.
 *            언리얼 `FTickFunction` 의 `bRunOnAnyThread` + 끝 그룹(`EndTickGroup`) · 유니티 `JobHandle.Complete` 를 정한 자리에서 부르는 것과 같은 모양입니다.
 *          - **비우기**: `GameObjectManager::clear` 가 `onSceneCleared` 를 부릅니다. 서비스는 남고 다음 씬에서 다시 씁니다.
 *          - **얻기**: `manager.getSubsystem<T>()` 입니다(씬 범위에서 타입으로 찾는다).
 *
 * @note 표는 Engine 이미지 안에서만 채웁니다. 표의 항목은 내리지 않으므로, 핫 리로드 되는 모듈(게임 · 키트)이 쓰면 언로드 뒤 죽은 함수를 가리킵니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/Subsystem.h"

namespace sw
{
    class GameObjectManager;
    class ISceneParticleSink;
    struct GPULight;
    struct ScenePreviewPolicy;
    struct SceneServiceRegistration;

    /**
     * @brief 기능이 씬 라이트 목록에 원소를 붙이는 자리입니다(`GameObjectManager::collectServiceLights`). 값의 순서가 목록의 순서입니다 — 상한을 넘으면 뒤에서부터 잘리므로 순서가 곧 뜻입니다.
     *        라이트 컴포넌트 → `Particles` → 프로브 볼륨 · 반사 프로브 → `ShadowOccluders` 입니다(`collectSceneLights`).
     */
    enum class SceneLightPhase : uint8
    {
        Particles,       ///< 라이트 컴포넌트 뒤 · 프로브 앞(파티클 라이트)
        ShadowOccluders, ///< 맨 뒤(2D 그림자 가림막) — 넘치면 빛보다 먼저 빠진다
    };

    /**
     * @class ISceneService
     * @brief 씬마다 하나 있는 기능 서비스입니다. 매니저가 만들고 소유합니다(파일 머리말 참고).
     */
    class SW_API ISceneService : public Subsystem
    {
    public:
        ~ISceneService() override = default;

        /** @brief 이번 프레임을 진행합니다(게임 스레드, 등록 줄의 단계에서). 매니저가 서비스 형식의 할당 태그(`kSubsystemMemoryTag`)를 걸고 부른다. */
        virtual void update( float32 deltaTime ) = 0;
        /**
         * @brief `update` 가 워커에 넘긴 일을 기다리고 결과를 씁니다(게임 스레드, 등록 줄의 `_completeStep` 단계 본문 앞). 넘기는 일이 없는 서비스에는 불리지 않습니다.
         * @details 넘긴 일이 없는 프레임(빈 씬)에도 불린다 — 할 일이 없으면 바로 돌아온다. 씬을 비울 때(`onSceneCleared`)는 서비스가 스스로 기다린다.
         */
        virtual void completeFrameJob() {}
        /** @brief 씬을 비울 때 들고 있던 것을 놓습니다(`GameObjectManager::clear`). 서비스 자체는 남습니다. */
        virtual void onSceneCleared() = 0;

        // 씬이 기능을 모르게 하는 훅 — 씬(`Scene` · `SceneManager`)은 모든 서비스에 같은 부름을 돌리고, 기능이 자기 몫만 한다.
        /** @brief 씬 문서를 짓기 전에 그 원본 경로 @p sourcePath 를 알립니다(쿠킹본 이름을 찾는 서비스 — 내비메시). */
        virtual void onSceneLoading( string_view sourcePath ) { (void)sourcePath; }
        /**
         * @brief 월드 원점이 옮겨졌습니다 — 씬이 모든 루트 오브젝트를 @p delta 만큼 옮긴 뒤 부릅니다(`Scene::shiftWorldOrigin`, 게임 스레드 · 틱 밖).
         * @details 오브젝트 밖에 월드 좌표를 들고 있는 서비스(캐시 · 시뮬레이션 상태)가 같은 만큼 옮긴다. 오브젝트에서 매 프레임 다시 읽는 서비스(오디오 · 렌더 수집)는 할 일이 없다.
         */
        virtual void onWorldOriginShifted( const float3& delta ) { (void)delta; }
        /** @brief 미리보기 씬의 정책을 적용합니다 — 오디오는 켜고 끄기, 물리는 고정 스텝을 읽는다. */
        virtual void applyPreviewPolicy( const ScenePreviewPolicy& policy ) { (void)policy; }

        // 렌더 기여 — 렌더러(층 7)가 기능 컴포넌트를 이름으로 알지 않게 모든 서비스에 같은 부름을 돌린다(게임 스레드, 패킷을 채울 때 씬 · 뷰마다 한 번). 언리얼 `FSceneViewExtension` 과 같은 자리다.
        // 새 렌더 기능은 자기 폴더의 씬 서비스가 이것을 구현한다 — 렌더러 본문을 고치지 않는다.
        /** @brief @p phase 자리에 자기 GPU 라이트 원소를 붙입니다. */
        virtual void contributeLights( SceneLightPhase phase, vector<GPULight>& inoutList ) const
        {
            (void)phase;
            (void)inoutList;
        }
        /** @brief 이번 프레임의 입자 그리기를 냅니다. */
        virtual void contributeParticles( ISceneParticleSink& sink ) const { (void)sink; }
        // 씬에 하나뿐인 환경 상태(하늘 · 안개 · 표면 변형)는 여기 없다 — 환경 층의 칸 하나가 든다(`SceneEnvironment`, 먼저 맡은 서비스가 이기던 순회를 지웠다).

        /** @brief 씬 서비스는 만들 때(매니저의 생성자) 선다 — 따로 세울 것이 없으면 그대로 둔다. */
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        /** @brief 내릴 것은 소멸자가 놓는다(매니저가 범위를 역순으로 지운다). */
        void                       shutdownSubsystem() override {}
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Scene;

    protected:
        ISceneService()                                  = default;
        ISceneService( const ISceneService& )            = delete;
        ISceneService& operator=( const ISceneService& ) = delete;
    };
} // namespace sw
