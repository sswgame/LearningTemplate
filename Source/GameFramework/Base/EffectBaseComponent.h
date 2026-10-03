#pragma once
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class EffectBaseComponent
     * @brief `_duration` 동안 흐려지다 오브젝트를 지우는 이펙트입니다. 흐림(`_currentAlpha`)을 같은 오브젝트 스프라이트들의 색 알파에 곱합니다.
     * @details 스프라이트의 알파는 시작할 때의 값을 기준으로 둡니다(반투명으로 만든 이펙트는 그 반투명에서 0 으로 흐려집니다). 머티리얼 인스턴스가
     *          아니라 스프라이트 색(`SpriteComponent::setTint` → GPU 인스턴스)으로 넣으므로, 같은 텍스처의 이펙트 백 개가 각자 다른 알파여도 한
     *          배치입니다. 예전에는 알파를 계산만 하고 읽는 곳이 없었습니다(`getCurrentAlpha` 를 부르는 곳이 없었다).
     *
     *          **페이드 중에 상태를 다시 읽어도 이어 갑니다**(플레이 중 되돌리기 · 핫 리로드). 다시 만든 컴포넌트에 `onBeginPlay` 가 다시 오는데,
     *          흐른 시간(`_currentTimer`)과 기준 알파(`_listBaseAlpha`)를 상태에 싣고, 흐른 시간이 있으면 처음으로 되돌리지 않습니다 — 이미
     *          흐려진 스프라이트 알파를 기준으로 다시 잡으면 그 알파에서 처음부터 다시 흐려집니다.
     */
    REFLECT( Category = "VFX", DisplayName = "Effect Base Component", Tooltip = "Fades the object's sprites out over its duration, then destroys the object" )
    class SW_GF_API EffectBaseComponent : public Component
    {
    public:
        REFLECT_BODY();
        EffectBaseComponent();
        virtual ~EffectBaseComponent() override = default;

        /**
         * @brief 타이머 · 알파를 처음으로 두고, 스프라이트들의 지금 알파를 기준으로 적어 둡니다.
         * @details 흐른 시간이 있으면(읽어 들인 상태) 타이머 · 알파 · 기준 알파를 그대로 두고 스프라이트에 다시 넣기만 합니다.
         */
        void onBeginPlay() override;
        void onEndPlay() override;
        /** @brief 흐림을 진행해 스프라이트에 넣고, 다 흐려지면 오브젝트를 지웁니다. */
        void onTick( float32 deltaTime ) override;

        float32 getDuration() const;
        float32 getCurrentTimer() const;
        float32 getCurrentAlpha() const;
        /** @brief 흐림을 정하고 스프라이트들에 바로 넣습니다. */
        void setCurrentAlpha( float32 alpha );

    private:
        /** @brief 같은 오브젝트의 스프라이트마다 색 알파 = 기준 알파 × `_currentAlpha` 로 둡니다. */
        void applyAlphaToSprites();

        PROPERTY( Category = "Effect", DisplayName = "Duration", Tooltip = "Seconds until the effect has faded out; 0 keeps it", Min = 0.0, Meta = "Units=s" )
        float32 _duration;
        PROPERTY( Category = "Effect", DisplayName = "Current Timer", Tooltip = "Seconds since the effect started", ReadOnly, Meta = "Units=s" )
        float32 _currentTimer;
        PROPERTY( Category = "Effect", DisplayName = "Current Alpha", Tooltip = "Fade multiplied into the sprites' alpha (1 to 0)", Min = 0.0, Max = 1.0 )
        float32 _currentAlpha;
        /** @brief 시작할 때 스프라이트마다의 색 알파(컴포넌트 순서)입니다. 페이드 중에 다시 읽은 상태가 같은 기준으로 이어 가도록 저장합니다. */
        PROPERTY( HideInInspector )
        vector<float32> _listBaseAlpha;
    };
} // namespace sw
