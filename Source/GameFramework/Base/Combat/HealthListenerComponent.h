/**
 * @file HealthListenerComponent.h
 * @brief 같은 오브젝트의 체력 변화를 받는 컴포넌트(HP 바 · 체력 연출)의 베이스와 그 이벤트입니다 — 체력 시스템은 뷰를 모르고 이것으로 알립니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;

    /** @brief 체력이 어떻게 바뀌었는가입니다. */
    enum class HealthChangeKind : uint8
    {
        Reset,   ///< 처음 · 부활 · 스탯 다시 두기 — 흔적 없이 그 값
        Changed, ///< 피해 · 회복
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
     * @class HealthListenerComponent
     * @brief 체력 변화를 받는 쪽입니다. 체력 원천(`HealthSourceComponent::notifyHealthChanged` — 어빌리티 시스템 · 키트의 유닛 스탯 · 게임의 적)이 `broadcast` 로 같은 오브젝트의 이 파생 모두에 알립니다.
     * @details 모델은 뷰를 모른다 — 체력 시스템은 이 베이스(전투 층)만 알고, HP 바(`HealthBarComponent`, UI 층)가 이것을 상속해 받습니다. 언리얼 Lyra 의
     *          `ULyraHealthComponent::OnHealthChanged` 를 위젯이 받는 자리입니다. 알림은 그 자리에서(같은 스레드 · 같은 프레임) 갑니다 — 체력 시스템이
     *          자기 오브젝트를 틱하는 중이면 같은 워커이므로 받는 쪽은 자기 상태만 고칩니다.
     */
    REFLECT( Abstract, Category = "Combat", DisplayName = "Health Listener", Tooltip = "Base of the components that react to their object's health (HP bars)" )
    class SW_GF_API HealthListenerComponent : public Component
    {
    public:
        REFLECT_BODY();

        HealthListenerComponent();
        virtual ~HealthListenerComponent() override;

        /** @brief 같은 오브젝트의 체력이 바뀌었습니다. */
        virtual void onHealthChanged( const HealthChangedEvent& event ) = 0;

        /** @brief @p owner 에 붙은 모든 받는 쪽에 @p event 를 알립니다. 받는 쪽이 없으면 아무것도 하지 않습니다. */
        static void broadcast( const GameObject& owner, const HealthChangedEvent& event );
    };
} // namespace sw
