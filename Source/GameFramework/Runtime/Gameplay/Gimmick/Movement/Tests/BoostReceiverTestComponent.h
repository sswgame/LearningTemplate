/**
 * @file BoostReceiverTestComponent.h
 * @brief 부스트 패드 테스트(`TestMovementPads.cpp`)가 다는 부스트 받는 쪽입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 패드가 구체 이동 클래스가 아니라 계약(`IBoostReceiver`)만 보는지 확인한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/Movement/IBoostReceiver.h"

namespace sw
{
    /** @brief 받은 부스트 시간을 남기고, `_bAccept` 가 꺼져 있으면 거절합니다. */
    REFLECT( HideInMenu )
    struct BoostReceiverTestComponent : public Component
    {
        REFLECT_BODY();
        REFLECT_INTERFACE( IBoostReceiver, _boostReceiverPort );

        /** @brief 주인에게 넘기는 포트입니다. */
        struct BoostReceiverPort final : IBoostReceiver
        {
            [[nodiscard]] bool receiveBoost( float32 durationSeconds ) override
            {
                BoostReceiverTestComponent& owner = getPortOwner<BoostReceiverTestComponent>( *this, offsetof( BoostReceiverTestComponent, _boostReceiverPort ) );
                owner._lastDuration               = durationSeconds;
                return owner._bAccept == SW_TRUE;
            }
        };

        float32           _lastDuration{ 0.0f };
        uint8             _bAccept{ SW_TRUE };
        BoostReceiverPort _boostReceiverPort;
    };
} // namespace sw
