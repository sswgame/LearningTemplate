/**
 * @file TickContext.h
 * @brief 컴포넌트 틱(`Component::onTick` · `onSubTick`)이 받는 잡 컨텍스트입니다 — 워커 레인 시스템의 `SceneJobContext` 에 자기 오브젝트를 더한 것입니다.
 * @details 틱은 자기 오브젝트(`getSelf`)만 쓰고, 씬은 읽기 전용(`getManager`)으로 보며, 남의 컴포넌트 쓰기 · 구조 변경은 명령 버퍼(`deferPostTick` ·
 *          `deferPostTickOnComponent`, 틱 뒤 큐)로 보낸다. 유니티 `IJobEntity` + `EntityCommandBuffer`, 언리얼 `FMassExecutionContext::Defer()` 와 같은
 *          자리다. 디스패치(`SceneTickScheduler` · `TickBatchOf`)가 원소마다 스택에 세운다.
 *          **쓰기 가능한 매니저 길:** `GameObject::getManager` 는 읽기 전용(const)을 돌려주고, 쓰기 가능한 매니저는 이름이 다른 `GameObject::getMutableManager`
 *          로만 얻는다 — 틱 본문의 습관 길(`getOwner()->getManager()`)로는 매니저를 바꾸는 호출이 컴파일되지 않는다. 틱 뒤 작업은 쓰기 가능한 매니저를 인자로 받는다.
 *          남은 틈(NEXT 5-3): const 매니저의 찾기(`resolveGameObject` · `resolveComponent` · `findGameObjectBy…`)가 아직 쓰기 가능한 포인터를 돌려준다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

namespace sw
{
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

        /** @brief 틱하는 오브젝트입니다 — 이 컨텍스트가 쓰기를 여는 유일한 오브젝트입니다. */
        GameObject& getSelf() const { return *_pSelf; }

        /**
         * @brief 컴포넌트 @p self 에 대한 일을 틱 뒤로 미룹니다(`GameObjectManager::deferPostTickOnComponent` 와 같은 길 — 몸체는 `GameObjectManager.inl`).
         * @details 늘 큐로 간다(`deferPostTick` 과 같은 까닭). 돌 때 핸들로 다시 찾고, 사라졌거나 삭제 대기면 부르지 않는다. @p func 는 붙잡은 값이 없는
         *          람다로 `func( TComponent& )` 또는 `func( TComponent&, GameObjectManager& )` 다 — 틱 뒤라 쓰기 가능한 매니저를 받는다.
         */
        template <typename TComponent, typename TFunc>
        void deferPostTickOnComponent( const TComponent& self, TFunc func );

    private:
        GameObject*        _pSelf;
        GameObjectManager* _pSceneManager; ///< 틱 뒤 작업을 거는 데만 쓴다 — 틱 본문에는 내지 않는다
    };
} // namespace sw
