/**
 * @file IBoostReceiver.h
 * @brief 부스트(대시 판 · 버섯)를 받는 이동 컴포넌트가 내보내는 리플렉션 인터페이스입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class IBoostReceiver
     * @brief 부스트를 받는 계약입니다. 이동 컴포넌트(바디 없는 탈것 등)가 포트 멤버로 내보내고(`REFLECT_INTERFACE( IBoostReceiver, _boostReceiverPort )`),
     *        주는 쪽(부스트 패드)은 대상 오브젝트에서 `GameObject::findInterface<IBoostReceiver>()` 로 찾는다 — 범용 기믹이 구체 이동 클래스를 모르게 한다.
     * @details 언리얼 UINTERFACE · 유니티 `GetComponent<IBoostable>()` 자리. 세기(최고 속도 보너스 · 밀기)는 받는 쪽 튜닝이 정하고 주는 쪽은 시간만 준다.
     */
    REFLECT( Interface, Category = "Movement", DisplayName = "Boost Receiver", Tooltip = "Contract of movement components that accept a timed boost (boost pads, pickups)" )
    struct SW_GF_API IBoostReceiver
    {
        REFLECT_BODY();

        virtual ~IBoostReceiver() = default;

        /** @brief @p durationSeconds 초 부스트를 겁니다. 받는 쪽이 부스트를 쓰지 않으면(모듈이 꺼짐 · 시간 0 이하) false 입니다. */
        [[nodiscard]] virtual bool receiveBoost( float32 durationSeconds ) = 0;
    };
} // namespace sw
