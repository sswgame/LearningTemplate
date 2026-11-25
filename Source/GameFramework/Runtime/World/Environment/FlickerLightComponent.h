/**
 * @file FlickerLightComponent.h
 * @brief 같은 오브젝트의 라이트 세기를 퀘이크 라이트 스타일 문자열 패턴으로 깜빡이는 환경 효과입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class FlickerLightComponent
     * @brief 같은 오브젝트의 `LightComponent` 세기를 문자열 패턴으로 깜빡입니다. 퀘이크 라이트 스타일이며 'a' 는 0, 'm' 은 1, 'z' 는 2배이고, 속도는 `_rate` 글자/초입니다.
     * @details 스텝 수로 글자를 고르므로 결정적이다. 같은 스텝이면 같은 밝기가 나온다. 처음 세기를 기준으로 곱하고, 바뀔 때만 틱 뒤에 쓴다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Flicker Light", Tooltip = "Drives the light intensity from a Quake-style pattern string" )
    class SW_GF_API FlickerLightComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr float32 kTickInterval = constant::time::kDefaultFixedStepSeconds; ///< 고정 걸음마다 한 번 — 지연 · 간격을 걸음 수로 센다(프레임 시간과 무관하게 같은 걸음)

        FlickerLightComponent();
        virtual ~FlickerLightComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 스텝 @p step 의 밝기 배율입니다. 패턴이 비어 있으면 1 입니다. */
        float32 computeScaleAtStep( int32 step ) const;

    private:
        PROPERTY( Required, Category = "Flicker", DisplayName = "Pattern", Tooltip = "a = off, m = normal, z = double" )
        string _pattern;
        PROPERTY( Category = "Flicker", DisplayName = "Rate", Min = 0.1, Tooltip = "Pattern letters per second" )
        float32 _rate;
        PROPERTY( RuntimeState, Category = "Flicker", DisplayName = "Step", Tooltip = "Runtime" )
        int32 _step;

        SW_TRANSIENT float32 _baseIntensity;
        SW_TRANSIENT float32 _appliedScale;
    };
} // namespace sw
