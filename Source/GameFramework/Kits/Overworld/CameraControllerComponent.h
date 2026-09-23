#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    REFLECT()
    class SW_GF_API CameraControllerComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief `shake()` 에 진동 수를 주지 않았을 때 쓰는 값(초당 진동 수)입니다. */
        static constexpr float32 kDefaultShakeFrequency = 30.0f;

        CameraControllerComponent();
        virtual ~CameraControllerComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /**
         * @brief 카메라를 흔듭니다.
         * @param intensity 최대 흔들림 크기. 남은 시간에 비례해 **0 으로 잦아듭니다.**
         * @param duration 흔들리는 시간(초).
         * @param frequency 초당 진동 수. 기본값은 `kDefaultShakeFrequency`.
         * @details 예전에는 이 함수가 `_shakeFrequency` 를 **건드리지 않았습니다.** 그 값은 기본이
         *          0 이고 코드에서 넣을 창구가 없었으므로(리플렉션 프로퍼티뿐), 코드로 부른
         *          흔들림은 `sin( t * 0 ) = 0` · `cos( t * 0 ) = 1` 이 되어 **떨리지 않고
         *          한쪽으로 밀린 채 있다가 시간이 다 되면 툭 돌아왔습니다.** 흔들림이 아니라
         *          정적인 어긋남이었습니다. 씬 파일에 `shakeFrequency` 를 손으로 적은 경우에만
         *          제대로 흔들렸습니다.
         *
         *          그리고 위상을 **남은 시간**으로 계산해서, 잦아들 무렵 `cos` 항이 최대가 됐습니다.
         *          끝나기 직전이 가장 크게 튀고 그 다음 프레임에 0 으로 끊겼습니다. 위상은 흐른
         *          시간으로, 크기는 남은 비율로 갑니다.
         */
        void shake( float32 intensity, float32 duration, float32 frequency = kDefaultShakeFrequency );

        /** @brief 이번 프레임의 흔들림 오프셋입니다. 흔들림이 없으면 (0,0) 입니다. */
        float2 getShakeOffset() const;
        /** @brief 흔들리는 중인지 여부입니다. */
        bool isShaking() const { return _shakeDuration > 0.0f; }

    private:
        PROPERTY( Alias = "targetPos" )
        float2 _targetPos;
        PROPERTY( Alias = "currentPos" )
        float2 _currentPos;
        PROPERTY( Alias = "followSpeed" )
        float32 _followSpeed;
        PROPERTY( Alias = "shakeIntensity" )
        float32 _shakeIntensity;
        PROPERTY( Alias = "shakeDuration" )
        float32 _shakeDuration;
        PROPERTY( Alias = "shakeFrequency" )
        float32 _shakeFrequency;
        PROPERTY( Alias = "shakeElapsed" )
        float32 _shakeElapsed; ///< 흔들림이 시작된 뒤 흐른 시간. **위상은 이것으로 간다**
        PROPERTY( Alias = "shakeTotalDuration" )
        float32 _shakeTotalDuration; ///< 잦아드는 비율의 분모
    };
} // namespace sw
