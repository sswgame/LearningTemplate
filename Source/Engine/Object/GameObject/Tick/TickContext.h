/**
 * @file TickContext.h
 * @brief 컴포넌트 틱(`Component::onTick` · `onSubTick`)이 받는 잡 컨텍스트입니다 — 워커 스레드 시스템의 `SceneJobContext` 에 자기 오브젝트를 더한 것입니다.
 * @details 틱은 자기 오브젝트(`getSelf`)만 쓰고, 씬은 읽기 전용(`getManager`)으로 보며, 남의 컴포넌트 쓰기 · 구조 변경은 명령 버퍼(`deferPostTick` ·
 *          `deferPostTickOnComponent`, 틱 뒤 큐)로 보낸다. 유니티 `IJobEntity` + `EntityCommandBuffer`, 언리얼 `FMassExecutionContext::Defer()` 와 같은
 *          자리다. 디스패치(`SceneTickScheduler` · `TickBatchOf`)가 원소마다 스택에 세운다.
 *          **쓰기 가능한 매니저 길:** `GameObject::getManager` 는 읽기 전용(const)을 돌려주고, 쓰기 가능한 매니저는 이름이 다른 `GameObject::getMutableManager`
 *          로만 얻는다 — 틱 본문의 습관 길(`getOwner()->getManager()`)로는 매니저를 바꾸는 호출이 컴파일되지 않는다. 틱 뒤 작업은 쓰기 가능한 매니저를 인자로 받는다.
 *          const 매니저의 찾기 · 순회(`resolveGameObject` · `resolveComponent` · `findGameObjectBy…` · `forEach…`)는 const 포인터만 낸다(`GameObjectManager::findGameObjectByName`).
 *          **남 읽기의 길:** const 는 쓰기만 막고 읽기는 연다 — 남의 컴포넌트는 다른 워커가 같은 순간 틱하며 쓰고 있을 수 있다. 그래서 병렬 틱 안에서
 *          남의 오브젝트 컴포넌트를 찾으면(`getComponent` · `resolveComponent`) 검사 구성에서 멈추고(`GameObject::assertParallelTickRead`), 남 읽기는
 *          틱 중에 바뀌지 않는 형식(`Component::kTickReadSafe` — 트랜스폼 · 디렉터)만 받는 `readOther` 하나로 좁힌다. 그 밖의 남 상태는 틱 뒤 작업에서 읽는다.
 *          유니티 잡 안전 시스템(`[ReadOnly]` 선언 · 경합 검출), 언리얼 Mass 처리기의 읽기 · 쓰기 요구 선언과 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

namespace sw
{
    struct TickTurn;

    class GameObject;

    /**
     * @class TickContext
     * @brief 틱 하나의 잡 컨텍스트입니다(파일 머리말).
     */
    class SW_API TickContext final : public SceneJobContext
    {
    public:
        /** @brief @p self 의 매니저와 그 틱 뒤 큐로 컨텍스트를 세웁니다. @p self 는 매니저에 붙은 오브젝트여야 합니다. */
        TickContext( GameObject& self, float32 deltaTime );
        /** @brief 주기가 있는 틱의 차례 @p turn 으로 세웁니다(디스패치가 부른다). */
        TickContext( GameObject& self, const TickTurn& turn );

        /** @brief 틱하는 오브젝트입니다 — 이 컨텍스트가 쓰기를 여는 유일한 오브젝트입니다. */
        GameObject& getSelf() const { return *_pSelf; }
        /** @brief 이번 틱에 찬 주기 수입니다(`Component::kTickInterval` · 서브틱 주기). 주기가 없으면 1 이고, 프레임이 주기보다 길면 2 이상입니다 — 고정 걸음을 세는 틱이 그만큼 돈다. */
        int32 getIntervalCount() const { return static_cast<int32>( _intervalCount ); }

        /**
         * @brief 남의 오브젝트 @p object 의 첫 @p T 를 읽기 전용으로 찾습니다 — 병렬 틱 안의 남 읽기 길입니다(몸체는 `GameObjectManager.inl`).
         * @details @p T 는 틱 중에 바뀌지 않는 형식(`T::kTickReadSafe`)이어야 컴파일된다(`GameObject::getTickSafeComponent`). 오브젝트가 없거나 그 형식이 없으면 nullptr 입니다.
         */
        template <typename T>
        const T* readOther( GameObjectHandle object ) const;

        /**
         * @brief 컴포넌트 @p self 에 대한 일을 틱 뒤로 미룹니다(`GameObjectManager::deferPostTickOnComponent` 와 같은 길 — 몸체는 `GameObjectManager.inl`).
         * @details 늘 큐로 간다(`deferPostTick` 과 같은 까닭). 돌 때 핸들로 다시 찾고, 사라졌거나 삭제 대기면 부르지 않는다. @p func 는 붙잡은 값이 없는
         *          람다로 `func( TComponent& )` 또는 `func( TComponent&, GameObjectManager& )` 다 — 틱 뒤라 쓰기 가능한 매니저를 받는다.
         */
        template <typename TComponent, typename TFunc>
        void deferPostTickOnComponent( const TComponent& self, TFunc func );

        /**
         * @brief 씬을 바꾸는 일을 틱 뒤로 미룹니다 — `deferPostTick` 과 같되 @p func 가 쓰기 가능한 매니저를 받습니다(`func( GameObjectManager& )`).
         * @details 붙잡은 값(핸들 · 단계 정의 사본)을 함께 넘길 때 쓴다. 핸들은 돌 때 다시 찾는다 — 틱과 틱 뒤 사이에 사라질 수 있다.
         */
        template <typename TFunc>
        void deferPostTickOnScene( TFunc func )
        {
            GameObjectManager* pManager = _pSceneManager;
            deferPostTick( [pManager, func]()
            { func( *pManager ); } );
        }

    private:
        GameObject*        _pSelf;
        GameObjectManager* _pSceneManager; ///< 틱 뒤 작업을 거는 데만 쓴다 — 틱 본문에는 내지 않는다
        uint32             _intervalCount;
    };
} // namespace sw
