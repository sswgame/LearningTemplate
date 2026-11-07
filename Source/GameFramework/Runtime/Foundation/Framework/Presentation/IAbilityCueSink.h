/**
 * @file GameFramework/Runtime/Foundation/Framework/Presentation/IAbilityCueSink.h
 * @brief 어빌리티 게임플레이 큐의 연출(프리팹 · 소리)을 내는 창구입니다 — 어빌리티는 큐를 정하고 자리를 읽기만 하고, 연출은 클라이언트 전용 구현이 냅니다.
 * @details 구현은 클라이언트 전용 소스(`GameFramework/Client/Ability/AbilityCueSink.cpp`)의 모듈 서브시스템이다. 전용 서버에는 창구가 없어 큐 연출을 하지 않는다
 *          (예전에는 `#if SW_WITH_CLIENT_CODE` 구역이었다). 언리얼 GAS 의 게임플레이 큐가 데디케이티드 서버에서 돌지 않는 것과 같다(기억).
 */
#pragma once
#include "Core/Math/VectorMath.h"

namespace sw
{
    class GameObjectManager;
    struct AbilityPresentationDef;

    /** @class IAbilityCueSink @brief 큐 연출을 받는 쪽입니다. 틱 워커에서 불러도 된다(구현이 게임 스레드의 틱 뒤로 미룬다). */
    class IAbilityCueSink
    {
    public:
        virtual ~IAbilityCueSink() = default;

        /** @brief @p manager 의 월드 @p position 에 @p cueDef 의 프리팹을 띄우고 소리를 냅니다. */
        virtual void presentCue( GameObjectManager& manager, const AbilityPresentationDef& cueDef, const float3& position ) = 0;
    };
} // namespace sw
