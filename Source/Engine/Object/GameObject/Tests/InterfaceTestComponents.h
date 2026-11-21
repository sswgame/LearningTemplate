/**
 * @file InterfaceTestComponents.h
 * @brief 리플렉션 인터페이스 테스트(`TestFindInterface.cpp`)가 다는 인터페이스와 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details `ICountSourceTest` 를 포트 멤버로 내보내는 컴포넌트, 그것을 이어받기만 하는 파생(조상 표 합침), 인터페이스가 없는 컴포넌트를 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 수를 읽는 계약입니다(상태 없음 — 생성 코드가 크기를 vptr 하나로 지킨다). */
    REFLECT( Interface )
    struct ICountSourceTest
    {
        REFLECT_BODY();

        virtual ~ICountSourceTest()     = default;
        virtual uint32 getCount() const = 0;
    };

    /** @brief `ICountSourceTest` 를 포트 멤버로 내보냅니다 — 포트는 주인의 `_count` 를 읽는다. */
    REFLECT( HideInMenu )
    struct CountSourceTestComponent : public Component
    {
        REFLECT_BODY();
        REFLECT_INTERFACE( ICountSourceTest, _countSourcePort );

        /** @brief 주인에게 넘기는 포트입니다. */
        struct CountSourcePort final : ICountSourceTest
        {
            uint32 getCount() const override { return getPortOwner<const CountSourceTestComponent>( *this, offsetof( CountSourceTestComponent, _countSourcePort ) )._count; }
        };

        uint32          _count{ 0 };
        CountSourcePort _countSourcePort;
    };

    /** @brief 인터페이스를 다시 적지 않고 이어받기만 합니다 — 조상 표가 합쳐져 찾힌다. */
    REFLECT( HideInMenu )
    struct DerivedCountSourceTestComponent : public CountSourceTestComponent
    {
        REFLECT_BODY();
    };

    /** @brief 인터페이스가 없는 컴포넌트입니다 — 찾기가 건너뛴다. */
    REFLECT( HideInMenu )
    struct PlainInterfaceTestComponent : public Component
    {
        REFLECT_BODY();
    };
} // namespace sw
