/**
 * @file FlagsOwnerTestComponent.h
 * @brief 월드 플래그 주인 테스트(`TestGameStateOpen.cpp`)가 다는 둘째 플래그 주인입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 판 상태는 씬의 둘째 열기를 거절하므로, 같은 계약(`IGameFlagsOwner`)을 내보내는 다른 컴포넌트로 "둘 열림" 을 만든다(하위 레벨이 둘째 소유자를 연 경우).
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Condition/IGameFlagsOwner.h"

namespace sw
{
    /** @brief 늘 열린 자기 플래그를 내는 플래그 주인입니다. */
    REFLECT( HideInMenu )
    struct FlagsOwnerTestComponent : public Component
    {
        REFLECT_BODY();
        REFLECT_INTERFACE( IGameFlagsOwner, _gameFlagsOwnerPort );

        /** @brief 주인의 `_flags` 를 내는 포트입니다. */
        struct GameFlagsOwnerPort final : IGameFlagsOwner
        {
            GameFlags* findOpenFlags() override { return &getPortOwner<FlagsOwnerTestComponent>( *this, offsetof( FlagsOwnerTestComponent, _gameFlagsOwnerPort ) )._flags; }
        };

        GameFlags          _flags;
        GameFlagsOwnerPort _gameFlagsOwnerPort;
    };
} // namespace sw
