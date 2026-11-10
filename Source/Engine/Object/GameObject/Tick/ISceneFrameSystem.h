/**
 * @file ISceneFrameSystem.h
 * @brief 씬 한 프레임의 정해진 자리(`FrameSystems` 단계)에서 한 번 도는 시스템의 계약입니다. 매니저가 소유합니다.
 * @details 시스템은 둘 중 하나입니다 — **서명이 곧 레인입니다**(5-3 2 단계, 예전의 `_bLaneSafe` 선언 대신).
 *          - `ISceneMainFrameSystem::runMain( GameObjectManager&, dt )`: 게임 스레드. 오브젝트를 만들고 지우고, 입력 · UI · 싱글턴을 만져도 된다.
 *          - `ISceneLaneFrameSystem::runLane( SceneJobContext& )`: 워커 레인. 매니저는 읽기 전용으로만 보이고 쓰기는 컨텍스트의 명령 버퍼(틱 뒤 큐)뿐이라,
 *            남의 컴포넌트를 바꾸거나 구조를 바꾸는 코드는 컴파일되지 않는다. 자기 멤버(색인 · 장부)만 바꾼다.
 *          유니티 `ISystem`(메인) / `IJobEntity`(잡 + `EntityCommandBuffer.ParallelWriter`), 언리얼 Mass 프로세서의 `FMassExecutionContext` 와 같은 자리다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Tick/SceneFrameAccess.h"

namespace sw
{
    class GameObjectManager;
    class StructuralChangeBuffer;
    class ISceneLaneFrameSystem;

    /**
     * @class SceneJobContext
     * @brief 워커 레인 시스템이 받는 잡 컨텍스트입니다 — 매니저 읽기 전용, 이번 프레임의 시간, 틱 뒤 큐(명령 버퍼)만 엽니다.
     */
    class SW_API SceneJobContext
    {
    public:
        using Callback = Delegate<void()>;

        SceneJobContext( const GameObjectManager& manager, StructuralChangeBuffer& commandBuffer, float32 deltaTime )
            : _pManager{ &manager }
            , _pCommandBuffer{ &commandBuffer }
            , _deltaTime{ deltaTime }
        {
        }

        /** @brief 씬을 읽기 전용으로 봅니다. */
        const GameObjectManager& getManager() const { return *_pManager; }
        /** @brief 이번 프레임의 시간입니다. */
        float32 getDeltaTime() const { return _deltaTime; }
        /** @brief 씬을 바꾸는 일을 틱 뒤 큐에 넣습니다(게임 스레드가 `ApplyPrePhysics` 에서 돌린다). 남의 오브젝트 쓰기 · 구조 변경은 여기로만 간다. */
        void deferPostTick( Callback func );

    private:
        const GameObjectManager* _pManager;
        StructuralChangeBuffer*  _pCommandBuffer;
        float32                  _deltaTime;
    };

    /**
     * @class ISceneFrameSystem
     * @brief `SceneFrameStepList.xxx` 의 `FrameSystems` 단계(BeginPlay 뒤 · PrePhysics 틱 앞)에서 붙인 순서대로 불리는 시스템의 공통 바탕입니다.
     * @details 틱 밖이라 구조가 얼어 있지 않습니다. 컴포넌트 틱 그룹으로는 "병렬 틱보다 먼저" 를 말할 수 없어서 있는 자리입니다.
     *          **핫 리로드 되는 모듈(게임 · 키트)의 타입은 붙이지 않습니다** — 매니저가 소유한 객체의 vtable 이 모듈과 함께 사라집니다.
     *          엔진 · GameFramework 기반의 것만 붙이고, 그것도 자기 컴포넌트가 모두 빠지면 뗍니다(모듈을 내리면 컴포넌트가 먼저 지워진다).
     *
     *          **붙인 순서가 의미입니다.** 그 의미를 말하는 것이 `getFrameAccess` 입니다 — 매니저는 읽는 시스템 뒤에 쓰는 시스템을 붙이는 등록을 거절합니다
     *          (`addFrameSystem`). 접근이 겹치지 않는 레인 시스템끼리는 워커에서 같이 돌고(`FrameSystemLanePlan`), 겹치는 것은 붙인 순서의 파도로 차례로 돕니다.
     *          이 바탕을 바로 상속하지 않고 `ISceneMainFrameSystem` · `ISceneLaneFrameSystem` 중 하나를 상속합니다.
     */
    class SW_API ISceneFrameSystem
    {
    public:
        virtual ~ISceneFrameSystem() = default;

        /**
         * @brief 시스템이 읽고 쓰는 씬 데이터의 종류입니다. 기본값이 없어 구현체가 반드시 적습니다(건드리지 않으면 빈 집합으로 적는다).
         * @details 늘 같은 답이어야 합니다 — `addFrameSystem` 이 이미 붙은 시스템의 선언을 다시 묻습니다. 코드를 따라가 확인한 것만 적고, 확신이 없는 종류는 쓰기로 적습니다.
         */
        virtual SceneFrameAccess getFrameAccess() const = 0;

        /** @brief 워커 레인 시스템이면 그 형식입니다. 게임 스레드 시스템이면 nullptr 입니다(매니저가 디스패치를 가른다). */
        virtual ISceneLaneFrameSystem* findLaneSystem() = 0;
        /** @brief 게임 스레드에서 돕니다. 레인 시스템은 매니저가 이것을 부르지 않는다. */
        virtual void runMain( GameObjectManager& manager, float32 deltaTime ) = 0;

    protected:
        ISceneFrameSystem()                                      = default;
        ISceneFrameSystem( const ISceneFrameSystem& )            = delete;
        ISceneFrameSystem& operator=( const ISceneFrameSystem& ) = delete;
    };

    /** @class ISceneMainFrameSystem @brief 게임 스레드 시스템입니다 — `runMain` 을 구현합니다. */
    class SW_API ISceneMainFrameSystem : public ISceneFrameSystem
    {
    public:
        ISceneLaneFrameSystem* findLaneSystem() final { return nullptr; }
    };

    /** @class ISceneLaneFrameSystem @brief 워커 레인 시스템입니다 — `runLane` 을 구현합니다. 구조 쓰기(`SceneDataKind::Structure`)를 선언하면 등록이 거절됩니다. */
    class SW_API ISceneLaneFrameSystem : public ISceneFrameSystem
    {
    public:
        /** @brief 이번 프레임의 일을 합니다(워커 또는 게임 스레드 — 어느 쪽이어도 같아야 한다). */
        virtual void runLane( SceneJobContext& context ) = 0;

        ISceneLaneFrameSystem* findLaneSystem() final { return this; }
        /** @brief 레인 시스템도 게임 스레드에서 돌 때는 같은 컨텍스트로 돈다(같은 형식 제약). */
        void runMain( GameObjectManager& manager, float32 deltaTime ) final;
    };
} // namespace sw
