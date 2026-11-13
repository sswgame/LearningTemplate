/**
 * @file TickContext.h
 * @brief 컴포넌트 주 틱(`Component::onTick`)이 받는 잡 컨텍스트입니다 — 워커 스레드 시스템의 `SceneJobContext` 에 자기 오브젝트를 더한 것입니다.
 * @details 틱은 자기 오브젝트(`getSelf`)만 쓰고, 씬은 읽기 전용(`getManager`)으로 보며, 남의 컴포넌트 쓰기 · 구조 변경은 명령 버퍼(`deferPostTick`, 틱 뒤 큐)로 보낸다.
 *          유니티 `IJobEntity` + `EntityCommandBuffer`, 언리얼 `FMassExecutionContext::Defer()` 와 같은 자리다. 디스패치(`SceneTickScheduler` · `TickBatchOf`)가
 *          원소마다 스택에 세운다.
 * @note 남은 틈: 틱 본문이 `getOwner()->getManager()` 로 쓰기 가능한 매니저를 아직 꺼낼 수 있다(본문 45 곳). 그 읽기를 `getManager()`(const)로 옮기고
 *       쓰기를 `deferPostTick` 로 옮긴 뒤 `GameObject::getManager` 를 틱에서 닫는 것이 다음 단계다(NEXT 5-3).
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
     * @brief 주 틱 하나의 잡 컨텍스트입니다(파일 머리말).
     */
    class SW_API TickContext final : public SceneJobContext
    {
    public:
        /** @brief @p self 의 매니저와 그 틱 뒤 큐로 컨텍스트를 세웁니다. @p self 는 매니저에 붙은 오브젝트여야 합니다. */
        TickContext( GameObject& self, float32 deltaTime );

        /** @brief 틱하는 오브젝트입니다 — 이 컨텍스트가 쓰기를 여는 유일한 오브젝트입니다. */
        GameObject& getSelf() const { return *_pSelf; }

    private:
        GameObject* _pSelf;
    };
} // namespace sw
