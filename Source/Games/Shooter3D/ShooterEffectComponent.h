/**
 * @file ShooterEffectComponent.h
 * @brief 잠깐 보이는 탄착 · 터짐 구 하나 — 디렉터의 풀에 숨어 있다가 꺼내지고, 수명이 다하면 스스로 숨습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class ShooterEffectComponent
     * @brief 효과 프리팹의 컴포넌트입니다. 오브젝트를 효과마다 만들고 지우지 않는다 — 디렉터가 풀로 세워 두고 `activate` 로 꺼낸다(게임 스레드).
     * @details 기본 틱 그룹에서 수명을 줄이고, 다하면 자기 메시를 숨긴다(자기 오브젝트에만 쓴다).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Effect", Tooltip = "Pooled hit / burst sphere that hides itself after its lifetime" )
    class ShooterEffectComponent : public Component
    {
    public:
        REFLECT_BODY();

        ShooterEffectComponent();
        virtual ~ShooterEffectComponent() override = default;

        void onTick( float32 deltaTime ) override;

        /** @brief @p lifetime 초 동안 보입니다(자리 · 크기 · 색은 디렉터가 메시에 넣는다). */
        void activate( float32 lifetime );
        /** @brief 숨어 있어 다시 꺼낼 수 있으면 true 입니다. */
        bool isIdle() const { return _bShowing == SW_FALSE; }

    private:
        float32 _remaining;
        uint8   _bShowing : 1;
        uint8   _reserved : 7;
    };
} // namespace sw
