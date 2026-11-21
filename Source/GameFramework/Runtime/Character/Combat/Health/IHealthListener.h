/**
 * @file IHealthListener.h
 * @brief 같은 오브젝트의 체력 변화를 받는 컴포넌트(HP 바, 체력 연출)가 내보내는 리플렉션 인터페이스와 그 이벤트입니다. 체력 시스템은 뷰를 모르고 이것으로 알립니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;

    /** @brief 체력이 어떻게 바뀌었는가입니다. */
    enum class HealthChangeKind : uint8
    {
        Reset,   ///< 처음, 부활, 스탯 재설정. 연출 없이 그 값으로 바꾼다
        Changed, ///< 피해나 회복
        Died     ///< 쓰러졌다(비율 0)
    };

    /** @brief 체력 변화 하나입니다. 비율은 0..1 입니다. */
    struct HealthChangedEvent
    {
        float32          _ratio{ 1.0f };
        HealthChangeKind _kind{ HealthChangeKind::Changed };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IHealthListener
     * @brief 체력 변화를 받는 계약입니다. 체력 원천(어빌리티 시스템, 키트의 유닛 스탯, 게임의 적)이 `IHealthSource::notifyHealthChanged` 에서
     *        `broadcast` 로 같은 오브젝트에서 이 인터페이스를 내보내는 컴포넌트(`REFLECT_INTERFACE( IHealthListener, _healthListenerPort )`) 모두에 알립니다.
     * @details 모델은 뷰를 모릅니다. 체력 시스템은 이 인터페이스(전투 레이어)만 알고, HP 바(`HealthBarComponent`, UI 레이어)가 이것을 내보내 받습니다.
     *          언리얼 Lyra 에서 위젯이 `ULyraHealthComponent::OnHealthChanged` 를 받는 것에 해당합니다.
     *          알림은 같은 스레드, 같은 프레임에 바로 갑니다. 체력 시스템이 자기 오브젝트를 틱하는 중이면 같은 워커이므로 받는 쪽은 자기 상태만 고칩니다.
     */
    REFLECT( Interface, Category = "Combat", DisplayName = "Health Listener", Tooltip = "Contract of the components that react to their object's health (HP bars)" )
    struct SW_GF_API IHealthListener
    {
        REFLECT_BODY();

        virtual ~IHealthListener() = default;

        /** @brief 같은 오브젝트의 체력이 바뀌었습니다. */
        virtual void onHealthChanged( const HealthChangedEvent& event ) = 0;

        /** @brief @p owner 에서 이 인터페이스를 내보내는 모든 컴포넌트에 @p event 를 알립니다. 받는 쪽이 없으면 아무것도 하지 않습니다. 힙을 쓰지 않는다(받는 쪽 넷까지). */
        static void broadcast( const GameObject& owner, const HealthChangedEvent& event );
    };
} // namespace sw
